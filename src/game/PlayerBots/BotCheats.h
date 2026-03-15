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
    // Resting behavior - cast food/drink spells for natural regen with animations
    // Returns true if bot is currently resting (caller should skip other actions)
    // Thresholds: Start eating at 50% HP, drinking at 40% mana, stop at 90% both
    // Uses spell 1131 (food) and 1137 (drink) - same as PartyBotAI
    static bool HandleResting(Player* bot, bool& isResting);

    // Check if it's safe to rest (not in combat, no group members in combat)
    static bool CanRest(Player* bot);

    // Future cheats (placeholders for now):
    // static bool HasReagent(Player* bot, uint32 spellId);  // Always true
    // static bool HasAmmo(Player* bot);                      // Always true
    // static void RepairEquipment(Player* bot);              // Free repair

private:
    // Resting spell IDs (percentage-based regen, same as cMangos bots)
    static constexpr uint32 SPELL_FOOD = 24005;  // 2% max HP per tick + eating animation
    static constexpr uint32 SPELL_DRINK = 24355; // 2% max mana per tick + drinking animation

    // Resting thresholds
    static constexpr float RESTING_HP_START_THRESHOLD = 50.0f;   // Start eating below this HP%
    static constexpr float RESTING_MANA_START_THRESHOLD = 40.0f; // Start drinking below this mana%
    static constexpr float RESTING_STOP_THRESHOLD = 90.0f;       // Stop resting above this %
};

#endif // MANGOS_BOTCHEATS_H
