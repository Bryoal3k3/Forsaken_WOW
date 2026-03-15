/*
 * QuestingActivity.h
 *
 * Tier 1 Activity: autonomous questing.
 * Coordinates quest discovery, acceptance, objective completion, and turn-in.
 *
 * State Machine:
 *   CHECKING_QUEST_LOG -> FINDING_QUEST_GIVER -> TRAVELING_TO_GIVER ->
 *   AT_GIVER_PICKING_UP -> SELECTING_QUEST -> WORKING_ON_QUEST ->
 *   FINDING_TURN_IN -> TRAVELING_TO_TURN_IN -> AT_TURN_IN -> (loop)
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_QUESTINGACTIVITY_H
#define MANGOS_QUESTINGACTIVITY_H

#include "PlayerBots/IBotActivity.h"
#include "PlayerBots/Utilities/BotQuestCache.h"
#include <memory>

class GrindingStrategy;
class BotCombatMgr;
class BotMovementManager;
class Creature;
class GameObject;

enum class QuestActivityState
{
    // Quest acquisition
    CHECKING_QUEST_LOG,       // Evaluate quest log: have quests? need to abandon any?
    FINDING_QUEST_GIVER,      // Cache lookup for nearest giver (locality-first)
    TRAVELING_TO_GIVER,       // Walking to quest giver
    AT_GIVER_PICKING_UP,      // At giver, accepting quests from this + nearby givers

    // Quest execution
    SELECTING_QUEST,          // Pick which quest to work on
    WORKING_ON_QUEST,         // Pursuing objectives (Phase Q3+)

    // Quest turn-in
    FINDING_TURN_IN,          // Cache lookup for turn-in NPC/object
    TRAVELING_TO_TURN_IN,     // Walking to turn-in target
    AT_TURN_IN,               // Turning in quest, selecting reward

    // Fallback
    NO_QUESTS_AVAILABLE,      // No quests found - signal strategy switch
};

class QuestingActivity : public IBotActivity
{
public:
    QuestingActivity();
    ~QuestingActivity() override;

    // IBotActivity interface
    bool Update(Player* pBot, uint32 diff) override;
    void OnEnterCombat(Player* pBot) override;
    void OnLeaveCombat(Player* pBot) override;
    char const* GetName() const override { return "Questing"; }

    // Behavior permissions
    bool AllowsLooting() const override { return true; }
    bool AllowsVendoring() const override { return true; }
    bool AllowsTraining() const override { return true; }

    // Wiring (called by RandomBotAI during initialization)
    void SetCombatMgr(BotCombatMgr* pMgr) { m_pCombatMgr = pMgr; }
    void SetMovementManager(BotMovementManager* pMgr) { m_pMovementMgr = pMgr; }

    // Query
    QuestActivityState GetState() const { return m_state; }
    bool WantsActivitySwitch() const { return m_state == QuestActivityState::NO_QUESTS_AVAILABLE; }

    // Access internal grinding behavior (for RandomBotAI attacker handling)
    GrindingStrategy* GetGrindingStrategy() { return nullptr; }  // Q3+ will add this

private:
    // ---- State handlers ----
    void HandleCheckingQuestLog(Player* pBot);
    void HandleFindingQuestGiver(Player* pBot);
    void HandleTravelingToGiver(Player* pBot, uint32 diff);
    void HandleAtGiverPickingUp(Player* pBot);
    void HandleSelectingQuest(Player* pBot);
    void HandleWorkingOnQuest(Player* pBot, uint32 diff);
    void HandleFindingTurnIn(Player* pBot);
    void HandleTravelingToTurnIn(Player* pBot, uint32 diff);
    void HandleAtTurnIn(Player* pBot);

    // ---- Quest log helpers ----
    uint32 GetQuestCount(Player* pBot) const;
    uint32 GetFreeQuestSlots(Player* pBot) const;
    uint32 FindCompletedQuest(Player* pBot) const;
    bool HasIncompleteQuests(Player* pBot) const;

    // ---- NPC interaction ----
    Creature* FindNearbyQuestNPC(Player* pBot, uint32 entry, float range = 30.0f) const;
    void AcceptAvailableQuests(Player* pBot, Creature* pNPC);
    void AcceptAvailableQuestsFromCluster(Player* pBot);
    bool TurnInQuest(Player* pBot, Creature* pNPC, uint32 questId);

    // ---- State ----
    QuestActivityState m_state = QuestActivityState::CHECKING_QUEST_LOG;

    // References
    BotCombatMgr* m_pCombatMgr = nullptr;
    BotMovementManager* m_pMovementMgr = nullptr;

    // Current targets
    QuestGiverInfo const* m_targetGiver = nullptr;
    QuestTurnInInfo const* m_targetTurnIn = nullptr;
    uint32 m_activeQuestId = 0;         // Quest currently being turned in

    // Travel stuck detection (same pattern as VendoringStrategy/TrainingStrategy)
    uint32 m_stuckTimer = 0;
    uint32 m_lastDistanceCheckTime = 0;
    float m_lastDistanceToTarget = 0.0f;

    // Search jitter (prevents all bots from searching on same tick)
    uint32 m_searchDelayTimer = 0;
    bool m_searchDelayActive = false;

    // Constants
    static constexpr float NPC_INTERACT_RANGE = 5.0f;
    static constexpr uint32 STUCK_TIMEOUT_MS = 300000;          // 5 min (long travel)
    static constexpr uint32 DISTANCE_CHECK_INTERVAL_MS = 3000;  // Check every 3 sec
    static constexpr uint32 MAX_SEARCH_JITTER_MS = 5000;        // 0-5 sec random delay
    static constexpr float CLUSTER_RADIUS = 100.0f;             // Nearby quest givers
};

#endif // MANGOS_QUESTINGACTIVITY_H
