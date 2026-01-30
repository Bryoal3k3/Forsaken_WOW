/*
 * RogueCombat.cpp
 *
 * Rogue-specific combat handler.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "RogueCombat.h"
#include "BotMovementManager.h"
#include "CombatBotBaseAI.h"
#include "../CombatHelpers.h"

RogueCombat::RogueCombat(CombatBotBaseAI* pAI)
    : m_pAI(pAI)
{
}

bool RogueCombat::Engage(Player* pBot, Unit* pTarget)
{
    return CombatHelpers::EngageMelee(pBot, pTarget, "RogueCombat", m_pMoveMgr);
}

void RogueCombat::UpdateCombat(Player* pBot, Unit* pVictim)
{
    if (!pVictim)
        return;

    // Ensure we keep chasing if not in melee range (handles movement interruptions)
    CombatHelpers::HandleMeleeMovement(pBot, pVictim, m_pMoveMgr);

    // Slice and Dice if we have combo points
    if (m_pAI->m_spells.rogue.pSliceAndDice &&
        pBot->GetComboPoints() >= 2 &&
        !pBot->HasAura(m_pAI->m_spells.rogue.pSliceAndDice->Id) &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.rogue.pSliceAndDice))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.rogue.pSliceAndDice) == SPELL_CAST_OK)
            return;
    }

    // Eviscerate at 5 combo points
    if (m_pAI->m_spells.rogue.pEviscerate &&
        pBot->GetComboPoints() >= 5 &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.rogue.pEviscerate))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.rogue.pEviscerate) == SPELL_CAST_OK)
            return;
    }

    // Sinister Strike to build combo points
    if (m_pAI->m_spells.rogue.pSinisterStrike &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.rogue.pSinisterStrike))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.rogue.pSinisterStrike) == SPELL_CAST_OK)
            return;
    }
}

void RogueCombat::UpdateOutOfCombat(Player* pBot)
{
    // Nothing special for rogues out of combat
}
