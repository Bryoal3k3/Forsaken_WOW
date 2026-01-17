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
#include "Log.h"

MageCombat::MageCombat(CombatBotBaseAI* pAI)
    : m_pAI(pAI)
{
}

bool MageCombat::Engage(Player* pBot, Unit* pTarget)
{
    // Use Attack(false) to establish combat state without melee swings
    // This sets GetVictim() and adds us to mob's attacker list
    // First spell in UpdateCombat() will deal damage and fully engage
    if (pBot->Attack(pTarget, false))
    {
        // Move into casting range (28 yards gives buffer for 30-yard spells)
        pBot->GetMotionMaster()->MoveChase(pTarget, 28.0f);

        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[MageCombat] %s engaging %s (Attack success, moving to range)",
            pBot->GetName(), pTarget->GetName());
        return true;
    }

    sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[MageCombat] %s failed to engage %s (Attack returned false)",
        pBot->GetName(), pTarget->GetName());
    return false;
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
