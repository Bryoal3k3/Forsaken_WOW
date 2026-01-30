/*
 * RogueCombat.h
 *
 * Rogue-specific combat handler.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_ROGUECOMBAT_H
#define MANGOS_ROGUECOMBAT_H

#include "Combat/IClassCombat.h"

class CombatBotBaseAI;
class BotMovementManager;

class RogueCombat : public IClassCombat
{
public:
    explicit RogueCombat(CombatBotBaseAI* pAI);

    void SetMovementManager(BotMovementManager* pMoveMgr) override { m_pMoveMgr = pMoveMgr; }
    bool Engage(Player* pBot, Unit* pTarget) override;
    void UpdateCombat(Player* pBot, Unit* pVictim) override;
    void UpdateOutOfCombat(Player* pBot) override;
    char const* GetName() const override { return "Rogue"; }

private:
    CombatBotBaseAI* m_pAI;
    BotMovementManager* m_pMoveMgr = nullptr;
};

#endif // MANGOS_ROGUECOMBAT_H
