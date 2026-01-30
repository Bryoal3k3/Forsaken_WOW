/*
 * GrindingStrategy.cpp
 *
 * Grinding behavior: scan mobs -> pick random -> approach -> kill -> repeat
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "GrindingStrategy.h"
#include "BotMovementManager.h"
#include "Combat/BotCombatMgr.h"
#include "Player.h"
#include "Creature.h"
#include "Map.h"
#include "World.h"
#include "MotionMaster.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "PathFinder.h"
#include "Log.h"

#include <algorithm>
#include <random>

// ============================================================================
// Constructor
// ============================================================================

GrindingStrategy::GrindingStrategy()
{
}

// ============================================================================
// IBotStrategy Interface
// ============================================================================

bool GrindingStrategy::Update(Player* pBot, uint32 diff)
{
    return UpdateGrinding(pBot, diff) == GrindingResult::ENGAGED;
}

void GrindingStrategy::OnEnterCombat(Player* pBot)
{
    // Transition to IN_COMBAT state
    if (m_state == GrindState::APPROACHING)
    {
        m_state = GrindState::IN_COMBAT;
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[Grinding] %s entered combat, state -> IN_COMBAT",
            pBot->GetName());
    }
}

void GrindingStrategy::OnLeaveCombat(Player* pBot)
{
    // Reset to IDLE when combat ends
    ClearTarget(pBot);
    m_backoffLevel = 0;
    m_skipTicks = 0;
    sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[Grinding] %s left combat, state -> IDLE",
        pBot->GetName());
}

// ============================================================================
// Main Update - State Machine Dispatcher
// ============================================================================

GrindingResult GrindingStrategy::UpdateGrinding(Player* pBot, uint32 /*diff*/)
{
    if (!pBot || !pBot->IsAlive())
        return GrindingResult::BUSY;

    // If bot is in actual combat (game state), let combat system handle it
    if (pBot->IsInCombat())
    {
        m_state = GrindState::IN_COMBAT;
        return GrindingResult::ENGAGED;
    }

    // Dispatch based on current state
    switch (m_state)
    {
        case GrindState::IDLE:
            return HandleIdle(pBot);

        case GrindState::APPROACHING:
            return HandleApproaching(pBot);

        case GrindState::IN_COMBAT:
            return HandleInCombat(pBot);
    }

    return GrindingResult::BUSY;
}

// ============================================================================
// State Handlers
// ============================================================================

GrindingResult GrindingStrategy::HandleIdle(Player* pBot)
{
    // Adaptive search: skip ticks based on backoff level
    if (m_skipTicks > 0)
    {
        m_skipTicks--;
        return GrindingResult::BUSY;
    }

    // Scan for all valid targets in range
    std::vector<Creature*> candidates = ScanForTargets(pBot, SEARCH_RANGE);

    if (candidates.empty())
    {
        // No mobs found - apply exponential backoff
        m_noMobsCount++;
        if (m_backoffLevel < BACKOFF_MAX_LEVEL)
            m_backoffLevel++;
        m_skipTicks = (1u << m_backoffLevel) - 1;

        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[Grinding] %s found no targets, backoff level %u",
            pBot->GetName(), m_backoffLevel);
        return GrindingResult::NO_TARGETS;
    }

    // Pick a random target (validates path internally)
    Creature* pTarget = SelectRandomTarget(pBot, candidates);

    if (!pTarget)
    {
        // Had candidates but none had valid paths
        m_noMobsCount++;
        if (m_backoffLevel < BACKOFF_MAX_LEVEL)
            m_backoffLevel++;
        m_skipTicks = (1u << m_backoffLevel) - 1;

        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[Grinding] %s found %zu mobs but none reachable",
            pBot->GetName(), candidates.size());
        return GrindingResult::NO_TARGETS;
    }

    // Reset backoff on finding a valid target
    m_noMobsCount = 0;
    m_backoffLevel = 0;
    m_skipTicks = 0;

    // Store target and engage
    m_currentTarget = pTarget->GetObjectGuid();
    m_approachStartTime = WorldTimer::getMSTime();
    m_state = GrindState::APPROACHING;

    // Use combat manager for class-appropriate engagement
    if (m_pCombatMgr)
        m_pCombatMgr->Engage(pBot, pTarget);
    else
        pBot->Attack(pTarget, true);

    sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[Grinding] %s selected %s (dist: %.1f), state -> APPROACHING",
        pBot->GetName(), pTarget->GetName(), pBot->GetDistance(pTarget));

    return GrindingResult::ENGAGED;
}

GrindingResult GrindingStrategy::HandleApproaching(Player* pBot)
{
    // Get our tracked target
    Creature* pTarget = GetCurrentTargetCreature(pBot);

    // Target gone or dead?
    if (!pTarget || !pTarget->IsAlive())
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[Grinding] %s target lost or dead, clearing",
            pBot->GetName());
        ClearTarget(pBot);
        return GrindingResult::BUSY;  // Will search next tick
    }

    // Target evading?
    if (pTarget->IsInEvadeMode())
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[Grinding] %s target evading, clearing",
            pBot->GetName());
        ClearTarget(pBot);
        return GrindingResult::BUSY;
    }

    // Check approach timeout
    uint32 elapsed = WorldTimer::getMSTime() - m_approachStartTime;
    if (elapsed > APPROACH_TIMEOUT_MS)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[Grinding] %s approach timeout (%.1fs) for %s at dist %.1f, giving up",
            pBot->GetName(), elapsed / 1000.0f, pTarget->GetName(), pBot->GetDistance(pTarget));
        ClearTarget(pBot);
        return GrindingResult::BUSY;
    }

    // Are we in combat now? (target started fighting back)
    if (pBot->IsInCombat())
    {
        m_state = GrindState::IN_COMBAT;
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[Grinding] %s now in combat, state -> IN_COMBAT",
            pBot->GetName());
        return GrindingResult::ENGAGED;
    }

    // Still approaching - combat manager handles the movement
    return GrindingResult::ENGAGED;
}

