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

    // Reset the character GUID counter to reflect the actual database state
    // This prevents new bots from getting unnecessarily high GUIDs
    sObjectMgr.ReloadCharacterGuids();
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
    // Generate gender first so we can use it for race-appropriate name generation
    uint8 gender = urand(0, 1);
    std::string charName = GenerateUniqueBotName(race, gender);

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
        "0, 0, 0, 0, 0, "  // played_time_total=0 allows starting items (cinematic skipped via IsBot check)
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

void RandomBotGenerator::InitializeNameData()
{
    if (m_nameDataInitialized)
        return;

    // RACE_ORC (2)
    m_raceNameData[RACE_ORC] = {
        // prefixes
        { "Gor", "Grim", "Gar", "Kar", "Yel", "Org", "Muk", "Grezz", "Thrumn",
          "Sor", "Gul", "Or", "Kaz", "Rogg", "God", "Lum", "Gal", "Hag", "Hor",
          "Bur", "Wua", "Uhg", "Ghrawt", "Flakk", "Jark", "Jab", "Thot", "Harr",
          "Krunn", "Rawrk", "Dwukk", "Thonk", "Bor", "Sho", "Run", "Mag", "Kith",
          "Cut", "Drek", "Zug", "Mok", "Lok", "Thro", "Gro", "Rak", "Shar", "Gur",
          "Sar", "Drog", "Krag", "Grom", "Dur", "Naz", "Rok" },
        // middles
        { "a", "o", "u", "ar", "or", "ur", "ok", "ak", "uk" },
        // maleEndings
        { "dul", "tak", "thok", "lek", "mak", "nil", "drak", "ek", "dor", "mok",
          "ak", "an", "thuk", "rus", "thus", "eth", "ok", "ar", "ul", "uk",
          "ash", "gor", "rak", "gash", "nak", "gul", "rok", "gar", "rim", "osh" },
        // femaleEndings
        { "ya", "ma", "da", "ga", "a", "ac", "ka", "ra", "sha", "na", "tra",
          "gra", "zha", "tha" },
        1, 3  // minSyllables, maxSyllables
    };

    // RACE_TROLL (8)
    m_raceNameData[RACE_TROLL] = {
        // prefixes
        { "Gad", "Zal", "Rok", "Mor", "Nek", "Teg", "Zen", "Zjol", "Jor", "Kor",
          "Vor", "Vel", "Zun", "Rwag", "Tray", "Zab", "Den", "Han", "Ki", "Sor",
          "Tun", "Ul", "Yen", "Jam", "Un", "Zan", "Van", "Zul", "Vol", "Sen",
          "Jin", "Rak", "Tal", "Zik", "Raj", "Jik", "Zor", "Tek", "Kal", "Jaz",
          "Rik", "Vaz", "Jet", "Zak", "Kol", "Hex", "Ral", "Bom", "Mun" },
        // middles
        { "a", "i", "o", "ak", "ta", "za", "ka", "ri", "ji", "ex", "ja", "thu" },
        // maleEndings
        { "rin", "zane", "han", "ki", "li", "shi", "ji", "nir", "nun", "tin",
          "nal", "i", "ir", "rax", "jai", "jin", "taz", "kal", "tho", "zek",
          "tal", "vos", "zal", "tek", "zon", "kil", "raj", "zim", "tik", "vol" },
        // femaleEndings
        { "zua", "ra", "ya", "tha", "ri", "elek", "iss", "ai", "wa", "soa",
          "bra", "zi", "ki", "ja", "li", "za", "ti", "vi", "ka", "ni", "xi",
          "shi", "ta", "la", "va", "mi" },
        2, 3
    };

    // RACE_TAUREN (6)
    m_raceNameData[RACE_TAUREN] = {
        // prefixes
        { "Ah", "Kad", "Holt", "Sark", "Brek", "Kom", "Ot", "Pand", "Tep", "Tuh",
          "Del", "Et", "Hal", "Hog", "Kard", "Kurm", "Kur", "Mah", "Oh", "Pak",
          "Tag", "Thrumn", "Bul", "Krumn", "Taim", "Torn", "Harb", "Har", "Roh",
          "Skorn", "Tak", "Varg", "Krang", "Narm", "Bronk", "Moor", "Ask", "Pal",
          "Sheal", "Sur", "Nah", "Un", "Chep", "Fel", "Kag", "Kun", "Naal", "Nat",
          "Nid", "Sew", "Shad", "Sunn", "Ad", "Dy", "Genn", "Lank", "Meel", "Wunn",
          "Koda", "Mato", "Taho", "Waka", "Naru", "Shon", "Hira", "Tala", "Yona",
          "Mika", "Hosa", "Tawa", "Noka", "Wira", "Kana", "Halu", "Toma", "Runa" },
        // middles
        { "a", "u", "o", "an", "ah" },
        // maleEndings
        { "nu", "or", "in", "oh", "go", "u", "pa", "ko", "ain", "rug", "utt",
          "ku", "at", "uk", "he", "no", "wa", "mo", "lu", "so", "ho", "ro",
          "mu", "ha", "na", "rn", "rg", "nk", "mp", "ak", "om", "im" },
        // femaleEndings
        { "a", "ta", "ri", "i", "wa", "ah", "na", "mi", "ia", "la", "wi",
          "ya", "ra", "sa", "ti", "li", "ka", "ma", "si", "ni", "lo", "hi" },
        1, 3
    };

    // RACE_UNDEAD (5)
    m_raceNameData[RACE_UNDEAD] = {
        // prefixes
        { "Greg", "Gord", "Mich", "Christ", "Ed", "Will", "Rand", "Mort", "Adr",
          "Al", "And", "Bas", "Beth", "Ced", "Cole", "Dan", "Ez", "Herb", "Norm",
          "Rup", "Tim", "Walt", "Xav", "An", "Chlo", "Clar", "Is", "Mar", "Oph",
          "Mor", "Drath", "Vel", "Sev", "Krath", "Neth", "Vor", "Grav", "Thal",
          "Zeth", "Mal", "Crav", "Drek", "Fen", "Gol", "Loth", "Nol", "Rath",
          "Soth", "Trev", "Wrath", "Zol", "Aust", "Ded", "Max" },
        // middles
        { "a", "i", "o", "e", "er", "el", "ek", "yss" },
        // maleEndings
        { "ry", "on", "el", "pher", "ward", "am", "olph", "mer", "an", "ic",
          "rew", "il", "or", "ric", "man", "ert", "thy", "er", "us", "os",
          "ius", "ath", "is", "oth", "eth", "ul", "om", "ax", "ez" },
        // femaleEndings
        { "ya", "a", "ette", "e", "ce", "bella", "on", "ia", "ra", "is",
          "ith", "ora", "yth", "ana", "eth", "ira", "osa", "yra", "ena",
          "ila", "ova", "ysa", "ara", "ura", "esa", "yna", "ssa", "lia" },
        2, 3
    };

    // RACE_HUMAN (1)
    m_raceNameData[RACE_HUMAN] = {
        // prefixes
        { "And", "Ang", "Bar", "Ben", "Bart", "Col", "Colt", "Dane", "Dan", "Dun",
          "Ger", "Gord", "Harr", "Hein", "Jasp", "Jord", "Jorg", "Just", "Ken",
          "Luc", "Mag", "Morg", "Morr", "Os", "Owen", "Ray", "Rob", "Stan", "Steph",
          "Terr", "Thom", "Thur", "Warr", "Will", "Ash", "Bern", "Kat", "May",
          "Mill", "Sar", "Jos", "Ell", "Hel", "Mich", "Cor", "Dawn", "Kir",
          "Bren", "Cal", "Dor", "Eld", "Fen", "Gal", "Hal", "Lor", "Nor", "Per",
          "Ral", "Sel", "Tor", "Val", "Wes", "Ald", "Bor", "Cyr", "Dav", "Fyn",
          "Gar", "Hen", "Kar", "Ler", "Nav", "Ren" },
        // middles
        { "a", "i", "e", "o", "le", "ja", "ri", "er", "an", "en", "ett" },
        // maleEndings
        { "er", "us", "os", "min", "by", "in", "on", "el", "can", "ard", "rich",
          "an", "en", "dor", "nor", "is", "ric", "len", "ert", "ley", "as",
          "man", "am", "son", "don", "ton", "ford", "well", "ham", "win", "mund" },
        // femaleEndings
        { "ley", "ice", "ie", "bell", "y", "a", "en", "ene", "elle", "na",
          "ine", "wen", "ira", "ora", "lyn", "ana", "ela", "isa", "ria",
          "ara", "ina", "eth", "wyn", "ola", "una", "esa", "ala", "la", "ra" },
        2, 3
    };

    // RACE_DWARF (3)
    m_raceNameData[RACE_DWARF] = {
        // prefixes
        { "Barr", "Gol", "Grim", "Gryth", "Dar", "Hulf", "Mel", "Roett", "Val",
          "Wul", "Bor", "Brom", "Bruuk", "Dol", "Em", "Geof", "Gren", "Grum",
          "Heg", "Hjol", "Jor", "Kel", "Man", "Ol", "Skol", "Sog", "Thal", "Thar",
          "Thur", "Bel", "Thor", "Groum", "Krom", "Lar", "Mur", "Muir", "Rot",
          "Steeg", "Din", "Niss", "Bail", "Fred", "Gull", "Jag", "Dur", "Kol",
          "Bal", "Gar", "Hol", "Kur", "Mor", "Nor", "Stor", "Vor", "Brun", "Drak",
          "Kar", "Lok", "Nak", "Rok", "Skor", "Brak", "Dun", "Krag" },
        // middles
        { "a", "i", "e", "o", "u" },
        // maleEndings
        { "us", "nir", "nur", "yl", "dan", "nan", "en", "gar", "mort", "im",
          "ir", "kin", "man", "rul", "ram", "il", "nus", "nar", "dir", "mund",
          "mir", "strum", "gorn", "thran", "min", "gus", "gen", "grum", "don",
          "gath", "din", "ak", "or", "ar", "um", "ik", "ok", "ad", "am", "uk",
          "om", "ag", "un", "id", "od", "az", "in" },
        // femaleEndings
        { "ta", "a", "ey", "da", "dra", "ra", "li", "na", "mi", "ga", "ri",
          "la", "di", "ni", "ma", "gi", "ru", "lu", "du", "nu", "mu", "lo",
          "ina", "ita" },
        1, 3
    };

    // RACE_GNOME (7)
    m_raceNameData[RACE_GNOME] = {
        // prefixes
        { "Bub", "Jask", "Alph", "Bing", "Bink", "Bizm", "Nam", "Carv", "Beeg",
          "Knaz", "Bo", "Box", "Ben", "Wiz", "Fizz", "Rizz", "Wizz", "Hum", "Sock",
          "Tink", "Blaiz", "Skip", "Cog", "Dorb", "Niv", "Ozz", "Gno", "Kern",
          "Sicc", "Ash", "Jub", "Ar", "Rose", "Tall", "Sool", "Bert", "Bet",
          "Linz", "Trix", "Mill", "Izz", "Mox", "Em", "Ginn", "Trin", "Bix",
          "Coz", "Diz", "Fiz", "Gix", "Hep", "Jix", "Kip", "Lix", "Mip", "Nix",
          "Pip", "Rix", "Sip", "Wix", "Zap", "Bop", "Dek", "Fip", "Gaz", "Hix",
          "Jep", "Koz", "Lep", "Nep", "Poz", "Spro", "Glit", "Mek", "Zin" },
        // middles
        { "a", "i", "o", "u", "e", "y" },
        // maleEndings
        { "lo", "a", "us", "o", "do", "le", "ik", "bang", "bolt", "ie", "e",
          "in", "vet", "arn", "bee", "wick", "ton", "bot", "zle", "nik", "gig",
          "pop", "bit", "zap", "rig", "dok", "wob", "zik", "nob", "gop", "tik" },
        // femaleEndings
        { "li", "lee", "na", "y", "ie", "i", "zi", "pri", "fi", "ti", "ni",
          "xi", "bi", "gi", "pi", "ri", "si", "wi", "mi", "ki", "di", "vi",
          "sy", "xy", "ey", "ine" },
        1, 3
    };

    // RACE_NIGHTELF (4)
    m_raceNameData[RACE_NIGHTELF] = {
        // prefixes
        { "Al", "Er", "Loth", "Math", "Tar", "Dar", "Sil", "An", "Dor", "Den",
          "Fyl", "Lyr", "Tal", "Mael", "And", "Cayn", "Char", "Cyr", "Glor",
          "Kier", "Mydr", "Myth", "Shal", "Tur", "Ul", "Vol", "Yld", "Cor", "Fal",
          "Garr", "Jar", "Jen", "Ast", "Lel", "Jan", "Lar", "Syur", "Joc", "Dyr",
          "Ar", "Cyl", "Dend", "Eal", "Ell", "El", "Fyr", "Ill", "Jae", "Kyr",
          "Land", "Mel", "Mer", "Vin", "Fae", "Lal", "Syl", "Tres", "Tri", "Thel",
          "Shan", "Kel", "Vor", "Ara", "Nyl", "Aeth", "Lyth", "Thal", "Mynd",
          "Eld", "Val", "Sher", "Ven", "Ryn", "Dal", "Eth", "Zeph", "Lor", "Myr",
          "Shyl", "Aen", "Vel", "Nym", "Fyn", "Dyn", "Ryl" },
        // middles
        { "a", "e", "i", "o", "u", "an", "en", "er", "el", "ren", "dan", "ish",
          "ath", "ae", "or", "y", "ain", "ady", "ysh", "and", "yn", "van", "ryth",
          "as", "l" },
        // maleEndings
        { "gorn", "on", "ias", "gyl", "vir", "nath", "air", "ar", "arion",
          "ian", "os", "ran", "ir", "rus", "dryn", "dan", "diir", "an", "nul",
          "mon", "lar", "thir", "en", "and", "dron", "eth", "al", "el", "dris",
          "thor", "wyn", "ras", "nos", "ril", "dor", "ven", "las", "nis", "thil",
          "ren", "dral", "vyn", "ros", "din", "thal", "lor", "iel", "orn", "dir" },
        // femaleEndings
        { "ia", "ea", "ai", "dria", "na", "aste", "hara", "dia", "yell", "is",
          "rieth", "anna", "ana", "ria", "yssa", "aria", "hala", "ara", "ith",
          "wyn", "iel", "ese", "ril", "aea", "dra", "lyn", "ris", "ael", "ira",
          "ena", "ali", "ura", "dri", "lis", "aia", "ande", "ella", "itha",
          "lia", "nna", "ra", "sha" },
        2, 4
    };

    // Blacklisted names (famous WoW characters) - stored lowercase for comparison
    m_blacklistedNames = {
        // Alliance Leaders & Heroes
        "varian", "anduin", "bolvar", "arthas", "uther", "tirion", "jaina", "magni",
        "muradin", "brann", "moira", "gelbin", "mekkatorque", "tyrande", "malfurion",
        "illidan", "fandral", "staghelm", "shandris", "cenarius",
        // Horde Leaders & Heroes
        "thrall", "durotan", "orgrim", "grommash", "garrosh", "saurfang", "sylvanas",
        "cairne", "baine", "voljin", "senjin", "zuljin", "rokhan",
        // Villains & Major NPCs
        "ragnaros", "onyxia", "nefarian", "deathwing", "neltharion", "kelthuzad",
        "archimonde", "kiljaedan", "mannoroth", "medivh", "guldan", "nerzhul",
        "azshara", "xavius", "hakkar", "geddon", "garr", "shazzrah", "lucifron",
        // Other Notable Characters
        "rexxar", "mankrik", "hogger", "edwin", "vancleef", "rhonin", "krasus",
        "aegwynn", "lothar", "turalyon", "alleria", "khadgar", "antonidas"
    };

    m_nameDataInitialized = true;
    sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[RandomBotGenerator] Name generation data initialized for 8 races.");
}

