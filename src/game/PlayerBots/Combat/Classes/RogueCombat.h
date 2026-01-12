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

class RogueCombat : public IClassCombat
{
public:
    explicit RogueCombat(CombatBotBaseAI* pAI);

    bool Engage(Player* pBot, Unit* pTarget) override;
    void UpdateCombat(Player* pBot, Unit* pVictim) override;
    void UpdateOutOfCombat(Player* pBot) override;
    char const* GetName() const override { return "Rogue"; }

private:
    CombatBotBaseAI* m_pAI;
};

#endif // MANGOS_ROGUECOMBAT_H
