/*
 * TravelingStrategy.cpp
 *
 * Strategy for traveling to level-appropriate grind spots.
 * Uses database-driven grind_spots table for destination selection.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "TravelingStrategy.h"
#include "VendoringStrategy.h"
#include "Player.h"
#include "MotionMaster.h"
#include "Log.h"
#include "World.h"
#include "Database/DatabaseEnv.h"
#include "ProgressBar.h"
#include <cmath>
#include <cfloat>

using namespace TravelConstants;

// Static member initialization
std::vector<GrindSpotData> TravelingStrategy::s_grindSpotCache;
bool TravelingStrategy::s_cacheBuilt = false;
std::mutex TravelingStrategy::s_cacheMutex;

TravelingStrategy::TravelingStrategy()
    : m_state(TravelState::IDLE)
    , m_noMobsSignaled(false)
    , m_targetX(0.0f)
    , m_targetY(0.0f)
    , m_targetZ(0.0f)
    , m_arrivalTime(0)
    , m_lastX(0.0f)
    , m_lastY(0.0f)
    , m_lastProgressTime(0)
{
}

void TravelingStrategy::BuildGrindSpotCache()
{
    std::lock_guard<std::mutex> lock(s_cacheMutex);

    if (s_cacheBuilt)
        return;

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[TravelingStrategy] Building grind spot cache...");

    // Query all grind spots from database
    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
        "SELECT id, map_id, x, y, z, min_level, max_level, faction, priority, name "
        "FROM grind_spots ORDER BY priority DESC"));

    if (!result)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Grind spot cache: 0 spots loaded (table empty or missing)");
        s_cacheBuilt = true;
        return;
    }

    // Count rows for progress bar
    uint32 rowCount = result->GetRowCount();
    BarGoLink bar(rowCount);

    do
    {
        bar.step();
        Field* fields = result->Fetch();

        GrindSpotData spot;
        spot.id       = fields[0].GetUInt32();
        spot.mapId    = fields[1].GetUInt32();
        spot.x        = fields[2].GetFloat();
        spot.y        = fields[3].GetFloat();
        spot.z        = fields[4].GetFloat();
        spot.minLevel = fields[5].GetUInt8();
        spot.maxLevel = fields[6].GetUInt8();
        spot.faction  = fields[7].GetUInt8();
        spot.priority = fields[8].GetUInt8();
        spot.name     = fields[9].GetCppString();

        s_grindSpotCache.push_back(spot);
    }
    while (result->NextRow());

    s_cacheBuilt = true;
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Grind spot cache built: %u spots loaded",
             static_cast<uint32>(s_grindSpotCache.size()));
}

bool TravelingStrategy::Update(Player* pBot, uint32 /*diff*/)
{
    if (!pBot || !pBot->IsAlive())
        return false;

    switch (m_state)
    {
        case TravelState::IDLE:
        {
            if (!ShouldTravel(pBot))
                return false;

            // Check if we should vendor first - if so, trigger vendoring and yield
            if (VendoringStrategy::GetLowestDurabilityPercent(pBot) < DURABILITY_THRESHOLD ||
                VendoringStrategy::GetBagFullPercent(pBot) > BAG_FULL_THRESHOLD)
            {
                sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
                    "[TravelingStrategy] %s needs vendor before travel, triggering vendoring",
                    pBot->GetName());

                // Force vendoring to start (it won't trigger naturally at these thresholds)
                if (m_pVendoringStrategy)
                    m_pVendoringStrategy->ForceStart();

                return false;  // Yield - vendoring will handle it
            }

            m_state = TravelState::FINDING_SPOT;
            // Fall through to FINDING_SPOT
        }
        // fallthrough
        case TravelState::FINDING_SPOT:
        {
            if (!FindGrindSpot(pBot))
            {
                sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
                    "[TravelingStrategy] %s no grind spot found for level %u",
                    pBot->GetName(), pBot->GetLevel());
                m_state = TravelState::IDLE;
                return false;
            }

            sLog.Out(LOG_BASIC, LOG_LVL_BASIC,
                "[TravelingStrategy] %s traveling to %s (%.1f, %.1f, %.1f)",
                pBot->GetName(), m_targetName.c_str(), m_targetX, m_targetY, m_targetZ);

            // Record starting position for stuck detection
            m_lastX = pBot->GetPositionX();
            m_lastY = pBot->GetPositionY();
            m_lastProgressTime = WorldTimer::getMSTime();

            // Start moving with pathfinding
            pBot->GetMotionMaster()->MovePoint(
                0, m_targetX, m_targetY, m_targetZ,
                MOVE_PATHFINDING | MOVE_RUN_MODE);

            m_state = TravelState::WALKING;
            return true;
        }

        case TravelState::WALKING:
        {
            if (IsAtDestination(pBot))
            {
                sLog.Out(LOG_BASIC, LOG_LVL_BASIC,
                    "[TravelingStrategy] %s arrived at %s",
                    pBot->GetName(), m_targetName.c_str());

                m_arrivalTime = WorldTimer::getMSTime();
                m_state = TravelState::ARRIVED;
                m_noMobsSignaled = false;
                return false;  // Let grinding take over
            }

            // Stuck detection
            float dx = pBot->GetPositionX() - m_lastX;
            float dy = pBot->GetPositionY() - m_lastY;
            float distMoved = std::sqrt(dx * dx + dy * dy);

            uint32 currentTime = WorldTimer::getMSTime();

            if (distMoved >= STUCK_MIN_DISTANCE)
            {
                // Making progress - update position
                m_lastX = pBot->GetPositionX();
                m_lastY = pBot->GetPositionY();
                m_lastProgressTime = currentTime;
            }
            else if (WorldTimer::getMSTimeDiff(m_lastProgressTime, currentTime) > STUCK_TIMEOUT_MS)
            {
                sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
                    "[TravelingStrategy] %s stuck while traveling, resetting",
                    pBot->GetName());
                m_state = TravelState::IDLE;
                m_noMobsSignaled = false;
                return false;
            }

            return true;  // Still walking
        }

        case TravelState::ARRIVED:
        {
            // Cooldown period - stay here and let grinding work
            uint32 currentTime = WorldTimer::getMSTime();
            if (WorldTimer::getMSTimeDiff(m_arrivalTime, currentTime) < ARRIVAL_COOLDOWN_MS)
            {
                return false;  // Yield to grinding
            }

            // Cooldown expired, back to idle
            m_state = TravelState::IDLE;
            return false;
        }
    }

    return false;
}

