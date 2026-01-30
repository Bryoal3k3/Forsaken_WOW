/*
 * HunterCombat.cpp
 *
 * Hunter-specific combat handler.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "HunterCombat.h"
#include "BotMovementManager.h"
#include "CombatBotBaseAI.h"
#include "Player.h"
#include "Unit.h"
#include "MotionMaster.h"
#include "Log.h"
#include "../CombatHelpers.h"

HunterCombat::HunterCombat(CombatBotBaseAI* pAI)
    : m_pAI(pAI)
{
}

bool HunterCombat::Engage(Player* pBot, Unit* pTarget)
{
    // Use Attack(false) to establish combat state without melee swings
    // This sets GetVictim() and adds us to mob's attacker list
    if (pBot->Attack(pTarget, false))
    {
        // Chase directly to target - HandleRangedMovement() will stop at cast range
        // NOTE: Don't use offset - it causes pathfinding issues that make bot stop early
        if (m_pMoveMgr)
            m_pMoveMgr->Chase(pTarget, 0.0f, MovementPriority::PRIORITY_COMBAT);
        else
            pBot->GetMotionMaster()->MoveChase(pTarget);

        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[HunterCombat] %s engaging %s (Attack success, moving to range)",
            pBot->GetName(), pTarget->GetName());
        return true;
    }

    sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[HunterCombat] %s failed to engage %s (Attack returned false)",
        pBot->GetName(), pTarget->GetName());
    return false;
}

void HunterCombat::UpdateCombat(Player* pBot, Unit* pVictim)
{
    if (!pVictim)
        return;

    CombatHelpers::HandleRangedMovement(pBot, pVictim, 30.0f, m_pMoveMgr);

    // Melee combat - when victim can hit us (deadzone)
    if (pVictim->CanReachWithMeleeAutoAttack(pBot))
    {
        // Enable melee auto-attack and stay on target
        pBot->Attack(pVictim, true);
        if (m_pMoveMgr)
            m_pMoveMgr->Chase(pVictim, 0.0f, MovementPriority::PRIORITY_COMBAT);
        else
            pBot->GetMotionMaster()->MoveChase(pVictim);

        // Wing Clip to snare
        if (m_pAI->m_spells.hunter.pWingClip &&
            m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.hunter.pWingClip))
        {
            m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.hunter.pWingClip);
        }

        // Mongoose Bite
        if (m_pAI->m_spells.hunter.pMongooseBite &&
            m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.hunter.pMongooseBite))
        {
            m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.hunter.pMongooseBite);
        }

        // Raptor Strike
        if (m_pAI->m_spells.hunter.pRaptorStrike &&
            m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.hunter.pRaptorStrike))
        {
            m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.hunter.pRaptorStrike);
        }

        return; // Don't try ranged attacks in melee
    }

    // --- Ranged Combat (outside deadzone) ---

    // Maintain Auto Shot (only start if not already auto-shooting)
    if (pBot->HasSpell(SPELL_AUTO_SHOT) &&
        !pBot->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL) &&
        !pBot->IsNonMeleeSpellCasted())
    {
        pBot->CastSpell(pVictim, SPELL_AUTO_SHOT, false);
    }

    // Hunter's Mark
    if (m_pAI->m_spells.hunter.pHuntersMark &&
        !pVictim->HasAura(m_pAI->m_spells.hunter.pHuntersMark->Id) &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.hunter.pHuntersMark))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.hunter.pHuntersMark) == SPELL_CAST_OK)
            return;
    }

    // Aimed Shot
    if (m_pAI->m_spells.hunter.pAimedShot &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.hunter.pAimedShot))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.hunter.pAimedShot) == SPELL_CAST_OK)
            return;
    }

    // Multi-Shot
    if (m_pAI->m_spells.hunter.pMultiShot &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.hunter.pMultiShot))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.hunter.pMultiShot) == SPELL_CAST_OK)
            return;
    }

    // Arcane Shot
    if (m_pAI->m_spells.hunter.pArcaneShot &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.hunter.pArcaneShot))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.hunter.pArcaneShot) == SPELL_CAST_OK)
            return;
    }

    // Serpent Sting
    if (m_pAI->m_spells.hunter.pSerpentSting &&
        !pVictim->HasAura(m_pAI->m_spells.hunter.pSerpentSting->Id) &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.hunter.pSerpentSting))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.hunter.pSerpentSting) == SPELL_CAST_OK)
            return;
    }
}

void HunterCombat::UpdateOutOfCombat(Player* pBot)
{
    // Keep aspect up
    if (m_pAI->m_spells.hunter.pAspectOfTheHawk &&
        !pBot->HasAura(m_pAI->m_spells.hunter.pAspectOfTheHawk->Id) &&
        m_pAI->CanTryToCastSpell(pBot, m_pAI->m_spells.hunter.pAspectOfTheHawk))
    {
        m_pAI->DoCastSpell(pBot, m_pAI->m_spells.hunter.pAspectOfTheHawk);
    }
}
