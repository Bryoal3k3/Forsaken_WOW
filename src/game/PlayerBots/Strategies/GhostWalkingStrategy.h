/*
 * GhostWalkingStrategy.h
 *
 * Strategy for handling bot death - ghost walking back to corpse.
 * Handles death loop detection and spirit healer resurrection.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_GHOSTWALKINGSTRATEGY_H
#define MANGOS_GHOSTWALKINGSTRATEGY_H

#include "IBotStrategy.h"
#include <vector>
#include <ctime>

class GhostWalkingStrategy : public IBotStrategy
{
public:
    GhostWalkingStrategy();

    // IBotStrategy interface
    bool Update(Player* pBot, uint32 diff) override;
    void OnEnterCombat(Player* pBot) override {}  // N/A when dead
    void OnLeaveCombat(Player* pBot) override {}  // N/A when dead
    char const* GetName() const override { return "GhostWalking"; }

    // Called when bot first dies - sets up ghost state
    void OnDeath(Player* pBot);

    // Check if resurrection is complete (bot is alive again)
    bool IsComplete(Player* pBot) const;

    // Reset state for reuse
    void Reset();

private:
    bool m_initialized = false;
    bool m_isWalkingToCorpse = false;
    std::vector<time_t> m_recentDeaths;  // Timestamps for death loop detection

    void RecordDeath();
    void ClearOldDeaths();
    bool IsInDeathLoop() const;

    // Constants
    static constexpr int DEATH_LOOP_COUNT = 3;            // 3 deaths = death loop
    static constexpr time_t DEATH_LOOP_WINDOW = 600;      // 10 minutes in seconds
    static constexpr float CORPSE_RESURRECT_RANGE = 5.0f; // Must be on top of corpse
};

#endif // MANGOS_GHOSTWALKINGSTRATEGY_H
