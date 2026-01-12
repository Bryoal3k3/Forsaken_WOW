/*
 * PriestCombat.h
 *
 * Priest-specific combat handler.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_PRIESTCOMBAT_H
#define MANGOS_PRIESTCOMBAT_H

#include "Combat/IClassCombat.h"

class CombatBotBaseAI;

class PriestCombat : public IClassCombat
{
public:
    explicit PriestCombat(CombatBotBaseAI* pAI);

    bool Engage(Player* pBot, Unit* pTarget) override;
    void UpdateCombat(Player* pBot, Unit* pVictim) override;
    void UpdateOutOfCombat(Player* pBot) override;
    char const* GetName() const override { return "Priest"; }

private:
    CombatBotBaseAI* m_pAI;
};

#endif // MANGOS_PRIESTCOMBAT_H
