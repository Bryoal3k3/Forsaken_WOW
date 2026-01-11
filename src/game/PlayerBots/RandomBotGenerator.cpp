/*
 * RandomBotGenerator.cpp
 *
 * Auto-generation system for RandomBots.
 * Creates bot accounts, characters, and playerbot entries on first server launch.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "RandomBotGenerator.h"
#include "Database/DatabaseEnv.h"
#include "ObjectMgr.h"
#include "World.h"
#include "Log.h"
#include "SharedDefines.h"
#include "Player.h"
#include "ProgressBar.h"

RandomBotGenerator& RandomBotGenerator::Instance()
{
    static RandomBotGenerator instance;
    return instance;
}

RandomBotGenerator::RandomBotGenerator()
{
    InitializeRaceClassData();
}

void RandomBotGenerator::InitializeRaceClassData()
{
    // Valid race/class combinations for Vanilla WoW
    m_classRaces = {
        {CLASS_WARRIOR, {RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF, RACE_GNOME, RACE_ORC, RACE_UNDEAD, RACE_TAUREN, RACE_TROLL}},
        {CLASS_PALADIN, {RACE_HUMAN, RACE_DWARF}},
        {CLASS_HUNTER,  {RACE_DWARF, RACE_NIGHTELF, RACE_ORC, RACE_TAUREN, RACE_TROLL}},
        {CLASS_ROGUE,   {RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF, RACE_GNOME, RACE_ORC, RACE_UNDEAD, RACE_TROLL}},
        {CLASS_PRIEST,  {RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF, RACE_UNDEAD, RACE_TROLL}},
        {CLASS_SHAMAN,  {RACE_ORC, RACE_TAUREN, RACE_TROLL}},
        {CLASS_MAGE,    {RACE_HUMAN, RACE_GNOME, RACE_UNDEAD, RACE_TROLL}},
        {CLASS_WARLOCK, {RACE_HUMAN, RACE_GNOME, RACE_ORC, RACE_UNDEAD}},
        {CLASS_DRUID,   {RACE_NIGHTELF, RACE_TAUREN}}
    };

    m_allClasses = {CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE,
                    CLASS_PRIEST, CLASS_SHAMAN, CLASS_MAGE, CLASS_WARLOCK, CLASS_DRUID};
}

// ============================================================================
// Public Interface
// ============================================================================

void RandomBotGenerator::GenerateIfNeeded(uint32 maxBots)
{
    // Check if playerbot table is empty
    if (!IsPlayerbotTableEmpty())
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[RandomBotGenerator] Playerbot table not empty, skipping generation.");
        return;
    }

    // Check if bot accounts already exist (edge case: playerbot cleared but accounts remain)
    if (HasRandomBotAccounts())
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[RandomBotGenerator] Bot accounts exist but playerbot table is empty.");
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[RandomBotGenerator] Please manually clean up RNDBOT accounts or regenerate.");
        return;
    }

    // First launch - generate everything
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[RandomBotGenerator] First launch detected - generating %u random bots...", maxBots);
    GenerateRandomBots(maxBots);
}

bool RandomBotGenerator::IsPlayerbotTableEmpty()
{
    std::unique_ptr<QueryResult> result = CharacterDatabase.PQuery("SELECT COUNT(*) FROM playerbot");
    if (!result)
        return true;
    return result->Fetch()[0].GetUInt32() == 0;
}

bool RandomBotGenerator::HasRandomBotAccounts()
{
    std::unique_ptr<QueryResult> result = LoginDatabase.PQuery(
        "SELECT COUNT(*) FROM account WHERE username LIKE 'RNDBOT%%'");
    if (!result)
        return false;
    return result->Fetch()[0].GetUInt32() > 0;
}

void RandomBotGenerator::PurgeAllRandomBots()
{
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[RandomBotGenerator] Purging all RandomBots...");

    // Step 1: Get all RandomBot account IDs
    std::unique_ptr<QueryResult> accountResult = LoginDatabase.PQuery(
        "SELECT id FROM account WHERE username LIKE 'RNDBOT%%'");

    if (!accountResult)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[RandomBotGenerator] No RandomBot accounts found. Nothing to purge.");
        return;
    }

    // Collect all account IDs
    std::vector<uint32> accountIds;
    do
    {
        Field* fields = accountResult->Fetch();
        accountIds.push_back(fields[0].GetUInt32());
    }
    while (accountResult->NextRow());

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[RandomBotGenerator] Found %zu RandomBot accounts to purge.", accountIds.size());

    // Step 2: Get all character GUIDs from these accounts
    std::vector<uint32> charGuids;
    for (uint32 accountId : accountIds)
    {
        std::unique_ptr<QueryResult> charResult = CharacterDatabase.PQuery(
            "SELECT guid FROM characters WHERE account = %u", accountId);

        if (charResult)
        {
            do
            {
                Field* fields = charResult->Fetch();
                charGuids.push_back(fields[0].GetUInt32());
            }
            while (charResult->NextRow());
        }
    }

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[RandomBotGenerator] Found %zu characters to delete.", charGuids.size());

    if (!charGuids.empty())
    {
        // Step 3: Delete each character using Player::DeleteFromDB (handles all related tables)
        BarGoLink bar(charGuids.size());

        for (uint32 charGuid : charGuids)
        {
            bar.step();

            // Get account ID for this character (needed by DeleteFromDB)
            uint32 accountId = 0;
            std::unique_ptr<QueryResult> accResult = CharacterDatabase.PQuery(
                "SELECT account FROM characters WHERE guid = %u", charGuid);
            if (accResult)
                accountId = accResult->Fetch()[0].GetUInt32();

            // Delete character and all related data (deleteFinally=true bypasses level check)
            Player::DeleteFromDB(ObjectGuid(HIGHGUID_PLAYER, charGuid), accountId, false, true);
        }

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Deleted %zu characters.", charGuids.size());
    }

    // Step 4: Delete from playerbot table (in case any orphaned entries exist)
    CharacterDatabase.PExecute("DELETE FROM playerbot WHERE ai = 'RandomBotAI'");

    // Step 5: Delete the bot accounts from realmd
    for (uint32 accountId : accountIds)
    {
        LoginDatabase.PExecute("DELETE FROM account WHERE id = %u", accountId);
    }

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[RandomBotGenerator] Purge complete. Deleted %zu accounts.", accountIds.size());
}

// ============================================================================
// Generation Logic
// ============================================================================

void RandomBotGenerator::GenerateRandomBots(uint32 count)
{
    // Calculate how many accounts we need (9 characters per account max in vanilla)
    uint32 accountsNeeded = (count + 8) / 9;

    uint32 nextAccountId = GetNextFreeAccountId();

    uint32 botsCreated = 0;

    for (uint32 accIdx = 0; accIdx < accountsNeeded && botsCreated < count; ++accIdx)
    {
        uint32 accountId = nextAccountId + accIdx;
        char accountName[16];
        snprintf(accountName, sizeof(accountName), "RNDBOT%03u", accIdx + 1);

        // Create account
        CreateBotAccount(accountId, accountName);
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[RandomBotGenerator] Created bot account: %s (ID: %u)", accountName, accountId);

        // Create up to 9 characters per account (one per class if possible)
        uint32 charsOnAccount = 0;
        for (uint8 classId : m_allClasses)
        {
            if (botsCreated >= count || charsOnAccount >= 9)
                break;

            uint8 raceId = SelectRandomRaceForClass(classId);
            if (raceId == 0)
                continue;

            // Use ObjectMgr's GUID generator so the counter stays in sync
            // This prevents player character creation from getting conflicting GUIDs
            uint32 charGuid = sObjectMgr.GeneratePlayerLowGuid();
            uint8 level = 1;  // All bots start at level 1 (TODO: make configurable)

            // Create character and playerbot entry
            CreateBotCharacter(charGuid, accountId, raceId, classId, level);
            CreatePlayerbotEntry(charGuid);

            botsCreated++;
            charsOnAccount++;
        }
    }

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[RandomBotGenerator] Successfully generated %u random bots across %u accounts.",
        botsCreated, accountsNeeded);
}

uint32 RandomBotGenerator::CreateBotAccount(uint32 accountId, const std::string& username)
{
    // Create account in realmd.account
    // Minimal fields only - bot accounts shouldn't be logged into by players
    LoginDatabase.PExecute(
        "INSERT INTO account (id, username, gmlevel) VALUES (%u, '%s', 0)",
        accountId, username.c_str());

    return accountId;
}

uint32 RandomBotGenerator::CreateBotCharacter(uint32 charGuid, uint32 accountId, uint8 race, uint8 classId, uint8 level)
{
    std::string charName = GenerateUniqueBotName();
    uint8 gender = urand(0, 1);

    // Get starting position based on race
    uint32 mapId;
    float posX, posY, posZ, posO;
    GetStartingPosition(race, mapId, posX, posY, posZ, posO);

    // Random appearance
    uint8 skin = urand(0, 5);
    uint8 face = urand(0, 5);
    uint8 hairStyle = urand(0, 5);
    uint8 hairColor = urand(0, 5);
    uint8 facialHair = urand(0, 5);

    // Create character in characters.characters
    CharacterDatabase.PExecute(
        "INSERT INTO characters (guid, account, name, race, class, gender, level, xp, money, "
        "skin, face, hair_style, hair_color, facial_hair, bank_bag_slots, character_flags, "
        "map, position_x, position_y, position_z, orientation, "
        "online, played_time_total, played_time_level, rest_bonus, logout_time, "
        "reset_talents_multiplier, reset_talents_time, extra_flags, stable_slots, zone, "
        "death_expire_time, honor_rank_points, honor_highest_rank, honor_standing, "
        "honor_last_week_hk, honor_last_week_cp, honor_stored_hk, honor_stored_dk, "
        "watched_faction, drunk, health, power1, power2, power3, power4, power5, "
        "explored_zones, equipment_cache, ammo_id, action_bars, world_phase_mask, create_time) "
        "VALUES (%u, %u, '%s', %u, %u, %u, %u, 0, 0, "
        "%u, %u, %u, %u, %u, 0, 0, "
        "%u, %f, %f, %f, %f, "
        "0, 0, 0, 0, 0, "
        "0, 0, 0, 0, 0, "
        "0, 0, 0, 0, "
        "0, 0, 0, 0, "
        "0, 0, 100, 100, 100, 100, 100, 100, "
        "'', '', 0, 0, 1, %u)",  // world_phase_mask = 1 (normal world)
        charGuid, accountId, charName.c_str(), race, classId, gender, level,
        skin, face, hairStyle, hairColor, facialHair,
        mapId, posX, posY, posZ, posO,
        (uint32)time(nullptr));

    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[RandomBotGenerator] Created bot: %s (GUID: %u, Class: %u, Race: %u, Level: %u)",
        charName.c_str(), charGuid, classId, race, level);

    return charGuid;
}

void RandomBotGenerator::CreatePlayerbotEntry(uint32 charGuid)
{
    // Link character to RandomBotAI in playerbot table
    CharacterDatabase.PExecute(
        "INSERT INTO playerbot (char_guid, chance, ai) VALUES (%u, 100, 'RandomBotAI')",
        charGuid);
}

// ============================================================================
// Helper Functions
// ============================================================================

uint32 RandomBotGenerator::GetNextFreeAccountId()
{
    std::unique_ptr<QueryResult> result = LoginDatabase.PQuery("SELECT MAX(id) FROM account");
    if (!result)
        return 1;

    Field* fields = result->Fetch();
    if (fields[0].IsNULL())
        return 1;

    return fields[0].GetUInt32() + 1;
}

uint32 RandomBotGenerator::GetNextFreeCharacterGuid()
{
    std::unique_ptr<QueryResult> result = CharacterDatabase.PQuery("SELECT MAX(guid) FROM characters");
    if (!result)
        return 1;

    Field* fields = result->Fetch();
    if (fields[0].IsNULL())
        return 1;

    return fields[0].GetUInt32() + 1;
}

std::string RandomBotGenerator::GenerateUniqueBotName()
{
    std::string name;
    int attempts = 0;
    const int maxAttempts = 100;

    do
    {
        name = sObjectMgr.GenerateFreePlayerName();
        attempts++;
    }
    while (std::find(m_generatedNames.begin(), m_generatedNames.end(), name) != m_generatedNames.end()
           && attempts < maxAttempts);

    m_generatedNames.push_back(name);
    return name;
}

uint8 RandomBotGenerator::SelectRandomRaceForClass(uint8 classId)
{
    auto it = m_classRaces.find(classId);
    if (it == m_classRaces.end() || it->second.empty())
        return 0;

    const std::vector<uint8>& validRaces = it->second;
    return validRaces[urand(0, validRaces.size() - 1)];
}

void RandomBotGenerator::GetStartingPosition(uint8 race, uint32& mapId, float& x, float& y, float& z, float& o)
{
    // Starting positions for each race (Vanilla WoW)
    switch (race)
    {
        case RACE_HUMAN:
            mapId = 0; x = -8949.95f; y = -132.493f; z = 83.5312f; o = 0.0f;
            break;
        case RACE_DWARF:
        case RACE_GNOME:
            mapId = 0; x = -6240.32f; y = 331.033f; z = 382.758f; o = 0.0f;
            break;
        case RACE_NIGHTELF:
            mapId = 1; x = 10311.3f; y = 832.463f; z = 1326.41f; o = 0.0f;
            break;
        case RACE_ORC:
        case RACE_TROLL:
            mapId = 1; x = -618.518f; y = -4251.67f; z = 38.718f; o = 0.0f;
            break;
        case RACE_UNDEAD:
            mapId = 0; x = 1676.71f; y = 1678.31f; z = 121.67f; o = 0.0f;
            break;
        case RACE_TAUREN:
            mapId = 1; x = -2917.58f; y = -257.98f; z = 52.9968f; o = 0.0f;
            break;
        default:
            // Fallback to human starting area
            mapId = 0; x = -8949.95f; y = -132.493f; z = 83.5312f; o = 0.0f;
            break;
    }
}
