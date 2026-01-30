/*
 * PriestCombat.cpp
 *
 * Priest-specific combat handler.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "PriestCombat.h"
#include "BotMovementManager.h"
#include "CombatBotBaseAI.h"
#include "../CombatHelpers.h"

PriestCombat::PriestCombat(CombatBotBaseAI* pAI)
    : m_pAI(pAI)
{
}

bool PriestCombat::Engage(Player* pBot, Unit* pTarget)
{
    return CombatHelpers::EngageCaster(pBot, pTarget, "PriestCombat", m_pMoveMgr);
}

void PriestCombat::UpdateCombat(Player* pBot, Unit* pVictim)
{
    if (!pVictim)
        return;

    CombatHelpers::HandleRangedMovement(pBot, pVictim, 30.0f, m_pMoveMgr);

    // Shield self at low health
    if (pBot->GetHealthPercent() < 50.0f &&
        m_pAI->m_spells.priest.pPowerWordShield &&
        !pBot->HasAura(m_pAI->m_spells.priest.pPowerWordShield->Id) &&
        m_pAI->CanTryToCastSpell(pBot, m_pAI->m_spells.priest.pPowerWordShield))
    {
        if (m_pAI->DoCastSpell(pBot, m_pAI->m_spells.priest.pPowerWordShield) == SPELL_CAST_OK)
            return;
    }

    // Heal self at low health (try to heal, but continue to damage spells regardless)
    if (pBot->GetHealthPercent() < 40.0f)
    {
        m_pAI->FindAndHealInjuredAlly(40.0f, 0.0f);
    }

    // Shadow Word: Pain
    if (m_pAI->m_spells.priest.pShadowWordPain &&
        !pVictim->HasAura(m_pAI->m_spells.priest.pShadowWordPain->Id) &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.priest.pShadowWordPain))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.priest.pShadowWordPain) == SPELL_CAST_OK)
            return;
    }

    // Mind Blast
    if (m_pAI->m_spells.priest.pMindBlast &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.priest.pMindBlast))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.priest.pMindBlast) == SPELL_CAST_OK)
            return;
    }

    // Smite
    if (m_pAI->m_spells.priest.pSmite &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.priest.pSmite))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.priest.pSmite) == SPELL_CAST_OK)
            return;
    }

    // Fallback if all spells failed
    CombatHelpers::HandleCasterFallback(pBot, pVictim, "PriestCombat", m_pMoveMgr);
}

void PriestCombat::UpdateOutOfCombat(Player* pBot)
{
    // Power Word: Fortitude
    if (m_pAI->m_spells.priest.pPowerWordFortitude &&
        !pBot->HasAura(m_pAI->m_spells.priest.pPowerWordFortitude->Id) &&
        m_pAI->CanTryToCastSpell(pBot, m_pAI->m_spells.priest.pPowerWordFortitude))
    {
        m_pAI->DoCastSpell(pBot, m_pAI->m_spells.priest.pPowerWordFortitude);
    }

    // Inner Fire
    if (m_pAI->m_spells.priest.pInnerFire &&
        !pBot->HasAura(m_pAI->m_spells.priest.pInnerFire->Id) &&
        m_pAI->CanTryToCastSpell(pBot, m_pAI->m_spells.priest.pInnerFire))
    {
        m_pAI->DoCastSpell(pBot, m_pAI->m_spells.priest.pInnerFire);
    }
}
