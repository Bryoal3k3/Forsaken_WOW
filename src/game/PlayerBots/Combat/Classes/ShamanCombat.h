/*
 * ShamanCombat.h
 *
 * Shaman-specific combat handler.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_SHAMANCOMBAT_H
#define MANGOS_SHAMANCOMBAT_H

#include "Combat/IClassCombat.h"

class CombatBotBaseAI;

class ShamanCombat : public IClassCombat
{
public:
    explicit ShamanCombat(CombatBotBaseAI* pAI);

    bool Engage(Player* pBot, Unit* pTarget) override;
    void UpdateCombat(Player* pBot, Unit* pVictim) override;
    void UpdateOutOfCombat(Player* pBot) override;
    char const* GetName() const override { return "Shaman"; }

private:
    CombatBotBaseAI* m_pAI;
};

#endif // MANGOS_SHAMANCOMBAT_H
