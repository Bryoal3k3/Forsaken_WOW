/*
 * PaladinCombat.h
 *
 * Paladin-specific combat handler.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_PALADINCOMBAT_H
#define MANGOS_PALADINCOMBAT_H

#include "Combat/IClassCombat.h"

class CombatBotBaseAI;
class BotMovementManager;

class PaladinCombat : public IClassCombat
{
public:
    explicit PaladinCombat(CombatBotBaseAI* pAI);

    void SetMovementManager(BotMovementManager* pMoveMgr) override { m_pMoveMgr = pMoveMgr; }
    bool Engage(Player* pBot, Unit* pTarget) override;
    void UpdateCombat(Player* pBot, Unit* pVictim) override;
    void UpdateOutOfCombat(Player* pBot) override;
    char const* GetName() const override { return "Paladin"; }

private:
    CombatBotBaseAI* m_pAI;
    BotMovementManager* m_pMoveMgr = nullptr;
};

#endif // MANGOS_PALADINCOMBAT_H
