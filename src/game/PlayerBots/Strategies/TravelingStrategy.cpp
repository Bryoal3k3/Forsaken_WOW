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
#include <cmath>

using namespace TravelConstants;

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

            // Check if we should vendor first - if so, YIELD entirely
            // Let VendoringStrategy handle it on next tick
            if (VendoringStrategy::GetLowestDurabilityPercent(pBot) < DURABILITY_THRESHOLD ||
                VendoringStrategy::GetBagFullPercent(pBot) > BAG_FULL_THRESHOLD)
            {
                sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
                    "[TravelingStrategy] %s needs vendor before travel, yielding",
                    pBot->GetName());
                return false;  // Yield - vendoring will trigger naturally
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

    uint32 level = pBot->GetLevel();
    uint32 mapId = pBot->GetMapId();
    uint8 faction = GetBotFaction(pBot);

    float px = pBot->GetPositionX();
    float py = pBot->GetPositionY();

    // Query with bounding box pre-filter for efficiency (5000 yard box)
    // Priority first, then distance as tiebreaker
    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
        "SELECT x, y, z, name FROM grind_spots "
        "WHERE map_id = %u "
        "AND min_level <= %u AND max_level >= %u "
        "AND (faction = 0 OR faction = %u) "
        "AND x BETWEEN %f AND %f "
        "AND y BETWEEN %f AND %f "
        "ORDER BY priority DESC, POW(x - %f, 2) + POW(y - %f, 2) ASC "
        "LIMIT 1",
        mapId, level, level, faction,
        px - 5000.0f, px + 5000.0f,
        py - 5000.0f, py + 5000.0f,
        px, py));

    if (result)
    {
        Field* fields = result->Fetch();
        m_targetX = fields[0].GetFloat();
        m_targetY = fields[1].GetFloat();
        m_targetZ = fields[2].GetFloat();
        m_targetName = fields[3].GetCppString();
        return true;
    }

    // No spot in bounding box, try without distance limit
    result = CharacterDatabase.PQuery(
        "SELECT x, y, z, name FROM grind_spots "
        "WHERE map_id = %u "
        "AND min_level <= %u AND max_level >= %u "
        "AND (faction = 0 OR faction = %u) "
        "ORDER BY priority DESC, POW(x - %f, 2) + POW(y - %f, 2) ASC "
        "LIMIT 1",
        mapId, level, level, faction, px, py);

    if (result)
    {
        Field* fields = result->Fetch();
        m_targetX = fields[0].GetFloat();
        m_targetY = fields[1].GetFloat();
        m_targetZ = fields[2].GetFloat();
        m_targetName = fields[3].GetCppString();
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
