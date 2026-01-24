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

class BotCombatMgr;
class Creature;
class Player;

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

private:
    // Target finding
    Creature* FindGrindTarget(Player* pBot, float range = 50.0f);
    bool IsValidGrindTarget(Player* pBot, Creature* pCreature) const;

    // Combat manager (set by RandomBotAI, avoids dynamic_cast in hot path)
    BotCombatMgr* m_pCombatMgr = nullptr;

    // Consecutive "no mobs" counter for travel system
    uint32 m_noMobsCount = 0;

    // Configuration
    static constexpr float SEARCH_RANGE = 150.0f;  // Search radius for finding mobs
    static constexpr int32 LEVEL_RANGE = 2;  // Bot level +/- this value
};

#endif // MANGOS_GRINDINGSTRATEGY_H
