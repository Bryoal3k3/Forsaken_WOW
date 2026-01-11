/*
 * VendoringStrategy.cpp
 *
 * Strategy for handling bot vendoring - selling items and repairing gear.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "VendoringStrategy.h"
#include "Player.h"
#include "Creature.h"
#include "ObjectMgr.h"
#include "Bag.h"
#include "Item.h"
#include "Log.h"
#include "MotionMaster.h"
#include "UnitDefines.h"
#include "DBCStructure.h"
#include "SharedDefines.h"
#include "Map.h"
#include "ProgressBar.h"
#include <cmath>
#include <cfloat>

// Static member initialization
std::vector<VendorLocation> VendoringStrategy::s_vendorCache;
bool VendoringStrategy::s_cacheBuilt = false;
std::mutex VendoringStrategy::s_cacheMutex;

VendoringStrategy::VendoringStrategy()
    : m_state(VendorState::IDLE)
    , m_targetVendor{}
    , m_startX(0.0f)
    , m_startY(0.0f)
    , m_startZ(0.0f)
    , m_stuckTimer(0)
    , m_lastDistanceCheckTime(0)
    , m_lastDistanceToVendor(FLT_MAX)
{
}

void VendoringStrategy::BuildVendorCache()
{
    std::lock_guard<std::mutex> lock(s_cacheMutex);

    if (s_cacheBuilt)
        return;

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[VendoringStrategy] Building vendor cache...");

    // First pass: count all creature spawns for progress bar
    uint32 totalCreatures = 0;
    auto counter = [&totalCreatures](CreatureDataPair const& /*pair*/) {
        ++totalCreatures;
        return false; // continue iteration
    };
    sObjectMgr.DoCreatureData(counter);

    // Create progress bar
    BarGoLink bar(totalCreatures);

    uint32 vendorCount = 0;
    uint32 repairCount = 0;

    // Second pass: find vendors with progress
    auto vendorFinder = [&](CreatureDataPair const& pair) {
        bar.step();

        CreatureData const& data = pair.second;
        uint32 guid = pair.first;

        // Get creature template info
        CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(data.creature_id[0]);
        if (!info)
            return false; // continue

        // Check if this NPC is a vendor
        bool isVendor = (info->npc_flags & UNIT_NPC_FLAG_VENDOR) != 0;
        bool canRepair = (info->npc_flags & UNIT_NPC_FLAG_REPAIR) != 0;

        if (!isVendor && !canRepair)
            return false; // continue - not a vendor

        // Add to cache
        VendorLocation loc;
        loc.x = data.position.x;
        loc.y = data.position.y;
        loc.z = data.position.z;
        loc.mapId = data.position.mapId;
        loc.creatureEntry = data.creature_id[0];
        loc.creatureGuid = guid;
        loc.canRepair = canRepair;

        s_vendorCache.push_back(loc);

        vendorCount++;
        if (canRepair)
            repairCount++;

        return false; // continue iteration
    };

    // Iterate all creature spawns (already loaded in memory)
    sObjectMgr.DoCreatureData(vendorFinder);

    s_cacheBuilt = true;
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Vendor cache built: %u vendors (%u can repair)",
             vendorCount, repairCount);
}

bool VendoringStrategy::IsVendorFriendly(Player* pBot, uint32 creatureEntry)
{
    if (!pBot)
        return false;

    CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(creatureEntry);
    if (!info)
        return false;

    // Get faction templates
    FactionTemplateEntry const* botFaction = pBot->GetFactionTemplateEntry();
    FactionTemplateEntry const* vendorFaction = sObjectMgr.GetFactionTemplateEntry(info->faction);

    if (!botFaction || !vendorFaction)
        return false;

    // Check if vendor is friendly (not hostile) to the bot
    return !botFaction->IsHostileTo(*vendorFaction);
}

bool VendoringStrategy::FindNearestVendor(Player* pBot)
{
    if (!pBot)
        return false;

    // Build cache on first use
    if (!s_cacheBuilt)
        BuildVendorCache();

    float botX = pBot->GetPositionX();
    float botY = pBot->GetPositionY();
    uint32 botMap = pBot->GetMapId();

    float closestDist = FLT_MAX;
    VendorLocation const* nearest = nullptr;

    // Search cache for nearest friendly vendor that can repair
    for (VendorLocation const& loc : s_vendorCache)
    {
        // Must be on same map
        if (loc.mapId != botMap)
            continue;

        // Must be able to repair (we want both sell + repair)
        if (!loc.canRepair)
            continue;

        // Must be friendly to bot's faction
        if (!IsVendorFriendly(pBot, loc.creatureEntry))
            continue;

        // Calculate 2D distance
        float dx = loc.x - botX;
        float dy = loc.y - botY;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist < closestDist)
        {
            closestDist = dist;
            nearest = &loc;
        }
    }

    if (nearest)
    {
        m_targetVendor = *nearest;
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[VendoringStrategy] Bot %s found vendor at (%.1f, %.1f, %.1f) map %u, distance: %.1f yards",
                 pBot->GetName(), m_targetVendor.x, m_targetVendor.y, m_targetVendor.z,
                 m_targetVendor.mapId, closestDist);
        return true;
    }

    sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[VendoringStrategy] Bot %s could not find a friendly vendor on map %u",
             pBot->GetName(), botMap);
    return false;
}

