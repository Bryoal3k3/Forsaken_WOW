/*
 * BotCombatMgr.cpp
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "BotCombatMgr.h"
#include "IClassCombat.h"
#include "Classes/WarriorCombat.h"
#include "Classes/PaladinCombat.h"
#include "Classes/HunterCombat.h"
#include "Classes/MageCombat.h"
#include "Classes/PriestCombat.h"
#include "Classes/WarlockCombat.h"
#include "Classes/RogueCombat.h"
#include "Classes/ShamanCombat.h"
#include "Classes/DruidCombat.h"
#include "Player.h"
#include "CombatBotBaseAI.h"

BotCombatMgr::BotCombatMgr() = default;
BotCombatMgr::~BotCombatMgr() = default;

void BotCombatMgr::SetMovementManager(BotMovementManager* pMoveMgr)
{
    m_pMovementMgr = pMoveMgr;
    if (m_handler)
        m_handler->SetMovementManager(pMoveMgr);
}

void BotCombatMgr::Initialize(Player* pBot, CombatBotBaseAI* pAI)
{
    if (!pBot || !pAI)
        return;

    switch (pBot->GetClass())
    {
        case CLASS_WARRIOR:
            m_handler = std::make_unique<WarriorCombat>(pAI);
            break;
        case CLASS_PALADIN:
            m_handler = std::make_unique<PaladinCombat>(pAI);
            break;
        case CLASS_HUNTER:
            m_handler = std::make_unique<HunterCombat>(pAI);
            break;
        case CLASS_MAGE:
            m_handler = std::make_unique<MageCombat>(pAI);
            break;
        case CLASS_PRIEST:
            m_handler = std::make_unique<PriestCombat>(pAI);
            break;
        case CLASS_WARLOCK:
            m_handler = std::make_unique<WarlockCombat>(pAI);
            break;
        case CLASS_ROGUE:
            m_handler = std::make_unique<RogueCombat>(pAI);
            break;
        case CLASS_SHAMAN:
            m_handler = std::make_unique<ShamanCombat>(pAI);
            break;
        case CLASS_DRUID:
            m_handler = std::make_unique<DruidCombat>(pAI);
            break;
    }
}

bool BotCombatMgr::Engage(Player* pBot, Unit* pTarget)
{
    if (m_handler)
        return m_handler->Engage(pBot, pTarget);
    return false;
}

void BotCombatMgr::UpdateCombat(Player* pBot, Unit* pVictim)
{
    // Ensure bot is facing target when not moving (fixes stuck combat)
    // Pattern from BattleBotAI - check 120 degree arc (2*PI/3)
    if (pVictim && !pBot->HasInArc(pVictim, 2 * M_PI_F / 3) && !pBot->IsMoving())
    {
        pBot->SetInFront(pVictim);
        pBot->SendMovementPacket(MSG_MOVE_SET_FACING, false);
    }

    if (m_handler)
        m_handler->UpdateCombat(pBot, pVictim);
}

void BotCombatMgr::UpdateOutOfCombat(Player* pBot)
{
    if (m_handler)
        m_handler->UpdateOutOfCombat(pBot);
}
