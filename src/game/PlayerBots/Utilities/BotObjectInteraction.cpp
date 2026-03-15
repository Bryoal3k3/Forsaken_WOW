/*
 * BotObjectInteraction.cpp
 *
 * Shared utility for bot interaction with gameobjects.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "BotObjectInteraction.h"
#include "Player.h"
#include "GameObject.h"
#include "ObjectMgr.h"
#include "Log.h"
#include "ObjectDefines.h"
#include "LootMgr.h"
#include "WorldSession.h"

// ============================================================================
// Find Nearby Objects
// ============================================================================

GameObject* BotObjectInteraction::FindNearbyObject(Player* pBot, uint32 entry, float range)
{
    if (!pBot)
        return nullptr;

    return pBot->FindNearestGameObject(entry, range);
}

GameObject* BotObjectInteraction::FindNearbyObjectByType(Player* pBot, uint32 /*type*/, float /*range*/)
{
    // TODO: Implement when needed (gathering, etc.)
    // FindNearestGameObject only searches by entry, not by type.
    // Will need a custom grid visitor when this is required.
    return nullptr;
}

// ============================================================================
// Interaction
// ============================================================================

bool BotObjectInteraction::CanInteractWith(Player* pBot, GameObject* pObject)
{
    if (!pBot || !pObject)
        return false;

    if (!pBot->IsAlive())
        return false;

    // Check distance
    if (!pBot->IsWithinDistInMap(pObject, INTERACTION_DISTANCE))
        return false;

    // Check if object is spawned/usable
    if (!pObject->isSpawned())
        return false;

    return true;
}

bool BotObjectInteraction::InteractWith(Player* pBot, GameObject* pObject)
{
    if (!CanInteractWith(pBot, pObject))
        return false;

    pObject->Use(pBot);

    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL,
        "[BotObjectInteraction] %s interacted with %s (entry %u, type %u)",
        pBot->GetName(), pObject->GetGOInfo()->name.c_str(),
        pObject->GetEntry(), pObject->GetGOInfo()->type);

    return true;
}

bool BotObjectInteraction::LootObject(Player* pBot, GameObject* pObject)
{
    if (!CanInteractWith(pBot, pObject))
        return false;

    // Open loot explicitly (same pattern as LootingBehavior for creatures)
    pBot->SendLoot(pObject->GetObjectGuid(), LOOT_SKINNING);

    // Take all items from the gameobject's loot
    Loot& loot = pObject->loot;

    // Take gold first
    if (loot.gold > 0)
    {
        pBot->ModifyMoney(loot.gold);
        loot.gold = 0;
    }

    // Take all items
    pBot->AutoStoreLoot(loot);

    // Release loot (closes loot window, marks object as looted)
    if (pBot->GetSession())
        pBot->GetSession()->DoLootRelease(pObject->GetObjectGuid());

    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL,
        "[BotObjectInteraction] %s looted %s (entry %u)",
        pBot->GetName(), pObject->GetGOInfo()->name.c_str(), pObject->GetEntry());

    return true;
}