bool VendoringStrategy::NeedsToVendor(Player* pBot)
{
    if (!pBot || !pBot->IsAlive())
        return false;

    return AreBagsFull(pBot) || IsGearBroken(pBot);
}

bool VendoringStrategy::AreBagsFull(Player* pBot)
{
    return GetFreeBagSlots(pBot) == 0;
}

uint32 VendoringStrategy::GetFreeBagSlots(Player* pBot)
{
    if (!pBot)
        return 0;

    uint32 freeSlots = 0;

    // Check backpack (bag 0) - slots 23 to 38
    for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        if (!pBot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            freeSlots++;
    }

    // Check equipped bags (slots 19-22)
    for (int bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        if (Bag* pBag = dynamic_cast<Bag*>(pBot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag)))
        {
            freeSlots += pBag->GetFreeSlots();
        }
    }

    return freeSlots;
}

bool VendoringStrategy::IsGearBroken(Player* pBot)
{
    if (!pBot)
        return false;

    // Check all equipment slots
    for (int i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
    {
        Item* pItem = pBot->GetItemByPos(INVENTORY_SLOT_BAG_0, i);
        if (pItem && pItem->IsBroken())
            return true;
    }

    return false;
}

Creature* VendoringStrategy::GetVendorCreature(Player* pBot) const
{
    if (!pBot)
        return nullptr;

    // Find vendor creature by GUID in the world
    Map* map = pBot->GetMap();
    if (!map)
        return nullptr;

    // Search for the vendor near the target location
    Creature* vendor = map->GetCreature(ObjectGuid(HIGHGUID_UNIT, m_targetVendor.creatureEntry, m_targetVendor.creatureGuid));

    if (vendor && vendor->IsAlive() && vendor->IsVendor())
        return vendor;

    return nullptr;
}

void VendoringStrategy::SellAllItems(Player* pBot, Creature* vendor)
{
    if (!pBot || !vendor)
        return;

    uint32 totalSold = 0;
    uint32 totalMoney = 0;

    // Sell items from backpack
    for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        Item* pItem = pBot->GetItemByPos(INVENTORY_SLOT_BAG_0, i);
        if (!pItem)
            continue;

        ItemPrototype const* proto = pItem->GetProto();
        if (!proto || proto->SellPrice == 0)
            continue; // Can't sell items with no vendor price

        uint32 count = pItem->GetCount();
        uint32 money = proto->SellPrice * count;

        // Remove item and give money
        pBot->RemoveItem(INVENTORY_SLOT_BAG_0, i, true);
        pBot->ModifyMoney(money);

        totalSold++;
        totalMoney += money;
    }

    // Sell items from bags
    for (int bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Bag* pBag = dynamic_cast<Bag*>(pBot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag));
        if (!pBag)
            continue;

        for (uint32 slot = 0; slot < pBag->GetBagSize(); ++slot)
        {
            Item* pItem = pBag->GetItemByPos(slot);
            if (!pItem)
                continue;

            ItemPrototype const* proto = pItem->GetProto();
            if (!proto || proto->SellPrice == 0)
                continue;

            uint32 count = pItem->GetCount();
            uint32 money = proto->SellPrice * count;

            // Remove item and give money
            pBot->RemoveItem(bag, slot, true);
            pBot->ModifyMoney(money);

            totalSold++;
            totalMoney += money;
        }
    }

    if (totalSold > 0)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[VendoringStrategy] Bot %s sold %u items for %u copper",
                 pBot->GetName(), totalSold, totalMoney);
    }
}

void VendoringStrategy::RepairAllGear(Player* pBot, Creature* vendor)
{
    if (!pBot || !vendor)
        return;

    if (!vendor->IsArmorer())
        return; // Can't repair at this vendor

    // Get reputation discount
    float discount = pBot->GetReputationPriceDiscount(vendor);

    // Repair all items
    uint32 totalCost = pBot->DurabilityRepairAll(true, discount);

    if (totalCost > 0)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[VendoringStrategy] Bot %s repaired all gear for %u copper",
                 pBot->GetName(), totalCost);
    }
}

