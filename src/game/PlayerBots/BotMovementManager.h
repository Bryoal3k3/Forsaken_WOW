/*
 * BotMovementManager.h
 *
 * Centralized movement coordinator for RandomBot AI.
 * Handles all bot movement with proper validation, state tracking, and recovery.
 *
 * Features:
 * - Movement priority system (prevents combat interrupted by travel)
 * - Duplicate detection (prevents MoveChase spam)
 * - Multi-Z height search (handles caves, multi-story terrain)
 * - Fast stuck detection (5 sec vs 30 sec)
 * - Path smoothing (skips unnecessary waypoints)
 * - Destination randomization (prevents bot stacking)
 * - Approach angle variation (natural combat positioning)
 * - Flee mechanism (survival when overwhelmed)
 *
 * Inspired by AzerothCore mod-playerbots, adapted for vMangos.
 */

#ifndef BOT_MOVEMENT_MANAGER_H
#define BOT_MOVEMENT_MANAGER_H

#include "Common.h"
#include "ObjectGuid.h"
#include "G3D/Vector3.h"

#include <vector>

class Player;
class Unit;
class Map;

// Use G3D::Vector3 for path points
using G3D::Vector3;

// Movement priority - higher values override lower
enum class MovementPriority : uint8
{
    PRIORITY_IDLE = 0,      // Standing still, can be interrupted by anything
    PRIORITY_WANDER = 1,    // Random exploration (future use)
    PRIORITY_NORMAL = 2,    // Travel, vendor, loot, ghost walk
    PRIORITY_COMBAT = 3,    // Chase, engage, combat positioning
    PRIORITY_FORCED = 4     // Cannot be overridden (flee, emergency)
};

// Movement result for diagnostics
enum class MoveResult : uint8
{
    SUCCESS,                // Movement command issued
    FAILED_CC,              // Bot is CC'd (stunned/rooted/frozen)
    FAILED_DUPLICATE,       // Already moving to same destination
    FAILED_PRIORITY,        // Lower priority blocked by higher
    FAILED_NO_PATH,         // PathFinder couldn't find route
    FAILED_INVALID_TARGET,  // Target null or wrong map
    FAILED_INVALID_POS,     // Destination has no valid terrain
    FAILED_COOLDOWN         // Too soon after last move command
};

// State tracking structure (inspired by AzerothCore LastMovement)
struct MovementState
{
    // Destination
    uint32 destMapId = 0;
    float destX = 0.0f;
    float destY = 0.0f;
    float destZ = 0.0f;

    // Timing
    uint32 moveStartTime = 0;       // When movement was issued (ms)
    uint32 expectedDuration = 0;    // How long move should take (ms)

    // Priority
    MovementPriority priority = MovementPriority::PRIORITY_IDLE;

    // Chase target (if chasing)
    ObjectGuid chaseTarget;
    float chaseDistance = 0.0f;

    // Stuck detection
    float lastX = 0.0f;
    float lastY = 0.0f;
    uint32 lastProgressTime = 0;    // Last time we made progress
    uint32 stuckCount = 0;          // Consecutive stuck detections

    void Clear()
    {
        destMapId = 0;
        destX = destY = destZ = 0.0f;
        moveStartTime = expectedDuration = 0;
        priority = MovementPriority::PRIORITY_IDLE;
        chaseTarget.Clear();
        chaseDistance = 0.0f;
        lastX = lastY = 0.0f;
        lastProgressTime = 0;
        stuckCount = 0;
    }
};

class BotMovementManager
{
public:
    explicit BotMovementManager(Player* bot);
    ~BotMovementManager() = default;

    // === MAIN MOVEMENT COMMANDS ===

    // Move to a specific point (travel, vendor, loot, ghost)
    // pointId is passed to MovementInform callback (use for waypoint tracking)
    MoveResult MoveTo(float x, float y, float z,
                      MovementPriority priority = MovementPriority::PRIORITY_NORMAL,
                      uint32 pointId = 0);

    // Move near a point (with 8-angle search + randomization to avoid stacking)
    MoveResult MoveNear(float x, float y, float z, float maxDist = 3.0f,
                        MovementPriority priority = MovementPriority::PRIORITY_NORMAL);

    // Move near a unit (with destination randomization)
    MoveResult MoveNear(Unit* target, float distance = 3.0f,
                        MovementPriority priority = MovementPriority::PRIORITY_NORMAL);

    // Chase a unit (combat) - with approach angle variation
    MoveResult Chase(Unit* target, float distance = 0.0f,
                     MovementPriority priority = MovementPriority::PRIORITY_COMBAT);

    // Chase with specific angle (for flanking/positioning)
    MoveResult ChaseAtAngle(Unit* target, float distance, float angle,
                            MovementPriority priority = MovementPriority::PRIORITY_COMBAT);

