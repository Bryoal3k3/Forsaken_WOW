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

    Loot& loot = pObject->loot;

    // Diagnostic: log GO loot state before we start
    uint8 goLootState = static_cast<uint8>(pObject->getLootState());
    uint32 lootId = pObject->GetGOInfo()->GetLootId();

    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL,
        "[BotObjectInteraction] %s opening %s (entry %u) lootState=%u lootId=%u items=%zu unlootedCount=%u",
        pBot->GetName(), pObject->GetGOInfo()->name.c_str(), pObject->GetEntry(),
        goLootState, lootId, loot.items.size(), (uint32)loot.unlootedCount);

    // Open loot — use LOOT_CORPSE to match real client flow (HandleLootOpcode)
    pBot->SendLoot(pObject->GetObjectGuid(), LOOT_CORPSE);

    // Diagnostic: log what SendLoot generated
    uint32 questItemCount = loot.GetMaxSlotInLootFor(pBot->GetGUIDLow()) - loot.items.size();

    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL,
        "[BotObjectInteraction] %s post-SendLoot: items=%zu questItems=%u unlootedCount=%u gold=%u",
        pBot->GetName(), loot.items.size(), questItemCount, (uint32)loot.unlootedCount, loot.gold);

    // Take gold first
    if (loot.gold > 0)
    {
        pBot->ModifyMoney(loot.gold);
        loot.gold = 0;
    }

    // Take all items
    pBot->AutoStoreLoot(loot);

    // Mark all regular items as looted so the GO properly despawns.
    // AutoStoreLoot takes items into inventory but doesn't set is_looted on the
    // Loot container, so isLooted() stays false and the GO never transitions to
    // GO_JUST_DEACTIVATED — causing an infinite re-loot loop.
    for (auto& item : loot.items)
    {
        if (!item.is_looted)
            item.is_looted = true;
    }
    loot.unlootedCount = 0;

    // Diagnostic: log final state
    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL,
        "[BotObjectInteraction] %s post-loot: unlootedCount=%u isLooted=%s",
        pBot->GetName(), (uint32)loot.unlootedCount,
        (loot.gold == 0 && loot.unlootedCount == 0) ? "yes" : "no");

    // Release loot (closes loot window, despawns chest if fully looted)
    if (pBot->GetSession())
        pBot->GetSession()->DoLootRelease(pObject->GetObjectGuid());

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
        "[BotObjectInteraction] %s looted %s (entry %u)",
        pBot->GetName(), pObject->GetGOInfo()->name.c_str(), pObject->GetEntry());

    return true;
}
