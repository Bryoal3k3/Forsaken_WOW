/*
 * BotCombatMgr.h
 *
 * Combat coordinator - manages class-specific combat handlers.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_BOTCOMBATMGR_H
#define MANGOS_BOTCOMBATMGR_H

#include "Common.h"
#include <memory>

class Player;
class Unit;
class IClassCombat;
class CombatBotBaseAI;
class BotMovementManager;

class BotCombatMgr
{
public:
    BotCombatMgr();
    ~BotCombatMgr();

    // Initialize for a specific class - creates appropriate handler
    // Must be called after PopulateSpellData()
    void Initialize(Player* pBot, CombatBotBaseAI* pAI);

    // Set movement manager (called by RandomBotAI after construction)
    void SetMovementManager(BotMovementManager* pMoveMgr);

    // Engage a target (how to pull)
    bool Engage(Player* pBot, Unit* pTarget);

    // Combat rotation
    void UpdateCombat(Player* pBot, Unit* pVictim);

    // Out of combat (buffs, etc.)
    void UpdateOutOfCombat(Player* pBot);

    // Check if initialized
    bool IsInitialized() const { return m_handler != nullptr; }

private:
    std::unique_ptr<IClassCombat> m_handler;
    BotMovementManager* m_pMovementMgr = nullptr;
};

#endif // MANGOS_BOTCOMBATMGR_H
