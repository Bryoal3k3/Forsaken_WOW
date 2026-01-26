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
#include "Strategies/TravelingStrategy.h"
#include "DangerZoneCache.h"
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
#include "PathFinder.h"
#include "Log.h"
#include <cmath>

#define RB_UPDATE_INTERVAL 1000

// ============================================================================
// Constructor / Destructor
// ============================================================================

RandomBotAI::RandomBotAI()
    : CombatBotBaseAI()
    , m_strategy(std::make_unique<GrindingStrategy>())
    , m_ghostStrategy(std::make_unique<GhostWalkingStrategy>())
    , m_vendoringStrategy(std::make_unique<VendoringStrategy>())
    , m_travelingStrategy(std::make_unique<TravelingStrategy>())
    , m_combatMgr(std::make_unique<BotCombatMgr>())
{
    m_updateTimer.Reset(1000);

    // Wire up cross-strategy references
    if (m_travelingStrategy && m_vendoringStrategy)
        m_travelingStrategy->SetVendoringStrategy(m_vendoringStrategy.get());
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
    // NOTE: Initialization is handled in UpdateAI() during the first tick
    // This ensures the player is fully loaded from DB before we modify state
}

void RandomBotAI::MovementInform(uint32 movementType, uint32 data)
{
    // Handle waypoint completion for TravelingStrategy
    // Move to next waypoint immediately (not on next Update tick) for smooth movement
    if (movementType == POINT_MOTION_TYPE && m_travelingStrategy)
    {
        m_travelingStrategy->OnWaypointReached(me, data);
    }
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

    // Invalid position detection - check if bot has fallen through floor
    // If position has no valid navmesh for consecutive ticks, teleport to safety
    {
        PathFinder path(me);
        // Try to build a path to a point 5 yards ahead (trivial path)
        float destX = me->GetPositionX() + 5.0f * std::cos(me->GetOrientation());
        float destY = me->GetPositionY() + 5.0f * std::sin(me->GetOrientation());
        float destZ = me->GetPositionZ();
        path.calculate(destX, destY, destZ, false);  // false = don't use straight line fallback

        // PATHFIND_NOPATH with a trivial destination usually means startPoly=0 (invalid position)
        if (path.getPathType() & PATHFIND_NOPATH)
        {
            ++m_invalidPosCount;

            if (m_invalidPosCount >= INVALID_POS_THRESHOLD)
            {
                sLog.Out(LOG_BASIC, LOG_LVL_BASIC,
                    "[RandomBotAI] %s stuck at invalid position (%.1f, %.1f, %.1f) for %u ticks, teleporting to hearthstone",
                    me->GetName(), me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), m_invalidPosCount);

                // Clear movement to prevent further pathfinding attempts
                me->GetMotionMaster()->Clear(false, true);
                me->GetMotionMaster()->MoveIdle();

                // Teleport to hearthstone location
                me->TeleportToHomebind();

                // Reset counter and behaviors
                m_invalidPosCount = 0;
                ResetBehaviors();
                if (m_travelingStrategy)
                    m_travelingStrategy->ResetArrivalCooldown();

                return;
            }
        }
        else
        {
            // Position is valid - reset counter
            m_invalidPosCount = 0;
        }
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

        // Clear intro cinematic - new characters spawn with cinematic pending
        // which makes them untargetable until "watched". Must be here, not OnPlayerLogin,
        // because LoadFromDB sets the cinematic after OnPlayerLogin is called.
        if (me->GetCurrentCinematicEntry() != 0)
            me->CinematicEnd();

        // Learn spells and populate spell data for combat
        ResetSpellData();
        PopulateSpellData();

        // Initialize combat manager with spell data
        m_combatMgr->Initialize(me, this);

        // Wire up combat manager to grinding strategy (avoids dynamic_cast in hot path)
        if (GrindingStrategy* pGrinding = static_cast<GrindingStrategy*>(m_strategy.get()))
            pGrinding->SetCombatMgr(m_combatMgr.get());

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
            // TESTING: Commented out to isolate PathFinder issue
            // If attacked by high-level creature while traveling, report danger zone
            /*
            if (Creature* pCreature = pAttacker->ToCreature())
            {
                int32 levelDiff = static_cast<int32>(pCreature->GetLevel()) - static_cast<int32>(me->GetLevel());
                bool isTraveling = m_travelingStrategy && m_travelingStrategy->IsTraveling();

                sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
                    "[RandomBotAI] %s attacked by %s (level %u, diff %d), traveling: %s",
                    me->GetName(), pCreature->GetName(), pCreature->GetLevel(), levelDiff,
                    isTraveling ? "YES" : "NO");

                if (isTraveling && levelDiff >= DangerZoneConstants::LEVEL_DIFF_THRESHOLD)
                {
                    sLog.Out(LOG_BASIC, LOG_LVL_BASIC,
                        "[RandomBotAI] %s reporting danger zone from %s (level %u)",
                        me->GetName(), pCreature->GetName(), pCreature->GetLevel());

                    sDangerZoneCache.ReportDanger(
                        me->GetMapId(),
                        me->GetPositionX(),
                        me->GetPositionY(),
                        pCreature->GetLevel());
                }
            }
            */

            me->Attack(pAttacker, true);
            return;
        }
    }

    // Check vendoring - bags full or gear broken?
    // This runs before grinding so bots don't keep trying to loot with full bags
    if (m_vendoringStrategy && m_vendoringStrategy->Update(me, RB_UPDATE_INTERVAL))
        return;  // Busy vendoring

    // Get grinding strategy for explicit result handling
    GrindingStrategy* pGrinding = static_cast<GrindingStrategy*>(m_strategy.get());
    if (pGrinding)
    {
        GrindingResult grindResult = pGrinding->UpdateGrinding(me, 0);

        if (grindResult == GrindingResult::ENGAGED)
        {
            // Found and attacking a target - reset travel state
            if (m_travelingStrategy)
                m_travelingStrategy->ResetArrivalCooldown();
            return;
        }

        // Check if we should travel (grinding found no targets)
        if (grindResult == GrindingResult::NO_TARGETS)
        {
            // Check if we've had enough consecutive failures
            if (pGrinding->GetNoMobsCount() >= TravelConstants::NO_MOBS_THRESHOLD)
            {
                if (m_travelingStrategy)
                {
                    m_travelingStrategy->SignalNoMobs();
                    if (m_travelingStrategy->Update(me, RB_UPDATE_INTERVAL))
                        return;  // Busy traveling
                }
            }
        }
    }

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
