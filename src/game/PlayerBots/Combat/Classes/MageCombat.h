/*
 * MageCombat.h
 *
 * Mage-specific combat handler.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_MAGECOMBAT_H
#define MANGOS_MAGECOMBAT_H

#include "Combat/IClassCombat.h"

class CombatBotBaseAI;

class MageCombat : public IClassCombat
{
public:
    explicit MageCombat(CombatBotBaseAI* pAI);

    bool Engage(Player* pBot, Unit* pTarget) override;
    void UpdateCombat(Player* pBot, Unit* pVictim) override;
    void UpdateOutOfCombat(Player* pBot) override;
    char const* GetName() const override { return "Mage"; }

private:
    CombatBotBaseAI* m_pAI;
};

#endif // MANGOS_MAGECOMBAT_H
