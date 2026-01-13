/*
 * PaladinCombat.cpp
 *
 * Paladin-specific combat handler.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "PaladinCombat.h"
#include "CombatBotBaseAI.h"
#include "Player.h"
#include "Unit.h"
#include "MotionMaster.h"
#include "Log.h"

PaladinCombat::PaladinCombat(CombatBotBaseAI* pAI)
    : m_pAI(pAI)
{
}

bool PaladinCombat::Engage(Player* pBot, Unit* pTarget)
{
    // Paladins use melee - Attack + Chase
    if (pBot->Attack(pTarget, true))
    {
        pBot->GetMotionMaster()->MoveChase(pTarget);
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[PaladinCombat] %s engaging %s (Attack success)",
            pBot->GetName(), pTarget->GetName());
        return true;
    }
    sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[PaladinCombat] %s failed to engage %s (Attack returned false)",
        pBot->GetName(), pTarget->GetName());
    return false;
}

void PaladinCombat::UpdateCombat(Player* pBot, Unit* pVictim)
{
    if (!pVictim)
        return;

    // Judgement
    if (m_pAI->m_spells.paladin.pJudgement &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.paladin.pJudgement))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.paladin.pJudgement) == SPELL_CAST_OK)
            return;
    }

    // Hammer of Wrath at low health
    if (m_pAI->m_spells.paladin.pHammerOfWrath &&
        (pVictim->GetHealthPercent() < 20.0f) &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.paladin.pHammerOfWrath))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.paladin.pHammerOfWrath) == SPELL_CAST_OK)
            return;
    }

    // Consecration
    if (m_pAI->m_spells.paladin.pConsecration &&
        m_pAI->CanTryToCastSpell(pBot, m_pAI->m_spells.paladin.pConsecration))
    {
        if (m_pAI->DoCastSpell(pBot, m_pAI->m_spells.paladin.pConsecration) == SPELL_CAST_OK)
            return;
    }

    // Holy Shield for protection
    if (m_pAI->m_spells.paladin.pHolyShield &&
        m_pAI->CanTryToCastSpell(pBot, m_pAI->m_spells.paladin.pHolyShield))
    {
        if (m_pAI->DoCastSpell(pBot, m_pAI->m_spells.paladin.pHolyShield) == SPELL_CAST_OK)
            return;
    }

    // Self heal at low health
    if (pBot->GetHealthPercent() < 30.0f)
    {
        if (m_pAI->FindAndHealInjuredAlly(30.0f, 0.0f))
            return;
    }
}

void PaladinCombat::UpdateOutOfCombat(Player* pBot)
{
    // Keep seal up
    if (m_pAI->m_spells.paladin.pSeal &&
        !pBot->HasAura(m_pAI->m_spells.paladin.pSeal->Id) &&
        m_pAI->CanTryToCastSpell(pBot, m_pAI->m_spells.paladin.pSeal))
    {
        m_pAI->DoCastSpell(pBot, m_pAI->m_spells.paladin.pSeal);
    }

    // Keep aura up
    if (m_pAI->m_spells.paladin.pAura &&
        !pBot->HasAura(m_pAI->m_spells.paladin.pAura->Id) &&
        m_pAI->CanTryToCastSpell(pBot, m_pAI->m_spells.paladin.pAura))
    {
        m_pAI->DoCastSpell(pBot, m_pAI->m_spells.paladin.pAura);
    }
}
