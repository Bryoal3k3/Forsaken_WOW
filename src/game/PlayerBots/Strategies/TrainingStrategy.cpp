/*
 * TrainingStrategy.cpp
 *
 * Strategy for handling bot spell training - traveling to class trainers
 * and learning available spells at even levels.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "TrainingStrategy.h"
#include "BotMovementManager.h"
#include "Player.h"
#include "Creature.h"
#include "ObjectMgr.h"
#include "Log.h"
#include "MotionMaster.h"
#include "UnitDefines.h"
#include "DBCStructure.h"
#include "SharedDefines.h"
#include "Map.h"
#include "ProgressBar.h"
#include "Cell.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "SpellMgr.h"
#include "Database/DatabaseEnv.h"
#include <cmath>
#include <cfloat>

// Static member initialization
std::vector<TrainerLocation> TrainingStrategy::s_trainerCache;
bool TrainingStrategy::s_cacheBuilt = false;
std::mutex TrainingStrategy::s_cacheMutex;

TrainingStrategy::TrainingStrategy()
    : m_state(TrainingState::IDLE)
    , m_targetTrainer{}
    , m_stuckTimer(0)
    , m_lastDistanceCheckTime(0)
    , m_lastDistanceToTrainer(FLT_MAX)
    , m_trainingTriggeredForLevel(0)
{
}

void TrainingStrategy::BuildTrainerCache()
{
    std::lock_guard<std::mutex> lock(s_cacheMutex);

    if (s_cacheBuilt)
        return;

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[TrainingStrategy] Building class trainer cache...");

    // First pass: count all creature spawns for progress bar
    uint32 totalCreatures = 0;
    auto counter = [&totalCreatures](CreatureDataPair const& /*pair*/) {
        ++totalCreatures;
        return false; // continue iteration
    };
    sObjectMgr.DoCreatureData(counter);

    // Create progress bar
    BarGoLink bar(totalCreatures);

    uint32 trainerCount = 0;

    // Track trainers per class for logging
    uint32 trainersPerClass[12] = {0};

    // Second pass: find class trainers with progress
    auto trainerFinder = [&](CreatureDataPair const& pair) {
        bar.step();

        CreatureData const& data = pair.second;
        uint32 guid = pair.first;

        // Get creature template info
        CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(data.creature_id[0]);
        if (!info)
            return false; // continue

        // Check if this NPC is a class trainer (trainer_type = 0, trainer_class > 0)
        // trainer_type: 0 = class trainer, 1 = mount trainer, 2 = tradeskill trainer
        if (info->trainer_type != 0 || info->trainer_class == 0)
            return false; // not a class trainer

        // Must have trainer flag
        if (!(info->npc_flags & UNIT_NPC_FLAG_TRAINER))
            return false;

        // Add to cache
        TrainerLocation loc;
        loc.x = data.position.x;
        loc.y = data.position.y;
        loc.z = data.position.z;
        loc.mapId = data.position.mapId;
        loc.creatureEntry = data.creature_id[0];
        loc.creatureGuid = guid;
        loc.trainerClass = info->trainer_class;
        loc.trainerId = info->trainer_id;
        loc.factionTemplateId = info->faction;

        s_trainerCache.push_back(loc);

        trainerCount++;
        if (info->trainer_class < 12)
            trainersPerClass[info->trainer_class]++;

        return false; // continue iteration
    };

    // Iterate all creature spawns
    sObjectMgr.DoCreatureData(trainerFinder);

    s_cacheBuilt = true;

    // Log class breakdown
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Class trainer cache built: %u trainers", trainerCount);
    sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "   Warrior: %u, Paladin: %u, Hunter: %u, Rogue: %u",
             trainersPerClass[1], trainersPerClass[2], trainersPerClass[3], trainersPerClass[4]);
    sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "   Priest: %u, Shaman: %u, Mage: %u, Warlock: %u, Druid: %u",
             trainersPerClass[5], trainersPerClass[7], trainersPerClass[8], trainersPerClass[9], trainersPerClass[11]);
}

