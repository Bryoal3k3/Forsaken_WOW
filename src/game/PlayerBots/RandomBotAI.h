/*
 * RandomBotAI.h
 *
 * AI class for autonomous RandomBots.
 * Combat rotations live here; high-level behavior delegated to strategies.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_RANDOMBOTAI_H
#define MANGOS_RANDOMBOTAI_H

#include "CombatBotBaseAI.h"
#include <memory>

class IBotStrategy;

class RandomBotAI : public CombatBotBaseAI
{
public:
    RandomBotAI();
    ~RandomBotAI() override;

    // Core overrides
    bool OnSessionLoaded(PlayerBotEntry* entry, WorldSession* sess) override;
    void OnPlayerLogin() override;
    void UpdateAI(uint32 const diff) override;

    // Combat AI - required by CombatBotBaseAI
    void UpdateInCombatAI() override;
    void UpdateOutOfCombatAI() override;

    // Class-specific combat routines
    void UpdateInCombatAI_Paladin() override;
    void UpdateOutOfCombatAI_Paladin() override;
    void UpdateInCombatAI_Shaman() override;
    void UpdateOutOfCombatAI_Shaman() override;
    void UpdateInCombatAI_Hunter() override;
    void UpdateOutOfCombatAI_Hunter() override;
    void UpdateInCombatAI_Mage() override;
    void UpdateOutOfCombatAI_Mage() override;
    void UpdateInCombatAI_Priest() override;
    void UpdateOutOfCombatAI_Priest() override;
    void UpdateInCombatAI_Warlock() override;
    void UpdateOutOfCombatAI_Warlock() override;
    void UpdateInCombatAI_Warrior() override;
    void UpdateOutOfCombatAI_Warrior() override;
    void UpdateInCombatAI_Rogue() override;
    void UpdateOutOfCombatAI_Rogue() override;
    void UpdateInCombatAI_Druid() override;
    void UpdateOutOfCombatAI_Druid() override;

private:
    ShortTimeTracker m_updateTimer;
    bool m_initialized = false;

    // Strategy for high-level behavior (grinding, resting, etc.)
    std::unique_ptr<IBotStrategy> m_strategy;
};

#endif // MANGOS_RANDOMBOTAI_H
