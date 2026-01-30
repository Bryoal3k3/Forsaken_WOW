/*
 * GhostWalkingStrategy.cpp
 *
 * Strategy for handling bot death - ghost walking back to corpse.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "GhostWalkingStrategy.h"
#include "BotMovementManager.h"
#include "TravelingStrategy.h"
#include "RandomBotAI.h"
#include "Player.h"
#include "Corpse.h"
#include "UnitDefines.h"
#include "MotionMaster.h"
#include "Log.h"
#include <algorithm>

GhostWalkingStrategy::GhostWalkingStrategy()
    : m_initialized(false)
    , m_isWalkingToCorpse(false)
{
}

void GhostWalkingStrategy::OnDeath(Player* pBot)
{
    if (!pBot)
        return;

    RecordDeath();
    m_initialized = true;
    m_isWalkingToCorpse = false;

    // Check for death loop - if so, resurrect at spirit healer with sickness
    if (IsInDeathLoop())
    {
        // Make sure we're at graveyard as ghost first
        if (pBot->GetDeathState() == CORPSE)
        {
            pBot->BuildPlayerRepop();
            pBot->RepopAtGraveyard();
        }
        else if (pBot->GetDeathState() == DEAD)
        {
            pBot->RepopAtGraveyard();
        }

        // Resurrect with sickness at graveyard
        pBot->ResurrectPlayer(0.5f, true);  // true = apply resurrection sickness
        pBot->SpawnCorpseBones();
        m_recentDeaths.clear();  // Reset death counter after spirit healer res
        m_initialized = false;

        // Check for resurrection sickness (spell ID 15007)
        if (pBot->HasAura(15007))
        {
            sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
                "[GhostWalkingStrategy] %s has resurrection sickness, will wait before grinding",
                pBot->GetName());
            // Don't signal travel - let bot rest/wait
            // The res sickness will expire and normal grinding will resume
        }

        // Reset travel state so it can evaluate fresh after resurrection
        if (RandomBotAI* pAI = dynamic_cast<RandomBotAI*>(pBot->AI()))
        {
            if (TravelingStrategy* pTravel = pAI->GetTravelingStrategy())
                pTravel->ResetArrivalCooldown();
        }
        return;
    }

    // Normal death - release spirit and become ghost at graveyard
    // JUST_DIED (1) = transient state, will become CORPSE next tick
    // CORPSE (2) = dead but spirit still in body, need to release
    // DEAD (3) = already released spirit, is a ghost
    DeathState deathState = pBot->GetDeathState();

    sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[GhostWalking] %s OnDeath - deathState=%u",
        pBot->GetName(), static_cast<uint32>(deathState));

    if (deathState == JUST_DIED)
    {
        // Wait for next tick - state will transition to CORPSE
        m_initialized = false;  // Re-trigger OnDeath next tick
        return;
    }
    else if (deathState == CORPSE)
    {
        pBot->BuildPlayerRepop();
        pBot->RepopAtGraveyard();
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[GhostWalking] %s released spirit, now at graveyard",
            pBot->GetName());
    }
    else if (deathState == DEAD)
    {
        // Already a ghost - just go to graveyard if not there
        pBot->RepopAtGraveyard();
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[GhostWalking] %s already ghost, sent to graveyard",
            pBot->GetName());
    }
}

bool GhostWalkingStrategy::Update(Player* pBot, uint32 /*diff*/)
{
    if (!pBot || pBot->IsAlive())
        return false;

    // First update after death - initialize ghost state
    if (!m_initialized)
    {
        OnDeath(pBot);
        // If OnDeath resurrected us (death loop), we're done
        if (pBot->IsAlive())
            return false;
        // Wait for next tick to let ghost state settle
        return true;
    }

    // Get corpse location
    Corpse* corpse = pBot->GetCorpse();
    if (!corpse)
    {
        // No corpse found - just resurrect in place
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[GhostWalking] %s has no corpse, resurrecting in place",
            pBot->GetName());
        pBot->ResurrectPlayer(0.5f);
        m_initialized = false;
        m_isWalkingToCorpse = false;
        return false;
    }

    // Check distance to corpse
    float distToCorpse = pBot->GetDistance(corpse->GetPositionX(), corpse->GetPositionY(), corpse->GetPositionZ());

    if (distToCorpse <= CORPSE_RESURRECT_RANGE)
    {
        // Close enough - resurrect!
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[GhostWalking] %s reached corpse, resurrecting",
            pBot->GetName());

        pBot->ResurrectPlayer(0.5f);
        pBot->SpawnCorpseBones();
        m_initialized = false;
        m_isWalkingToCorpse = false;

        // Reset travel state so bot can evaluate if current location has mobs
        if (RandomBotAI* pAI = dynamic_cast<RandomBotAI*>(pBot->AI()))
        {
            if (TravelingStrategy* pTravel = pAI->GetTravelingStrategy())
                pTravel->ResetArrivalCooldown();
        }
        return false;
    }

    // Not close enough - move toward corpse
    // NOTE: Ghosts can walk through walls, so we DON'T use BotMovementManager
    // (it has path validation that may fail for ghost paths)
    // Just use direct MovePoint without pathfinding
    uint8 currentMoveType = pBot->GetMotionMaster()->GetCurrentMovementGeneratorType();

    if (!m_isWalkingToCorpse || currentMoveType != POINT_MOTION_TYPE)
    {
        // Direct movement for ghosts - no pathfinding needed (can walk through walls)
        pBot->GetMotionMaster()->MovePoint(0, corpse->GetPositionX(), corpse->GetPositionY(), corpse->GetPositionZ(), MOVE_RUN_MODE);
        m_isWalkingToCorpse = true;

        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[GhostWalking] %s moving to corpse at (%.1f, %.1f, %.1f), dist: %.1f",
            pBot->GetName(), corpse->GetPositionX(), corpse->GetPositionY(), corpse->GetPositionZ(), distToCorpse);
    }

    return true;  // Still working on it
}

bool GhostWalkingStrategy::IsComplete(Player* pBot) const
{
    return pBot && pBot->IsAlive();
}

void GhostWalkingStrategy::Reset()
{
    m_initialized = false;
    m_isWalkingToCorpse = false;
    // Note: Don't clear m_recentDeaths - we want to track deaths across resets
}

void GhostWalkingStrategy::RecordDeath()
{
    ClearOldDeaths();
    m_recentDeaths.push_back(time(nullptr));
}

void GhostWalkingStrategy::ClearOldDeaths()
{
    time_t now = time(nullptr);
    time_t cutoff = now - DEATH_LOOP_WINDOW;

    // Remove deaths older than the window
    m_recentDeaths.erase(
        std::remove_if(m_recentDeaths.begin(), m_recentDeaths.end(),
            [cutoff](time_t deathTime) { return deathTime < cutoff; }),
        m_recentDeaths.end()
    );
}

bool GhostWalkingStrategy::IsInDeathLoop() const
{
    return static_cast<int>(m_recentDeaths.size()) >= DEATH_LOOP_COUNT;
}
