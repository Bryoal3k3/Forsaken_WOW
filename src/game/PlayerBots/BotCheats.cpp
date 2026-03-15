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
#include "Spells/SpellMgr.h"
#include "MotionMaster.h"

bool BotCheats::HandleResting(Player* bot, bool& isResting)
{
    if (!bot || !bot->IsAlive())
        return false;

    bool isEating = bot->HasAura(SPELL_FOOD);
    bool isDrinking = bot->HasAura(SPELL_DRINK);

    // If we can't rest (in combat or group in combat), stop resting immediately
    if (!CanRest(bot))
    {
        if (isEating)
            bot->RemoveAurasDueToSpell(SPELL_FOOD);
        if (isDrinking)
            bot->RemoveAurasDueToSpell(SPELL_DRINK);
        isResting = false;
        return false;
    }

    float hpPercent = bot->GetHealthPercent();
    float manaPercent = 100.0f; // Default for non-mana classes

    // Check if bot uses mana
    if (bot->GetMaxPower(POWER_MANA) > 0)
        manaPercent = bot->GetPowerPercent(POWER_MANA);

    // Check if we should stop resting (both resources above threshold)
    if (hpPercent >= RESTING_STOP_THRESHOLD && manaPercent >= RESTING_STOP_THRESHOLD)
    {
        if (isEating)
            bot->RemoveAurasDueToSpell(SPELL_FOOD);
        if (isDrinking)
            bot->RemoveAurasDueToSpell(SPELL_DRINK);
        isResting = false;
        return false;
    }

    // Check if we need to eat (HP below threshold and not already eating)
    bool needsHpRegen = hpPercent < RESTING_HP_START_THRESHOLD;
    bool needsManaRegen = (bot->GetMaxPower(POWER_MANA) > 0) && (manaPercent < RESTING_MANA_START_THRESHOLD);

    // Also continue eating/drinking if we have the aura but haven't reached stop threshold
    if (isEating && hpPercent < RESTING_STOP_THRESHOLD)
        needsHpRegen = true;
    if (isDrinking && manaPercent < RESTING_STOP_THRESHOLD)
        needsManaRegen = true;

    if (needsHpRegen && !isEating)
    {
        // Stop movement before eating
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType())
        {
            bot->StopMoving();
            bot->GetMotionMaster()->Clear(false, true);
            bot->GetMotionMaster()->MoveIdle();
        }
        if (SpellEntry const* pSpellEntry = sSpellMgr.GetSpellEntry(SPELL_FOOD))
        {
            bot->CastSpell(bot, pSpellEntry, true);
            bot->RemoveSpellCooldown(*pSpellEntry);
        }
        isResting = true;
    }

    if (needsManaRegen && !isDrinking)
    {
        // Stop movement before drinking
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType())
        {
            bot->StopMoving();
            bot->GetMotionMaster()->Clear(false, true);
            bot->GetMotionMaster()->MoveIdle();
        }
        if (SpellEntry const* pSpellEntry = sSpellMgr.GetSpellEntry(SPELL_DRINK))
        {
            bot->CastSpell(bot, pSpellEntry, true);
            bot->RemoveSpellCooldown(*pSpellEntry);
        }
        isResting = true;
    }

    // Update resting state based on current auras
    isResting = bot->HasAura(SPELL_FOOD) || bot->HasAura(SPELL_DRINK);
    return isResting;
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
