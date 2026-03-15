/*
 * BotQuestCache.cpp
 *
 * Static caches for quest-related data. Built once at server startup.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "BotQuestCache.h"
#include "Player.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "Log.h"
#include "ProgressBar.h"
#include "DBCStructure.h"
#include "Database/DatabaseEnv.h"
#include <cmath>
#include <cfloat>

// ============================================================================
// Static member initialization
// ============================================================================

std::unordered_map<uint32, std::vector<QuestGiverInfo>> BotQuestCache::s_questGiversByMap;
bool BotQuestCache::s_giverCacheBuilt = false;
std::mutex BotQuestCache::s_giverCacheMutex;
uint32 BotQuestCache::s_totalGiverCount = 0;

std::unordered_map<uint32, std::vector<QuestTurnInInfo>> BotQuestCache::s_turnInsByQuestId;
bool BotQuestCache::s_turnInCacheBuilt = false;
std::mutex BotQuestCache::s_turnInCacheMutex;
uint32 BotQuestCache::s_totalTurnInCount = 0;

std::unordered_map<uint32, std::vector<uint32>> BotQuestCache::s_itemDropSources;
std::unordered_map<uint32, std::vector<uint32>> BotQuestCache::s_itemGoSources;
bool BotQuestCache::s_itemDropCacheBuilt = false;
std::mutex BotQuestCache::s_itemDropCacheMutex;

std::unordered_set<uint32> BotQuestCache::s_questGiverCreatureEntries;
std::unordered_set<uint32> BotQuestCache::s_questGiverObjectEntries;

// ============================================================================
// Helpers
// ============================================================================

bool BotQuestCache::IsFactionFriendly(Player* pBot, uint32 factionTemplateId)
{
    if (!pBot || factionTemplateId == 0)
        return true;  // GameObjects have no faction, treat as friendly

    FactionTemplateEntry const* botFaction = pBot->GetFactionTemplateEntry();
    FactionTemplateEntry const* npcFaction = sObjectMgr.GetFactionTemplateEntry(factionTemplateId);

    if (!botFaction || !npcFaction)
        return false;

    return !botFaction->IsHostileTo(*npcFaction);
}

// ============================================================================
// Quest Giver Cache
// ============================================================================

void BotQuestCache::BuildQuestGiverCache()
{
    std::lock_guard<std::mutex> lock(s_giverCacheMutex);

    if (s_giverCacheBuilt)
        return;

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[BotQuestCache] Building quest giver cache...");

    // Step 1: Load creature quest relations into a map: creatureEntry -> questIds
    std::unordered_map<uint32, std::vector<uint32>> creatureQuests;
    {
        std::unique_ptr<QueryResult> result(WorldDatabase.Query(
            "SELECT id, quest FROM creature_questrelation"));
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 creatureEntry = fields[0].GetUInt32();
                uint32 questId = fields[1].GetUInt32();
                creatureQuests[creatureEntry].push_back(questId);
            } while (result->NextRow());
        }
    }

    // Step 2: Load gameobject quest relations
    std::unordered_map<uint32, std::vector<uint32>> gameobjectQuests;
    {
        std::unique_ptr<QueryResult> result(WorldDatabase.Query(
            "SELECT id, quest FROM gameobject_questrelation"));
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 goEntry = fields[0].GetUInt32();
                uint32 questId = fields[1].GetUInt32();
                gameobjectQuests[goEntry].push_back(questId);
            } while (result->NextRow());
        }
    }

    // Step 3: Iterate creature spawns, find those that are quest givers
    uint32 creatureGiverCount = 0;

    // Count for progress bar
    uint32 totalCreatures = 0;
    auto counter = [&totalCreatures](CreatureDataPair const&) {
        ++totalCreatures;
        return false;
    };
    sObjectMgr.DoCreatureData(counter);

    BarGoLink bar(totalCreatures);

    auto giverFinder = [&](CreatureDataPair const& pair) {
        bar.step();

        CreatureData const& data = pair.second;
        uint32 guid = pair.first;
        uint32 entry = data.creature_id[0];

        auto it = creatureQuests.find(entry);
        if (it == creatureQuests.end())
            return false;  // Not a quest giver

        CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(entry);
        if (!info)
            return false;

        // Build QuestGiverInfo
        QuestGiverInfo giver;
        giver.x = data.position.x;
        giver.y = data.position.y;
        giver.z = data.position.z;
        giver.mapId = data.position.mapId;
        giver.sourceEntry = entry;
        giver.sourceGuid = guid;
        giver.isGameObject = false;
        giver.factionTemplateId = info->faction;
        giver.questIds = it->second;

        // Pre-compute filter fields from quest data
        giver.minQuestLevel = 255;
        giver.maxQuestLevel = 0;
        giver.classesMask = 0;
        giver.racesMask = 0;

        for (uint32 questId : giver.questIds)
        {
            Quest const* pQuest = sObjectMgr.GetQuestTemplate(questId);
            if (!pQuest)
                continue;

            uint8 minLevel = pQuest->GetMinLevel();
            uint8 questLevel = pQuest->GetQuestLevel();

            if (minLevel < giver.minQuestLevel)
                giver.minQuestLevel = minLevel;
            if (questLevel > giver.maxQuestLevel)
                giver.maxQuestLevel = questLevel;

            giver.classesMask |= pQuest->GetRequiredClasses();
            giver.racesMask |= pQuest->GetRequiredRaces();
        }

        // If no valid quests found, skip
        if (giver.minQuestLevel == 255)
            return false;

        s_questGiversByMap[giver.mapId].push_back(std::move(giver));
        s_questGiverCreatureEntries.insert(entry);
        creatureGiverCount++;

        return false;
    };
    sObjectMgr.DoCreatureData(giverFinder);

    // Step 4: Gameobject quest givers via SQL (no DoGameObjectData equivalent)
    uint32 goGiverCount = 0;

    if (!gameobjectQuests.empty())
    {
        std::unique_ptr<QueryResult> result(WorldDatabase.Query(
            "SELECT guid, id, position_x, position_y, position_z, map FROM gameobject"));

        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 guid = fields[0].GetUInt32();
                uint32 goEntry = fields[1].GetUInt32();

                auto it = gameobjectQuests.find(goEntry);
                if (it == gameobjectQuests.end())
                    continue;  // Not a quest giver

                QuestGiverInfo giver;
                giver.x = fields[2].GetFloat();
                giver.y = fields[3].GetFloat();
                giver.z = fields[4].GetFloat();
                giver.mapId = fields[5].GetUInt32();
                giver.sourceEntry = goEntry;
                giver.sourceGuid = guid;
                giver.isGameObject = true;
                giver.factionTemplateId = 0;  // GameObjects have no faction
                giver.questIds = it->second;

                // Pre-compute filter fields
                giver.minQuestLevel = 255;
                giver.maxQuestLevel = 0;
                giver.classesMask = 0;
                giver.racesMask = 0;

                for (uint32 questId : giver.questIds)
                {
                    Quest const* pQuest = sObjectMgr.GetQuestTemplate(questId);
                    if (!pQuest)
                        continue;

                    uint8 minLevel = pQuest->GetMinLevel();
                    uint8 questLevel = pQuest->GetQuestLevel();

                    if (minLevel < giver.minQuestLevel)
                        giver.minQuestLevel = minLevel;
                    if (questLevel > giver.maxQuestLevel)
                        giver.maxQuestLevel = questLevel;

                    giver.classesMask |= pQuest->GetRequiredClasses();
                    giver.racesMask |= pQuest->GetRequiredRaces();
                }

                if (giver.minQuestLevel == 255)
                    continue;

                s_questGiversByMap[giver.mapId].push_back(std::move(giver));
                s_questGiverObjectEntries.insert(goEntry);
                goGiverCount++;

            } while (result->NextRow());
        }
    }

    s_totalGiverCount = creatureGiverCount + goGiverCount;
    s_giverCacheBuilt = true;

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
        ">> Quest giver cache: %u creature givers, %u gameobject givers (%u total, %zu maps)",
        creatureGiverCount, goGiverCount, s_totalGiverCount, s_questGiversByMap.size());
}

// ============================================================================
// Turn-in Cache
// ============================================================================

void BotQuestCache::BuildTurnInCache()
{
    std::lock_guard<std::mutex> lock(s_turnInCacheMutex);

    if (s_turnInCacheBuilt)
        return;

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[BotQuestCache] Building turn-in cache...");

    uint32 creatureTurnInCount = 0;
    uint32 goTurnInCount = 0;

    // Step 1: Load creature turn-in relations
    std::unordered_map<uint32, std::vector<uint32>> creatureTurnIns;  // entry -> questIds
    {
        std::unique_ptr<QueryResult> result(WorldDatabase.Query(
            "SELECT id, quest FROM creature_involvedrelation"));
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 creatureEntry = fields[0].GetUInt32();
                uint32 questId = fields[1].GetUInt32();
                creatureTurnIns[creatureEntry].push_back(questId);
            } while (result->NextRow());
        }
    }

    // Step 2: Iterate creature spawns, find turn-in NPCs
    auto turnInFinder = [&](CreatureDataPair const& pair) {
        CreatureData const& data = pair.second;
        uint32 guid = pair.first;
        uint32 entry = data.creature_id[0];

        auto it = creatureTurnIns.find(entry);
        if (it == creatureTurnIns.end())
            return false;

        CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(entry);
        if (!info)
            return false;

        for (uint32 questId : it->second)
        {
            QuestTurnInInfo turnIn;
            turnIn.x = data.position.x;
            turnIn.y = data.position.y;
            turnIn.z = data.position.z;
            turnIn.mapId = data.position.mapId;
            turnIn.targetEntry = entry;
            turnIn.targetGuid = guid;
            turnIn.isGameObject = false;
            turnIn.factionTemplateId = info->faction;
            turnIn.questId = questId;

            s_turnInsByQuestId[questId].push_back(turnIn);
            creatureTurnInCount++;
        }

        return false;
    };
    sObjectMgr.DoCreatureData(turnInFinder);

    // Step 3: Gameobject turn-ins
    std::unordered_map<uint32, std::vector<uint32>> goTurnIns;
    {
        std::unique_ptr<QueryResult> result(WorldDatabase.Query(
            "SELECT id, quest FROM gameobject_involvedrelation"));
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 goEntry = fields[0].GetUInt32();
                uint32 questId = fields[1].GetUInt32();
                goTurnIns[goEntry].push_back(questId);
            } while (result->NextRow());
        }
    }

    if (!goTurnIns.empty())
    {
        std::unique_ptr<QueryResult> result(WorldDatabase.Query(
            "SELECT guid, id, position_x, position_y, position_z, map FROM gameobject"));

        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 guid = fields[0].GetUInt32();
                uint32 goEntry = fields[1].GetUInt32();

                auto it = goTurnIns.find(goEntry);
                if (it == goTurnIns.end())
                    continue;

                for (uint32 questId : it->second)
                {
                    QuestTurnInInfo turnIn;
                    turnIn.x = fields[2].GetFloat();
                    turnIn.y = fields[3].GetFloat();
                    turnIn.z = fields[4].GetFloat();
                    turnIn.mapId = fields[5].GetUInt32();
                    turnIn.targetEntry = goEntry;
                    turnIn.targetGuid = guid;
                    turnIn.isGameObject = true;
                    turnIn.factionTemplateId = 0;
                    turnIn.questId = questId;

                    s_turnInsByQuestId[questId].push_back(turnIn);
                    goTurnInCount++;
                }

            } while (result->NextRow());
        }
    }

    s_totalTurnInCount = creatureTurnInCount + goTurnInCount;
    s_turnInCacheBuilt = true;

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
        ">> Turn-in cache: %u creature targets, %u gameobject targets (%u total, %zu quests)",
        creatureTurnInCount, goTurnInCount, s_totalTurnInCount, s_turnInsByQuestId.size());
}

// ============================================================================
// Item Drop Cache
// ============================================================================

void BotQuestCache::BuildItemDropCache()
{
    std::lock_guard<std::mutex> lock(s_itemDropCacheMutex);

    if (s_itemDropCacheBuilt)
        return;

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[BotQuestCache] Building item drop cache...");

    uint32 mappingCount = 0;

    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT entry, item FROM creature_loot_template WHERE item > 0"));

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 creatureEntry = fields[0].GetUInt32();
            uint32 itemEntry = fields[1].GetUInt32();

            // Avoid duplicates in the vector
            auto& entries = s_itemDropSources[itemEntry];
            bool found = false;
            for (uint32 e : entries)
            {
                if (e == creatureEntry)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                entries.push_back(creatureEntry);
                mappingCount++;
            }

        } while (result->NextRow());
    }

    // Step 2: Build gameobject item source reverse lookup
    // gameobject_loot_template.entry is a loot ID, not a GO entry.
    // gameobject_template.data1 = loot ID for type 3 (CHEST) objects.
    uint32 goMappingCount = 0;
    {
        // First build lootId -> items map
        std::unordered_map<uint32, std::vector<uint32>> lootIdToItems;
        std::unique_ptr<QueryResult> goLootResult(WorldDatabase.Query(
            "SELECT entry, item FROM gameobject_loot_template WHERE item > 0"));
        if (goLootResult)
        {
            do
            {
                Field* fields = goLootResult->Fetch();
                uint32 lootId = fields[0].GetUInt32();
                uint32 itemEntry = fields[1].GetUInt32();
                lootIdToItems[lootId].push_back(itemEntry);
            } while (goLootResult->NextRow());
        }

        // Then map lootId -> gameobject entry via gameobject_template (type 3, data1 = lootId)
        std::unique_ptr<QueryResult> goTemplateResult(WorldDatabase.Query(
            "SELECT entry, data1 FROM gameobject_template WHERE type = 3 AND data1 > 0"));
        if (goTemplateResult)
        {
            do
            {
                Field* fields = goTemplateResult->Fetch();
                uint32 goEntry = fields[0].GetUInt32();
                uint32 lootId = fields[1].GetUInt32();

                auto it = lootIdToItems.find(lootId);
                if (it == lootIdToItems.end())
                    continue;

                for (uint32 itemEntry : it->second)
                {
                    auto& entries = s_itemGoSources[itemEntry];
                    bool found = false;
                    for (uint32 e : entries)
                    {
                        if (e == goEntry) { found = true; break; }
                    }
                    if (!found)
                    {
                        entries.push_back(goEntry);
                        goMappingCount++;
                    }
                }
            } while (goTemplateResult->NextRow());
        }
    }

    s_itemDropCacheBuilt = true;

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
        ">> Item drop cache: %zu items from %u creature sources, %zu items from %u gameobject sources",
        s_itemDropSources.size(), mappingCount, s_itemGoSources.size(), goMappingCount);
}

// ============================================================================
// Quest Giver Lookups
// ============================================================================

QuestGiverInfo const* BotQuestCache::FindNearestQuestGiver(Player* pBot,
    std::unordered_set<uint32> const* skipEntries)
{
    if (!pBot || !s_giverCacheBuilt)
        return nullptr;

    uint32 botMap = pBot->GetMapId();
    float botX = pBot->GetPositionX();
    float botY = pBot->GetPositionY();
    uint32 botLevel = pBot->GetLevel();
    uint32 botClassMask = pBot->GetClassMask();
    uint32 botRaceMask = pBot->GetRaceMask();

    auto mapIt = s_questGiversByMap.find(botMap);
    if (mapIt == s_questGiversByMap.end())
        return nullptr;

    float closestDist = FLT_MAX;
    QuestGiverInfo const* nearest = nullptr;

    for (QuestGiverInfo const& giver : mapIt->second)
    {
        // Skip exhausted givers the caller has already visited
        if (skipEntries && skipEntries->count(giver.sourceEntry))
            continue;

        // Pre-filter: level range (bot level must be >= minQuestLevel, with some margin)
        // Allow quests up to 4 levels above current quest level range
        if (botLevel + 4 < giver.minQuestLevel)
            continue;

        // Pre-filter: class mask (if set, bot's class must match)
        if (giver.classesMask != 0 && !(giver.classesMask & botClassMask))
            continue;

        // Pre-filter: race mask (if set, bot's race must match)
        if (giver.racesMask != 0 && !(giver.racesMask & botRaceMask))
            continue;

        // Pre-filter: faction (skip hostile NPCs)
        if (!IsFactionFriendly(pBot, giver.factionTemplateId))
            continue;

        // Distance check
        float dx = giver.x - botX;
        float dy = giver.y - botY;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist < closestDist)
        {
            closestDist = dist;
            nearest = &giver;
        }
    }

    return nearest;
}

std::vector<QuestGiverInfo const*> BotQuestCache::FindQuestGiverCluster(
    uint32 mapId, float x, float y, float z, float radius)
{
    std::vector<QuestGiverInfo const*> cluster;

    if (!s_giverCacheBuilt)
        return cluster;

    auto mapIt = s_questGiversByMap.find(mapId);
    if (mapIt == s_questGiversByMap.end())
        return cluster;

    float radiusSq = radius * radius;

    for (QuestGiverInfo const& giver : mapIt->second)
    {
        float dx = giver.x - x;
        float dy = giver.y - y;
        float distSq = dx * dx + dy * dy;

        if (distSq <= radiusSq)
            cluster.push_back(&giver);
    }

    return cluster;
}

// ============================================================================
// Turn-in Lookups
// ============================================================================

QuestTurnInInfo const* BotQuestCache::FindNearestTurnIn(Player* pBot, uint32 questId)
{
    if (!pBot || !s_turnInCacheBuilt)
        return nullptr;

    auto it = s_turnInsByQuestId.find(questId);
    if (it == s_turnInsByQuestId.end())
        return nullptr;

    uint32 botMap = pBot->GetMapId();
    float botX = pBot->GetPositionX();
    float botY = pBot->GetPositionY();

    float closestDist = FLT_MAX;
    QuestTurnInInfo const* nearest = nullptr;

    for (QuestTurnInInfo const& turnIn : it->second)
    {
        // Same map only
        if (turnIn.mapId != botMap)
            continue;

        // Faction check
        if (!IsFactionFriendly(pBot, turnIn.factionTemplateId))
            continue;

        float dx = turnIn.x - botX;
        float dy = turnIn.y - botY;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist < closestDist)
        {
            closestDist = dist;
            nearest = &turnIn;
        }
    }

    return nearest;
}

// ============================================================================
// Item Drop Lookups
// ============================================================================

std::vector<uint32> const* BotQuestCache::GetCreaturesDropping(uint32 itemEntry)
{
    if (!s_itemDropCacheBuilt)
        return nullptr;

    auto it = s_itemDropSources.find(itemEntry);
    if (it == s_itemDropSources.end())
        return nullptr;

    return &it->second;
}

std::vector<uint32> const* BotQuestCache::GetGameObjectsDropping(uint32 itemEntry)
{
    if (!s_itemDropCacheBuilt)
        return nullptr;

    auto it = s_itemGoSources.find(itemEntry);
    if (it == s_itemGoSources.end())
        return nullptr;

    return &it->second;
}

// ============================================================================
// O(1) Quest Giver Checks
// ============================================================================

// ============================================================================
// Creature Spawn Location Lookup
// ============================================================================

bool BotQuestCache::FindCreatureSpawnLocation(uint32 creatureEntry, uint32 mapId,
                                               float botX, float botY,
                                               float& outX, float& outY, float& outZ)
{
    float closestDist = FLT_MAX;
    bool found = false;

    auto finder = [&](CreatureDataPair const& pair) {
        CreatureData const& data = pair.second;
        if (data.creature_id[0] != creatureEntry)
            return false;
        if (data.position.mapId != mapId)
            return false;

        float dx = data.position.x - botX;
        float dy = data.position.y - botY;
        float dist = dx * dx + dy * dy;  // squared distance is fine for comparison

        if (dist < closestDist)
        {
            closestDist = dist;
            outX = data.position.x;
            outY = data.position.y;
            outZ = data.position.z;
            found = true;
        }
        return false;  // continue iteration
    };
    sObjectMgr.DoCreatureData(finder);

    return found;
}

bool BotQuestCache::FindGameObjectSpawnLocation(uint32 goEntry, uint32 mapId,
                                                  float botX, float botY,
                                                  float& outX, float& outY, float& outZ)
{
    // Query gameobject spawn table (no DoGameObjectData equivalent, so use SQL)
    // This runs rarely (only when bot needs to travel to a gameobject), so SQL is acceptable
    std::unique_ptr<QueryResult> result(WorldDatabase.PQuery(
        "SELECT position_x, position_y, position_z FROM gameobject WHERE id = %u AND map = %u",
        goEntry, mapId));

    if (!result)
        return false;

    float closestDist = FLT_MAX;
    bool found = false;

    do
    {
        Field* fields = result->Fetch();
        float x = fields[0].GetFloat();
        float y = fields[1].GetFloat();
        float z = fields[2].GetFloat();

        float dx = x - botX;
        float dy = y - botY;
        float dist = dx * dx + dy * dy;

        if (dist < closestDist)
        {
            closestDist = dist;
            outX = x;
            outY = y;
            outZ = z;
            found = true;
        }
    } while (result->NextRow());

    return found;
}

// ============================================================================
// Areatrigger Lookup
// ============================================================================

bool BotQuestCache::FindQuestAreaTrigger(uint32 questId, uint32 mapId,
                                          float& outX, float& outY, float& outZ)
{
    // Query areatrigger_involvedrelation + areatrigger_template
    // Runs rarely (only for exploration quests), so SQL is acceptable
    std::unique_ptr<QueryResult> result(WorldDatabase.PQuery(
        "SELECT at.x, at.y, at.z, at.map_id FROM areatrigger_template at "
        "JOIN areatrigger_involvedrelation ai ON at.id = ai.id "
        "WHERE ai.quest = %u AND at.map_id = %u LIMIT 1",
        questId, mapId));

    if (!result)
        return false;

    Field* fields = result->Fetch();
    outX = fields[0].GetFloat();
    outY = fields[1].GetFloat();
    outZ = fields[2].GetFloat();
    return true;
}

// ============================================================================
// O(1) Quest Giver Checks
// ============================================================================

bool BotQuestCache::IsQuestGiverCreature(uint32 creatureEntry)
{
    return s_questGiverCreatureEntries.count(creatureEntry) > 0;
}

bool BotQuestCache::IsQuestGiverObject(uint32 gameobjectEntry)
{
    return s_questGiverObjectEntries.count(gameobjectEntry) > 0;
}
