/*
 * LootingBehavior.h
 *
 * Handles looting corpses after combat.
 * Scans for nearby dead creatures the bot has tapped, walks to them, and loots.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_LOOTINGBEHAVIOR_H
#define MANGOS_LOOTINGBEHAVIOR_H

#include "ObjectGuid.h"
#include "Timer.h"

class Player;
class Creature;
class BotMovementManager;

class LootingBehavior
{
public:
    LootingBehavior();

    // Returns true if actively looting (bot is busy)
    bool Update(Player* pBot, uint32 diff);

    // Call when combat ends to trigger looting
    void OnCombatEnded(Player* pBot);

    // Check if currently looting
    bool IsLooting() const { return m_isLooting; }

    // Reset state (call on bot death or other edge cases)
    void Reset();

    // Set movement manager (called by RandomBotAI after construction)
    void SetMovementManager(BotMovementManager* pMoveMgr) { m_pMovementMgr = pMoveMgr; }

private:
    // Movement manager (set by RandomBotAI, centralized movement coordination)
    BotMovementManager* m_pMovementMgr = nullptr;
    Creature* FindLootableCorpse(Player* pBot);
    void LootCorpse(Player* pBot, Creature* pCorpse);

    ObjectGuid m_lootTarget;
    bool m_isLooting = false;
    ShortTimeTracker m_timeoutTimer;

    // Tunable constants
    static constexpr float LOOT_RANGE = 40.0f;        // Max distance to travel for loot
    static constexpr float INTERACT_RANGE = 5.0f;     // Distance needed to loot
    static constexpr uint32 LOOT_TIMEOUT_MS = 12000;  // 12 second timeout
};

#endif // MANGOS_LOOTINGBEHAVIOR_H
