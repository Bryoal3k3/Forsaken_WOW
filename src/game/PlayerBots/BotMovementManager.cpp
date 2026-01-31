/*
 * BotMovementManager.cpp
 *
 * Implementation of centralized movement coordinator for RandomBot AI.
 */

#include "BotMovementManager.h"
#include "Player.h"
#include "Unit.h"
#include "Map.h"
#include "World.h"
#include "MotionMaster.h"
#include "PathFinder.h"
#include "SpellAuraDefines.h"
#include "ObjectAccessor.h"
#include "Log.h"

#include <cmath>

// Note: frand() is already defined in Util.h

// ============================================================================
// Constructor
// ============================================================================

BotMovementManager::BotMovementManager(Player* bot)
    : m_bot(bot)
    , m_botGuid(bot ? bot->GetObjectGuid() : ObjectGuid())
    , m_lastMoveCommandTime(0)
    , m_stuckCheckTimer(0)
{
    m_state.Clear();
}

void BotMovementManager::SetBot(Player* bot)
{
    m_bot = bot;
    m_botGuid = bot ? bot->GetObjectGuid() : ObjectGuid();
    m_state.Clear();  // Clear stale movement state
}

// ============================================================================
// Main Movement Commands
// ============================================================================

MoveResult BotMovementManager::MoveTo(float x, float y, float z, MovementPriority priority, uint32 pointId)
{
    // Guard against use-after-free
    if (!IsValid())
        return MoveResult::FAILED_CC;

    // Step 1: CC Check
    if (IsCC())
        return MoveResult::FAILED_CC;

    // Step 2: Priority Check
    if (IsWaitingForMove(priority))
        return MoveResult::FAILED_PRIORITY;

    // Step 3: Duplicate Check
    if (IsDuplicateMove(x, y, z))
        return MoveResult::FAILED_DUPLICATE;

    // Step 4: Command Cooldown
    uint32 now = WorldTimer::getMSTime();
    if (now - m_lastMoveCommandTime < MOVE_COMMAND_COOLDOWN)
        return MoveResult::FAILED_COOLDOWN;

    // Step 5: Z Optimization
    float bestZ = SearchBestZ(x, y, z);
    if (bestZ <= INVALID_HEIGHT)
        return MoveResult::FAILED_INVALID_POS;

    // Step 6: Path Validation
    if (!ValidatePath(x, y, bestZ))
        return MoveResult::FAILED_NO_PATH;

    // Step 7: Issue Movement (pointId passed to MovementInform callback)
    m_bot->GetMotionMaster()->MovePoint(pointId, x, y, bestZ,
        MOVE_PATHFINDING | MOVE_RUN_MODE | MOVE_EXCLUDE_STEEP_SLOPES);

    // Step 8: Record State
    float distance = m_bot->GetDistance(x, y, bestZ);
    uint32 duration = CalculateMoveDelay(distance);
    RecordMovement(x, y, bestZ, priority, duration);

    m_lastMoveCommandTime = now;
    return MoveResult::SUCCESS;
}

MoveResult BotMovementManager::MoveNear(float x, float y, float z, float maxDist, MovementPriority priority)
{
    if (!IsValid())
        return MoveResult::FAILED_CC;

    if (IsCC())
        return MoveResult::FAILED_CC;

    if (IsWaitingForMove(priority))
        return MoveResult::FAILED_PRIORITY;

    // Get a random starting angle (so bots don't all try same direction first)
    float startAngle = frand(0.0f, 2.0f * M_PI_F);

    // Try 8 angles (45 degrees apart) like AzerothCore
    for (int i = 0; i < MOVE_NEAR_ANGLE_ATTEMPTS; ++i)
    {
        float angle = startAngle + (i * (M_PI_F / 4.0f));  // 45 degree increments

        // Add slight random offset to distance (prevents exact stacking)
        float dist = maxDist + frand(-MOVE_NEAR_RANDOM_OFFSET, MOVE_NEAR_RANDOM_OFFSET);
        if (dist < 0.5f) dist = 0.5f;

        float tryX = x + cos(angle) * dist;
        float tryY = y + sin(angle) * dist;
        float tryZ = SearchBestZ(tryX, tryY, z);

        if (tryZ <= INVALID_HEIGHT)
            continue;

        // Check line of sight from bot to this position
        if (!m_bot->IsWithinLOS(tryX, tryY, tryZ + 1.5f))
            continue;

        // Validate path exists
        if (!ValidatePath(tryX, tryY, tryZ))
            continue;

        // Found valid position - move there
        return MoveTo(tryX, tryY, tryZ, priority);
    }

    // Fallback: try exact position
    return MoveTo(x, y, z, priority);
}

