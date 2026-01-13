/*
 * WarlockCombat.cpp
 *
 * Warlock-specific combat handler.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "WarlockCombat.h"
#include "CombatBotBaseAI.h"
#include "Player.h"
#include "Unit.h"
#include "MotionMaster.h"
#include "Log.h"

WarlockCombat::WarlockCombat(CombatBotBaseAI* pAI)
    : m_pAI(pAI)
{
}

bool WarlockCombat::Engage(Player* pBot, Unit* pTarget)
{
    // Use Attack(false) to establish combat state without melee swings
    // This sets GetVictim() and adds us to mob's attacker list
    // First spell in UpdateCombat() will deal damage and fully engage
    if (pBot->Attack(pTarget, false))
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[WarlockCombat] %s engaging %s (Attack success)",
            pBot->GetName(), pTarget->GetName());
        return true;
    }

    sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[WarlockCombat] %s failed to engage %s (Attack returned false)",
        pBot->GetName(), pTarget->GetName());
    return false;
}

void WarlockCombat::UpdateCombat(Player* pBot, Unit* pVictim)
{
    if (!pVictim)
        return;

    // Corruption
    if (m_pAI->m_spells.warlock.pCorruption &&
        !pVictim->HasAura(m_pAI->m_spells.warlock.pCorruption->Id) &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.warlock.pCorruption))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.warlock.pCorruption) == SPELL_CAST_OK)
            return;
    }

    // Curse of Agony
    if (m_pAI->m_spells.warlock.pCurseofAgony &&
        !pVictim->HasAura(m_pAI->m_spells.warlock.pCurseofAgony->Id) &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.warlock.pCurseofAgony))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.warlock.pCurseofAgony) == SPELL_CAST_OK)
            return;
    }

    // Immolate
    if (m_pAI->m_spells.warlock.pImmolate &&
        !pVictim->HasAura(m_pAI->m_spells.warlock.pImmolate->Id) &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.warlock.pImmolate))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.warlock.pImmolate) == SPELL_CAST_OK)
            return;
    }

    // Shadow Bolt
    if (m_pAI->m_spells.warlock.pShadowBolt &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.warlock.pShadowBolt))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.warlock.pShadowBolt) == SPELL_CAST_OK)
            return;
    }
}

void WarlockCombat::UpdateOutOfCombat(Player* pBot)
{
    // Demon Armor
    if (m_pAI->m_spells.warlock.pDemonArmor &&
        !pBot->HasAura(m_pAI->m_spells.warlock.pDemonArmor->Id) &&
        m_pAI->CanTryToCastSpell(pBot, m_pAI->m_spells.warlock.pDemonArmor))
    {
        m_pAI->DoCastSpell(pBot, m_pAI->m_spells.warlock.pDemonArmor);
    }
}
