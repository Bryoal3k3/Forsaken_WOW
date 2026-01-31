/*
 * GrindingStrategy.h
 *
 * Grinding behavior: scan mobs -> pick random -> approach -> kill -> repeat
 *
 * State Machine:
 *   IDLE -> Scan & pick target -> APPROACHING -> IN_COMBAT -> IDLE
 *                                      |
 *                               TIMEOUT (15s) -> Clear target -> IDLE
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_GRINDINGSTRATEGY_H
#define MANGOS_GRINDINGSTRATEGY_H

#include "IBotStrategy.h"
#include "SharedDefines.h"  // For CLASS_* constants
#include "ObjectGuid.h"

#include <vector>

class BotCombatMgr;
class BotMovementManager;
class Creature;
class Player;

// Helper: Returns true for classes that engage at range and need Line of Sight
inline bool IsRangedClass(uint8 classId)
{
    switch (classId)
    {
        case CLASS_MAGE:
        case CLASS_PRIEST:
        case CLASS_WARLOCK:
        case CLASS_HUNTER:
            return true;
        default:
            return false;
    }
}

// Result of grinding update - explicit signaling for travel decisions
enum class GrindingResult
{
    ENGAGED,        // Have a target, approaching or fighting
    NO_TARGETS,     // Searched area, no valid mobs found
    BUSY            // Doing something else (in combat, looting, etc.)
};

// Internal state for grinding state machine
enum class GrindState
{
    IDLE,           // No target, ready to search
    APPROACHING,    // Moving toward target
    IN_COMBAT       // Fighting target
};

class GrindingStrategy : public IBotStrategy
{
public:
    GrindingStrategy();

    // Set combat manager (called by RandomBotAI after construction)
    void SetCombatMgr(BotCombatMgr* pCombatMgr) { m_pCombatMgr = pCombatMgr; }

    // Set movement manager (called by RandomBotAI after construction)
    void SetMovementManager(BotMovementManager* pMoveMgr) { m_pMovementMgr = pMoveMgr; }

    // IBotStrategy interface
    bool Update(Player* pBot, uint32 diff) override;
    void OnEnterCombat(Player* pBot) override;
    void OnLeaveCombat(Player* pBot) override;
    char const* GetName() const override { return "Grinding"; }

    // Extended interface with explicit result
    GrindingResult UpdateGrinding(Player* pBot, uint32 diff);

    // Track consecutive "no mobs" failures for travel decisions
    uint32 GetNoMobsCount() const { return m_noMobsCount; }
    void ResetNoMobsCount() { m_noMobsCount = 0; }

    // Reset state completely (used after spirit healer resurrection)
    void Reset(Player* pBot);

    // Set target externally (used when bot switches to a new attacker)
    void SetTarget(Creature* pTarget);

    // Get current state (for debugging)
    GrindState GetState() const { return m_state; }
    ObjectGuid GetCurrentTarget() const { return m_currentTarget; }

    // Validate a single creature as a grind target (basic checks, no path)
    // Public because AllGrindTargets checker needs access
    bool IsValidGrindTarget(Player* pBot, Creature* pCreature) const;

private:
    // === Target Finding ===

    // Scan all valid mobs in range (not just nearest)
    std::vector<Creature*> ScanForTargets(Player* pBot, float range);

    // Validate that we can path to the target
    bool HasValidPathTo(Player* pBot, Creature* pCreature) const;

    // Pick a random target from candidates (validates path for each)
    Creature* SelectRandomTarget(Player* pBot, std::vector<Creature*>& candidates);

    // === State Handlers ===

    GrindingResult HandleIdle(Player* pBot);
    GrindingResult HandleApproaching(Player* pBot);
    GrindingResult HandleInCombat(Player* pBot);

    // Clear current target and reset to IDLE
    void ClearTarget(Player* pBot);

    // Get creature from our stored GUID
    Creature* GetCurrentTargetCreature(Player* pBot) const;

    // === Members ===

    // Combat manager (set by RandomBotAI)
    BotCombatMgr* m_pCombatMgr = nullptr;

    // Movement manager (set by RandomBotAI)
    BotMovementManager* m_pMovementMgr = nullptr;

    // State machine
    GrindState m_state = GrindState::IDLE;
    ObjectGuid m_currentTarget;         // Our tracked target (NOT GetVictim)
    uint32 m_approachStartTime = 0;     // When we started approaching (ms)

    // Consecutive "no mobs" counter for travel system
    uint32 m_noMobsCount = 0;

    // Adaptive search - backoff when no mobs found
    uint32 m_skipTicks = 0;
    uint32 m_backoffLevel = 0;

    // === Configuration ===

    static constexpr float SEARCH_RANGE = 75.0f;            // Scan radius for mobs
    static constexpr int32 LEVEL_RANGE = 2;                 // Bot level +/- this value
    static constexpr uint32 APPROACH_TIMEOUT_MS = 30000;    // 30 seconds to reach target
    static constexpr float PATH_LENGTH_RATIO = 2.0f;        // Reject if path > 2x straight-line dist
    static constexpr uint32 BACKOFF_MAX_LEVEL = 3;          // Max backoff: 8 ticks
};

#endif // MANGOS_GRINDINGSTRATEGY_H
