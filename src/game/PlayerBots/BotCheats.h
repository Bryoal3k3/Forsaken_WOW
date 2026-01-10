/*
 * BotCheats.h
 *
 * Utility class for RandomBot "cheats" - shortcuts that bypass
 * tedious player mechanics like consumables, reagents, ammo, etc.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_BOTCHEATS_H
#define MANGOS_BOTCHEATS_H

#include "Common.h"

class Player;

class BotCheats
{
public:
    // Resting behavior - sit and regen HP/mana without consuming items
    // Returns true if bot is currently resting (caller should skip other actions)
    // Thresholds: Start resting at 35% HP or 45% mana, stop at 90% both
    // State variables (isResting, tickTimer) are owned by the caller (RandomBotAI)
    static bool HandleResting(Player* bot, uint32 diff, bool& isResting, uint32& tickTimer);

    // Check if it's safe to rest (not in combat, no group members in combat)
    static bool CanRest(Player* bot);

    // Future cheats (placeholders for now):
    // static bool HasReagent(Player* bot, uint32 spellId);  // Always true
    // static bool HasAmmo(Player* bot);                      // Always true
    // static void RepairEquipment(Player* bot);              // Free repair

private:
    // Resting constants
    static constexpr float RESTING_HP_START_THRESHOLD = 35.0f;   // Start resting below this HP%
    static constexpr float RESTING_MANA_START_THRESHOLD = 45.0f; // Start resting below this mana%
    static constexpr float RESTING_STOP_THRESHOLD = 90.0f;       // Stop resting above this %
    static constexpr uint32 RESTING_TICK_INTERVAL = 2000;        // 2 seconds between ticks
    static constexpr float RESTING_REGEN_PERCENT = 5.0f;         // 5% per tick
};

#endif // MANGOS_BOTCHEATS_H
