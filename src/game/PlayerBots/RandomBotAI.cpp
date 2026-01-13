/*
 * RandomBotAI.cpp
 *
 * AI class for autonomous RandomBots.
 * Combat rotations live here; high-level behavior delegated to strategies.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "RandomBotAI.h"
#include "Combat/BotCombatMgr.h"
#include "Strategies/GrindingStrategy.h"
#include "Strategies/GhostWalkingStrategy.h"
#include "Strategies/VendoringStrategy.h"
#include "Player.h"
#include "Creature.h"
#include "Corpse.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
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
    , m_ghostStrategy(std::make_unique<GhostWalkingStrategy>())
    , m_vendoringStrategy(std::make_unique<VendoringStrategy>())
    , m_combatMgr(std::make_unique<BotCombatMgr>())
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

        // Initialize combat manager with spell data
        m_combatMgr->Initialize(me, this);

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

    // Dead? Use ghost walking strategy
    if (!me->IsAlive())
    {
        m_ghostStrategy->Update(me, RB_UPDATE_INTERVAL);
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

    // Handle resting (cheat: no food/drink items needed)
    // This must be checked early - if combat starts while resting, bot stands up
    if (BotCheats::HandleResting(me, RB_UPDATE_INTERVAL, m_isResting, m_restingTickTimer))
        return;  // Busy resting

    // Combat logic
    // Use OR instead of AND: run combat AI if in combat OR if we have a victim
    // This fixes ranged classes where Attack(false) sets victim but doesn't enter combat
    // until the first spell deals damage
    if (inCombat || me->GetVictim())
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
    m_isResting = false;
    m_restingTickTimer = 0;
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

    // Delegate combat to the combat manager
    m_combatMgr->UpdateCombat(me, pVictim);
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

    // Check vendoring - bags full or gear broken?
    // This runs before grinding so bots don't keep trying to loot with full bags
    if (m_vendoringStrategy && m_vendoringStrategy->Update(me, RB_UPDATE_INTERVAL))
        return;  // Busy vendoring

    // Delegate to strategy for high-level behavior (finding targets, etc.)
    if (m_strategy && m_strategy->Update(me, 0))
        return;

    // Delegate out of combat (buffs, etc.) to combat manager
    m_combatMgr->UpdateOutOfCombat(me);
}

// ============================================================================
// Class-Specific Combat Stubs
// These are required by CombatBotBaseAI but now delegate to BotCombatMgr
// ============================================================================

void RandomBotAI::UpdateInCombatAI_Warrior() { }
void RandomBotAI::UpdateOutOfCombatAI_Warrior() { }
void RandomBotAI::UpdateInCombatAI_Paladin() { }
void RandomBotAI::UpdateOutOfCombatAI_Paladin() { }
void RandomBotAI::UpdateInCombatAI_Hunter() { }
void RandomBotAI::UpdateOutOfCombatAI_Hunter() { }
void RandomBotAI::UpdateInCombatAI_Mage() { }
void RandomBotAI::UpdateOutOfCombatAI_Mage() { }
void RandomBotAI::UpdateInCombatAI_Priest() { }
void RandomBotAI::UpdateOutOfCombatAI_Priest() { }
void RandomBotAI::UpdateInCombatAI_Warlock() { }
void RandomBotAI::UpdateOutOfCombatAI_Warlock() { }
void RandomBotAI::UpdateInCombatAI_Rogue() { }
void RandomBotAI::UpdateOutOfCombatAI_Rogue() { }
void RandomBotAI::UpdateInCombatAI_Shaman() { }
void RandomBotAI::UpdateOutOfCombatAI_Shaman() { }
void RandomBotAI::UpdateInCombatAI_Druid() { }
void RandomBotAI::UpdateOutOfCombatAI_Druid() { }
