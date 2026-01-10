/*
 * VendoringStrategy.h
 *
 * Strategy for handling bot vendoring - selling items and repairing gear.
 * Finds nearest friendly vendor, walks there, sells/repairs, returns.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_VENDORINGSTRATEGY_H
#define MANGOS_VENDORINGSTRATEGY_H

#include "IBotStrategy.h"
#include <vector>
#include <mutex>

class Player;
class Creature;

// Cached vendor location data
struct VendorLocation
{
    float x;
    float y;
    float z;
    uint32 mapId;
    uint32 creatureEntry;   // For faction checking
    uint32 creatureGuid;    // To find the actual NPC when we arrive
    bool canRepair;         // Has UNIT_NPC_FLAG_REPAIR
};

class VendoringStrategy : public IBotStrategy
{
public:
    VendoringStrategy();

    // IBotStrategy interface
    bool Update(Player* pBot, uint32 diff) override;
    void OnEnterCombat(Player* pBot) override;
    void OnLeaveCombat(Player* pBot) override;
    char const* GetName() const override { return "VendoringStrategy"; }

    // Additional helpers
    bool IsComplete(Player* pBot) const;
    void Reset();

    // Check if bot needs to vendor (bags full OR gear broken)
    static bool NeedsToVendor(Player* pBot);

    // Check if bags are full (no free slots)
    static bool AreBagsFull(Player* pBot);

    // Check if any equipped gear is broken (durability = 0)
    static bool IsGearBroken(Player* pBot);

    // Get total free bag slots
    static uint32 GetFreeBagSlots(Player* pBot);

private:
    enum class VendorState
    {
        IDLE,               // Not vendoring
        FINDING_VENDOR,     // Looking for nearest vendor
        WALKING_TO_VENDOR,  // Moving toward vendor
        AT_VENDOR,          // Close enough, selling/repairing
        DONE                // Finished vendoring
    };

    VendorState m_state;
    VendorLocation m_targetVendor;
    float m_startX, m_startY, m_startZ;  // Where we started (to return later if needed)
    uint32 m_stuckTimer;                  // Detect if bot gets stuck walking
    uint32 m_lastDistanceCheckTime;
    float m_lastDistanceToVendor;

    // Vendor cache (static, shared across all bots)
    static std::vector<VendorLocation> s_vendorCache;
    static bool s_cacheBuilt;
    static std::mutex s_cacheMutex;

    // Cache management
    static void BuildVendorCache();

    // Find nearest friendly vendor for this bot
    bool FindNearestVendor(Player* pBot);

    // Check if vendor is friendly to bot's faction
    static bool IsVendorFriendly(Player* pBot, uint32 creatureEntry);

    // Perform selling and repairing
    bool DoVendorBusiness(Player* pBot);

    // Sell all items in bags
    void SellAllItems(Player* pBot, Creature* vendor);

    // Repair all gear
    void RepairAllGear(Player* pBot, Creature* vendor);

    // Get the vendor creature when we arrive
    Creature* GetVendorCreature(Player* pBot) const;

    // Constants
    static constexpr float VENDOR_INTERACT_RANGE = 5.0f;    // Distance to interact with vendor
    static constexpr uint32 STUCK_TIMEOUT = 30000;          // 30 seconds stuck = give up
    static constexpr uint32 DISTANCE_CHECK_INTERVAL = 3000; // Check progress every 3 sec
};

#endif // MANGOS_VENDORINGSTRATEGY_H
