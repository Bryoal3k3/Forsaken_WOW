/*
 * GrindingStrategy.h
 *
 * Grinding behavior: find mob -> attack -> kill -> loot -> rest -> repeat
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_GRINDINGSTRATEGY_H
#define MANGOS_GRINDINGSTRATEGY_H

#include "IBotStrategy.h"

class Creature;
class Player;

class GrindingStrategy : public IBotStrategy
{
public:
    GrindingStrategy();

    // IBotStrategy interface
    bool Update(Player* pBot, uint32 diff) override;
    void OnEnterCombat(Player* pBot) override;
    void OnLeaveCombat(Player* pBot) override;
    char const* GetName() const override { return "Grinding"; }

private:
    // Target finding
    Creature* FindGrindTarget(Player* pBot, float range = 50.0f);
    bool IsValidGrindTarget(Player* pBot, Creature* pCreature) const;

    // Configuration
    static constexpr float SEARCH_RANGE = 50.0f;
    static constexpr int32 LEVEL_RANGE = 2;  // Bot level +/- this value
};

#endif // MANGOS_GRINDINGSTRATEGY_H
