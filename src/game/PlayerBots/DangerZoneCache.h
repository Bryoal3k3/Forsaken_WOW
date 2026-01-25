/*
 * DangerZoneCache.h
 *
 * Global cache for dangerous mob locations discovered by bots during travel.
 * When a bot takes damage from a high-level mob, the location is recorded.
 * Other bots query this cache to avoid known dangerous areas.
 *
 * Design: Reactive discovery + shared knowledge
 * - First bot to encounter danger may die
 * - All subsequent bots avoid the area
 * - Scales to 3000+ bots with O(1) lookups
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_DANGERZONECACHE_H
#define MANGOS_DANGERZONECACHE_H

#include "Common.h"
#include "Policies/Singleton.h"
#include <unordered_map>
#include <vector>
#include <mutex>

namespace DangerZoneConstants
{
    // Spatial grid cell size (matches GrindingStrategy SEARCH_RANGE_CLOSE)
    constexpr float CELL_SIZE = 50.0f;

    // Danger zone radius - how far from the recorded point is considered dangerous
    constexpr float DANGER_RADIUS = 50.0f;

    // How long danger zones persist before expiring (mobs patrol, spawns change)
    constexpr uint32 EXPIRE_TIME_MS = 5 * 60 * 1000;  // 5 minutes

    // Level difference threshold - only record mobs 3+ levels above bot
    constexpr int32 LEVEL_DIFF_THRESHOLD = 3;

    // Cleanup interval - how often to purge expired entries
    constexpr uint32 CLEANUP_INTERVAL_MS = 60 * 1000;  // 1 minute

    // Detour distance - how far to route around danger zones
    constexpr float DETOUR_DISTANCE = 40.0f;
}

struct DangerZone
{
    float x;
    float y;
    uint8 threatLevel;      // Level of the mob that caused danger
    uint32 expireTime;      // WorldTimer::getMSTime() when this expires
};

class DangerZoneCache
{
public:
    DangerZoneCache();

    /**
     * Report a dangerous location. Called when bot takes damage from high-level mob.
     * @param mapId     Map where danger was encountered
     * @param x, y      Position of danger
     * @param threatLevel Level of the dangerous mob
     */
    void ReportDanger(uint32 mapId, float x, float y, uint8 threatLevel);

    /**
     * Check if a point is in a known danger zone.
     * @param mapId     Map to check
     * @param x, y      Position to check
     * @param botLevel  Level of the bot (for level-relative danger check)
     * @return true if point is dangerous for a bot of this level
     */
    bool IsDangerous(uint32 mapId, float x, float y, uint8 botLevel) const;

    /**
     * Get all danger zones near a point (for detour calculation).
     * @param mapId     Map to check
     * @param x, y      Center position
     * @param radius    Search radius
     * @param out       Output vector of nearby danger zones
     */
    void GetNearbyDangers(uint32 mapId, float x, float y, float radius,
                          std::vector<DangerZone>& out) const;

    /**
     * Periodic cleanup of expired entries. Called from PlayerBotMgr::Update().
     * @param diff      Time since last update in milliseconds
     */
    void Update(uint32 diff);

    /**
     * Get statistics for debugging/logging.
     */
    uint32 GetTotalZoneCount() const;

private:
    // Convert world coordinates to grid cell coordinates
    int32 GetCellX(float x) const { return static_cast<int32>(std::floor(x / DangerZoneConstants::CELL_SIZE)); }
    int32 GetCellY(float y) const { return static_cast<int32>(std::floor(y / DangerZoneConstants::CELL_SIZE)); }

    // Spatial grid: mapId -> cellX -> cellY -> vector<DangerZone>
    // Using nested unordered_maps for O(1) cell lookup
    using CellMap = std::unordered_map<int32, std::vector<DangerZone>>;
    using RowMap = std::unordered_map<int32, CellMap>;
    using MapGrid = std::unordered_map<uint32, RowMap>;

    MapGrid m_grid;
    mutable std::mutex m_mutex;

    // Cleanup timer
    uint32 m_cleanupTimer = 0;

    // Helper to check if a specific zone is dangerous for a bot level
    bool IsZoneDangerousForLevel(DangerZone const& zone, uint8 botLevel) const;
};

#define sDangerZoneCache MaNGOS::Singleton<DangerZoneCache>::Instance()

#endif // MANGOS_DANGERZONECACHE_H
