/*
 * WarriorCombat.cpp
 *
 * Warrior-specific combat handler.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "WarriorCombat.h"
#include "BotMovementManager.h"
#include "CombatBotBaseAI.h"
#include "../CombatHelpers.h"

WarriorCombat::WarriorCombat(CombatBotBaseAI* pAI)
    : m_pAI(pAI)
{
}

bool WarriorCombat::Engage(Player* pBot, Unit* pTarget)
{
    return CombatHelpers::EngageMelee(pBot, pTarget, "WarriorCombat", m_pMoveMgr);
}

void WarriorCombat::UpdateCombat(Player* pBot, Unit* pVictim)
{
    if (!pVictim)
        return;

    // Ensure we keep chasing if not in melee range (handles movement interruptions)
    CombatHelpers::HandleMeleeMovement(pBot, pVictim, m_pMoveMgr);

    // Execute at low health
    if (m_pAI->m_spells.warrior.pExecute &&
        (pVictim->GetHealthPercent() < 20.0f) &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.warrior.pExecute))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.warrior.pExecute) == SPELL_CAST_OK)
            return;
    }

    // Overpower when available
    if (m_pAI->m_spells.warrior.pOverpower &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.warrior.pOverpower))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.warrior.pOverpower) == SPELL_CAST_OK)
            return;
    }

    // Mortal Strike / Bloodthirst
    if (m_pAI->m_spells.warrior.pMortalStrike &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.warrior.pMortalStrike))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.warrior.pMortalStrike) == SPELL_CAST_OK)
            return;
    }

    if (m_pAI->m_spells.warrior.pBloodthirst &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.warrior.pBloodthirst))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.warrior.pBloodthirst) == SPELL_CAST_OK)
            return;
    }

    // Heroic Strike as filler
    if (m_pAI->m_spells.warrior.pHeroicStrike &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.warrior.pHeroicStrike))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.warrior.pHeroicStrike) == SPELL_CAST_OK)
            return;
    }

    // Battle Shout buff during combat
    if (m_pAI->m_spells.warrior.pBattleShout &&
        !pBot->HasAura(m_pAI->m_spells.warrior.pBattleShout->Id) &&
        m_pAI->CanTryToCastSpell(pBot, m_pAI->m_spells.warrior.pBattleShout))
    {
        if (m_pAI->DoCastSpell(pBot, m_pAI->m_spells.warrior.pBattleShout) == SPELL_CAST_OK)
            return;
    }
}

void WarriorCombat::UpdateOutOfCombat(Player* pBot)
{
    // Battle Shout
    if (m_pAI->m_spells.warrior.pBattleShout &&
        !pBot->HasAura(m_pAI->m_spells.warrior.pBattleShout->Id) &&
        m_pAI->CanTryToCastSpell(pBot, m_pAI->m_spells.warrior.pBattleShout))
    {
        m_pAI->DoCastSpell(pBot, m_pAI->m_spells.warrior.pBattleShout);
    }
}
