/*
 * BotCheats.cpp
 *
 * Implementation of bot "cheats" - shortcuts for tedious player mechanics.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "BotCheats.h"
#include "Player.h"
#include "Group/Group.h"
#include "ObjectAccessor.h"
#include "UnitDefines.h"

bool BotCheats::HandleResting(Player* bot, uint32 diff, bool& isResting, uint32& tickTimer)
{
    if (!bot || !bot->IsAlive())
        return false;

    // If we can't rest (in combat or group in combat), stop resting immediately
    if (!CanRest(bot))
    {
        if (isResting)
        {
            isResting = false;
            tickTimer = 0;
            bot->SetStandState(UNIT_STAND_STATE_STAND);
        }
        return false;
    }

    float hpPercent = bot->GetHealthPercent();
    float manaPercent = 100.0f; // Default for non-mana classes

    // Check if bot uses mana
    if (bot->GetMaxPower(POWER_MANA) > 0)
        manaPercent = bot->GetPowerPercent(POWER_MANA);

    // Currently resting - check if we should stop
    if (isResting)
    {
        // Stop if HP and mana are both above threshold
        if (hpPercent >= RESTING_STOP_THRESHOLD && manaPercent >= RESTING_STOP_THRESHOLD)
        {
            isResting = false;
            tickTimer = 0;
            bot->SetStandState(UNIT_STAND_STATE_STAND);
            return false;
        }

        // Continue resting - apply regen on tick
        if (tickTimer <= diff)
        {
            tickTimer = RESTING_TICK_INTERVAL;

            // Regen HP if below max
            if (hpPercent < 100.0f)
            {
                uint32 hpRegen = (bot->GetMaxHealth() * RESTING_REGEN_PERCENT) / 100;
                bot->ModifyHealth(hpRegen);
            }

            // Regen mana if below max and bot uses mana
            if (manaPercent < 100.0f && bot->GetMaxPower(POWER_MANA) > 0)
            {
                uint32 manaRegen = (bot->GetMaxPower(POWER_MANA) * RESTING_REGEN_PERCENT) / 100;
                bot->ModifyPower(POWER_MANA, manaRegen);
            }
        }
        else
        {
            tickTimer -= diff;
        }

        return true; // Still resting
    }

    // Not currently resting - check if we should start
    bool needsHpRegen = hpPercent < RESTING_HP_START_THRESHOLD;
    bool needsManaRegen = (bot->GetMaxPower(POWER_MANA) > 0) && (manaPercent < RESTING_MANA_START_THRESHOLD);

    if (needsHpRegen || needsManaRegen)
    {
        isResting = true;
        tickTimer = RESTING_TICK_INTERVAL;
        bot->SetStandState(UNIT_STAND_STATE_SIT);
        return true;
    }

    return false;
}

bool BotCheats::CanRest(Player* bot)
{
    if (!bot)
        return false;

    // Can't rest if bot is in combat
    if (bot->IsInCombat())
        return false;

    // Check if any group/raid member is in combat
    Group* group = bot->GetGroup();
    if (group)
    {
        for (const auto& memberSlot : group->GetMemberSlots())
        {
            // Skip self
            if (memberSlot.guid == bot->GetObjectGuid())
                continue;

            Player* member = ObjectAccessor::FindPlayer(memberSlot.guid);
            if (member && member->IsInCombat())
                return false; // Group member in combat - don't rest
        }
    }

    return true;
}
