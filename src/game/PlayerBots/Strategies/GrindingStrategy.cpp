/*
 * GrindingStrategy.cpp
 *
 * Grinding behavior: find mob -> attack -> kill -> loot -> rest -> repeat
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "GrindingStrategy.h"
#include "Combat/BotCombatMgr.h"
#include "Player.h"
#include "Creature.h"
#include "MotionMaster.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "PathFinder.h"

// Checker that finds the nearest valid grind target in a single pass.
// Used with CreatureLastSearcher - accepts creature only if it's closer than current best.
class NearestGrindTarget
{
public:
    NearestGrindTarget(Player* pBot, GrindingStrategy const* pStrategy, float maxRange)
        : m_pBot(pBot), m_pStrategy(pStrategy), m_bestDist(maxRange + 1.0f) {}

    bool operator()(Creature* pCreature)
    {
        if (!m_pStrategy->IsValidGrindTarget(m_pBot, pCreature))
            return false;

        float dist = m_pBot->GetDistance(pCreature);
        if (dist < m_bestDist)
        {
            m_bestDist = dist;
            return true;  // Accept - this is the new closest
        }
        return false;
    }

private:
    Player* m_pBot;
    GrindingStrategy const* m_pStrategy;
    float m_bestDist;
};

// ============================================================================
// Constructor
// ============================================================================

GrindingStrategy::GrindingStrategy()
{
}

// ============================================================================
// IBotStrategy Interface
// ============================================================================

GrindingResult GrindingStrategy::UpdateGrinding(Player* pBot, uint32 /*diff*/)
{
    if (!pBot || !pBot->IsAlive() || pBot->IsInCombat())
        return GrindingResult::BUSY;

    // Already have a victim? We're engaged, just waiting for combat to start
    // (e.g., caster has Attack() called but first spell hasn't landed yet)
    if (pBot->GetVictim())
    {
        m_noMobsCount = 0;  // Reset on successful engagement
        m_backoffLevel = 0; // Reset backoff
        m_skipTicks = 0;
        return GrindingResult::ENGAGED;
    }

    // Adaptive search: skip ticks based on backoff level
    if (m_skipTicks > 0)
    {
        m_skipTicks--;
        return GrindingResult::BUSY;  // Cooling down, don't search yet
    }

    // Tiered search: try close range first (faster), then full range
    Creature* pTarget = FindGrindTarget(pBot, SEARCH_RANGE_CLOSE);
    if (!pTarget)
        pTarget = FindGrindTarget(pBot, SEARCH_RANGE_FAR);

    if (pTarget)
    {
        // Reset backoff on finding a target
        m_noMobsCount = 0;
        m_backoffLevel = 0;
        m_skipTicks = 0;

        // Use combat manager for class-appropriate engagement
        if (m_pCombatMgr && m_pCombatMgr->Engage(pBot, pTarget))
            return GrindingResult::ENGAGED;

        // Fallback if combat manager not available
        if (pBot->Attack(pTarget, true))
        {
            pBot->GetMotionMaster()->MoveChase(pTarget);
            return GrindingResult::ENGAGED;
        }
    }

    // No mobs found - apply exponential backoff
    m_noMobsCount++;
    if (m_backoffLevel < BACKOFF_MAX_LEVEL)
        m_backoffLevel++;
    // Skip 2^level - 1 ticks: level 1=1, level 2=3, level 3=7
    m_skipTicks = (1u << m_backoffLevel) - 1;

    return GrindingResult::NO_TARGETS;
}

bool GrindingStrategy::Update(Player* pBot, uint32 diff)
{
    return UpdateGrinding(pBot, diff) == GrindingResult::ENGAGED;
}

void GrindingStrategy::OnEnterCombat(Player* /*pBot*/)
{
    // Nothing special for now
}

void GrindingStrategy::OnLeaveCombat(Player* /*pBot*/)
{
    // Reset backoff after combat - mobs may have respawned
    m_backoffLevel = 0;
    m_skipTicks = 0;
}

// ============================================================================
// Target Finding
// ============================================================================

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

    // Level check: bot level +/- LEVEL_RANGE
    int32 levelDiff = (int32)pCreature->GetLevel() - (int32)pBot->GetLevel();
    if (levelDiff < -LEVEL_RANGE || levelDiff > LEVEL_RANGE)
        return false;

    // Skip evading creatures
    if (pCreature->IsInEvadeMode())
        return false;

    // Skip mobs already tapped by others
    if (pCreature->HasLootRecipient() && !pCreature->IsTappedBy(pBot))
        return false;

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

    // Reachability check: verify mob's position is on valid navmesh
    // This prevents targeting mobs on steep slopes or unreachable terrain
    // PathFinder does a quick poly lookup first - if endPoly=0, returns NOPATH immediately
    PathFinder path(pBot);
    path.calculate(pCreature->GetPositionX(), pCreature->GetPositionY(), pCreature->GetPositionZ(), false);
    if (path.getPathType() & PATHFIND_NOPATH)
        return false;

    return true;
}

Creature* GrindingStrategy::FindGrindTarget(Player* pBot, float range)
{
    // Single-pass search: finds nearest valid target directly, no list allocation
    Creature* pBestTarget = nullptr;
    NearestGrindTarget check(pBot, this, range);
    MaNGOS::CreatureLastSearcher<NearestGrindTarget> searcher(pBestTarget, check);
    Cell::VisitGridObjects(pBot, searcher, range);
    return pBestTarget;
}
