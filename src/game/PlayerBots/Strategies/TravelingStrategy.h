/*
 * TravelingStrategy.h
 *
 * Strategy for traveling to level-appropriate grind spots.
 * Uses database-driven grind_spots table for destination selection.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_TRAVELINGSTRATEGY_H
#define MANGOS_TRAVELINGSTRATEGY_H

#include "IBotStrategy.h"
#include <string>

class Player;

namespace TravelConstants
{
    // Pre-travel vendor thresholds
    constexpr float DURABILITY_THRESHOLD = 0.5f;    // Vendor if any gear < 50%
    constexpr float BAG_FULL_THRESHOLD = 0.6f;      // Vendor if bags > 60% full

    // Anti-thrashing
    constexpr uint32 ARRIVAL_COOLDOWN_MS = 90000;   // 90 sec minimum stay at spot
    constexpr uint32 NO_MOBS_THRESHOLD = 5;         // Consecutive "no mobs" before travel

    // Movement
    constexpr float ARRIVAL_DISTANCE = 30.0f;       // Consider "arrived" within 30 yards
    constexpr uint32 STUCK_TIMEOUT_MS = 30000;      // 30 sec without progress = stuck
    constexpr float STUCK_MIN_DISTANCE = 5.0f;      // Must move 5 yards per check
}

class TravelingStrategy : public IBotStrategy
{
public:
    TravelingStrategy();

    // IBotStrategy interface
    bool Update(Player* pBot, uint32 diff) override;
    void OnEnterCombat(Player* pBot) override;
    void OnLeaveCombat(Player* pBot) override;
    char const* GetName() const override { return "TravelingStrategy"; }

    // Called by RandomBotAI when grinding reports NO_TARGETS
    void SignalNoMobs() { m_noMobsSignaled = true; }

    // Called when bot arrives at destination or finds mobs
    void ResetArrivalCooldown();

    // Check if currently traveling
    bool IsTraveling() const;

private:
    enum class TravelState
    {
        IDLE,           // Not traveling, checking if needed
        FINDING_SPOT,   // Query DB for destination
        WALKING,        // Moving to destination
        ARRIVED         // At destination, on cooldown
    };

    TravelState m_state = TravelState::IDLE;
    bool m_noMobsSignaled = false;

    // Destination
    float m_targetX = 0.0f;
    float m_targetY = 0.0f;
    float m_targetZ = 0.0f;
    std::string m_targetName;

    // Anti-thrashing
    uint32 m_arrivalTime = 0;           // When we arrived at current spot

    // Stuck detection
    float m_lastX = 0.0f;
    float m_lastY = 0.0f;
    uint32 m_lastProgressTime = 0;

    // Helpers
    bool FindGrindSpot(Player* pBot);
    bool IsAtDestination(Player* pBot) const;
    bool ShouldTravel(Player* pBot) const;
    static uint8 GetBotFaction(Player* pBot);
};

#endif // MANGOS_TRAVELINGSTRATEGY_H
