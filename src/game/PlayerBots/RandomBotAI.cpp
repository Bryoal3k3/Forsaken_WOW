/*
 * RandomBotAI.cpp
 *
 * AI class for autonomous RandomBots.
 * Combat rotations live here; high-level behavior delegated to strategies.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "RandomBotAI.h"
#include "Strategies/GrindingStrategy.h"
#include "Player.h"
#include "Creature.h"
#include "ObjectMgr.h"
#include "PlayerBotMgr.h"
#include "WorldSession.h"
#include "World.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "MotionMaster.h"
#include "Log.h"

#define RB_UPDATE_INTERVAL 1000

// ============================================================================
// Constructor / Destructor
// ============================================================================

RandomBotAI::RandomBotAI()
    : CombatBotBaseAI()
    , m_strategy(std::make_unique<GrindingStrategy>())
{
    m_updateTimer.Reset(1000);
}

RandomBotAI::~RandomBotAI() = default;

// ============================================================================
// Core Functions
// ============================================================================

bool RandomBotAI::OnSessionLoaded(PlayerBotEntry* entry, WorldSession* sess)
{
    // RandomBots are loaded from database, not spawned fresh
    sess->LoginPlayer(entry->playerGUID);
    return true;
}

void RandomBotAI::OnPlayerLogin()
{
    if (!m_initialized)
        me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING);
}

void RandomBotAI::UpdateAI(uint32 const diff)
{
    // Throttle updates
    m_updateTimer.Update(diff);
    if (m_updateTimer.Passed())
        m_updateTimer.Reset(RB_UPDATE_INTERVAL);
    else
        return;

    if (!me->IsInWorld() || me->IsBeingTeleported())
        return;

    // Handle teleport acks
    if (me->IsBeingTeleportedNear())
    {
        WorldPacket data(MSG_MOVE_TELEPORT_ACK, 10);
        data << me->GetObjectGuid();
        data << uint32(0) << uint32(0);
        me->GetSession()->HandleMoveTeleportAckOpcode(data);
        return;
    }
    if (me->IsBeingTeleportedFar())
    {
        me->GetSession()->HandleMoveWorldportAckOpcode();
        return;
    }

    // One-time initialization
    if (!m_initialized)
    {
        // Auto-assign role based on class/spec
        if (m_role == ROLE_INVALID)
            AutoAssignRole();

        // Disable GM mode if it was somehow enabled
        if (me->IsGameMaster())
            me->SetGameMaster(false);

        // Learn spells and populate spell data for combat
        ResetSpellData();
        PopulateSpellData();

        // Summon pet for hunters/warlocks
        SummonPetIfNeeded();

        // Ensure full health/mana
        me->SetHealthPercent(100.0f);
        me->SetPowerPercent(me->GetPowerType(), 100.0f);

        // Update zone
        uint32 newzone, newarea;
        me->GetZoneAndAreaId(newzone, newarea);
        me->UpdateZone(newzone, newarea);

        // Clear spawning flag
        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING);

        m_initialized = true;

        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[RandomBotAI] Bot %s initialized (Class: %u, Level: %u, Strategy: %s)",
            me->GetName(), me->GetClass(), me->GetLevel(), m_strategy->GetName());
        return;
    }

    // Dead? Reset behaviors and wait (future phases will handle respawning)
    if (!me->IsAlive())
    {
        ResetBehaviors();
        return;
    }

    // Track combat state transitions
    bool inCombat = me->IsInCombat();
    if (m_wasInCombat && !inCombat)
    {
        // Just left combat - trigger looting
        m_looting.OnCombatEnded(me);
    }
    m_wasInCombat = inCombat;

    // Combat logic
    if (inCombat && me->GetVictim())
    {
        UpdateInCombatAI();
    }
    else
    {
        // Let looting run first (universal behavior)
        if (m_looting.Update(me, diff))
            return;  // Busy looting

        UpdateOutOfCombatAI();
    }
}

void RandomBotAI::ResetBehaviors()
{
    m_looting.Reset();
    m_wasInCombat = false;
}

// ============================================================================
// Combat AI - Main Entry Points
// ============================================================================

void RandomBotAI::UpdateInCombatAI()
{
    Unit* pVictim = me->GetVictim();
    if (!pVictim || pVictim->IsDead())
    {
        // Find a new target - anyone attacking us
        if (!me->GetAttackers().empty())
        {
            Unit* pAttacker = *me->GetAttackers().begin();
            if (pAttacker && pAttacker->IsAlive())
            {
                me->Attack(pAttacker, true);
                pVictim = pAttacker;
            }
        }
    }

    if (!pVictim)
        return;

    // Move to target if needed
    if (!me->CanReachWithMeleeAutoAttack(pVictim) && CombatBotBaseAI::IsMeleeDamageClass(me->GetClass()))
    {
        me->GetMotionMaster()->MoveChase(pVictim);
    }

    // Class-specific combat
    switch (me->GetClass())
    {
        case CLASS_PALADIN:
            UpdateInCombatAI_Paladin();
            break;
        case CLASS_SHAMAN:
            UpdateInCombatAI_Shaman();
            break;
        case CLASS_HUNTER:
            UpdateInCombatAI_Hunter();
            break;
        case CLASS_MAGE:
            UpdateInCombatAI_Mage();
            break;
        case CLASS_PRIEST:
            UpdateInCombatAI_Priest();
            break;
        case CLASS_WARLOCK:
            UpdateInCombatAI_Warlock();
            break;
        case CLASS_WARRIOR:
            UpdateInCombatAI_Warrior();
            break;
        case CLASS_ROGUE:
            UpdateInCombatAI_Rogue();
            break;
        case CLASS_DRUID:
            UpdateInCombatAI_Druid();
            break;
    }
}

void RandomBotAI::UpdateOutOfCombatAI()
{
    // Check if someone is attacking us - respond immediately
    if (!me->GetAttackers().empty())
    {
        Unit* pAttacker = *me->GetAttackers().begin();
        if (pAttacker && pAttacker->IsAlive() && me->IsValidAttackTarget(pAttacker))
        {
            me->Attack(pAttacker, true);
            return;
        }
    }

    // Delegate to strategy for high-level behavior (finding targets, etc.)
    if (m_strategy && m_strategy->Update(me, 0))
        return;

    // Class-specific out of combat behavior (buffs, etc.)
    switch (me->GetClass())
    {
        case CLASS_PALADIN:
            UpdateOutOfCombatAI_Paladin();
            break;
        case CLASS_SHAMAN:
            UpdateOutOfCombatAI_Shaman();
            break;
        case CLASS_HUNTER:
            UpdateOutOfCombatAI_Hunter();
            break;
        case CLASS_MAGE:
            UpdateOutOfCombatAI_Mage();
            break;
        case CLASS_PRIEST:
            UpdateOutOfCombatAI_Priest();
            break;
        case CLASS_WARLOCK:
            UpdateOutOfCombatAI_Warlock();
            break;
        case CLASS_WARRIOR:
            UpdateOutOfCombatAI_Warrior();
            break;
        case CLASS_ROGUE:
            UpdateOutOfCombatAI_Rogue();
            break;
        case CLASS_DRUID:
            UpdateOutOfCombatAI_Druid();
            break;
    }
}

// ============================================================================
// Class-Specific Combat - Warrior
// ============================================================================

void RandomBotAI::UpdateInCombatAI_Warrior()
{
    Unit* pVictim = me->GetVictim();
    if (!pVictim)
        return;

    // Execute at low health
    if (m_spells.warrior.pExecute &&
        (pVictim->GetHealthPercent() < 20.0f) &&
        CanTryToCastSpell(pVictim, m_spells.warrior.pExecute))
    {
        if (DoCastSpell(pVictim, m_spells.warrior.pExecute) == SPELL_CAST_OK)
            return;
    }

    // Overpower when available
    if (m_spells.warrior.pOverpower &&
        CanTryToCastSpell(pVictim, m_spells.warrior.pOverpower))
    {
        if (DoCastSpell(pVictim, m_spells.warrior.pOverpower) == SPELL_CAST_OK)
            return;
    }

    // Mortal Strike / Bloodthirst
    if (m_spells.warrior.pMortalStrike &&
        CanTryToCastSpell(pVictim, m_spells.warrior.pMortalStrike))
    {
        if (DoCastSpell(pVictim, m_spells.warrior.pMortalStrike) == SPELL_CAST_OK)
            return;
    }

    if (m_spells.warrior.pBloodthirst &&
        CanTryToCastSpell(pVictim, m_spells.warrior.pBloodthirst))
    {
        if (DoCastSpell(pVictim, m_spells.warrior.pBloodthirst) == SPELL_CAST_OK)
            return;
    }

    // Heroic Strike as filler
    if (m_spells.warrior.pHeroicStrike &&
        CanTryToCastSpell(pVictim, m_spells.warrior.pHeroicStrike))
    {
        if (DoCastSpell(pVictim, m_spells.warrior.pHeroicStrike) == SPELL_CAST_OK)
            return;
    }

    // Battle Shout buff
    if (m_spells.warrior.pBattleShout &&
        !me->HasAura(m_spells.warrior.pBattleShout->Id) &&
        CanTryToCastSpell(me, m_spells.warrior.pBattleShout))
    {
        if (DoCastSpell(me, m_spells.warrior.pBattleShout) == SPELL_CAST_OK)
            return;
    }
}

void RandomBotAI::UpdateOutOfCombatAI_Warrior()
{
    // Battle Shout
    if (m_spells.warrior.pBattleShout &&
        !me->HasAura(m_spells.warrior.pBattleShout->Id) &&
        CanTryToCastSpell(me, m_spells.warrior.pBattleShout))
    {
        DoCastSpell(me, m_spells.warrior.pBattleShout);
    }
}

// ============================================================================
// Class-Specific Combat - Paladin
// ============================================================================

void RandomBotAI::UpdateInCombatAI_Paladin()
{
    Unit* pVictim = me->GetVictim();
    if (!pVictim)
        return;

    // Judgement
    if (m_spells.paladin.pJudgement &&
        CanTryToCastSpell(pVictim, m_spells.paladin.pJudgement))
    {
        if (DoCastSpell(pVictim, m_spells.paladin.pJudgement) == SPELL_CAST_OK)
            return;
    }

    // Hammer of Wrath at low health
    if (m_spells.paladin.pHammerOfWrath &&
        (pVictim->GetHealthPercent() < 20.0f) &&
        CanTryToCastSpell(pVictim, m_spells.paladin.pHammerOfWrath))
    {
        if (DoCastSpell(pVictim, m_spells.paladin.pHammerOfWrath) == SPELL_CAST_OK)
            return;
    }

    // Consecration
    if (m_spells.paladin.pConsecration &&
        CanTryToCastSpell(me, m_spells.paladin.pConsecration))
    {
        if (DoCastSpell(me, m_spells.paladin.pConsecration) == SPELL_CAST_OK)
            return;
    }

    // Holy Shield for protection
    if (m_spells.paladin.pHolyShield &&
        CanTryToCastSpell(me, m_spells.paladin.pHolyShield))
    {
        if (DoCastSpell(me, m_spells.paladin.pHolyShield) == SPELL_CAST_OK)
            return;
    }

    // Self heal at low health
    if (me->GetHealthPercent() < 30.0f)
    {
        if (FindAndHealInjuredAlly(30.0f, 0.0f))
            return;
    }
}

void RandomBotAI::UpdateOutOfCombatAI_Paladin()
{
    // Keep seal up
    if (m_spells.paladin.pSeal &&
        !me->HasAura(m_spells.paladin.pSeal->Id) &&
        CanTryToCastSpell(me, m_spells.paladin.pSeal))
    {
        DoCastSpell(me, m_spells.paladin.pSeal);
    }

    // Keep aura up
    if (m_spells.paladin.pAura &&
        !me->HasAura(m_spells.paladin.pAura->Id) &&
        CanTryToCastSpell(me, m_spells.paladin.pAura))
    {
        DoCastSpell(me, m_spells.paladin.pAura);
    }
}

// ============================================================================
// Class-Specific Combat - Hunter
// ============================================================================

void RandomBotAI::UpdateInCombatAI_Hunter()
{
    Unit* pVictim = me->GetVictim();
    if (!pVictim)
        return;

    // Auto shot is handled automatically, use abilities

    // Hunter's Mark
    if (m_spells.hunter.pHuntersMark &&
        !pVictim->HasAura(m_spells.hunter.pHuntersMark->Id) &&
        CanTryToCastSpell(pVictim, m_spells.hunter.pHuntersMark))
    {
        if (DoCastSpell(pVictim, m_spells.hunter.pHuntersMark) == SPELL_CAST_OK)
            return;
    }

    // Aimed Shot
    if (m_spells.hunter.pAimedShot &&
        CanTryToCastSpell(pVictim, m_spells.hunter.pAimedShot))
    {
        if (DoCastSpell(pVictim, m_spells.hunter.pAimedShot) == SPELL_CAST_OK)
            return;
    }

    // Multi-Shot
    if (m_spells.hunter.pMultiShot &&
        CanTryToCastSpell(pVictim, m_spells.hunter.pMultiShot))
    {
        if (DoCastSpell(pVictim, m_spells.hunter.pMultiShot) == SPELL_CAST_OK)
            return;
    }

    // Arcane Shot
    if (m_spells.hunter.pArcaneShot &&
        CanTryToCastSpell(pVictim, m_spells.hunter.pArcaneShot))
    {
        if (DoCastSpell(pVictim, m_spells.hunter.pArcaneShot) == SPELL_CAST_OK)
            return;
    }

    // Serpent Sting
    if (m_spells.hunter.pSerpentSting &&
        !pVictim->HasAura(m_spells.hunter.pSerpentSting->Id) &&
        CanTryToCastSpell(pVictim, m_spells.hunter.pSerpentSting))
    {
        if (DoCastSpell(pVictim, m_spells.hunter.pSerpentSting) == SPELL_CAST_OK)
            return;
    }
}

void RandomBotAI::UpdateOutOfCombatAI_Hunter()
{
    // Keep aspect up
    if (m_spells.hunter.pAspectOfTheHawk &&
        !me->HasAura(m_spells.hunter.pAspectOfTheHawk->Id) &&
        CanTryToCastSpell(me, m_spells.hunter.pAspectOfTheHawk))
    {
        DoCastSpell(me, m_spells.hunter.pAspectOfTheHawk);
    }
}

// ============================================================================
// Class-Specific Combat - Mage
// ============================================================================

void RandomBotAI::UpdateInCombatAI_Mage()
{
    Unit* pVictim = me->GetVictim();
    if (!pVictim)
        return;

    // Frost Nova if enemy is close
    if (m_spells.mage.pFrostNova &&
        me->CanReachWithMeleeAutoAttack(pVictim) &&
        CanTryToCastSpell(pVictim, m_spells.mage.pFrostNova))
    {
        if (DoCastSpell(pVictim, m_spells.mage.pFrostNova) == SPELL_CAST_OK)
            return;
    }

    // Fire Blast (instant)
    if (m_spells.mage.pFireBlast &&
        CanTryToCastSpell(pVictim, m_spells.mage.pFireBlast))
    {
        if (DoCastSpell(pVictim, m_spells.mage.pFireBlast) == SPELL_CAST_OK)
            return;
    }

    // Frostbolt
    if (m_spells.mage.pFrostbolt &&
        CanTryToCastSpell(pVictim, m_spells.mage.pFrostbolt))
    {
        if (DoCastSpell(pVictim, m_spells.mage.pFrostbolt) == SPELL_CAST_OK)
            return;
    }

    // Fireball
    if (m_spells.mage.pFireball &&
        CanTryToCastSpell(pVictim, m_spells.mage.pFireball))
    {
        if (DoCastSpell(pVictim, m_spells.mage.pFireball) == SPELL_CAST_OK)
            return;
    }
}

void RandomBotAI::UpdateOutOfCombatAI_Mage()
{
    // Keep armor up
    if (m_spells.mage.pIceArmor &&
        !me->HasAura(m_spells.mage.pIceArmor->Id) &&
        CanTryToCastSpell(me, m_spells.mage.pIceArmor))
    {
        DoCastSpell(me, m_spells.mage.pIceArmor);
    }

    // Arcane Intellect
    if (m_spells.mage.pArcaneIntellect &&
        !me->HasAura(m_spells.mage.pArcaneIntellect->Id) &&
        CanTryToCastSpell(me, m_spells.mage.pArcaneIntellect))
    {
        DoCastSpell(me, m_spells.mage.pArcaneIntellect);
    }
}

// ============================================================================
// Class-Specific Combat - Priest
// ============================================================================

void RandomBotAI::UpdateInCombatAI_Priest()
{
    Unit* pVictim = me->GetVictim();
    if (!pVictim)
        return;

    // Shield self at low health
    if (me->GetHealthPercent() < 50.0f &&
        m_spells.priest.pPowerWordShield &&
        !me->HasAura(m_spells.priest.pPowerWordShield->Id) &&
        CanTryToCastSpell(me, m_spells.priest.pPowerWordShield))
    {
        if (DoCastSpell(me, m_spells.priest.pPowerWordShield) == SPELL_CAST_OK)
            return;
    }

    // Heal self at low health
    if (me->GetHealthPercent() < 40.0f)
    {
        if (FindAndHealInjuredAlly(40.0f, 0.0f))
            return;
    }

    // Shadow Word: Pain
    if (m_spells.priest.pShadowWordPain &&
        !pVictim->HasAura(m_spells.priest.pShadowWordPain->Id) &&
        CanTryToCastSpell(pVictim, m_spells.priest.pShadowWordPain))
    {
        if (DoCastSpell(pVictim, m_spells.priest.pShadowWordPain) == SPELL_CAST_OK)
            return;
    }

    // Mind Blast
    if (m_spells.priest.pMindBlast &&
        CanTryToCastSpell(pVictim, m_spells.priest.pMindBlast))
    {
        if (DoCastSpell(pVictim, m_spells.priest.pMindBlast) == SPELL_CAST_OK)
            return;
    }

    // Smite
    if (m_spells.priest.pSmite &&
        CanTryToCastSpell(pVictim, m_spells.priest.pSmite))
    {
        if (DoCastSpell(pVictim, m_spells.priest.pSmite) == SPELL_CAST_OK)
            return;
    }
}

void RandomBotAI::UpdateOutOfCombatAI_Priest()
{
    // Power Word: Fortitude
    if (m_spells.priest.pPowerWordFortitude &&
        !me->HasAura(m_spells.priest.pPowerWordFortitude->Id) &&
        CanTryToCastSpell(me, m_spells.priest.pPowerWordFortitude))
    {
        DoCastSpell(me, m_spells.priest.pPowerWordFortitude);
    }

    // Inner Fire
    if (m_spells.priest.pInnerFire &&
        !me->HasAura(m_spells.priest.pInnerFire->Id) &&
        CanTryToCastSpell(me, m_spells.priest.pInnerFire))
    {
        DoCastSpell(me, m_spells.priest.pInnerFire);
    }
}

// ============================================================================
// Class-Specific Combat - Warlock
// ============================================================================

void RandomBotAI::UpdateInCombatAI_Warlock()
{
    Unit* pVictim = me->GetVictim();
    if (!pVictim)
        return;

    // Corruption
    if (m_spells.warlock.pCorruption &&
        !pVictim->HasAura(m_spells.warlock.pCorruption->Id) &&
        CanTryToCastSpell(pVictim, m_spells.warlock.pCorruption))
    {
        if (DoCastSpell(pVictim, m_spells.warlock.pCorruption) == SPELL_CAST_OK)
            return;
    }

    // Curse of Agony
    if (m_spells.warlock.pCurseofAgony &&
        !pVictim->HasAura(m_spells.warlock.pCurseofAgony->Id) &&
        CanTryToCastSpell(pVictim, m_spells.warlock.pCurseofAgony))
    {
        if (DoCastSpell(pVictim, m_spells.warlock.pCurseofAgony) == SPELL_CAST_OK)
            return;
    }

    // Immolate
    if (m_spells.warlock.pImmolate &&
        !pVictim->HasAura(m_spells.warlock.pImmolate->Id) &&
        CanTryToCastSpell(pVictim, m_spells.warlock.pImmolate))
    {
        if (DoCastSpell(pVictim, m_spells.warlock.pImmolate) == SPELL_CAST_OK)
            return;
    }

    // Shadow Bolt
    if (m_spells.warlock.pShadowBolt &&
        CanTryToCastSpell(pVictim, m_spells.warlock.pShadowBolt))
    {
        if (DoCastSpell(pVictim, m_spells.warlock.pShadowBolt) == SPELL_CAST_OK)
            return;
    }
}

void RandomBotAI::UpdateOutOfCombatAI_Warlock()
{
    // Demon Armor
    if (m_spells.warlock.pDemonArmor &&
        !me->HasAura(m_spells.warlock.pDemonArmor->Id) &&
        CanTryToCastSpell(me, m_spells.warlock.pDemonArmor))
    {
        DoCastSpell(me, m_spells.warlock.pDemonArmor);
    }
}

// ============================================================================
// Class-Specific Combat - Rogue
// ============================================================================

void RandomBotAI::UpdateInCombatAI_Rogue()
{
    Unit* pVictim = me->GetVictim();
    if (!pVictim)
        return;

    // Slice and Dice if we have combo points
    if (m_spells.rogue.pSliceAndDice &&
        me->GetComboPoints() >= 2 &&
        !me->HasAura(m_spells.rogue.pSliceAndDice->Id) &&
        CanTryToCastSpell(pVictim, m_spells.rogue.pSliceAndDice))
    {
        if (DoCastSpell(pVictim, m_spells.rogue.pSliceAndDice) == SPELL_CAST_OK)
            return;
    }

    // Eviscerate at 5 combo points
    if (m_spells.rogue.pEviscerate &&
        me->GetComboPoints() >= 5 &&
        CanTryToCastSpell(pVictim, m_spells.rogue.pEviscerate))
    {
        if (DoCastSpell(pVictim, m_spells.rogue.pEviscerate) == SPELL_CAST_OK)
            return;
    }

    // Sinister Strike to build combo points
    if (m_spells.rogue.pSinisterStrike &&
        CanTryToCastSpell(pVictim, m_spells.rogue.pSinisterStrike))
    {
        if (DoCastSpell(pVictim, m_spells.rogue.pSinisterStrike) == SPELL_CAST_OK)
            return;
    }
}

void RandomBotAI::UpdateOutOfCombatAI_Rogue()
{
    // Nothing special for now
}

// ============================================================================
// Class-Specific Combat - Shaman
// ============================================================================

void RandomBotAI::UpdateInCombatAI_Shaman()
{
    Unit* pVictim = me->GetVictim();
    if (!pVictim)
        return;

    // Earth Shock
    if (m_spells.shaman.pEarthShock &&
        CanTryToCastSpell(pVictim, m_spells.shaman.pEarthShock))
    {
        if (DoCastSpell(pVictim, m_spells.shaman.pEarthShock) == SPELL_CAST_OK)
            return;
    }

    // Flame Shock (DoT)
    if (m_spells.shaman.pFlameShock &&
        !pVictim->HasAura(m_spells.shaman.pFlameShock->Id) &&
        CanTryToCastSpell(pVictim, m_spells.shaman.pFlameShock))
    {
        if (DoCastSpell(pVictim, m_spells.shaman.pFlameShock) == SPELL_CAST_OK)
            return;
    }

    // Stormstrike (melee)
    if (m_spells.shaman.pStormstrike &&
        CanTryToCastSpell(pVictim, m_spells.shaman.pStormstrike))
    {
        if (DoCastSpell(pVictim, m_spells.shaman.pStormstrike) == SPELL_CAST_OK)
            return;
    }

    // Lightning Bolt
    if (m_spells.shaman.pLightningBolt &&
        CanTryToCastSpell(pVictim, m_spells.shaman.pLightningBolt))
    {
        if (DoCastSpell(pVictim, m_spells.shaman.pLightningBolt) == SPELL_CAST_OK)
            return;
    }

    // Self heal if needed
    if (me->GetHealthPercent() < 40.0f)
    {
        if (FindAndHealInjuredAlly(40.0f, 0.0f))
            return;
    }
}

void RandomBotAI::UpdateOutOfCombatAI_Shaman()
{
    // Lightning Shield
    if (m_spells.shaman.pLightningShield &&
        !me->HasAura(m_spells.shaman.pLightningShield->Id) &&
        CanTryToCastSpell(me, m_spells.shaman.pLightningShield))
    {
        DoCastSpell(me, m_spells.shaman.pLightningShield);
    }
}

// ============================================================================
// Class-Specific Combat - Druid
// ============================================================================

void RandomBotAI::UpdateInCombatAI_Druid()
{
    Unit* pVictim = me->GetVictim();
    if (!pVictim)
        return;

    // Moonfire (DoT)
    if (m_spells.druid.pMoonfire &&
        !pVictim->HasAura(m_spells.druid.pMoonfire->Id) &&
        CanTryToCastSpell(pVictim, m_spells.druid.pMoonfire))
    {
        if (DoCastSpell(pVictim, m_spells.druid.pMoonfire) == SPELL_CAST_OK)
            return;
    }

    // Wrath
    if (m_spells.druid.pWrath &&
        CanTryToCastSpell(pVictim, m_spells.druid.pWrath))
    {
        if (DoCastSpell(pVictim, m_spells.druid.pWrath) == SPELL_CAST_OK)
            return;
    }

    // Starfire for bigger hits
    if (m_spells.druid.pStarfire &&
        CanTryToCastSpell(pVictim, m_spells.druid.pStarfire))
    {
        if (DoCastSpell(pVictim, m_spells.druid.pStarfire) == SPELL_CAST_OK)
            return;
    }

    // Self heal if needed
    if (me->GetHealthPercent() < 40.0f)
    {
        if (FindAndHealInjuredAlly(40.0f, 0.0f))
            return;
    }
}

void RandomBotAI::UpdateOutOfCombatAI_Druid()
{
    // Mark of the Wild
    if (m_spells.druid.pMarkoftheWild &&
        !me->HasAura(m_spells.druid.pMarkoftheWild->Id) &&
        CanTryToCastSpell(me, m_spells.druid.pMarkoftheWild))
    {
        DoCastSpell(me, m_spells.druid.pMarkoftheWild);
    }

    // Thorns
    if (m_spells.druid.pThorns &&
        !me->HasAura(m_spells.druid.pThorns->Id) &&
        CanTryToCastSpell(me, m_spells.druid.pThorns))
    {
        DoCastSpell(me, m_spells.druid.pThorns);
    }
}
