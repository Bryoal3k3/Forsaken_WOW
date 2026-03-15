/*
 * IBotActivity.h
 *
 * Interface for bot activities (Tier 1).
 * Activities are coordinators that decide what a bot should be doing
 * and delegate execution to Tier 2 behaviors.
 *
 * Each activity declares which optional behaviors (looting, vendoring,
 * training) are allowed while it is active. Tier 0 survival behaviors
 * (ghost walking, resting, attacker reaction) always run regardless.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_IBOTACTIVITY_H
#define MANGOS_IBOTACTIVITY_H

#include "Common.h"

class Player;

class IBotActivity
{
public:
    virtual ~IBotActivity() = default;

    // Called every tick when out of combat and no Tier 0/behavior interrupts.
    // Returns true if the activity is busy (caller should not run fallbacks).
    virtual bool Update(Player* pBot, uint32 diff) = 0;

    // Combat state notifications
    virtual void OnEnterCombat(Player* pBot) = 0;
    virtual void OnLeaveCombat(Player* pBot) = 0;

    // Activity name for debugging (.bot status)
    virtual char const* GetName() const = 0;

    // Behavior permissions — activity declares what optional behaviors are allowed.
    // RandomBotAI checks these before running vendoring, training, looting.
    virtual bool AllowsLooting() const = 0;
    virtual bool AllowsVendoring() const = 0;
    virtual bool AllowsTraining() const = 0;
};

#endif // MANGOS_IBOTACTIVITY_H
