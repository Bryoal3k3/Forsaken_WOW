/*
 * WarlockCombat.h
 *
 * Warlock-specific combat handler.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_WARLOCKCOMBAT_H
#define MANGOS_WARLOCKCOMBAT_H

#include "Combat/IClassCombat.h"

class CombatBotBaseAI;
class BotMovementManager;

class WarlockCombat : public IClassCombat
{
public:
    explicit WarlockCombat(CombatBotBaseAI* pAI);

    void SetMovementManager(BotMovementManager* pMoveMgr) override { m_pMoveMgr = pMoveMgr; }
    bool Engage(Player* pBot, Unit* pTarget) override;
    void UpdateCombat(Player* pBot, Unit* pVictim) override;
    void UpdateOutOfCombat(Player* pBot) override;
    char const* GetName() const override { return "Warlock"; }

private:
    CombatBotBaseAI* m_pAI;
    BotMovementManager* m_pMoveMgr = nullptr;
};

#endif // MANGOS_WARLOCKCOMBAT_H
