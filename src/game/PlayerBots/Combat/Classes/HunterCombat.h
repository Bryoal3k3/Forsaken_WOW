/*
 * HunterCombat.h
 *
 * Hunter-specific combat handler.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_HUNTERCOMBAT_H
#define MANGOS_HUNTERCOMBAT_H

#include "Combat/IClassCombat.h"

class CombatBotBaseAI;

class HunterCombat : public IClassCombat
{
public:
    explicit HunterCombat(CombatBotBaseAI* pAI);

    bool Engage(Player* pBot, Unit* pTarget) override;
    void UpdateCombat(Player* pBot, Unit* pVictim) override;
    void UpdateOutOfCombat(Player* pBot) override;
    char const* GetName() const override { return "Hunter"; }

private:
    CombatBotBaseAI* m_pAI;

    static constexpr uint32 SPELL_AUTO_SHOT = 75;
};

#endif // MANGOS_HUNTERCOMBAT_H
