/*
 * HunterCombat.cpp
 *
 * Hunter-specific combat handler.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "HunterCombat.h"
#include "CombatBotBaseAI.h"
#include "Player.h"
#include "Unit.h"
#include "MotionMaster.h"

HunterCombat::HunterCombat(CombatBotBaseAI* pAI)
    : m_pAI(pAI)
{
}

bool HunterCombat::Engage(Player* pBot, Unit* pTarget)
{
    // Set target
    pBot->SetTargetGuid(pTarget->GetObjectGuid());

    // Move to ranged position (~25 yards)
    float dist = pBot->GetDistance(pTarget);
    if (dist > 30.0f || dist < 8.0f)
    {
        pBot->GetMotionMaster()->MoveChase(pTarget, 25.0f);
    }

    // Start Auto Shot (spell ID 75)
    if (pBot->HasSpell(SPELL_AUTO_SHOT) && !pBot->IsMoving() && !pBot->IsNonMeleeSpellCasted())
    {
        pBot->CastSpell(pTarget, SPELL_AUTO_SHOT, false);
    }

    return true;
}

void HunterCombat::UpdateCombat(Player* pBot, Unit* pVictim)
{
    if (!pVictim)
        return;

    // Maintain Auto Shot
    if (pBot->HasSpell(SPELL_AUTO_SHOT) &&
        !pBot->IsMoving() &&
        (pBot->GetCombatDistance(pVictim) > 8.0f) &&
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
