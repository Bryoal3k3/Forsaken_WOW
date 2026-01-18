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
#include "Log.h"

PriestCombat::PriestCombat(CombatBotBaseAI* pAI)
    : m_pAI(pAI)
{
}

bool PriestCombat::Engage(Player* pBot, Unit* pTarget)
{
    // Use Attack(false) to establish combat state without melee swings
    // This sets GetVictim() and adds us to mob's attacker list
    // First spell in UpdateCombat() will deal damage and fully engage
    if (pBot->Attack(pTarget, false))
    {
        // Move into casting range (28 yards gives buffer for 30-yard spells)
        pBot->GetMotionMaster()->MoveChase(pTarget, 28.0f);

        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[PriestCombat] %s engaging %s (Attack success, moving to range)",
            pBot->GetName(), pTarget->GetName());
        return true;
    }

    sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[PriestCombat] %s failed to engage %s (Attack returned false)",
        pBot->GetName(), pTarget->GetName());
    return false;
}

void PriestCombat::UpdateCombat(Player* pBot, Unit* pVictim)
{
    if (!pVictim)
        return;

    // Only kite if target is snared/rooted, otherwise stand and fight
    bool targetIsSnared = pVictim->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED) ||
                          pVictim->HasAuraType(SPELL_AURA_MOD_ROOT);

    float dist = pBot->GetDistance(pVictim);
    bool inCastRange = dist <= 30.0f;

    // Only stop movement if we're in casting range AND target isn't snared
    // This allows approach movement to continue until we can cast
    if (inCastRange && !targetIsSnared && pBot->IsMoving())
    {
        pBot->StopMoving();
        pBot->GetMotionMaster()->Clear();
    }

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
