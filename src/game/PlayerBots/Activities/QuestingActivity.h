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
#include "PlayerBots/Strategies/GrindingStrategy.h"
#include <memory>

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
    void SetCombatMgr(BotCombatMgr* pMgr);
    void SetMovementManager(BotMovementManager* pMgr);

    // Query
    QuestActivityState GetState() const { return m_state; }
    bool WantsActivitySwitch() const { return m_state == QuestActivityState::NO_QUESTS_AVAILABLE; }

    // Access internal grinding behavior (for RandomBotAI attacker handling)
    GrindingStrategy* GetGrindingStrategy() { return m_grindingHelper.get(); }
    GrindingStrategy const* GetGrindingStrategy() const { return m_grindingHelper.get(); }

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
    void AcceptAvailableQuests(Player* pBot, Creature* pNPC, uint32& acceptedCount);
    uint32 AcceptAvailableQuestsFromCluster(Player* pBot);
    bool TurnInQuest(Player* pBot, Creature* pNPC, uint32 questId);

    // ---- Quest objective helpers ----
    // Build combined creature target list from ALL active quest kill + collect objectives
    std::vector<uint32> BuildKillTargetList(Player* pBot) const;
    // Build list of gameobject entries needed for quest objectives (ReqCreatureOrGOId < 0)
    std::vector<uint32> BuildGameObjectTargetList(Player* pBot) const;
    // Build list of gameobject entries that contain needed quest items (ReqItemId from GO loot)
    std::vector<uint32> BuildItemGameObjectList(Player* pBot) const;
    // Check all quests for completion and mark them ready for turn-in
    void UpdateQuestCompletion(Player* pBot);
    // Abandon grey and stale quests to free log slots
    void ManageQuestLog(Player* pBot);
    // Try to interact with nearby quest gameobjects
    bool TryInteractWithQuestObjects(Player* pBot);
    // Find an exploration quest the bot can work on (returns questId, 0 if none)
    uint32 FindExplorationQuest(Player* pBot) const;

    // ---- State ----
    QuestActivityState m_state = QuestActivityState::CHECKING_QUEST_LOG;

    // References
    BotCombatMgr* m_pCombatMgr = nullptr;
    BotMovementManager* m_pMovementMgr = nullptr;

    // Internal GrindingStrategy for kill sub-tasks
    std::unique_ptr<GrindingStrategy> m_grindingHelper;

    // Current targets
    QuestGiverInfo const* m_targetGiver = nullptr;
    QuestTurnInInfo const* m_targetTurnIn = nullptr;
    uint32 m_activeQuestId = 0;         // Quest currently being turned in

    // Kill/collect quest working state
    float m_mobAreaX = 0.0f, m_mobAreaY = 0.0f, m_mobAreaZ = 0.0f;
    bool m_travelingToMobArea = false;
    uint32 m_noMobsAtAreaTimer = 0;    // How long we've been scanning with no targets
    static constexpr uint32 NO_MOBS_RELOCATE_MS = 10000;  // Relocate after 10 sec of no mobs

    // Gameobject quest working state
    float m_goTargetX = 0.0f, m_goTargetY = 0.0f, m_goTargetZ = 0.0f;
    uint32 m_goTargetEntry = 0;
    bool m_travelingToGameObject = false;
    uint32 m_goNoProgressTimer = 0;        // How long at a GO spawn with no item progress
    uint32 m_lastGoItemCount = 0;          // Track item count for progress detection
    static constexpr uint32 GO_RELOCATE_MS = 10000;  // 10 sec without progress → try different spawn
    static constexpr float GO_RELOCATE_MIN_DIST = 50.0f;  // Skip spawns within this range when relocating

    // Exploration quest working state
    float m_exploreTargetX = 0.0f, m_exploreTargetY = 0.0f, m_exploreTargetZ = 0.0f;
    uint32 m_exploreQuestId = 0;
    bool m_travelingToExploreTarget = false;

    // Track last known kill counts for progress logging (questId -> total kills)
    std::unordered_map<uint32, uint32> m_lastKnownKillCounts;

    // Soft timeout: track last progress time per quest (questId -> timestamp ms)
    std::unordered_map<uint32, uint32> m_questLastProgressTime;
    static constexpr uint32 QUEST_SOFT_TIMEOUT_MS = 900000;  // 15 minutes

    // Track failed gameobject interactions (goEntry -> tick when last tried)
    std::unordered_map<uint32, uint32> m_goInteractCooldowns;
    static constexpr uint32 GO_INTERACT_COOLDOWN_MS = 10000;  // 10 sec cooldown between interact attempts

    // Travel stuck detection (same pattern as VendoringStrategy/TrainingStrategy)
    uint32 m_stuckTimer = 0;
    uint32 m_lastDistanceCheckTime = 0;
    float m_lastDistanceToTarget = 0.0f;

    // Search jitter (prevents all bots from searching on same tick)
    uint32 m_searchDelayTimer = 0;
    bool m_searchDelayActive = false;

    // NO_QUESTS_AVAILABLE re-check timer
    uint32 m_noQuestsTimer = 0;
    static constexpr uint32 NO_QUESTS_RECHECK_MS = 30000;  // Re-check every 30 sec

    // Exhausted quest givers — visited but offered nothing. Entries are skipped
    // in subsequent searches. Cleared on level-up or after cooldown expires.
    std::unordered_set<uint32> m_exhaustedGiverEntries;
    uint32 m_lastKnownLevel = 0;  // Detect level-ups to clear exhausted set
    static constexpr uint32 GIVER_EXHAUSTED_COOLDOWN_MS = 300000;  // 5 min
    uint32 m_exhaustedGiverTimestamp = 0;  // When the first giver was exhausted

    // Constants
    static constexpr float NPC_INTERACT_RANGE = 5.0f;
    static constexpr uint32 STUCK_TIMEOUT_MS = 300000;          // 5 min (long travel)
    static constexpr uint32 DISTANCE_CHECK_INTERVAL_MS = 3000;  // Check every 3 sec
    static constexpr uint32 MAX_SEARCH_JITTER_MS = 5000;        // 0-5 sec random delay
    static constexpr float CLUSTER_RADIUS = 100.0f;             // Nearby quest givers
};

#endif // MANGOS_QUESTINGACTIVITY_H