bool TrainingStrategy::IsTrainerFriendly(Player* pBot, uint32 factionTemplateId)
{
    if (!pBot)
        return false;

    // Get faction templates
    FactionTemplateEntry const* botFaction = pBot->GetFactionTemplateEntry();
    FactionTemplateEntry const* trainerFaction = sObjectMgr.GetFactionTemplateEntry(factionTemplateId);

    if (!botFaction || !trainerFaction)
        return false;

    // Check if trainer is friendly (not hostile) to the bot
    return !botFaction->IsHostileTo(*trainerFaction);
}

bool TrainingStrategy::FindNearestTrainer(Player* pBot)
{
    if (!pBot)
        return false;

    // Build cache on first use
    if (!s_cacheBuilt)
        BuildTrainerCache();

    uint8 botClass = pBot->GetClass();
    float botX = pBot->GetPositionX();
    float botY = pBot->GetPositionY();
    uint32 botMap = pBot->GetMapId();

    float closestDist = FLT_MAX;
    TrainerLocation const* nearest = nullptr;

    // Search cache for nearest friendly trainer for this class
    for (TrainerLocation const& loc : s_trainerCache)
    {
        // Must be on same map (continent)
        if (loc.mapId != botMap)
            continue;

        // Must train this bot's class
        if (loc.trainerClass != botClass)
            continue;

        // Must be friendly to bot's faction
        if (!IsTrainerFriendly(pBot, loc.factionTemplateId))
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
        m_targetTrainer = *nearest;
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[TrainingStrategy] Bot %s (class %u) found trainer at (%.1f, %.1f, %.1f) map %u, distance: %.1f yards",
                 pBot->GetName(), botClass, m_targetTrainer.x, m_targetTrainer.y, m_targetTrainer.z,
                 m_targetTrainer.mapId, closestDist);
        return true;
    }

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[TrainingStrategy] Bot %s (class %u) could not find a friendly trainer on map %u",
             pBot->GetName(), botClass, botMap);
    return false;
}

bool TrainingStrategy::NeedsTraining(Player* pBot) const
{
    if (!pBot || !pBot->IsAlive())
        return false;

    // Check if we're already in training mode
    if (m_state != TrainingState::IDLE && m_state != TrainingState::NEEDS_TRAINING)
        return true; // Already training, continue

    // If training was triggered, we need to train
    if (m_state == TrainingState::NEEDS_TRAINING)
        return true;

    return false;
}

void TrainingStrategy::TriggerTraining()
{
    if (m_state == TrainingState::IDLE)
    {
        m_state = TrainingState::NEEDS_TRAINING;
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[TrainingStrategy] Training triggered");
    }
}

std::vector<uint32> TrainingStrategy::GetLearnableSpells(Player* pBot, uint32 trainerId) const
{
    std::vector<uint32> learnableSpells;

    if (!pBot || trainerId == 0)
        return learnableSpells;

    uint32 botLevel = pBot->GetLevel();

    // Query npc_trainer_template for spells this trainer teaches
    // that the bot doesn't already know and can learn at current level
    std::unique_ptr<QueryResult> result(WorldDatabase.PQuery(
        "SELECT spell, reqlevel FROM npc_trainer_template WHERE entry = %u AND reqlevel <= %u ORDER BY reqlevel",
        trainerId, botLevel));

    if (!result)
    {
        // Try npc_trainer table as fallback (some trainers use entry directly)
        result = WorldDatabase.PQuery(
            "SELECT spell, reqlevel FROM npc_trainer WHERE entry = %u AND reqlevel <= %u ORDER BY reqlevel",
            trainerId, botLevel);
    }

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 spellId = fields[0].GetUInt32();

            // Check if bot already knows this spell
            if (!pBot->HasSpell(spellId))
            {
                // Verify spell exists
                SpellEntry const* spellEntry = sSpellMgr.GetSpellEntry(spellId);
                if (spellEntry)
                {
                    learnableSpells.push_back(spellId);
                }
            }
        } while (result->NextRow());
    }

    return learnableSpells;
}