MoveResult BotMovementManager::MoveNear(Unit* target, float distance, MovementPriority priority)
{
    if (!IsValid())
        return MoveResult::FAILED_CC;

    if (!target || !target->IsInWorld())
        return MoveResult::FAILED_INVALID_TARGET;

    if (target->GetMapId() != m_bot->GetMapId())
        return MoveResult::FAILED_INVALID_TARGET;

    // Add target's combat reach to distance
    float totalDist = distance + target->GetCombatReach();

    return MoveNear(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(),
                    totalDist, priority);
}

MoveResult BotMovementManager::Chase(Unit* target, float distance, MovementPriority priority)
{
    if (!IsValid())
        return MoveResult::FAILED_CC;

    if (!target || !target->IsInWorld())
        return MoveResult::FAILED_INVALID_TARGET;

    if (target->GetMapId() != m_bot->GetMapId())
        return MoveResult::FAILED_INVALID_TARGET;

    if (IsCC())
        return MoveResult::FAILED_CC;

    if (IsWaitingForMove(priority))
        return MoveResult::FAILED_PRIORITY;

    // Check if already chasing same target at same distance
    if (m_state.chaseTarget == target->GetObjectGuid() &&
        std::abs(m_state.chaseDistance - distance) < 0.1f &&
        m_bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
    {
        return MoveResult::FAILED_DUPLICATE;
    }

    uint32 now = WorldTimer::getMSTime();
    if (now - m_lastMoveCommandTime < MOVE_COMMAND_COOLDOWN)
        return MoveResult::FAILED_COOLDOWN;

    // Issue chase - let engine handle pathfinding
    m_bot->GetMotionMaster()->MoveChase(target, distance);

    // Record state
    m_state.Clear();
    m_state.chaseTarget = target->GetObjectGuid();
    m_state.chaseDistance = distance;
    m_state.priority = priority;
    m_state.moveStartTime = now;
    m_state.expectedDuration = 0;  // Chase is continuous
    m_state.lastX = m_bot->GetPositionX();
    m_state.lastY = m_bot->GetPositionY();
    m_state.lastProgressTime = now;

    m_lastMoveCommandTime = now;
    return MoveResult::SUCCESS;
}

MoveResult BotMovementManager::ChaseAtAngle(Unit* target, float distance, float angle, MovementPriority priority)
{
    if (!IsValid())
        return MoveResult::FAILED_CC;

    if (!target || !target->IsInWorld())
        return MoveResult::FAILED_INVALID_TARGET;

    if (target->GetMapId() != m_bot->GetMapId())
        return MoveResult::FAILED_INVALID_TARGET;

    if (IsCC())
        return MoveResult::FAILED_CC;

    // Calculate position at specified angle from target
    float targetAngle = target->GetOrientation() + angle;
    float x = target->GetPositionX() + cos(targetAngle) * distance;
    float y = target->GetPositionY() + sin(targetAngle) * distance;
    float z = SearchBestZ(x, y, target->GetPositionZ());

    if (z <= INVALID_HEIGHT)
        return MoveResult::FAILED_INVALID_POS;

    return MoveTo(x, y, z, priority);
}

MoveResult BotMovementManager::MoveAway(Unit* threat, float distance, MovementPriority priority)
{
    if (!IsValid())
        return MoveResult::FAILED_CC;

    if (!threat)
        return MoveResult::FAILED_INVALID_TARGET;

    if (IsCC())
        return MoveResult::FAILED_CC;

    // FORCED priority always succeeds priority check (flee is critical survival)

    // Calculate angle away from threat
    float angle = threat->GetAngle(m_bot);  // Direction from threat to bot

    // Try multiple angles: directly away, then +/-45, +/-90, etc.
    const float angleOffsets[FLEE_ANGLE_ATTEMPTS] = {
        0.0f,                    // Directly away
        M_PI_F / 4.0f,           // 45 right
        -M_PI_F / 4.0f,          // 45 left
        M_PI_F / 2.0f,           // 90 right
        -M_PI_F / 2.0f,          // 90 left
        3.0f * M_PI_F / 4.0f,    // 135 right
        -3.0f * M_PI_F / 4.0f,   // 135 left
        M_PI_F                   // Backwards (toward threat - last resort)
    };

    for (int i = 0; i < FLEE_ANGLE_ATTEMPTS; ++i)
    {
        float tryAngle = angle + angleOffsets[i];
        float tryX = m_bot->GetPositionX() + cos(tryAngle) * distance;
        float tryY = m_bot->GetPositionY() + sin(tryAngle) * distance;
        float tryZ = SearchBestZ(tryX, tryY, m_bot->GetPositionZ());

        if (tryZ <= INVALID_HEIGHT)
            continue;

        if (!ValidatePath(tryX, tryY, tryZ))
            continue;

        // Found valid flee point
        m_bot->GetMotionMaster()->MovePoint(0, tryX, tryY, tryZ,
            MOVE_PATHFINDING | MOVE_RUN_MODE | MOVE_EXCLUDE_STEEP_SLOPES);

        float dist = m_bot->GetDistance(tryX, tryY, tryZ);
        RecordMovement(tryX, tryY, tryZ, priority, CalculateMoveDelay(dist));

        m_lastMoveCommandTime = WorldTimer::getMSTime();
        return MoveResult::SUCCESS;
    }

    return MoveResult::FAILED_NO_PATH;  // No valid flee point found
}

void BotMovementManager::StopMovement(bool force)
{
    if (!IsValid())
        return;

    if (force || m_state.priority < MovementPriority::PRIORITY_FORCED)
    {
        m_bot->StopMoving();
        m_bot->GetMotionMaster()->Clear();
        m_state.Clear();
    }
}

// ============================================================================
// Update (Stuck Detection)
// ============================================================================

bool BotMovementManager::Update(uint32 diff)
{
    // Guard against use-after-free (race condition with player logout)
    if (!IsValid())
        return false;

    if (!IsMoving())
    {
        m_state.stuckCount = 0;
        m_stuckCheckTimer = 0;
        return false;
    }

    m_stuckCheckTimer += diff;

    // Check stuck every STUCK_CHECK_INTERVAL
    if (m_stuckCheckTimer < STUCK_CHECK_INTERVAL)
        return false;

    m_stuckCheckTimer = 0;
    uint32 now = WorldTimer::getMSTime();

    float currentX = m_bot->GetPositionX();
    float currentY = m_bot->GetPositionY();

    float dx = currentX - m_state.lastX;
    float dy = currentY - m_state.lastY;
    float distMoved = sqrt(dx * dx + dy * dy);

    if (distMoved >= STUCK_DISTANCE_THRESHOLD)
    {
        // Making progress
        m_state.lastX = currentX;
        m_state.lastY = currentY;
        m_state.lastProgressTime = now;
        m_state.stuckCount = 0;
        return false;
    }

    // Not making progress
    ++m_state.stuckCount;

    if (m_state.stuckCount >= STUCK_EMERGENCY_THRESHOLD)
    {
        // Emergency teleport
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL,
            "[BotMovement] %s emergency teleport after %u stuck checks",
            m_bot->GetName(), m_state.stuckCount);
        EmergencyTeleport();
        return true;
    }

    if (m_state.stuckCount >= STUCK_MICRO_RECOVERY_THRESHOLD)
    {
        // Try micro-recovery
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
            "[BotMovement] %s attempting micro-recovery (stuck %u)",
            m_bot->GetName(), m_state.stuckCount);
        TryMicroRecovery();
    }

    return m_state.stuckCount >= STUCK_MICRO_RECOVERY_THRESHOLD;
}