GrindingResult GrindingStrategy::HandleInCombat(Player* pBot)
{
    // If we're no longer in combat, transition to IDLE
    if (!pBot->IsInCombat() && !pBot->GetVictim())
    {
        ClearTarget(pBot);
        return GrindingResult::BUSY;
    }

    // Combat system handles the actual fighting
    return GrindingResult::ENGAGED;
}

// ============================================================================
// Target Finding
// ============================================================================

// Checker that collects ALL valid grind targets (not just nearest)
class AllGrindTargets
{
public:
    AllGrindTargets(Player* pBot, GrindingStrategy const* pStrategy, std::vector<Creature*>& targets)
        : m_pBot(pBot), m_pStrategy(pStrategy), m_targets(targets) {}

    bool operator()(Creature* pCreature)
    {
        if (m_pStrategy->IsValidGrindTarget(m_pBot, pCreature))
            m_targets.push_back(pCreature);
        return false;  // Keep searching (don't stop at first)
    }

private:
    Player* m_pBot;
    GrindingStrategy const* m_pStrategy;
    std::vector<Creature*>& m_targets;
};

std::vector<Creature*> GrindingStrategy::ScanForTargets(Player* pBot, float range)
{
    std::vector<Creature*> targets;
    targets.reserve(20);  // Pre-allocate for typical case

    AllGrindTargets checker(pBot, this, targets);
    MaNGOS::CreatureWorker<AllGrindTargets> worker(pBot, checker);
    Cell::VisitGridObjects(pBot, worker, range);

    return targets;
}

bool GrindingStrategy::IsValidGrindTarget(Player* pBot, Creature* pCreature) const
{
    if (!pCreature || !pCreature->IsAlive())
        return false;

    // Must be a creature, not a totem
    if (pCreature->IsTotem())
        return false;

    // Skip critters (rabbits, squirrels, etc.)
    if (pCreature->GetCreatureInfo()->type == CREATURE_TYPE_CRITTER)
        return false;

    // Skip elite mobs
    if (pCreature->IsElite())
        return false;

    // Level check: same level or up to LEVEL_RANGE below (no higher level mobs)
    int32 levelDiff = (int32)pCreature->GetLevel() - (int32)pBot->GetLevel();
    if (levelDiff < -LEVEL_RANGE || levelDiff > 0)
        return false;

    // Skip evading creatures
    if (pCreature->IsInEvadeMode())
        return false;

    // Skip mobs already tapped by others
    if (pCreature->HasLootRecipient() && !pCreature->IsTappedBy(pBot))
        return false;

    // Skip mobs already in combat (being fought by someone else)
    if (pCreature->IsInCombat() && !pCreature->GetVictim())
        return false;  // In combat but no victim = weird state, skip
    if (pCreature->IsInCombat() && pCreature->GetVictim() != pBot)
        return false;  // Fighting someone else

    // Must be visible to us
    if (!pCreature->IsVisibleForOrDetect(pBot, pBot, false))
        return false;

    // Check reaction: accept hostile (red) AND neutral (yellow)
    ReputationRank reaction = pBot->GetReactionTo(pCreature);
    if (reaction > REP_NEUTRAL)  // REP_FRIENDLY or higher = can't attack
        return false;

    // Additional safety: make sure it's not friendly faction
    if (pBot->IsFriendlyTo(pCreature))
        return false;

    return true;
}

bool GrindingStrategy::HasValidPathTo(Player* pBot, Creature* pCreature) const
{
    PathFinder path(pBot);
    path.calculate(pCreature->GetPositionX(), pCreature->GetPositionY(), pCreature->GetPositionZ(), false);

    PathType type = path.getPathType();

    // Reject NOPATH and NOT_USING_PATH (direct line, ignores terrain)
    if ((type & PATHFIND_NOPATH) || (type & PATHFIND_NOT_USING_PATH))
        return false;

    return true;
}

Creature* GrindingStrategy::SelectRandomTarget(Player* pBot, std::vector<Creature*>& candidates)
{
    if (candidates.empty())
        return nullptr;

    // Shuffle candidates for random selection
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(candidates.begin(), candidates.end(), gen);

    // Try each candidate until we find one with a valid path
    for (Creature* pCreature : candidates)
    {
        if (HasValidPathTo(pBot, pCreature))
            return pCreature;
    }

    return nullptr;  // None had valid paths
}

// ============================================================================
// Helpers
// ============================================================================

void GrindingStrategy::ClearTarget(Player* pBot)
{
    // Stop attacking if we have a victim
    if (pBot->GetVictim())
        pBot->AttackStop();

    m_currentTarget.Clear();
    m_state = GrindState::IDLE;
    m_approachStartTime = 0;
}

Creature* GrindingStrategy::GetCurrentTargetCreature(Player* pBot) const
{
    if (m_currentTarget.IsEmpty())
        return nullptr;

    // Look up creature in the world
    return pBot->GetMap()->GetCreature(m_currentTarget);
}
