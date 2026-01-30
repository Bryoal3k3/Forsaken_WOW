/*
 * GrindingStrategy.h
 *
 * Grinding behavior: find mob -> attack -> kill -> loot -> rest -> repeat
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_GRINDINGSTRATEGY_H
#define MANGOS_GRINDINGSTRATEGY_H

#include "IBotStrategy.h"
#include "SharedDefines.h"  // For CLASS_* constants

class BotCombatMgr;
class BotMovementManager;
class Creature;
class Player;

// Helper: Returns true for classes that engage at range and need Line of Sight
// These classes stop at distance to cast/shoot - if LoS is blocked, they get stuck
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
    ENGAGED,        // Found target, attacking
    NO_TARGETS,     // Searched area, no valid mobs found
    BUSY            // Doing something else (in combat, looting, etc.)
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

    // Target validation (public for use by NearestGrindTarget checker)
    bool IsValidGrindTarget(Player* pBot, Creature* pCreature) const;

private:
    // Target finding
    Creature* FindGrindTarget(Player* pBot, float range = 50.0f);

    // Combat manager (set by RandomBotAI, avoids dynamic_cast in hot path)
    BotCombatMgr* m_pCombatMgr = nullptr;

    // Movement manager (set by RandomBotAI, centralized movement coordination)
    BotMovementManager* m_pMovementMgr = nullptr;

    // Consecutive "no mobs" counter for travel system
    uint32 m_noMobsCount = 0;

    // Adaptive search - backoff when no mobs found
    uint32 m_skipTicks = 0;      // Ticks to skip before next search
    uint32 m_backoffLevel = 0;   // Current backoff level (0-3)

    // Configuration
    static constexpr float SEARCH_RANGE_CLOSE = 50.0f;   // First tier - quick local search
    static constexpr float SEARCH_RANGE_FAR = 150.0f;    // Second tier - full range
    static constexpr int32 LEVEL_RANGE = 2;              // Bot level +/- this value
    static constexpr uint32 BACKOFF_MAX_LEVEL = 3;       // Max backoff: 8 ticks (8 seconds)
};

#endif // MANGOS_GRINDINGSTRATEGY_H
