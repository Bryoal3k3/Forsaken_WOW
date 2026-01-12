/*
 * PriestCombat.cpp
 *
 * Priest-specific combat handler.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "PriestCombat.h"
#include "CombatBotBaseAI.h"
#include "Player.h"
#include "Unit.h"
#include "MotionMaster.h"

PriestCombat::PriestCombat(CombatBotBaseAI* pAI)
    : m_pAI(pAI)
{
}

bool PriestCombat::Engage(Player* pBot, Unit* pTarget)
{
    // Casters don't melee auto-attack
    // Just set target - the first rotation spell will pull
    pBot->SetTargetGuid(pTarget->GetObjectGuid());
    return true;
}

void PriestCombat::UpdateCombat(Player* pBot, Unit* pVictim)
{
    if (!pVictim)
        return;

    // Shield self at low health
    if (pBot->GetHealthPercent() < 50.0f &&
        m_pAI->m_spells.priest.pPowerWordShield &&
        !pBot->HasAura(m_pAI->m_spells.priest.pPowerWordShield->Id) &&
        m_pAI->CanTryToCastSpell(pBot, m_pAI->m_spells.priest.pPowerWordShield))
    {
        if (m_pAI->DoCastSpell(pBot, m_pAI->m_spells.priest.pPowerWordShield) == SPELL_CAST_OK)
            return;
    }

    // Heal self at low health
    if (pBot->GetHealthPercent() < 40.0f)
    {
        if (m_pAI->FindAndHealInjuredAlly(40.0f, 0.0f))
            return;
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
