/*
 * DruidCombat.h
 *
 * Druid-specific combat handler.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_DRUIDCOMBAT_H
#define MANGOS_DRUIDCOMBAT_H

#include "Combat/IClassCombat.h"

class CombatBotBaseAI;
class BotMovementManager;

class DruidCombat : public IClassCombat
{
public:
    explicit DruidCombat(CombatBotBaseAI* pAI);

    void SetMovementManager(BotMovementManager* pMoveMgr) override { m_pMoveMgr = pMoveMgr; }
    bool Engage(Player* pBot, Unit* pTarget) override;
    void UpdateCombat(Player* pBot, Unit* pVictim) override;
    void UpdateOutOfCombat(Player* pBot) override;
    char const* GetName() const override { return "Druid"; }

private:
    CombatBotBaseAI* m_pAI;
    BotMovementManager* m_pMoveMgr = nullptr;
};

#endif // MANGOS_DRUIDCOMBAT_H
