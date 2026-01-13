/*
 * DruidCombat.cpp
 *
 * Druid-specific combat handler.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "DruidCombat.h"
#include "CombatBotBaseAI.h"
#include "Player.h"
#include "Unit.h"
#include "MotionMaster.h"
#include "Log.h"

DruidCombat::DruidCombat(CombatBotBaseAI* pAI)
    : m_pAI(pAI)
{
}

bool DruidCombat::Engage(Player* pBot, Unit* pTarget)
{
    // Druids treated as melee/hybrid for now (Balance caster but engage like melee)
    if (pBot->Attack(pTarget, true))
    {
        pBot->GetMotionMaster()->MoveChase(pTarget);
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[DruidCombat] %s engaging %s (Attack success)",
            pBot->GetName(), pTarget->GetName());
        return true;
    }
    sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[DruidCombat] %s failed to engage %s (Attack returned false)",
        pBot->GetName(), pTarget->GetName());
    return false;
}

void DruidCombat::UpdateCombat(Player* pBot, Unit* pVictim)
{
    if (!pVictim)
        return;

    // Moonfire (DoT)
    if (m_pAI->m_spells.druid.pMoonfire &&
        !pVictim->HasAura(m_pAI->m_spells.druid.pMoonfire->Id) &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.druid.pMoonfire))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.druid.pMoonfire) == SPELL_CAST_OK)
            return;
    }

    // Wrath
    if (m_pAI->m_spells.druid.pWrath &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.druid.pWrath))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.druid.pWrath) == SPELL_CAST_OK)
            return;
    }

    // Starfire for bigger hits
    if (m_pAI->m_spells.druid.pStarfire &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.druid.pStarfire))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.druid.pStarfire) == SPELL_CAST_OK)
            return;
    }

    // Self heal if needed
    if (pBot->GetHealthPercent() < 40.0f)
    {
        if (m_pAI->FindAndHealInjuredAlly(40.0f, 0.0f))
            return;
    }
}

void DruidCombat::UpdateOutOfCombat(Player* pBot)
{
    // Mark of the Wild
    if (m_pAI->m_spells.druid.pMarkoftheWild &&
        !pBot->HasAura(m_pAI->m_spells.druid.pMarkoftheWild->Id) &&
        m_pAI->CanTryToCastSpell(pBot, m_pAI->m_spells.druid.pMarkoftheWild))
    {
        m_pAI->DoCastSpell(pBot, m_pAI->m_spells.druid.pMarkoftheWild);
    }

    // Thorns
    if (m_pAI->m_spells.druid.pThorns &&
        !pBot->HasAura(m_pAI->m_spells.druid.pThorns->Id) &&
        m_pAI->CanTryToCastSpell(pBot, m_pAI->m_spells.druid.pThorns))
    {
        m_pAI->DoCastSpell(pBot, m_pAI->m_spells.druid.pThorns);
    }
}
