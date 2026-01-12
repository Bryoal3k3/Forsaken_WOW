/*
 * MageCombat.cpp
 *
 * Mage-specific combat handler.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "MageCombat.h"
#include "CombatBotBaseAI.h"
#include "Player.h"
#include "Unit.h"
#include "MotionMaster.h"

MageCombat::MageCombat(CombatBotBaseAI* pAI)
    : m_pAI(pAI)
{
}

bool MageCombat::Engage(Player* pBot, Unit* pTarget)
{
    // Casters don't melee auto-attack
    // Just set target - the first rotation spell will pull
    pBot->SetTargetGuid(pTarget->GetObjectGuid());
    return true;
}

void MageCombat::UpdateCombat(Player* pBot, Unit* pVictim)
{
    if (!pVictim)
        return;

    // Frost Nova if enemy is close
    if (m_pAI->m_spells.mage.pFrostNova &&
        pBot->CanReachWithMeleeAutoAttack(pVictim) &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.mage.pFrostNova))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.mage.pFrostNova) == SPELL_CAST_OK)
            return;
    }

    // Fire Blast (instant)
    if (m_pAI->m_spells.mage.pFireBlast &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.mage.pFireBlast))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.mage.pFireBlast) == SPELL_CAST_OK)
            return;
    }

    // Frostbolt
    if (m_pAI->m_spells.mage.pFrostbolt &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.mage.pFrostbolt))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.mage.pFrostbolt) == SPELL_CAST_OK)
            return;
    }

    // Fireball
    if (m_pAI->m_spells.mage.pFireball &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.mage.pFireball))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.mage.pFireball) == SPELL_CAST_OK)
            return;
    }
}

void MageCombat::UpdateOutOfCombat(Player* pBot)
{
    // Keep armor up
    if (m_pAI->m_spells.mage.pIceArmor &&
        !pBot->HasAura(m_pAI->m_spells.mage.pIceArmor->Id) &&
        m_pAI->CanTryToCastSpell(pBot, m_pAI->m_spells.mage.pIceArmor))
    {
        m_pAI->DoCastSpell(pBot, m_pAI->m_spells.mage.pIceArmor);
    }

    // Arcane Intellect
    if (m_pAI->m_spells.mage.pArcaneIntellect &&
        !pBot->HasAura(m_pAI->m_spells.mage.pArcaneIntellect->Id) &&
        m_pAI->CanTryToCastSpell(pBot, m_pAI->m_spells.mage.pArcaneIntellect))
    {
        m_pAI->DoCastSpell(pBot, m_pAI->m_spells.mage.pArcaneIntellect);
    }
}