bool RandomBotGenerator::ValidateGeneratedName(std::string const& name)
{
    // Rule 1: Length 3-12
    if (name.length() < 3 || name.length() > 12)
        return false;

    // Rule 2: No triple consecutive letters
    for (size_t i = 2; i < name.length(); ++i)
    {
        char c1 = std::tolower(name[i]);
        char c2 = std::tolower(name[i-1]);
        char c3 = std::tolower(name[i-2]);
        if (c1 == c2 && c2 == c3)
            return false;
    }

    // Rule 3: Not blacklisted
    std::string lowerName = name;
    for (char& c : lowerName)
        c = std::tolower(c);
    if (m_blacklistedNames.find(lowerName) != m_blacklistedNames.end())
        return false;

    return true;
}

std::string RandomBotGenerator::GenerateRaceName(uint8 race, uint8 gender)
{
    InitializeNameData();

    // Find race data, fallback to human if not found
    auto it = m_raceNameData.find(race);
    if (it == m_raceNameData.end())
        it = m_raceNameData.find(RACE_HUMAN);

    RaceNameData const& data = it->second;
    std::string name;

    // Determine syllable count
    uint8 syllableCount = urand(data.minSyllables, data.maxSyllables);

    // 1. Always start with a prefix
    name = data.prefixes[urand(0, data.prefixes.size() - 1)];

    // 2. Add middle syllables if needed (for each syllable beyond 2)
    if (syllableCount > 2 && !data.middles.empty())
    {
        for (uint8 i = 2; i < syllableCount; ++i)
        {
            name += data.middles[urand(0, data.middles.size() - 1)];
        }
    }

    // 3. Add gender-appropriate ending (if syllableCount > 1)
    if (syllableCount > 1)
    {
        if (gender == GENDER_FEMALE && !data.femaleEndings.empty())
            name += data.femaleEndings[urand(0, data.femaleEndings.size() - 1)];
        else if (!data.maleEndings.empty())
            name += data.maleEndings[urand(0, data.maleEndings.size() - 1)];
    }

    // 4. Capitalize first letter, lowercase rest
    if (!name.empty())
    {
        name[0] = std::toupper(name[0]);
        for (size_t i = 1; i < name.length(); ++i)
            name[i] = std::tolower(name[i]);
    }

    return name;
}

std::string RandomBotGenerator::GenerateUniqueBotName(uint8 race, uint8 gender)
{
    std::string name;
    int attempts = 0;
    const int maxAttempts = 100;

    do
    {
        name = GenerateRaceName(race, gender);
        attempts++;
    }
    while ((!ValidateGeneratedName(name) ||
            std::find(m_generatedNames.begin(), m_generatedNames.end(), name) != m_generatedNames.end() ||
            sObjectMgr.GetPlayerGuidByName(name))
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
