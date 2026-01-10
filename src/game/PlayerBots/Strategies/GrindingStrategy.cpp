/*
 * GrindingStrategy.cpp
 *
 * Grinding behavior: find mob -> attack -> kill -> loot -> rest -> repeat
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "GrindingStrategy.h"
#include "Player.h"
#include "Creature.h"
#include "MotionMaster.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"

// Custom checker to find ALL creatures in range (ignores faction)
class AllCreaturesInRange
{
public:
    AllCreaturesInRange(WorldObject const* pObject, float fMaxRange)
        : m_pObject(pObject), m_fRange(fMaxRange) {}

    bool operator() (Creature* pCreature)
    {
        return pCreature->IsAlive() && m_pObject->IsWithinDist(pCreature, m_fRange, false);
    }

private:
    WorldObject const* m_pObject;
    float m_fRange;
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

bool GrindingStrategy::Update(Player* pBot, uint32 /*diff*/)
{
    if (!pBot || !pBot->IsAlive() || pBot->IsInCombat())
        return false;

    // Find a mob to attack
    Creature* pTarget = FindGrindTarget(pBot, SEARCH_RANGE);
    if (pTarget)
    {
        // Attack the mob
        if (pBot->Attack(pTarget, true))
        {
            pBot->GetMotionMaster()->MoveChase(pTarget);
            return true;
        }
    }

    return false;
}

void GrindingStrategy::OnEnterCombat(Player* /*pBot*/)
{
    // Nothing special for now
}

void GrindingStrategy::OnLeaveCombat(Player* /*pBot*/)
{
    // Future: trigger looting behavior here
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

    return true;
}

Creature* GrindingStrategy::FindGrindTarget(Player* pBot, float range)
{
    // Use direct grid search to find ALL creatures in range (including neutral)
    std::list<Creature*> creatures;

    AllCreaturesInRange check(pBot, range);
    MaNGOS::CreatureListSearcher<AllCreaturesInRange> searcher(creatures, check);
    Cell::VisitGridObjects(pBot, searcher, range);

    Creature* pBestTarget = nullptr;
    float bestDistance = range + 1.0f;

    for (Creature* pCreature : creatures)
    {
        if (!IsValidGrindTarget(pBot, pCreature))
            continue;

        // Pick the closest valid target
        float dist = pBot->GetDistance(pCreature);
        if (dist < bestDistance)
        {
            bestDistance = dist;
            pBestTarget = pCreature;
        }
    }

    return pBestTarget;
}
