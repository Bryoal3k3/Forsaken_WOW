/*
 * CombatHelpers.h
 *
 * Shared helper functions for bot combat handlers.
 * Reduces code duplication across class-specific combat files.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_COMBATHELPERS_H
#define MANGOS_COMBATHELPERS_H

#include "Player.h"
#include "Unit.h"
#include "MotionMaster.h"
#include "BotMovementManager.h"
#include "Log.h"

namespace CombatHelpers
{
    // Caster engagement: Attack(false) + MoveChase (no offset!)
    // Used by: Mage, Priest, Warlock
    // NOTE: We use MoveChase without offset because MoveChase(target, 28.0f) calculates
    // a position 28 yards FROM target, which can result in INCOMPLETE paths that cause
    // the chase generator to stop (thinking bot has LoS and doesn't need to move).
    // HandleRangedMovement() will stop the bot at casting range.
    inline bool EngageCaster(Player* pBot, Unit* pTarget, const char* className, BotMovementManager* pMoveMgr = nullptr)
    {
        // Face target before attacking (fixes stuck bug when target is behind)
        pBot->SetFacingToObject(pTarget);

        // Use Attack(false) to establish combat state without melee swings
        // This sets GetVictim() and adds us to mob's attacker list
        // First spell in UpdateCombat() will deal damage and fully engage
        if (pBot->Attack(pTarget, false))
        {
            // Chase directly to target - HandleRangedMovement() will stop us at cast range
            if (pMoveMgr)
                pMoveMgr->Chase(pTarget, 0.0f, MovementPriority::PRIORITY_COMBAT);
            else
                pBot->GetMotionMaster()->MoveChase(pTarget);

            sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[%s] %s engaging %s (Attack success, moving to range)",
                className, pBot->GetName(), pTarget->GetName());
            return true;
        }

        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[%s] %s failed to engage %s (Attack returned false)",
            className, pBot->GetName(), pTarget->GetName());
        return false;
    }

    // Melee engagement: Attack(true) + MoveChase into melee
    // Used by: Warrior, Rogue, Paladin, Shaman, Druid
    inline bool EngageMelee(Player* pBot, Unit* pTarget, const char* className, BotMovementManager* pMoveMgr = nullptr)
    {
        // Face target before attacking (fixes stuck bug when target is behind)
        pBot->SetFacingToObject(pTarget);

        // Melee classes use Attack(true) to enable auto-attack swings
        if (pBot->Attack(pTarget, true))
        {
            if (pMoveMgr)
                pMoveMgr->Chase(pTarget, 0.0f, MovementPriority::PRIORITY_COMBAT);
            else
                pBot->GetMotionMaster()->MoveChase(pTarget);
            sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[%s] %s engaging %s (Attack success)",
                className, pBot->GetName(), pTarget->GetName());
            return true;
        }

        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[%s] %s failed to engage %s (Attack returned false)",
            className, pBot->GetName(), pTarget->GetName());
        return false;
    }

    // Ranged movement handling: stop moving when in range AND have LoS
    // Used by: Mage, Priest, Warlock, Hunter
    inline void HandleRangedMovement(Player* pBot, Unit* pVictim, float castRange = 30.0f, BotMovementManager* pMoveMgr = nullptr)
    {
        // Only kite if target is snared/rooted, otherwise stand and fight
        bool targetIsSnared = pVictim->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED) ||
                              pVictim->HasAuraType(SPELL_AURA_MOD_ROOT);

        float dist = pBot->GetDistance(pVictim);
        bool inCastRange = dist <= castRange;
        bool hasLoS = pBot->IsWithinLOSInMap(pVictim);

        // Check if we're actively chasing (not just "moving" in some other way)
        uint8 moveType = pMoveMgr
            ? pMoveMgr->GetCurrentMovementType()
            : pBot->GetMotionMaster()->GetCurrentMovementGeneratorType();
        bool isChasing = moveType == CHASE_MOTION_TYPE;

        // If OUT of cast range, ensure we're chasing
        // NOTE: Use MoveChase without offset - offset causes pathfinding issues
        // that result in bot stopping when it has LoS but is out of range
        if (!inCastRange)
        {
            if (!isChasing || !pBot->IsMoving())
            {
                if (pMoveMgr)
                    pMoveMgr->Chase(pVictim, 0.0f, MovementPriority::PRIORITY_COMBAT);
                else
                    pBot->GetMotionMaster()->MoveChase(pVictim);
            }
            return;
        }

        // If at range but NO line of sight, move all the way (caves/buildings)
        if (!hasLoS)
        {
            if (!isChasing || !pBot->IsMoving())
            {
                if (pMoveMgr)
                    pMoveMgr->Chase(pVictim, 0.0f, MovementPriority::PRIORITY_COMBAT);
                else
                    pBot->GetMotionMaster()->MoveChase(pVictim);
            }
            return;
        }

        // In range AND have LoS - stop if target isn't snared
        if (!targetIsSnared && pBot->IsMoving())
        {
            if (pMoveMgr)
                pMoveMgr->StopMovement(false);
            else
            {
                pBot->StopMoving();
                pBot->GetMotionMaster()->Clear();
            }
        }
    }

    // Melee movement handling: ensure bot keeps chasing if not in melee range
    // Used by: Warrior, Rogue, Paladin, Shaman, Druid
    inline void HandleMeleeMovement(Player* pBot, Unit* pVictim, BotMovementManager* pMoveMgr = nullptr)
    {
        // If not in melee range and not moving, re-issue chase command
        // This handles cases where movement gets interrupted (pathfinding edge cases, etc.)
        if (!pBot->CanReachWithMeleeAutoAttack(pVictim) && !pBot->IsMoving())
        {
            if (pMoveMgr)
                pMoveMgr->Chase(pVictim, 0.0f, MovementPriority::PRIORITY_COMBAT);
            else
                pBot->GetMotionMaster()->MoveChase(pVictim);
        }
    }

    // Wand shoot spell ID
    #define SPELL_SHOOT_WAND 5019

    // Fallback when all caster spells fail - try wand, then melee
    // Used by: Mage, Priest, Warlock
    inline void HandleCasterFallback(Player* pBot, Unit* pVictim, const char* className, BotMovementManager* pMoveMgr = nullptr)
    {
        // Try wand first (if equipped and not already shooting)
        if (pBot->HasSpell(SPELL_SHOOT_WAND) &&
            !pBot->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL) &&
            !pBot->IsNonMeleeSpellCasted())
        {
            pBot->CastSpell(pVictim, SPELL_SHOOT_WAND, false);
            return;
        }

        // No wand or already shooting - melee fallback if in range
        if (pBot->CanReachWithMeleeAutoAttack(pVictim))
        {
            if (!pBot->HasUnitState(UNIT_STATE_MELEE_ATTACKING))
            {
                pBot->Attack(pVictim, true);
            }
        }
        else if (!pBot->IsMoving())
        {
            // Out of range and not moving - move closer (no offset!)
            if (pMoveMgr)
                pMoveMgr->Chase(pVictim, 0.0f, MovementPriority::PRIORITY_COMBAT);
            else
                pBot->GetMotionMaster()->MoveChase(pVictim);
        }
    }
}

#endif // MANGOS_COMBATHELPERS_H
