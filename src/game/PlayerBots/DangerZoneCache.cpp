/*
 * DangerZoneCache.cpp
 *
 * Implementation of the global danger zone cache.
 * See DangerZoneCache.h for design overview.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "DangerZoneCache.h"
#include "Policies/SingletonImp.h"
#include "Log.h"
#include "World.h"
#include <cmath>

INSTANTIATE_SINGLETON_1(DangerZoneCache);

DangerZoneCache::DangerZoneCache()
    : m_cleanupTimer(0)
{
}

void DangerZoneCache::ReportDanger(uint32 mapId, float x, float y, uint8 threatLevel)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    int32 cellX = GetCellX(x);
    int32 cellY = GetCellY(y);

    // Check if we already have a recent danger zone at this location
    // (avoid duplicate entries from same encounter)
    auto& cell = m_grid[mapId][cellX][cellY];
    for (auto const& existing : cell)
    {
        float dx = existing.x - x;
        float dy = existing.y - y;
        float distSq = dx * dx + dy * dy;

        // If within 20 yards of existing zone, don't add duplicate
        if (distSq < 400.0f)  // 20^2 = 400
        {
            return;
        }
    }

    // Add new danger zone
    DangerZone zone;
    zone.x = x;
    zone.y = y;
    zone.threatLevel = threatLevel;
    zone.expireTime = WorldTimer::getMSTime() + DangerZoneConstants::EXPIRE_TIME_MS;

    cell.push_back(zone);

    sLog.Out(LOG_BASIC, LOG_LVL_BASIC,
        "[DangerZoneCache] Added danger zone at map %u (%.1f, %.1f) threat level %u",
        mapId, x, y, threatLevel);
}

bool DangerZoneCache::IsDangerous(uint32 mapId, float x, float y, uint8 botLevel) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    int32 cellX = GetCellX(x);
    int32 cellY = GetCellY(y);

    // Check this cell and 8 neighbors (danger zones can span cell boundaries)
    for (int32 dx = -1; dx <= 1; ++dx)
    {
        for (int32 dy = -1; dy <= 1; ++dy)
        {
            auto mapIt = m_grid.find(mapId);
            if (mapIt == m_grid.end())
                continue;

            auto rowIt = mapIt->second.find(cellX + dx);
            if (rowIt == mapIt->second.end())
                continue;

            auto cellIt = rowIt->second.find(cellY + dy);
            if (cellIt == rowIt->second.end())
                continue;

            uint32 currentTime = WorldTimer::getMSTime();

            for (auto const& zone : cellIt->second)
            {
                // Skip expired zones (will be cleaned up later)
                if (currentTime >= zone.expireTime)
                    continue;

                // Check if this zone is dangerous for the bot's level
                if (!IsZoneDangerousForLevel(zone, botLevel))
                    continue;

                // Check distance
                float distX = x - zone.x;
                float distY = y - zone.y;
                float distSq = distX * distX + distY * distY;

                if (distSq < DangerZoneConstants::DANGER_RADIUS * DangerZoneConstants::DANGER_RADIUS)
                {
                    return true;
                }
            }
        }
    }

    return false;
}

void DangerZoneCache::GetNearbyDangers(uint32 mapId, float x, float y, float radius,
                                        std::vector<DangerZone>& out) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    out.clear();

    // Calculate cell range to check
    int32 minCellX = GetCellX(x - radius);
    int32 maxCellX = GetCellX(x + radius);
    int32 minCellY = GetCellY(y - radius);
    int32 maxCellY = GetCellY(y + radius);

    auto mapIt = m_grid.find(mapId);
    if (mapIt == m_grid.end())
        return;

    float radiusSq = radius * radius;
    uint32 currentTime = WorldTimer::getMSTime();

    for (int32 cx = minCellX; cx <= maxCellX; ++cx)
    {
        auto rowIt = mapIt->second.find(cx);
        if (rowIt == mapIt->second.end())
            continue;

        for (int32 cy = minCellY; cy <= maxCellY; ++cy)
        {
            auto cellIt = rowIt->second.find(cy);
            if (cellIt == rowIt->second.end())
                continue;

            for (auto const& zone : cellIt->second)
            {
                // Skip expired
                if (currentTime >= zone.expireTime)
                    continue;

                // Check distance
                float dx = x - zone.x;
                float dy = y - zone.y;
                if (dx * dx + dy * dy <= radiusSq)
                {
                    out.push_back(zone);
                }
            }
        }
    }
}

void DangerZoneCache::Update(uint32 diff)
{
    m_cleanupTimer += diff;

    if (m_cleanupTimer < DangerZoneConstants::CLEANUP_INTERVAL_MS)
        return;

    m_cleanupTimer = 0;

    std::lock_guard<std::mutex> lock(m_mutex);

    uint32 currentTime = WorldTimer::getMSTime();
    uint32 removedCount = 0;

    // Iterate through all cells and remove expired zones
    for (auto& mapPair : m_grid)
    {
        for (auto& rowPair : mapPair.second)
        {
            for (auto& cellPair : rowPair.second)
            {
                auto& zones = cellPair.second;

                // Remove expired zones using erase-remove idiom
                auto newEnd = std::remove_if(zones.begin(), zones.end(),
                    [currentTime, &removedCount](DangerZone const& zone) {
                        if (currentTime >= zone.expireTime)
                        {
                            ++removedCount;
                            return true;
                        }
                        return false;
                    });

                zones.erase(newEnd, zones.end());
            }
        }
    }

    if (removedCount > 0)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
            "[DangerZoneCache] Cleanup: removed %u expired zones, %u remaining",
            removedCount, GetTotalZoneCount());
    }
}

uint32 DangerZoneCache::GetTotalZoneCount() const
{
    // Note: caller should hold lock if thread safety needed
    // For debug/logging purposes, approximate count is fine
    uint32 count = 0;

    for (auto const& mapPair : m_grid)
    {
        for (auto const& rowPair : mapPair.second)
        {
            for (auto const& cellPair : rowPair.second)
            {
                count += cellPair.second.size();
            }
        }
    }

    return count;
}

bool DangerZoneCache::IsZoneDangerousForLevel(DangerZone const& zone, uint8 botLevel) const
{
    // Zone is dangerous if the recorded threat level is 3+ levels above the bot
    return zone.threatLevel >= botLevel + DangerZoneConstants::LEVEL_DIFF_THRESHOLD;
}
