/*
 * ShamanCombat.cpp
 *
 * Shaman-specific combat handler.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "ShamanCombat.h"
#include "CombatBotBaseAI.h"
#include "Player.h"
#include "Unit.h"
#include "MotionMaster.h"

ShamanCombat::ShamanCombat(CombatBotBaseAI* pAI)
    : m_pAI(pAI)
{
}

bool ShamanCombat::Engage(Player* pBot, Unit* pTarget)
{
    // Shamans treated as melee for now (Enhancement focus)
    if (pBot->Attack(pTarget, true))
    {
        pBot->GetMotionMaster()->MoveChase(pTarget);
        return true;
    }
    return false;
}

void ShamanCombat::UpdateCombat(Player* pBot, Unit* pVictim)
{
    if (!pVictim)
        return;

    // Earth Shock
    if (m_pAI->m_spells.shaman.pEarthShock &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.shaman.pEarthShock))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.shaman.pEarthShock) == SPELL_CAST_OK)
            return;
    }

    // Flame Shock (DoT)
    if (m_pAI->m_spells.shaman.pFlameShock &&
        !pVictim->HasAura(m_pAI->m_spells.shaman.pFlameShock->Id) &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.shaman.pFlameShock))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.shaman.pFlameShock) == SPELL_CAST_OK)
            return;
    }

    // Stormstrike (melee)
    if (m_pAI->m_spells.shaman.pStormstrike &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.shaman.pStormstrike))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.shaman.pStormstrike) == SPELL_CAST_OK)
            return;
    }

    // Lightning Bolt
    if (m_pAI->m_spells.shaman.pLightningBolt &&
        m_pAI->CanTryToCastSpell(pVictim, m_pAI->m_spells.shaman.pLightningBolt))
    {
        if (m_pAI->DoCastSpell(pVictim, m_pAI->m_spells.shaman.pLightningBolt) == SPELL_CAST_OK)
            return;
    }

    // Self heal if needed
    if (pBot->GetHealthPercent() < 40.0f)
    {
        if (m_pAI->FindAndHealInjuredAlly(40.0f, 0.0f))
            return;
    }
}

void ShamanCombat::UpdateOutOfCombat(Player* pBot)
{
    // Lightning Shield
    if (m_pAI->m_spells.shaman.pLightningShield &&
        !pBot->HasAura(m_pAI->m_spells.shaman.pLightningShield->Id) &&
        m_pAI->CanTryToCastSpell(pBot, m_pAI->m_spells.shaman.pLightningShield))
    {
        m_pAI->DoCastSpell(pBot, m_pAI->m_spells.shaman.pLightningShield);
    }
}