// ============================================================================
// Validation Queries
// ============================================================================

bool BotMovementManager::IsValid() const
{
    // Guard against use-after-free: DON'T dereference m_bot directly!
    // Instead, look up the player by GUID and verify it matches our pointer.
    if (!m_bot || m_botGuid.IsEmpty())
        return false;

    Player* pPlayer = ObjectAccessor::FindPlayer(m_botGuid);
    return pPlayer && pPlayer == m_bot && pPlayer->IsInWorld();
}

bool BotMovementManager::CanMove() const
{
    return IsValid() && !IsCC() && m_bot->IsAlive();
}

bool BotMovementManager::IsMoving() const
{
    if (!IsValid())
        return false;

    return m_bot->IsMoving() ||
           m_bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE;
}

bool BotMovementManager::IsWaitingForMove(MovementPriority priority) const
{
    // If not moving, no wait needed
    if (!IsMoving())
        return false;

    // Higher priority always wins
    if (priority > m_state.priority)
        return false;

    // Same priority: check if previous move has completed
    if (priority == m_state.priority)
    {
        // For chase movements (continuous), don't block same priority
        if (m_state.chaseTarget)
            return false;

        // For point movements, check if expected duration has passed
        uint32 elapsed = WorldTimer::getMSTime() - m_state.moveStartTime;
        if (elapsed >= m_state.expectedDuration)
            return false;
    }

    // Lower priority is blocked
    return true;
}

