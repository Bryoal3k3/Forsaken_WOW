/*
 * TravelingStrategy.cpp
 *
 * Strategy for traveling to level-appropriate grind spots.
 * Uses database-driven grind_spots table for destination selection.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "TravelingStrategy.h"
#include "BotMovementManager.h"
#include "VendoringStrategy.h"
#include "DangerZoneCache.h"
#include "PlayerBotMgr.h"
#include "Player.h"
#include "MotionMaster.h"
#include "Log.h"
#include "World.h"
#include "Database/DatabaseEnv.h"
#include "ProgressBar.h"
#include "Map.h"
#include "MapManager.h"
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

    // Correct Z coordinates using terrain data
    uint32 correctedCount = 0;
    for (GrindSpotData& spot : s_grindSpotCache)
    {
        // Get the map for this spot (instanceId 0 for continents)
        Map* map = sMapMgr.FindMap(spot.mapId, 0);
        if (!map)
            continue;

        float terrainZ = map->GetHeight(spot.x, spot.y, spot.z + 10.0f);
        if (terrainZ > INVALID_HEIGHT)
        {
            // Check if Z needs correction (more than 1 yard difference)
            if (std::abs(terrainZ - spot.z) > 1.0f)
            {
                spot.z = terrainZ;
                ++correctedCount;
            }
        }
    }

    s_cacheBuilt = true;

    if (correctedCount > 0)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Grind spot cache built: %u spots loaded (%u Z-coordinates corrected)",
                 static_cast<uint32>(s_grindSpotCache.size()), correctedCount);
    }
    else
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Grind spot cache built: %u spots loaded",
                 static_cast<uint32>(s_grindSpotCache.size()));
    }
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

            // Validate path exists before committing to travel
            if (!ValidatePath(pBot, m_targetX, m_targetY, m_targetZ))
            {
                sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
                    "[TravelingStrategy] %s: Cannot reach %s, aborting travel",
                    pBot->GetName(), m_targetName.c_str());
                m_state = TravelState::IDLE;
                m_noMobsSignaled = false;
                return false;
            }

            sLog.Out(LOG_BASIC, LOG_LVL_BASIC,
                "[TravelingStrategy] %s traveling to %s (%.1f, %.1f, %.1f)",
                pBot->GetName(), m_targetName.c_str(), m_targetX, m_targetY, m_targetZ);

            // Generate waypoints for the journey
            GenerateWaypoints(pBot);

            // Record starting position for stuck detection
            m_lastX = pBot->GetPositionX();
            m_lastY = pBot->GetPositionY();
            m_lastProgressTime = WorldTimer::getMSTime();

            // Start moving to first waypoint
            MoveToCurrentWaypoint(pBot);

            m_state = TravelState::WALKING;
            return true;
        }

        case TravelState::WALKING:
        {
            // Check if we need to move to next waypoint (triggered by MovementInform)
            if (m_moveToNextWaypoint)
            {
                m_moveToNextWaypoint = false;
                MoveToCurrentWaypoint(pBot);
            }

            if (IsAtDestination(pBot))
            {
                sLog.Out(LOG_BASIC, LOG_LVL_BASIC,
                    "[TravelingStrategy] %s arrived at %s",
                    pBot->GetName(), m_targetName.c_str());

                m_arrivalTime = WorldTimer::getMSTime();
                m_state = TravelState::ARRIVED;
                m_noMobsSignaled = false;
                m_waypointsGenerated = false;
                m_waypoints.clear();
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
                m_waypointsGenerated = false;
                m_waypoints.clear();
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

    // Two-phase search: first look for nearby spots (same zone), then expand if needed
    const float LOCAL_RADIUS_SQ = 800.0f * 800.0f;  // ~800 yards = same zone/area

    std::vector<GrindSpotData const*> nearbySpots;
    std::vector<GrindSpotData const*> distantSpots;

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

        // Calculate distance squared
        float dx = spot.x - px;
        float dy = spot.y - py;
        float distSq = dx * dx + dy * dy;

        // Categorize by distance
        if (distSq <= LOCAL_RADIUS_SQ)
            nearbySpots.push_back(&spot);
        else
            distantSpots.push_back(&spot);
    }

    // Prefer nearby spots (stay in current zone, randomize between local options)
    GrindSpotData const* chosenSpot = nullptr;

    if (!nearbySpots.empty())
    {
        // Randomly pick from nearby spots (avoids "train" behavior)
        chosenSpot = nearbySpots[urand(0, nearbySpots.size() - 1)];

        if (sPlayerBotMgr.IsDebugGrindSelectionEnabled())
        {
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
                "[GRIND] %s selected '%s' from %zu nearby, %zu distant spots",
                pBot->GetName(), chosenSpot->name.c_str(),
                nearbySpots.size(), distantSpots.size());
        }
    }
    else if (!distantSpots.empty())
    {
        // No nearby spots - need to travel to new zone
        // Pick closest distant spot (weighted random favoring closer)
        if (distantSpots.size() == 1)
        {
            chosenSpot = distantSpots[0];
        }
        else
        {
            // Weight by inverse distance - closer spots more likely
            std::vector<float> weights;
            float totalWeight = 0.0f;

            for (GrindSpotData const* spot : distantSpots)
            {
                float dx = spot->x - px;
                float dy = spot->y - py;
                float distSq = dx * dx + dy * dy;
                float weight = 1.0f / (1.0f + distSq / 100000.0f);
                weights.push_back(weight);
                totalWeight += weight;
            }

            float roll = (float)urand(0, 10000) / 10000.0f * totalWeight;
            float cumulative = 0.0f;

            for (size_t i = 0; i < distantSpots.size(); ++i)
            {
                cumulative += weights[i];
                if (roll <= cumulative)
                {
                    chosenSpot = distantSpots[i];
                    break;
                }
            }

            if (!chosenSpot)
                chosenSpot = distantSpots[0];
        }

        if (sPlayerBotMgr.IsDebugGrindSelectionEnabled())
        {
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
                "[GRIND] %s traveling to '%s' (%zu distant spots, no nearby)",
                pBot->GetName(), chosenSpot->name.c_str(), distantSpots.size());
        }
    }

    if (!chosenSpot)
        return false;

    // Add small random offset to avoid bots stacking on exact same point
    float offsetX = (float)(irand(-25, 25));
    float offsetY = (float)(irand(-25, 25));

    m_targetX = chosenSpot->x + offsetX;
    m_targetY = chosenSpot->y + offsetY;
    m_targetZ = chosenSpot->z;
    m_targetName = chosenSpot->name;

    return true;
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
    if (m_state == TravelState::WALKING && pBot && m_waypointsGenerated)
    {
        MoveToCurrentWaypoint(pBot);
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

bool TravelingStrategy::ValidatePath(Player* pBot, float destX, float destY, float destZ)
{
    if (!pBot)
        return false;

    PathFinder path(pBot);
    path.calculate(destX, destY, destZ);

    PathType type = path.getPathType();

    sLog.Out(LOG_BASIC, LOG_LVL_BASIC,
        "[TravelingStrategy] %s: ValidatePath from (%.1f, %.1f, %.1f) to (%.1f, %.1f, %.1f) - PathType: %u",
        pBot->GetName(),
        pBot->GetPositionX(), pBot->GetPositionY(), pBot->GetPositionZ(),
        destX, destY, destZ, static_cast<uint32>(type));

    // PATHFIND_NORMAL = fully valid path
    // PATHFIND_INCOMPLETE = partial path (can still try)
    // PATHFIND_NOPATH = completely invalid
    if (type & PATHFIND_NOPATH)
    {
        return false;
    }

    return true;
}

void TravelingStrategy::GenerateWaypoints(Player* pBot)
{
    m_waypoints.clear();
    m_currentWaypoint = 0;
    m_moveToNextWaypoint = false;

    if (!pBot)
        return;

    float startX = pBot->GetPositionX();
    float startY = pBot->GetPositionY();
    float startZ = pBot->GetPositionZ();

    float dx = m_targetX - startX;
    float dy = m_targetY - startY;
    float totalDist = std::sqrt(dx * dx + dy * dy);

    uint32 skippedWaypoints = 0;

    if (totalDist <= WAYPOINT_SEGMENT_DISTANCE)
    {
        // Short distance - single waypoint at destination
        // Validate destination Z before adding
        if (Map* map = pBot->GetMap())
        {
            float validZ = map->GetHeight(m_targetX, m_targetY, MAX_HEIGHT);
            if (validZ > INVALID_HEIGHT)
            {
                m_waypoints.push_back(Vector3(m_targetX, m_targetY, validZ));
            }
            else
            {
                // Try with target Z as reference
                validZ = map->GetHeight(m_targetX, m_targetY, m_targetZ + 10.0f);
                if (validZ > INVALID_HEIGHT)
                {
                    m_waypoints.push_back(Vector3(m_targetX, m_targetY, validZ));
                }
                else
                {
                    // Use target Z as-is (destination from DB should be valid)
                    m_waypoints.push_back(Vector3(m_targetX, m_targetY, m_targetZ));
                }
            }
        }
        else
        {
            m_waypoints.push_back(Vector3(m_targetX, m_targetY, m_targetZ));
        }
    }
    else
    {
        // Long distance - generate intermediate waypoints
        uint32 numSegments = static_cast<uint32>(totalDist / WAYPOINT_SEGMENT_DISTANCE) + 1;

        for (uint32 i = 1; i <= numSegments; ++i)
        {
            float t = static_cast<float>(i) / numSegments;
            float wpX = startX + dx * t;
            float wpY = startY + dy * t;
            float wpZ = INVALID_HEIGHT;
            bool validZ = false;

            if (Map* map = pBot->GetMap())
            {
                // First attempt: query with MAX_HEIGHT
                wpZ = map->GetHeight(wpX, wpY, MAX_HEIGHT);
                if (wpZ > INVALID_HEIGHT)
                {
                    validZ = true;
                }
                else
                {
                    // Second attempt: query with interpolated Z as reference
                    float refZ = startZ + (m_targetZ - startZ) * t;
                    wpZ = map->GetHeight(wpX, wpY, refZ + 10.0f);
                    if (wpZ > INVALID_HEIGHT)
                    {
                        validZ = true;
                    }
                }
            }

            if (validZ)
            {
                m_waypoints.push_back(Vector3(wpX, wpY, wpZ));
            }
            else
            {
                ++skippedWaypoints;
                sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
                    "[TravelingStrategy] %s: Skipping waypoint %u at (%.1f, %.1f) - invalid terrain height",
                    pBot->GetName(), i, wpX, wpY);
            }
        }

        // Ensure final waypoint is exact destination (with validated Z)
        if (!m_waypoints.empty())
        {
            if (Map* map = pBot->GetMap())
            {
                float destZ = map->GetHeight(m_targetX, m_targetY, MAX_HEIGHT);
                if (destZ <= INVALID_HEIGHT)
                    destZ = map->GetHeight(m_targetX, m_targetY, m_targetZ + 10.0f);
                if (destZ <= INVALID_HEIGHT)
                    destZ = m_targetZ;  // Fallback to DB value
                m_waypoints.back() = Vector3(m_targetX, m_targetY, destZ);
            }
            else
            {
                m_waypoints.back() = Vector3(m_targetX, m_targetY, m_targetZ);
            }
        }
    }

    // TESTING: Commented out to isolate PathFinder issue
    // Filter waypoints through danger cache and insert detours
    // FilterWaypointsForDanger(pBot);

    // Apply path smoothing - skip unnecessary waypoints when LoS exists
    // This reduces hill-hugging by allowing more direct routes
    if (m_pMovementMgr && m_waypoints.size() > 2)
    {
        size_t beforeCount = m_waypoints.size();
        m_waypoints = m_pMovementMgr->SmoothPath(m_waypoints);
        if (m_waypoints.size() < beforeCount)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
                "[TravelingStrategy] %s: Path smoothed from %zu to %zu waypoints",
                pBot->GetName(), beforeCount, m_waypoints.size());
        }
    }

    m_waypointsGenerated = true;

    if (skippedWaypoints > 0)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
            "[TravelingStrategy] %s: Generated %zu waypoints for %.0f yard journey (skipped %u invalid)",
            pBot->GetName(), m_waypoints.size(), totalDist, skippedWaypoints);
    }
    else
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
            "[TravelingStrategy] %s: Generated %zu waypoints for %.0f yard journey",
            pBot->GetName(), m_waypoints.size(), totalDist);
    }
}

void TravelingStrategy::MoveToCurrentWaypoint(Player* pBot)
{
    if (!pBot || m_currentWaypoint >= m_waypoints.size())
        return;

    Vector3 const& wp = m_waypoints[m_currentWaypoint];

    // Use movement manager if available, with waypoint index as movement ID
    if (m_pMovementMgr)
    {
        MoveResult result = m_pMovementMgr->MoveTo(wp.x, wp.y, wp.z,
            MovementPriority::PRIORITY_NORMAL, m_currentWaypoint);

        if (result != MoveResult::SUCCESS)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
                "[TravelingStrategy] %s: MoveTo failed with result %u for waypoint %u",
                pBot->GetName(), static_cast<uint8>(result), m_currentWaypoint);
            return;
        }
    }
    else
    {
        // Fallback to direct MotionMaster call
        pBot->GetMotionMaster()->MovePoint(
            m_currentWaypoint,  // Use waypoint index as movement ID
            wp.x, wp.y, wp.z,
            MOVE_PATHFINDING | MOVE_RUN_MODE | MOVE_EXCLUDE_STEEP_SLOPES);
    }

    sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
        "[TravelingStrategy] %s: Moving to waypoint %u/%zu (%.1f, %.1f, %.1f)",
        pBot->GetName(), m_currentWaypoint + 1,
        m_waypoints.size(), wp.x, wp.y, wp.z);
}

void TravelingStrategy::OnWaypointReached(Player* pBot, uint32 waypointId)
{
    if (m_state != TravelState::WALKING || !m_waypointsGenerated)
        return;

    // Verify this is the waypoint we expected
    if (waypointId != m_currentWaypoint)
        return;

    m_currentWaypoint++;

    if (m_currentWaypoint >= m_waypoints.size())
    {
        // All waypoints reached - will be handled by IsAtDestination check
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
            "[TravelingStrategy] All waypoints reached, checking destination");
        return;
    }

    // Move to next waypoint immediately for smooth movement
    MoveToCurrentWaypoint(pBot);
}

void TravelingStrategy::FilterWaypointsForDanger(Player* pBot)
{
    if (!pBot || m_waypoints.empty())
        return;

    uint32 mapId = pBot->GetMapId();
    uint8 botLevel = pBot->GetLevel();

    // Track where we are coming from (bot's position initially)
    Vector3 fromPoint(pBot->GetPositionX(), pBot->GetPositionY(), pBot->GetPositionZ());

    // Check each waypoint and insert detours as needed
    // Use index-based iteration since we may insert elements
    for (size_t i = 0; i < m_waypoints.size(); ++i)
    {
        Vector3& wp = m_waypoints[i];

        // Check if this waypoint is in a danger zone
        if (!sDangerZoneCache.IsDangerous(mapId, wp.x, wp.y, botLevel))
        {
            fromPoint = wp;
            continue;
        }

        // Get nearby dangers for detour calculation
        std::vector<DangerZone> dangers;
        sDangerZoneCache.GetNearbyDangers(mapId, wp.x, wp.y,
            DangerZoneConstants::DANGER_RADIUS * 1.5f, dangers);

        if (dangers.empty())
        {
            fromPoint = wp;
            continue;
        }

        // Calculate detour point
        Vector3 detour = CalculateDetourPoint(pBot, fromPoint, wp, dangers);

        // Validate the detour isn't also dangerous
        if (sDangerZoneCache.IsDangerous(mapId, detour.x, detour.y, botLevel))
        {
            // Try opposite direction
            Vector3 oppositeDetour = CalculateDetourPoint(pBot, fromPoint, wp, dangers);
            // Flip the perpendicular direction by negating the offset
            float midX = (fromPoint.x + wp.x) / 2.0f;
            float midY = (fromPoint.y + wp.y) / 2.0f;
            oppositeDetour.x = 2.0f * midX - detour.x;
            oppositeDetour.y = 2.0f * midY - detour.y;

            if (!sDangerZoneCache.IsDangerous(mapId, oppositeDetour.x, oppositeDetour.y, botLevel))
            {
                detour = oppositeDetour;
            }
            else
            {
                // Both directions blocked - log and skip (bot may die, learning experience)
                sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
                    "[TravelingStrategy] %s: Waypoint %zu surrounded by danger, no safe detour",
                    pBot->GetName(), i);
                fromPoint = wp;
                continue;
            }
        }

        // Get terrain height for detour point
        if (Map* map = pBot->GetMap())
        {
            float detourZ = map->GetHeight(detour.x, detour.y, MAX_HEIGHT);
            if (detourZ > INVALID_HEIGHT)
            {
                detour.z = detourZ;
            }
            else
            {
                // Invalid height - skip this detour, it's off the navmesh
                sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
                    "[TravelingStrategy] %s: Detour point (%.1f, %.1f) has invalid height, skipping",
                    pBot->GetName(), detour.x, detour.y);
                fromPoint = wp;
                continue;
            }
        }

        // Validate the detour path is reachable
        if (!ValidatePath(pBot, detour.x, detour.y, detour.z))
        {
            sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
                "[TravelingStrategy] %s: Detour point (%.1f, %.1f) not reachable, skipping",
                pBot->GetName(), detour.x, detour.y);
            fromPoint = wp;
            continue;
        }

        // Insert detour before the dangerous waypoint
        m_waypoints.insert(m_waypoints.begin() + i, detour);

        sLog.Out(LOG_BASIC, LOG_LVL_BASIC,
            "[TravelingStrategy] %s: Inserted detour at (%.1f, %.1f, %.1f) to avoid danger zone",
            pBot->GetName(), detour.x, detour.y, detour.z);

        // Update fromPoint and skip to after the inserted detour
        fromPoint = detour;
        ++i;  // Skip the inserted detour, next iteration will check the original waypoint again
    }
}

Vector3 TravelingStrategy::CalculateDetourPoint(
    Player* pBot,
    Vector3 const& fromPoint,
    Vector3 const& blockedPoint,
    std::vector<DangerZone> const& dangers) const
{
    // Calculate average danger position (centroid of threats)
    float avgX = 0.0f, avgY = 0.0f;
    for (auto const& d : dangers)
    {
        avgX += d.x;
        avgY += d.y;
    }
    if (!dangers.empty())
    {
        avgX /= static_cast<float>(dangers.size());
        avgY /= static_cast<float>(dangers.size());
    }

    // Direction from "from" to blocked waypoint
    float dx = blockedPoint.x - fromPoint.x;
    float dy = blockedPoint.y - fromPoint.y;
    float len = std::sqrt(dx * dx + dy * dy);

    if (len < 0.01f)
    {
        // Points are essentially the same
        return blockedPoint;
    }

    // Normalize direction
    dx /= len;
    dy /= len;

    // Perpendicular direction (rotate 90 degrees)
    float px = -dy;
    float py = dx;

    // Determine which side of the path the danger is on
    // Project danger centroid onto perpendicular axis
    float midX = (fromPoint.x + blockedPoint.x) / 2.0f;
    float midY = (fromPoint.y + blockedPoint.y) / 2.0f;

    float dangerOffsetX = avgX - midX;
    float dangerOffsetY = avgY - midY;
    float dangerSide = dangerOffsetX * px + dangerOffsetY * py;

    // Go to opposite side of the danger
    float detourDir = (dangerSide > 0) ? -1.0f : 1.0f;

    // Calculate detour point: perpendicular offset from midpoint
    float detourDist = DangerZoneConstants::DETOUR_DISTANCE;

    Vector3 detour;
    detour.x = midX + px * detourDist * detourDir;
    detour.y = midY + py * detourDist * detourDir;
    detour.z = (fromPoint.z + blockedPoint.z) / 2.0f;  // Will be corrected with terrain height

    return detour;
}