Creature* TrainingStrategy::GetTrainerCreature(Player* pBot) const
{
    if (!pBot)
        return nullptr;

    Map* map = pBot->GetMap();
    if (!map)
        return nullptr;

    // Search for the trainer near the target location
    ObjectGuid trainerGuid(HIGHGUID_UNIT, m_targetTrainer.creatureEntry, m_targetTrainer.creatureGuid);
    Creature* trainer = map->GetCreature(trainerGuid);

    if (!trainer || !trainer->IsAlive())
    {
        // Try to find any friendly trainer of this class nearby
        static constexpr float SEARCH_RANGE = 30.0f;
        std::list<Creature*> creatures;
        MaNGOS::AnyUnitInObjectRangeCheck check(pBot, SEARCH_RANGE);
        MaNGOS::CreatureListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> searcher(creatures, check);
        Cell::VisitGridObjects(pBot, searcher, SEARCH_RANGE);

        for (Creature* creature : creatures)
        {
            if (!creature || !creature->IsAlive())
                continue;

            CreatureInfo const* info = creature->GetCreatureInfo();
            if (!info)
                continue;

            // Must be a class trainer for our class
            if (info->trainer_type != 0 || info->trainer_class != pBot->GetClass())
                continue;

            // Must be friendly
            if (!IsTrainerFriendly(pBot, info->faction))
                continue;

            return creature;
        }

        return nullptr;
    }

    return trainer;
}

void TrainingStrategy::LearnAvailableSpells(Player* pBot, Creature* trainer)
{
    if (!pBot || !trainer)
        return;

    CreatureInfo const* info = trainer->GetCreatureInfo();
    if (!info)
        return;

    uint32 trainerId = info->trainer_id;
    if (trainerId == 0)
    {
        // Some trainers use their entry as the trainer ID
        trainerId = trainer->GetEntry();
    }

    std::vector<uint32> learnableSpells = GetLearnableSpells(pBot, trainerId);

    if (learnableSpells.empty())
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[TrainingStrategy] Bot %s has no new spells to learn from %s",
                 pBot->GetName(), trainer->GetName());
        return;
    }

    uint32 learnedCount = 0;
    for (uint32 spellId : learnableSpells)
    {
        // Learn the spell (free for now - no gold cost)
        SpellEntry const* spellEntry = sSpellMgr.GetSpellEntry(spellId);
        if (spellEntry)
        {
            // Check if this is a talent spell
            uint32 firstRankId = sSpellMgr.GetFirstSpellInChain(spellId);
            bool isTalent = (firstRankId == spellId && GetTalentSpellPos(firstRankId));

            pBot->LearnSpell(spellId, false, isTalent);
            learnedCount++;

            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[TrainingStrategy] Bot %s learned: %s",
                     pBot->GetName(), spellEntry->SpellName[0].c_str());
        }
    }

    if (learnedCount > 0)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[TrainingStrategy] Bot %s finished learning %u spells from %s",
                 pBot->GetName(), learnedCount, trainer->GetName());
    }
}

