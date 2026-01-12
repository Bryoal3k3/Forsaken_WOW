/*
 * IClassCombat.h
 *
 * Interface for class-specific combat handlers.
 * Each class (Warrior, Mage, etc.) implements this interface.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_ICLASSCOMBAT_H
#define MANGOS_ICLASSCOMBAT_H

#include "Common.h"

class Player;
class Unit;

class IClassCombat
{
public:
    virtual ~IClassCombat() = default;

    // How to initiate combat (pull)
    // Returns true if engagement started successfully
    virtual bool Engage(Player* pBot, Unit* pTarget) = 0;

    // Combat rotation - called every tick while in combat
    virtual void UpdateCombat(Player* pBot, Unit* pVictim) = 0;

    // Out of combat behavior - buffs, pet management
    virtual void UpdateOutOfCombat(Player* pBot) = 0;

    // Debug name
    virtual char const* GetName() const = 0;
};

#endif // MANGOS_ICLASSCOMBAT_H
