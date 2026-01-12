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

class PaladinCombat : public IClassCombat
{
public:
    explicit PaladinCombat(CombatBotBaseAI* pAI);

    bool Engage(Player* pBot, Unit* pTarget) override;
    void UpdateCombat(Player* pBot, Unit* pVictim) override;
    void UpdateOutOfCombat(Player* pBot) override;
    char const* GetName() const override { return "Paladin"; }

private:
    CombatBotBaseAI* m_pAI;
};

#endif // MANGOS_PALADINCOMBAT_H