bool BotMovementManager::IsDuplicateMove(float x, float y, float z, float tolerance) const
{
    // Not moving = not a duplicate
    if (!IsMoving())
        return false;

    // Check if destination matches within tolerance
    float dx = x - m_state.destX;
    float dy = y - m_state.destY;
    float dz = z - m_state.destZ;

    float dist2D = sqrt(dx * dx + dy * dy);
    float dist3D = sqrt(dx * dx + dy * dy + dz * dz);

    // If very close to current destination, it's a duplicate
    if (dist2D < tolerance)
        return true;

    // Check if within timeout window
    uint32 elapsed = WorldTimer::getMSTime() - m_state.moveStartTime;
    if (elapsed < DUPLICATE_TIMEOUT && dist3D < tolerance * 2.0f)
        return true;

    return false;
}

// ============================================================================
// State Queries
// ============================================================================

uint32 BotMovementManager::GetRemainingMoveTime() const
{
    if (!IsMoving())
        return 0;

    // Chase movements are continuous
    if (m_state.chaseTarget)
        return 0;

    uint32 elapsed = WorldTimer::getMSTime() - m_state.moveStartTime;
    if (elapsed >= m_state.expectedDuration)
        return 0;

    return m_state.expectedDuration - elapsed;
}

uint8 BotMovementManager::GetCurrentMovementType() const
{
    return m_bot->GetMotionMaster()->GetCurrentMovementGeneratorType();
}

// ============================================================================
// Terrain Helpers
// ============================================================================

