/*
 * BotQuestCache.h
 *
 * Static caches for quest-related data: quest givers, turn-in targets,
 * and item drop sources. Built once at server startup, shared across
 * all bots. Zero runtime database queries.
 *
 * Follows the same pattern as VendoringStrategy::BuildVendorCache(),
 * TrainingStrategy::BuildTrainerCache(), etc.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_BOTQUESTCACHE_H
#define MANGOS_BOTQUESTCACHE_H

#include "Common.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

class Player;

// ============================================================================
// Quest Giver Info — WHO gives quests and WHERE
// ============================================================================

struct QuestGiverInfo
{
    float x, y, z;
    uint32 mapId;
    uint32 sourceEntry;         // Creature entry or gameobject entry
    uint32 sourceGuid;          // Spawn GUID (for world lookup)
    bool isGameObject;          // false=creature, true=gameobject
    uint32 factionTemplateId;   // For faction filtering (creatures only, 0 for objects)
    std::vector<uint32> questIds;  // Quests this NPC/object offers

    // Pre-filter fields (computed at cache build time)
    // Allow skipping expensive CanTakeQuest() for the vast majority of entries.
    uint8 minQuestLevel;        // Lowest MinLevel across all quests this NPC offers
    uint8 maxQuestLevel;        // Highest QuestLevel across all quests this NPC offers
    uint32 classesMask;         // OR'd RequiredClasses from all quests (0 = any class)
    uint32 racesMask;           // OR'd RequiredRaces from all quests (0 = any race)
};

// ============================================================================
// Quest Turn-in Info — WHO accepts completed quests
// ============================================================================

struct QuestTurnInInfo
{
    float x, y, z;
    uint32 mapId;
    uint32 targetEntry;         // Creature entry or gameobject entry
    uint32 targetGuid;          // Spawn GUID
    bool isGameObject;          // false=creature, true=gameobject
    uint32 factionTemplateId;   // For faction filtering
    uint32 questId;             // Which quest this NPC/object accepts
};

// ============================================================================
// BotQuestCache — Static cache manager
// ============================================================================

class BotQuestCache
{
public:
    // ---- Cache building (called from PlayerBotMgr::Load) ----
    static void BuildQuestGiverCache();
    static void BuildTurnInCache();
    static void BuildItemDropCache();

    // ---- Quest giver lookups ----

    // Find nearest quest giver(s) with quests potentially available for this bot.
    // Uses map partition + pre-filters (level/class/race/faction).
    // Returns nullptr if nothing found on bot's map.
    static QuestGiverInfo const* FindNearestQuestGiver(Player* pBot);

    // Find all quest givers within cluster radius of a point (for multi-pickup).
    // Returns givers within ~100 yards of the given position on the same map.
    static std::vector<QuestGiverInfo const*> FindQuestGiverCluster(
        uint32 mapId, float x, float y, float z, float radius = 100.0f);

    // ---- Turn-in lookups (indexed by quest ID) ----

    // Find nearest turn-in target for a specific completed quest on bot's map.
    // Returns nullptr if no turn-in found on this map.
    static QuestTurnInInfo const* FindNearestTurnIn(Player* pBot, uint32 questId);

    // ---- Item drop lookups (reverse cache) ----

    // Get creature entries that drop a specific item.
    // Returns nullptr if item not in loot tables.
    static std::vector<uint32> const* GetCreaturesDropping(uint32 itemEntry);

    // ---- Creature spawn location lookup ----

    // Find nearest spawn of a creature entry on a given map.
    // Returns false if no spawn found. Outputs coordinates.
    static bool FindCreatureSpawnLocation(uint32 creatureEntry, uint32 mapId,
                                           float botX, float botY,
                                           float& outX, float& outY, float& outZ);

    // ---- O(1) "is this NPC a quest giver?" checks ----

    static bool IsQuestGiverCreature(uint32 creatureEntry);
    static bool IsQuestGiverObject(uint32 gameobjectEntry);

    // ---- Stats for logging ----

    static uint32 GetQuestGiverCount() { return s_totalGiverCount; }
    static uint32 GetTurnInCount() { return s_totalTurnInCount; }

    // Faction check helper (same pattern as VendoringStrategy/TrainingStrategy)
    static bool IsFactionFriendly(Player* pBot, uint32 factionTemplateId);

private:

    // ---- Quest giver cache (partitioned by map) ----
    static std::unordered_map<uint32 /*mapId*/, std::vector<QuestGiverInfo>> s_questGiversByMap;
    static bool s_giverCacheBuilt;
    static std::mutex s_giverCacheMutex;
    static uint32 s_totalGiverCount;

    // ---- Turn-in cache (indexed by quest ID) ----
    static std::unordered_map<uint32 /*questId*/, std::vector<QuestTurnInInfo>> s_turnInsByQuestId;
    static bool s_turnInCacheBuilt;
    static std::mutex s_turnInCacheMutex;
    static uint32 s_totalTurnInCount;

    // ---- Item drop reverse cache ----
    static std::unordered_map<uint32 /*itemEntry*/, std::vector<uint32> /*creatureEntries*/> s_itemDropSources;
    static bool s_itemDropCacheBuilt;
    static std::mutex s_itemDropCacheMutex;

    // ---- O(1) quest giver entry sets ----
    static std::unordered_set<uint32> s_questGiverCreatureEntries;
    static std::unordered_set<uint32> s_questGiverObjectEntries;
};

#endif // MANGOS_BOTQUESTCACHE_H
