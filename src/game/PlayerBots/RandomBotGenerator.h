/*
 * RandomBotGenerator.h
 *
 * Auto-generation system for RandomBots.
 * Creates bot accounts, characters, and playerbot entries on first server launch.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_RANDOMBOT_GENERATOR_H
#define MANGOS_RANDOMBOT_GENERATOR_H

#include "Common.h"
#include <string>
#include <vector>
#include <map>
#include <set>

// Race-specific name generation data
struct RaceNameData
{
    std::vector<std::string> prefixes;
    std::vector<std::string> middles;
    std::vector<std::string> maleEndings;
    std::vector<std::string> femaleEndings;
    uint8 minSyllables;
    uint8 maxSyllables;
};

class RandomBotGenerator
{
public:
    // Singleton access
    static RandomBotGenerator& Instance();

    // Main entry point - called from PlayerBotMgr::Load()
    // Checks if generation is needed and performs it if so
    void GenerateIfNeeded(uint32 maxBots);

    // Purge all RandomBots - completely removes all bot accounts, characters, and related data
    // Called during startup if RandomBot.Purge config is enabled
    void PurgeAllRandomBots();

    // Check functions
    bool IsPlayerbotTableEmpty();
    bool HasRandomBotAccounts();

private:
    RandomBotGenerator();
    ~RandomBotGenerator() = default;

    // Prevent copying
    RandomBotGenerator(const RandomBotGenerator&) = delete;
    RandomBotGenerator& operator=(const RandomBotGenerator&) = delete;

    // Generation functions
    void GenerateRandomBots(uint32 count);
    uint32 CreateBotAccount(uint32 accountId, const std::string& username);
    uint32 CreateBotCharacter(uint32 charGuid, uint32 accountId, uint8 race, uint8 classId, uint8 level);
    void CreatePlayerbotEntry(uint32 charGuid);

    // Helper functions
    uint32 GetNextFreeAccountId();
    std::string GenerateUniqueBotName(uint8 race, uint8 gender);
    std::string GenerateRaceName(uint8 race, uint8 gender);
    bool ValidateGeneratedName(std::string const& name);
    void GetStartingPosition(uint8 race, uint32& mapId, float& x, float& y, float& z, float& o);
    uint8 SelectRandomRaceForClass(uint8 classId);

    // Valid race/class combinations for Vanilla WoW
    void InitializeRaceClassData();
    std::map<uint8, std::vector<uint8>> m_classRaces;
    std::vector<uint8> m_allClasses;

    // Race-specific name generation
    void InitializeNameData();
    std::map<uint8, RaceNameData> m_raceNameData;
    std::set<std::string> m_blacklistedNames;
    bool m_nameDataInitialized = false;

    // Track generated names to avoid duplicates within a session
    std::vector<std::string> m_generatedNames;
};

#define sRandomBotGenerator RandomBotGenerator::Instance()

#endif // MANGOS_RANDOMBOT_GENERATOR_H