float BotMovementManager::SearchBestZ(float x, float y, float hintZ) const
{
    Map* map = m_bot->GetMap();
    if (!map)
        return INVALID_HEIGHT;

    // Try exact hint Z first
    float z = map->GetHeight(x, y, hintZ);
    if (z > INVALID_HEIGHT && std::abs(z - hintZ) < 2.0f)
        return z;

    // Try MAX_HEIGHT query (catches most cases)
    z = map->GetHeight(x, y, MAX_HEIGHT);
    if (z > INVALID_HEIGHT)
        return z;

    // Multi-Z search (like AzerothCore)
    float bestZ = INVALID_HEIGHT;
    float bestDiff = 9999.0f;

    for (int i = 1; i <= Z_SEARCH_COUNT; ++i)
    {
        // Try above
        float testZ = hintZ + (i * Z_SEARCH_STEP);
        z = map->GetHeight(x, y, testZ);
        if (z > INVALID_HEIGHT)
        {
            float diff = std::abs(z - hintZ);
            if (diff < bestDiff)
            {
                bestZ = z;
                bestDiff = diff;
            }
        }

        // Try below
        testZ = hintZ - (i * Z_SEARCH_STEP);
        if (testZ > 0)  // Don't go below ground level
        {
            z = map->GetHeight(x, y, testZ);
            if (z > INVALID_HEIGHT)
            {
                float diff = std::abs(z - hintZ);
                if (diff < bestDiff)
                {
                    bestZ = z;
                    bestDiff = diff;
                }
            }
        }
    }

    return bestZ;
}

bool BotMovementManager::ValidatePath(float x, float y, float z) const
{
    PathFinder path(m_bot);
    path.calculate(x, y, z);

    PathType pathType = path.getPathType();

    // Accept normal complete paths
    if (pathType == PATHFIND_NORMAL)
        return true;

    // Accept incomplete but usable paths (long distance)
    if (pathType == PATHFIND_INCOMPLETE)
        return true;

    // Reject no-path and other failure types
    return false;
}

// ============================================================================
// Path Smoothing
// ============================================================================

std::vector<Vector3> BotMovementManager::SmoothPath(std::vector<Vector3> const& path) const
{
    if (path.size() <= 2)
        return path;  // Nothing to smooth

    std::vector<Vector3> smoothed;
    smoothed.reserve(path.size());
    smoothed.push_back(path[0]);  // Always keep start

    size_t current = 0;

    while (current < path.size() - 1)
    {
        // Look ahead to find furthest reachable waypoint
        size_t furthest = current + 1;

        for (size_t lookahead = current + 2;
             lookahead < path.size() && lookahead <= current + PATH_SMOOTH_LOOKAHEAD;
             ++lookahead)
        {
            if (CanSkipToWaypoint(path[current], path[lookahead]))
            {
                furthest = lookahead;
            }
        }

        // Add the furthest reachable waypoint
        smoothed.push_back(path[furthest]);
        current = furthest;
    }

    return smoothed;
}

bool BotMovementManager::CanSkipToWaypoint(Vector3 const& from, Vector3 const& to) const
{
    // Distance check - don't skip very short segments
    float dx = to.x - from.x;
    float dy = to.y - from.y;
    float dist = sqrt(dx * dx + dy * dy);

    if (dist < PATH_SMOOTH_MIN_SKIP_DIST)
        return false;

    // Line of sight check (no phase mask in vanilla WoW)
    if (!m_bot->GetMap()->isInLineOfSight(
        from.x, from.y, from.z + 1.5f,  // Eye height
        to.x, to.y, to.z + 1.5f))
    {
        return false;
    }

    // Terrain walkability check - ensure no steep slopes or water between points
    // Sample a few points along the line
    const int samples = 3;
    for (int i = 1; i < samples; ++i)
    {
        float t = static_cast<float>(i) / samples;
        float sampleX = from.x + (to.x - from.x) * t;
        float sampleY = from.y + (to.y - from.y) * t;
        float expectedZ = from.z + (to.z - from.z) * t;
        float sampleZ = SearchBestZ(sampleX, sampleY, expectedZ);

        if (sampleZ <= INVALID_HEIGHT)
            return false;  // No valid terrain at sample point

        // Check if terrain height differs too much from expected (slope check)
        if (std::abs(sampleZ - expectedZ) > 3.0f)
            return false;
    }

    return true;
}

// ============================================================================
// Stuck Recovery
// ============================================================================

