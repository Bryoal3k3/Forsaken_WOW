/*
 * GrindingActivity.cpp
 *
 * Tier 1 Activity: autonomous mob grinding with travel support.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "GrindingActivity.h"
#include "PlayerBots/Strategies/GrindingStrategy.h"
#include "PlayerBots/Strategies/TravelingStrategy.h"
#include "PlayerBots/Strategies/VendoringStrategy.h"
#include "Player.h"

// ============================================================================
// Constructor / Destructor
// ============================================================================

GrindingActivity::GrindingActivity()
    : m_grinding(std::make_unique<GrindingStrategy>())
    , m_traveling(std::make_unique<TravelingStrategy>())
{
}

GrindingActivity::~GrindingActivity() = default;

// ============================================================================
// Wiring
// ============================================================================

void GrindingActivity::SetCombatMgr(BotCombatMgr* pMgr)
{
    if (m_grinding)
        m_grinding->SetCombatMgr(pMgr);
}

void GrindingActivity::SetMovementManager(BotMovementManager* pMgr)
{
    if (m_grinding)
        m_grinding->SetMovementManager(pMgr);
    if (m_traveling)
        m_traveling->SetMovementManager(pMgr);
}

void GrindingActivity::SetVendoringStrategy(VendoringStrategy* pVendoring)
{
    if (m_traveling)
        m_traveling->SetVendoringStrategy(pVendoring);
}

// ============================================================================
// IBotActivity Interface
// ============================================================================

bool GrindingActivity::Update(Player* pBot, uint32 diff)
{
    if (!m_grinding)
        return false;

    GrindingResult grindResult = m_grinding->UpdateGrinding(pBot, 0);

    if (grindResult == GrindingResult::ENGAGED)
    {
        // Found and attacking a target — reset travel state
        if (m_traveling)
            m_traveling->ResetArrivalCooldown();
        return true;
    }

    // Check if we should travel (grinding found no targets)
    if (grindResult == GrindingResult::NO_TARGETS)
    {
        if (m_grinding->GetNoMobsCount() >= TravelConstants::NO_MOBS_THRESHOLD)
        {
            if (m_traveling)
            {
                m_traveling->SignalNoMobs();
                if (m_traveling->Update(pBot, diff))
                    return true;  // Busy traveling
            }
        }
    }

    return false;
}

void GrindingActivity::OnEnterCombat(Player* pBot)
{
    if (m_grinding)
        m_grinding->OnEnterCombat(pBot);
}

void GrindingActivity::OnLeaveCombat(Player* pBot)
{
    if (m_grinding)
        m_grinding->OnLeaveCombat(pBot);
}
