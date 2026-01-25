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
#include "Log.h"

namespace CombatHelpers
{
    // Caster engagement: Attack(false) + MoveChase at range
    // Used by: Mage, Priest, Warlock
    inline bool EngageCaster(Player* pBot, Unit* pTarget, const char* className)
    {
        // Face target before attacking (fixes stuck bug when target is behind)
        pBot->SetFacingToObject(pTarget);

        // Use Attack(false) to establish combat state without melee swings
        // This sets GetVictim() and adds us to mob's attacker list
        // First spell in UpdateCombat() will deal damage and fully engage
        if (pBot->Attack(pTarget, false))
        {
            // Move into casting range (28 yards gives buffer for 30-yard spells)
            pBot->GetMotionMaster()->MoveChase(pTarget, 28.0f);

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
    inline bool EngageMelee(Player* pBot, Unit* pTarget, const char* className)
    {
        // Face target before attacking (fixes stuck bug when target is behind)
        pBot->SetFacingToObject(pTarget);

        // Melee classes use Attack(true) to enable auto-attack swings
        if (pBot->Attack(pTarget, true))
        {
            pBot->GetMotionMaster()->MoveChase(pTarget);
            sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[%s] %s engaging %s (Attack success)",
                className, pBot->GetName(), pTarget->GetName());
            return true;
        }

        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[%s] %s failed to engage %s (Attack returned false)",
            className, pBot->GetName(), pTarget->GetName());
        return false;
    }

    // Ranged movement handling: stop moving when in range unless target is snared
    // Used by: Mage, Priest, Warlock, Hunter
    inline void HandleRangedMovement(Player* pBot, Unit* pVictim, float castRange = 30.0f)
    {
        // Only kite if target is snared/rooted, otherwise stand and fight
        bool targetIsSnared = pVictim->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED) ||
                              pVictim->HasAuraType(SPELL_AURA_MOD_ROOT);

        float dist = pBot->GetDistance(pVictim);
        bool inCastRange = dist <= castRange;

        // Only stop movement if we're in casting range AND target isn't snared
        // This allows approach movement to continue until we can cast
        if (inCastRange && !targetIsSnared && pBot->IsMoving())
        {
            pBot->StopMoving();
            pBot->GetMotionMaster()->Clear();
        }
    }
}

#endif // MANGOS_COMBATHELPERS_H
