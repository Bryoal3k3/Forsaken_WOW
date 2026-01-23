/*
 * LootingBehavior.cpp
 *
 * Handles looting corpses after combat.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "LootingBehavior.h"
#include "Player.h"
#include "Creature.h"
#include "MotionMaster.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "LootMgr.h"
#include "WorldSession.h"
#include "Map.h"

// Custom checker to find dead creatures in range
class DeadCreaturesInRange
{
public:
    DeadCreaturesInRange(WorldObject const* pObject, float fMaxRange)
        : m_pObject(pObject), m_fRange(fMaxRange) {}

    bool operator() (Creature* pCreature)
    {
        return !pCreature->IsAlive() && m_pObject->IsWithinDist(pCreature, m_fRange, false);
    }

private:
    WorldObject const* m_pObject;
    float m_fRange;
};

// ============================================================================
// Constructor
// ============================================================================

LootingBehavior::LootingBehavior()
{
    m_timeoutTimer.Reset(0);
}

// ============================================================================
// Public Interface
// ============================================================================

bool LootingBehavior::Update(Player* pBot, uint32 diff)
{
    if (!m_isLooting)
        return false;

    // Check timeout
    m_timeoutTimer.Update(diff);
    if (m_timeoutTimer.Passed())
    {
        Reset();
        return false;
    }

    // If we don't have a target, find one
    if (m_lootTarget.IsEmpty())
    {
        Creature* pCorpse = FindLootableCorpse(pBot);
        if (!pCorpse)
        {
            // No more corpses to loot
            Reset();
            return false;
        }
        m_lootTarget = pCorpse->GetObjectGuid();
    }

    // Get our target corpse
    Creature* pCorpse = pBot->GetMap()->GetCreature(m_lootTarget);
    if (!pCorpse || pCorpse->IsAlive())
    {
        // Corpse gone or somehow alive, find another
        m_lootTarget.Clear();
        return true; // Still in looting mode, will try again next update
    }

    // Check if we're close enough to loot
    float dist = pBot->GetDistance(pCorpse);
    if (dist <= INTERACT_RANGE)
    {
        // We're close enough, loot it
        LootCorpse(pBot, pCorpse);
        m_lootTarget.Clear();

        // For now, just loot one corpse then done
        // Future: could loop to find more corpses
        Reset();
        return false;
    }
    else
    {
        // Move towards the corpse (with pathfinding for collision avoidance)
        pBot->GetMotionMaster()->MovePoint(0, pCorpse->GetPositionX(),
            pCorpse->GetPositionY(), pCorpse->GetPositionZ(), MOVE_PATHFINDING | MOVE_RUN_MODE);
        return true; // Still busy
    }
}

void LootingBehavior::OnCombatEnded(Player* pBot)
{
    if (!pBot || !pBot->IsAlive())
        return;

    // Start looting mode
    m_isLooting = true;
    m_lootTarget.Clear();
    m_timeoutTimer.Reset(LOOT_TIMEOUT_MS);
}

void LootingBehavior::Reset()
{
    m_isLooting = false;
    m_lootTarget.Clear();
    m_timeoutTimer.Reset(0);
}

// ============================================================================
// Private Helpers
// ============================================================================

Creature* LootingBehavior::FindLootableCorpse(Player* pBot)
{
    std::list<Creature*> creatures;

    DeadCreaturesInRange check(pBot, LOOT_RANGE);
    MaNGOS::CreatureListSearcher<DeadCreaturesInRange> searcher(creatures, check);
    Cell::VisitGridObjects(pBot, searcher, LOOT_RANGE);

    Creature* pClosest = nullptr;
    float closestDist = LOOT_RANGE + 1.0f;

    for (Creature* pCreature : creatures)
    {
        // Must be tapped by us
        if (!pCreature->IsTappedBy(pBot))
            continue;

        // Must have loot flag (indicates lootable)
        if (!pCreature->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE))
            continue;

        // Find closest
        float dist = pBot->GetDistance(pCreature);
        if (dist < closestDist)
        {
            closestDist = dist;
            pClosest = pCreature;
        }
    }

    return pClosest;
}

void LootingBehavior::LootCorpse(Player* pBot, Creature* pCorpse)
{
    if (!pBot || !pCorpse)
        return;

    // Open loot (this generates the loot table if not already done)
    pBot->SendLoot(pCorpse->GetObjectGuid(), LOOT_CORPSE);

    // Get the loot object
    Loot& loot = pCorpse->loot;

    // Take gold first
    if (loot.gold > 0)
    {
        pBot->ModifyMoney(loot.gold);
        loot.gold = 0;
    }

    // Take all items
    pBot->AutoStoreLoot(loot);

    // Release the loot (closes loot window, marks corpse as looted if empty)
    if (pBot->GetSession())
        pBot->GetSession()->DoLootRelease(pCorpse->GetObjectGuid());
}