bool VendoringStrategy::DoVendorBusiness(Player* pBot)
{
    Creature* vendor = GetVendorCreature(pBot);
    if (!vendor)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[VendoringStrategy] Bot %s could not find vendor creature at destination",
                 pBot->GetName());
        return false;
    }

    // Sell all items first
    SellAllItems(pBot, vendor);

    // Then repair
    RepairAllGear(pBot, vendor);

    return true;
}

bool VendoringStrategy::Update(Player* pBot, uint32 diff)
{
    if (!pBot || !pBot->IsAlive())
    {
        Reset();
        return false;
    }

    switch (m_state)
    {
        case VendorState::IDLE:
        {
            // Check if we need to vendor
            if (!NeedsToVendor(pBot))
                return false;

            sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[VendoringStrategy] Bot %s needs to vendor (bags full: %s, gear broken: %s)",
                     pBot->GetName(),
                     AreBagsFull(pBot) ? "yes" : "no",
                     IsGearBroken(pBot) ? "yes" : "no");

            // Save starting position
            m_startX = pBot->GetPositionX();
            m_startY = pBot->GetPositionY();
            m_startZ = pBot->GetPositionZ();

            m_state = VendorState::FINDING_VENDOR;
            return true;
        }

        case VendorState::FINDING_VENDOR:
        {
            if (!FindNearestVendor(pBot))
            {
                // No vendor found, go back to idle
                sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[VendoringStrategy] Bot %s could not find any vendor, aborting",
                         pBot->GetName());
                Reset();
                return false;
            }

            // Start walking to vendor
            pBot->GetMotionMaster()->MovePoint(0, m_targetVendor.x, m_targetVendor.y, m_targetVendor.z);
            m_stuckTimer = 0;
            m_lastDistanceCheckTime = 0;
            m_lastDistanceToVendor = FLT_MAX;
            m_state = VendorState::WALKING_TO_VENDOR;

            sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[VendoringStrategy] Bot %s walking to vendor at (%.1f, %.1f)",
                     pBot->GetName(), m_targetVendor.x, m_targetVendor.y);
            return true;
        }

        case VendorState::WALKING_TO_VENDOR:
        {
            // Check if we've arrived
            float dist = pBot->GetDistance(m_targetVendor.x, m_targetVendor.y, m_targetVendor.z);

            if (dist <= VENDOR_INTERACT_RANGE)
            {
                // Arrived at vendor
                pBot->GetMotionMaster()->Clear();
                m_state = VendorState::AT_VENDOR;
                sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[VendoringStrategy] Bot %s arrived at vendor",
                         pBot->GetName());
                return true;
            }

            // Update stuck detection
            m_stuckTimer += diff;
            m_lastDistanceCheckTime += diff;

            if (m_lastDistanceCheckTime >= DISTANCE_CHECK_INTERVAL)
            {
                // Check if we're making progress
                if (std::abs(dist - m_lastDistanceToVendor) < 1.0f)
                {
                    // Not making progress, might be stuck
                    // Try moving again
                    pBot->GetMotionMaster()->MovePoint(0, m_targetVendor.x, m_targetVendor.y, m_targetVendor.z);
                }
                m_lastDistanceToVendor = dist;
                m_lastDistanceCheckTime = 0;
            }

            // Check for stuck timeout
            if (m_stuckTimer >= STUCK_TIMEOUT)
            {
                sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[VendoringStrategy] Bot %s got stuck walking to vendor, aborting",
                         pBot->GetName());
                Reset();
                return false;
            }

            return true; // Still walking
        }

        case VendorState::AT_VENDOR:
        {
            // Do business
            DoVendorBusiness(pBot);
            m_state = VendorState::DONE;
            return true;
        }

        case VendorState::DONE:
        {
            sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[VendoringStrategy] Bot %s finished vendoring",
                     pBot->GetName());
            Reset();
            return false; // Done, strategy complete
        }
    }

    return false;
}

void VendoringStrategy::OnEnterCombat(Player* pBot)
{
    // If we were walking to vendor and got attacked, abort vendoring
    if (m_state == VendorState::WALKING_TO_VENDOR)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[VendoringStrategy] Bot %s entered combat while walking to vendor, aborting",
                 pBot ? pBot->GetName() : "unknown");
        Reset();
    }
}

void VendoringStrategy::OnLeaveCombat(Player* pBot)
{
    // Nothing special needed - vendoring will resume on next Update
}

bool VendoringStrategy::IsComplete(Player* pBot) const
{
    return m_state == VendorState::IDLE || m_state == VendorState::DONE;
}

void VendoringStrategy::Reset()
{
    m_state = VendorState::IDLE;
    m_targetVendor = {};
    m_startX = 0.0f;
    m_startY = 0.0f;
    m_startZ = 0.0f;
    m_stuckTimer = 0;
    m_lastDistanceCheckTime = 0;
    m_lastDistanceToVendor = FLT_MAX;
}
