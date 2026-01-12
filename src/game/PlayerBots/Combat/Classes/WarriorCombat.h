/*
 * WarriorCombat.h
 *
 * Warrior-specific combat handler.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_WARRIORCOMBAT_H
#define MANGOS_WARRIORCOMBAT_H

#include "Combat/IClassCombat.h"

class CombatBotBaseAI;

class WarriorCombat : public IClassCombat
{
public:
    explicit WarriorCombat(CombatBotBaseAI* pAI);

    bool Engage(Player* pBot, Unit* pTarget) override;
    void UpdateCombat(Player* pBot, Unit* pVictim) override;
    void UpdateOutOfCombat(Player* pBot) override;
    char const* GetName() const override { return "Warrior"; }

private:
    CombatBotBaseAI* m_pAI;
};

#endif // MANGOS_WARRIORCOMBAT_H
