/*
 * IBotStrategy.h
 *
 * Interface for bot behavior strategies.
 * Strategies handle high-level goals (what to do next).
 * Combat rotations stay in the AI class (how to fight).
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_IBOTSTRATEGY_H
#define MANGOS_IBOTSTRATEGY_H

#include "Common.h"

class Player;
class Creature;

class IBotStrategy
{
public:
    virtual ~IBotStrategy() = default;

    // Called every AI update tick when out of combat
    // Returns true if the strategy took an action (found target, started resting, etc.)
    virtual bool Update(Player* pBot, uint32 diff) = 0;

    // Called when bot enters combat
    virtual void OnEnterCombat(Player* pBot) = 0;

    // Called when bot leaves combat
    virtual void OnLeaveCombat(Player* pBot) = 0;

    // Strategy name for debugging
    virtual char const* GetName() const = 0;
};

#endif // MANGOS_IBOTSTRATEGY_H