bool BotMovementManager::TryMicroRecovery()
{
    // Try stepping backwards
    float angle = m_bot->GetOrientation() + M_PI_F;  // Reverse direction

    float tryX = m_bot->GetPositionX() + cos(angle) * MICRO_RECOVERY_STEP;
    float tryY = m_bot->GetPositionY() + sin(angle) * MICRO_RECOVERY_STEP;
    float tryZ = SearchBestZ(tryX, tryY, m_bot->GetPositionZ());

    if (tryZ > INVALID_HEIGHT && ValidatePath(tryX, tryY, tryZ))
    {
        m_bot->GetMotionMaster()->MovePoint(0, tryX, tryY, tryZ,
            MOVE_PATHFINDING | MOVE_RUN_MODE);
        return true;
    }

    // Try stepping sideways (left then right)
    for (float sideAngle : { M_PI_F / 2.0f, -M_PI_F / 2.0f })
    {
        angle = m_bot->GetOrientation() + sideAngle;
        tryX = m_bot->GetPositionX() + cos(angle) * MICRO_RECOVERY_STEP;
        tryY = m_bot->GetPositionY() + sin(angle) * MICRO_RECOVERY_STEP;
        tryZ = SearchBestZ(tryX, tryY, m_bot->GetPositionZ());

        if (tryZ > INVALID_HEIGHT && ValidatePath(tryX, tryY, tryZ))
        {
            m_bot->GetMotionMaster()->MovePoint(0, tryX, tryY, tryZ,
                MOVE_PATHFINDING | MOVE_RUN_MODE);
            return true;
        }
    }

    return false;
}

void BotMovementManager::EmergencyTeleport()
{
    // Teleport to bind location (hearthstone location)
    // Use TeleportToHomebind with no hearthstone cooldown
    m_bot->TeleportToHomebind(0, false);
    m_state.Clear();

    sLog.Out(LOG_BASIC, LOG_LVL_BASIC,
        "[BotMovement] %s emergency teleported to bind location",
        m_bot->GetName());
}

// ============================================================================
// Internal Helpers
// ============================================================================

float BotMovementManager::CalculateMoveDelay(float distance) const
{
    // Approximate run speed is ~7 yards/second
    // Add buffer for pathfinding overhead
    const float runSpeed = 7.0f;
    float baseTime = (distance / runSpeed) * 1000.0f;  // Convert to ms

    // Add 500ms buffer for pathfinding/movement startup
    return baseTime + 500.0f;
}

bool BotMovementManager::IsCC() const
{
    if (!IsValid() || !m_bot->IsAlive())
        return true;

    // Check for crowd control states
    if (m_bot->HasUnitState(UNIT_STATE_STUNNED))
        return true;
    if (m_bot->HasUnitState(UNIT_STATE_ROOT))
        return true;
    if (m_bot->HasUnitState(UNIT_STATE_CONFUSED))
        return true;
    if (m_bot->HasUnitState(UNIT_STATE_FLEEING))
        return true;

    // Check for specific aura effects that prevent movement
    if (m_bot->HasAuraType(SPELL_AURA_MOD_STUN))
        return true;
    if (m_bot->HasAuraType(SPELL_AURA_MOD_ROOT))
        return true;
    if (m_bot->HasAuraType(SPELL_AURA_MOD_CONFUSE))
        return true;

    return false;
}

void BotMovementManager::RecordMovement(float x, float y, float z, MovementPriority priority, uint32 duration)
{
    m_state.destMapId = m_bot->GetMapId();
    m_state.destX = x;
    m_state.destY = y;
    m_state.destZ = z;
    m_state.priority = priority;
    m_state.moveStartTime = WorldTimer::getMSTime();
    m_state.expectedDuration = duration;

    // Clear chase state for point movements
    m_state.chaseTarget.Clear();
    m_state.chaseDistance = 0.0f;

    // Initialize stuck detection
    m_state.lastX = m_bot->GetPositionX();
    m_state.lastY = m_bot->GetPositionY();
    m_state.lastProgressTime = m_state.moveStartTime;
    m_state.stuckCount = 0;
}