bool TrainingStrategy::Update(Player* pBot, uint32 diff)
{
    if (!pBot || !pBot->IsAlive())
    {
        Reset();
        return false;
    }

    switch (m_state)
    {
        case TrainingState::IDLE:
        {
            // Nothing to do - waiting for TriggerTraining() call
            return false;
        }

        case TrainingState::NEEDS_TRAINING:
        {
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[TrainingStrategy] Bot %s (level %u) starting training process",
                     pBot->GetName(), pBot->GetLevel());

            m_state = TrainingState::FINDING_TRAINER;
            return true;
        }

        case TrainingState::FINDING_TRAINER:
        {
            if (!FindNearestTrainer(pBot))
            {
                // No trainer found on this map
                sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[TrainingStrategy] Bot %s could not find any trainer, aborting",
                         pBot->GetName());
                Reset();
                return false;
            }

            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[TrainingStrategy] Bot %s traveling to trainer at (%.1f, %.1f, %.1f)",
                     pBot->GetName(), m_targetTrainer.x, m_targetTrainer.y, m_targetTrainer.z);

            // Start traveling to trainer
            if (m_pMovementMgr)
                m_pMovementMgr->MoveTo(m_targetTrainer.x, m_targetTrainer.y, m_targetTrainer.z, MovementPriority::PRIORITY_NORMAL);
            else
                pBot->GetMotionMaster()->MovePoint(0, m_targetTrainer.x, m_targetTrainer.y, m_targetTrainer.z, MOVE_PATHFINDING | MOVE_RUN_MODE);

            m_stuckTimer = 0;
            m_lastDistanceCheckTime = 0;
            m_lastDistanceToTrainer = FLT_MAX;
            m_state = TrainingState::TRAVELING_TO_TRAINER;
            return true;
        }

        case TrainingState::TRAVELING_TO_TRAINER:
        {
            // Check if we've arrived
            float dist = pBot->GetDistance(m_targetTrainer.x, m_targetTrainer.y, m_targetTrainer.z);

            if (dist <= TRAINER_INTERACT_RANGE)
            {
                // Arrived at trainer
                pBot->GetMotionMaster()->Clear();
                m_state = TrainingState::AT_TRAINER;
                sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[TrainingStrategy] Bot %s arrived at trainer location",
                         pBot->GetName());
                return true;
            }

            // Update stuck detection
            m_stuckTimer += diff;
            m_lastDistanceCheckTime += diff;

            if (m_lastDistanceCheckTime >= DISTANCE_CHECK_INTERVAL)
            {
                // Check if we're making progress
                if (std::abs(dist - m_lastDistanceToTrainer) < 1.0f)
                {
                    // Not making progress, try moving again
                    if (m_pMovementMgr)
                        m_pMovementMgr->MoveTo(m_targetTrainer.x, m_targetTrainer.y, m_targetTrainer.z, MovementPriority::PRIORITY_NORMAL);
                    else
                        pBot->GetMotionMaster()->MovePoint(0, m_targetTrainer.x, m_targetTrainer.y, m_targetTrainer.z, MOVE_PATHFINDING | MOVE_RUN_MODE);
                }
                m_lastDistanceToTrainer = dist;
                m_lastDistanceCheckTime = 0;
            }

            // Check for stuck timeout
            if (m_stuckTimer >= STUCK_TIMEOUT)
            {
                sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[TrainingStrategy] Bot %s got stuck traveling to trainer, aborting",
                         pBot->GetName());
                Reset();
                return false;
            }

            return true; // Still traveling
        }

        case TrainingState::AT_TRAINER:
        {
            Creature* trainer = GetTrainerCreature(pBot);
            if (trainer)
            {
                sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[TrainingStrategy] Bot %s learning spells from %s",
                         pBot->GetName(), trainer->GetName());
                LearnAvailableSpells(pBot, trainer);
            }
            else
            {
                sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[TrainingStrategy] Bot %s: No trainer found nearby!",
                         pBot->GetName());
            }

            m_state = TrainingState::DONE;
            return true;
        }

        case TrainingState::DONE:
        {
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[TrainingStrategy] Bot %s finished training",
                     pBot->GetName());
            Reset();
            return false; // Done, strategy complete
        }
    }

    return false;
}

void TrainingStrategy::OnEnterCombat(Player* pBot)
{
    // Training has highest priority - don't abort when entering combat
    // The bot will fight and then resume traveling to trainer
    if (m_state == TrainingState::TRAVELING_TO_TRAINER)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[TrainingStrategy] Bot %s entered combat while traveling to trainer, will resume after combat",
                 pBot ? pBot->GetName() : "unknown");
        // Don't reset - keep state so we resume after combat
    }
}

void TrainingStrategy::OnLeaveCombat(Player* pBot)
{
    // If we were traveling to trainer, resume movement
    if (m_state == TrainingState::TRAVELING_TO_TRAINER && pBot)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG, "[TrainingStrategy] Bot %s left combat, resuming travel to trainer",
                 pBot->GetName());

        if (m_pMovementMgr)
            m_pMovementMgr->MoveTo(m_targetTrainer.x, m_targetTrainer.y, m_targetTrainer.z, MovementPriority::PRIORITY_NORMAL);
        else
            pBot->GetMotionMaster()->MovePoint(0, m_targetTrainer.x, m_targetTrainer.y, m_targetTrainer.z, MOVE_PATHFINDING | MOVE_RUN_MODE);
    }
}

void TrainingStrategy::Reset()
{
    m_state = TrainingState::IDLE;
    m_targetTrainer = {};
    m_stuckTimer = 0;
    m_lastDistanceCheckTime = 0;
    m_lastDistanceToTrainer = FLT_MAX;
    // Don't reset m_trainingTriggeredForLevel - we want to track what level we've trained for
}
