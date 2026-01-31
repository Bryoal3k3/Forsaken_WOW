/*
 * TrainingStrategy.h
 *
 * Strategy for handling bot spell training - traveling to class trainers
 * and learning available spells at even levels (2, 4, 6, 8, etc.).
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_TRAININGSTRATEGY_H
#define MANGOS_TRAININGSTRATEGY_H

#include "IBotStrategy.h"
#include <vector>
#include <mutex>

class Player;
class Creature;
class BotMovementManager;
class CombatBotBaseAI;

// Cached trainer location data
struct TrainerLocation
{
    float x;
    float y;
    float z;
    uint32 mapId;
    uint32 creatureEntry;
    uint32 creatureGuid;
    uint8 trainerClass;         // 1=Warrior, 2=Paladin, 3=Hunter, etc.
    uint32 trainerId;           // Links to npc_trainer_template for spell list
    uint32 factionTemplateId;   // For faction checking
};

class TrainingStrategy : public IBotStrategy
{
public:
    TrainingStrategy();

    // IBotStrategy interface
    bool Update(Player* pBot, uint32 diff) override;
    void OnEnterCombat(Player* pBot) override;
    void OnLeaveCombat(Player* pBot) override;
    char const* GetName() const override { return "TrainingStrategy"; }

    // Check if bot needs training (has unlearned spells available)
    bool NeedsTraining(Player* pBot) const;

    // Trigger training (called when bot levels to an even level)
    void TriggerTraining();

    // Check if currently in training process
    bool IsActive() const { return m_state != TrainingState::IDLE; }

    // Reset state
    void Reset();

    // Set movement manager (called by RandomBotAI after construction)
    void SetMovementManager(BotMovementManager* pMoveMgr) { m_pMovementMgr = pMoveMgr; }

    // Set AI reference (for refreshing spell cache after training)
    void SetAI(CombatBotBaseAI* pAI) { m_pAI = pAI; }

    // Pre-build trainer cache (call during server startup)
    static void BuildTrainerCache();

private:
    // AI reference (for refreshing spell cache after training)
    CombatBotBaseAI* m_pAI = nullptr;

    // Movement manager (set by RandomBotAI, centralized movement coordination)
    BotMovementManager* m_pMovementMgr = nullptr;

    enum class TrainingState
    {
        IDLE,                   // Not training
        NEEDS_TRAINING,         // Training triggered, waiting to start
        FINDING_TRAINER,        // Looking for nearest trainer
        TRAVELING_TO_TRAINER,   // Moving toward trainer
        AT_TRAINER,             // Close enough, learning spells
        DONE                    // Finished training, will reset to IDLE
    };

    TrainingState m_state;
    TrainerLocation m_targetTrainer;
    uint32 m_stuckTimer;
    uint32 m_lastDistanceCheckTime;
    float m_lastDistanceToTrainer;

    // Track the level we triggered training for (to avoid re-triggering)
    uint32 m_trainingTriggeredForLevel;

    // Trainer cache (static, shared across all bots)
    static std::vector<TrainerLocation> s_trainerCache;
    static bool s_cacheBuilt;
    static std::mutex s_cacheMutex;

    // Find nearest friendly trainer for this bot's class
    bool FindNearestTrainer(Player* pBot);

    // Check if trainer is friendly to bot's faction
    static bool IsTrainerFriendly(Player* pBot, uint32 factionTemplateId);

    // Learn all available spells from the trainer
    void LearnAvailableSpells(Player* pBot, Creature* trainer);

    // Get the trainer creature when we arrive
    Creature* GetTrainerCreature(Player* pBot) const;

    // Get list of spells bot can learn from a trainer at current level
    std::vector<uint32> GetLearnableSpells(Player* pBot, uint32 trainerId) const;

    // Constants
    static constexpr float TRAINER_INTERACT_RANGE = 5.0f;
    static constexpr uint32 STUCK_TIMEOUT = 300000;         // 5 minutes (long travel distances)
    static constexpr uint32 DISTANCE_CHECK_INTERVAL = 3000; // Check every 3 sec
};

#endif // MANGOS_TRAININGSTRATEGY_H
