/*
 * GrindingActivity.h
 *
 * Tier 1 Activity: autonomous mob grinding with travel support.
 * Coordinates GrindingStrategy (find and kill mobs) and
 * TravelingStrategy (move to new grind spots when area depleted).
 *
 * This is the refactored version of the grinding+traveling logic
 * that was previously hardwired in RandomBotAI::UpdateOutOfCombatAI().
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_GRINDINGACTIVITY_H
#define MANGOS_GRINDINGACTIVITY_H

#include "PlayerBots/IBotActivity.h"
#include <memory>

class GrindingStrategy;
class TravelingStrategy;
class VendoringStrategy;
class BotCombatMgr;
class BotMovementManager;

class GrindingActivity : public IBotActivity
{
public:
    GrindingActivity();
    ~GrindingActivity() override;

    // IBotActivity interface
    bool Update(Player* pBot, uint32 diff) override;
    void OnEnterCombat(Player* pBot) override;
    void OnLeaveCombat(Player* pBot) override;
    char const* GetName() const override { return "Grinding"; }

    // Behavior permissions
    bool AllowsLooting() const override { return true; }
    bool AllowsVendoring() const override { return true; }
    bool AllowsTraining() const override { return true; }

    // Wiring (called by RandomBotAI during initialization)
    void SetCombatMgr(BotCombatMgr* pMgr);
    void SetMovementManager(BotMovementManager* pMgr);
    void SetVendoringStrategy(VendoringStrategy* pVendoring);

    // Access to internal behaviors (needed by RandomBotAI)
    GrindingStrategy* GetGrindingStrategy() { return m_grinding.get(); }
    GrindingStrategy const* GetGrindingStrategy() const { return m_grinding.get(); }
    TravelingStrategy* GetTravelingStrategy() { return m_traveling.get(); }
    TravelingStrategy const* GetTravelingStrategy() const { return m_traveling.get(); }

private:
    std::unique_ptr<GrindingStrategy> m_grinding;
    std::unique_ptr<TravelingStrategy> m_traveling;
};

#endif // MANGOS_GRINDINGACTIVITY_H