bool TravelingStrategy::ShouldTravel(Player* /*pBot*/) const
{
    // Don't travel if we recently arrived somewhere
    if (m_state == TravelState::ARRIVED)
        return false;

    // Don't travel if grinding hasn't signaled NO_TARGETS
    if (!m_noMobsSignaled)
        return false;

    return true;
}

uint8 TravelingStrategy::GetBotFaction(Player* pBot)
{
    if (!pBot)
        return 0;

    // Map race to faction: 1=Alliance, 2=Horde
    switch (pBot->GetRace())
    {
        case RACE_HUMAN:
        case RACE_DWARF:
        case RACE_NIGHTELF:
        case RACE_GNOME:
            return 1;  // Alliance
        case RACE_ORC:
        case RACE_UNDEAD:
        case RACE_TAUREN:
        case RACE_TROLL:
            return 2;  // Horde
        default:
            return 0;  // Unknown
    }
}

bool TravelingStrategy::FindGrindSpot(Player* pBot)
{
    if (!pBot)
        return false;

    // Ensure cache is built (fallback - should already be built at startup)
    if (!s_cacheBuilt)
        BuildGrindSpotCache();

    uint32 level = pBot->GetLevel();
    uint32 mapId = pBot->GetMapId();
    uint8 faction = GetBotFaction(pBot);

    float px = pBot->GetPositionX();
    float py = pBot->GetPositionY();

    // Search cache for best matching spot
    // Priority: highest priority first, then closest distance as tiebreaker
    GrindSpotData const* bestSpot = nullptr;
    float bestScore = FLT_MAX;  // Lower is better (we'll use -priority + normalized_distance)

    for (GrindSpotData const& spot : s_grindSpotCache)
    {
        // Filter: must be on same map
        if (spot.mapId != mapId)
            continue;

        // Filter: level must be in range
        if (level < spot.minLevel || level > spot.maxLevel)
            continue;

        // Filter: faction must match (0 = both factions)
        if (spot.faction != 0 && spot.faction != faction)
            continue;

        // Calculate distance squared (avoid sqrt for comparison)
        float dx = spot.x - px;
        float dy = spot.y - py;
        float distSq = dx * dx + dy * dy;

        // Score: priority is primary (higher = better, so negate), distance is secondary
        // Multiply priority by large factor to ensure it dominates
        float score = -static_cast<float>(spot.priority) * 1000000.0f + distSq;

        if (score < bestScore)
        {
            bestScore = score;
            bestSpot = &spot;
        }
    }

    if (bestSpot)
    {
        m_targetX = bestSpot->x;
        m_targetY = bestSpot->y;
        m_targetZ = bestSpot->z;
        m_targetName = bestSpot->name;
        return true;
    }

    return false;
}

bool TravelingStrategy::IsAtDestination(Player* pBot) const
{
    if (!pBot)
        return false;

    float dx = pBot->GetPositionX() - m_targetX;
    float dy = pBot->GetPositionY() - m_targetY;
    return (dx * dx + dy * dy) < (ARRIVAL_DISTANCE * ARRIVAL_DISTANCE);
}

void TravelingStrategy::OnEnterCombat(Player* pBot)
{
    if (m_state == TravelState::WALKING)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
            "[TravelingStrategy] %s entered combat while traveling, pausing",
            pBot ? pBot->GetName() : "unknown");
        // Don't reset state - just pause. Combat handler takes over.
    }
}

void TravelingStrategy::OnLeaveCombat(Player* pBot)
{
    // Resume walking if we were interrupted
    if (m_state == TravelState::WALKING && pBot)
    {
        pBot->GetMotionMaster()->MovePoint(
            0, m_targetX, m_targetY, m_targetZ,
            MOVE_PATHFINDING | MOVE_RUN_MODE);
    }
}

void TravelingStrategy::ResetArrivalCooldown()
{
    m_arrivalTime = 0;
    m_noMobsSignaled = false;
    if (m_state == TravelState::ARRIVED)
        m_state = TravelState::IDLE;
}

bool TravelingStrategy::IsTraveling() const
{
    return m_state == TravelState::WALKING;
}
