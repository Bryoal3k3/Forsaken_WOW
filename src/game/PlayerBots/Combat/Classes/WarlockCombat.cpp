/*
 * WarlockCombat.cpp
 *
 * Warlock-specific combat handler.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "WarlockCombat.h"
#include "BotMovementManager.h"
#include "CombatBotBaseAI.h"
#include "../CombatHelpers.h"

WarlockCombat::WarlockCombat(CombatBotBaseAI* pAI)
    : m_pAI(pAI)
{
}

bool WarlockCombat::Engage(Player* pBot, Unit* pTarget)
{
    return CombatHelpers::EngageCaster(pBot, pTarget, "WarlockCombat", m_pMoveMgr);
}

void WarlockCombat::UpdateCombat(Player* pBot, Unit* pVictim)
{
    if (!pVictim)
        return;

    CombatHelpers::HandleRangedMovement(pBot, pVictim, 30.0f, m_pMoveMgr);

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

    // Fallback if all spells failed
    CombatHelpers::HandleCasterFallback(pBot, pVictim, "WarlockCombat", m_pMoveMgr);
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