    // Move away from a threat (flee)
    MoveResult MoveAway(Unit* threat, float distance = 15.0f,
                        MovementPriority priority = MovementPriority::PRIORITY_FORCED);

    // Stop all movement
    void StopMovement(bool force = false);

    // === UPDATE (call every AI tick) ===

    // Returns true if bot is stuck and needs intervention
    bool Update(uint32 diff);

    // === VALIDATION QUERIES ===

    // Can the bot move at all? (not stunned/rooted/etc)
    bool CanMove() const;

    // Is bot currently moving?
    bool IsMoving() const;

    // Is bot waiting for a higher priority move to complete?
    bool IsWaitingForMove(MovementPriority priority) const;

    // Would this move be a duplicate of current movement?
    bool IsDuplicateMove(float x, float y, float z, float tolerance = 0.5f) const;

    // === STATE QUERIES ===

    MovementPriority GetCurrentPriority() const { return m_state.priority; }
    MovementState const& GetState() const { return m_state; }

    // Estimated time remaining for current move (ms)
    uint32 GetRemainingMoveTime() const;

    // Get current movement generator type (for introspection)
    uint8 GetCurrentMovementType() const;

    // === TERRAIN HELPERS ===

    // Multi-Z height search (tries 5 heights like AzerothCore)
    float SearchBestZ(float x, float y, float hintZ) const;

    // Validate a path exists to destination
    bool ValidatePath(float x, float y, float z) const;

    // === PATH SMOOTHING ===

    // Smooth a path by skipping unnecessary waypoints (LoS-based)
    std::vector<Vector3> SmoothPath(std::vector<Vector3> const& path) const;

    // Check if we can skip directly to a further waypoint
    bool CanSkipToWaypoint(Vector3 const& from, Vector3 const& to) const;

    // === STUCK RECOVERY ===

    // Attempt micro-recovery (step back, sidestep)
    bool TryMicroRecovery();

    // Force teleport to safe location (last resort)
    void EmergencyTeleport();

private:
    Player* m_bot;
    MovementState m_state;

    // Timing
    uint32 m_lastMoveCommandTime = 0;   // Prevent command spam
    uint32 m_stuckCheckTimer = 0;       // Timer for stuck checks

    // === CONSTANTS ===

    // Core movement
    static constexpr uint32 MOVE_COMMAND_COOLDOWN = 250;  // ms between commands
    static constexpr uint32 DUPLICATE_TIMEOUT = 3000;     // 3 sec duplicate window

    // Stuck detection thresholds
    static constexpr uint32 STUCK_CHECK_INTERVAL = 1000;  // Check every 1 sec
    static constexpr float STUCK_DISTANCE_THRESHOLD = 1.0f;  // Must move > 1 yard
    static constexpr uint32 STUCK_TIME_THRESHOLD = 5000;  // 5 sec without progress
    static constexpr uint32 STUCK_MICRO_RECOVERY_THRESHOLD = 2;  // Try micro-recovery after 2 stuck checks
    static constexpr uint32 STUCK_EMERGENCY_THRESHOLD = 5;  // Emergency teleport after 5 stuck checks

    // Z-search parameters
    static constexpr int Z_SEARCH_COUNT = 5;      // How many Z levels to try
    static constexpr float Z_SEARCH_STEP = 8.0f;  // Height increment

    // Flee parameters
    static constexpr float FLEE_MIN_DISTANCE = 5.0f;
    static constexpr int FLEE_ANGLE_ATTEMPTS = 8;  // Try 8 directions

    // Destination randomization (MoveNear)
    static constexpr int MOVE_NEAR_ANGLE_ATTEMPTS = 8;  // 8 cardinal directions (45 degrees apart)
    static constexpr float MOVE_NEAR_RANDOM_OFFSET = 2.0f;  // +/-2 yard random offset

    // Approach angle variation (Chase)
    static constexpr float APPROACH_ANGLE_MAX = 0.2618f;  // ~15 degrees (pi/12)
    static constexpr float FLANK_ANGLE_MIN = 1.5708f;     // 90 degrees (pi/2)
    static constexpr float FLANK_ANGLE_MAX = 2.0944f;     // 120 degrees (2*pi/3)

    // Path smoothing
    static constexpr float PATH_SMOOTH_MIN_SKIP_DIST = 5.0f;  // Min distance to consider skip
    static constexpr int PATH_SMOOTH_LOOKAHEAD = 3;  // How many waypoints ahead to check

    // Recovery step distance
    static constexpr float MICRO_RECOVERY_STEP = 3.0f;  // yards

    // Internal helpers
    float CalculateMoveDelay(float distance) const;
    bool IsCC() const;  // Stun/root/freeze/confuse check
    void UpdateStuckDetection(uint32 diff);
    void RecordMovement(float x, float y, float z, MovementPriority priority, uint32 duration);
};

#endif // BOT_MOVEMENT_MANAGER_H
