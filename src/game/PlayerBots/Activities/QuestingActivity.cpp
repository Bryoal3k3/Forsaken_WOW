/*
 * QuestingActivity.cpp
 *
 * Tier 1 Activity: autonomous questing.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "QuestingActivity.h"
#include "PlayerBots/BotMovementManager.h"
#include "Player.h"
#include "Creature.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "Log.h"
#include "MotionMaster.h"
#include "Map.h"
#include "Cell.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "DBCStructure.h"
#include <cmath>
#include <cfloat>

// ============================================================================
// Constructor / Destructor
// ============================================================================

QuestingActivity::QuestingActivity() = default;
QuestingActivity::~QuestingActivity() = default;

// ============================================================================
// IBotActivity Interface
// ============================================================================

bool QuestingActivity::Update(Player* pBot, uint32 diff)
{
    if (!pBot || !pBot->IsAlive())
        return false;

    switch (m_state)
    {
        case QuestActivityState::CHECKING_QUEST_LOG:
            HandleCheckingQuestLog(pBot);
            return true;

        case QuestActivityState::FINDING_QUEST_GIVER:
            HandleFindingQuestGiver(pBot);
            return true;

        case QuestActivityState::TRAVELING_TO_GIVER:
            HandleTravelingToGiver(pBot, diff);
            return true;

        case QuestActivityState::AT_GIVER_PICKING_UP:
            HandleAtGiverPickingUp(pBot);
            return true;

        case QuestActivityState::SELECTING_QUEST:
            HandleSelectingQuest(pBot);
            return true;

        case QuestActivityState::WORKING_ON_QUEST:
            HandleWorkingOnQuest(pBot, diff);
            return true;

        case QuestActivityState::FINDING_TURN_IN:
            HandleFindingTurnIn(pBot);
            return true;

        case QuestActivityState::TRAVELING_TO_TURN_IN:
            HandleTravelingToTurnIn(pBot, diff);
            return true;

        case QuestActivityState::AT_TURN_IN:
            HandleAtTurnIn(pBot);
            return true;

        case QuestActivityState::NO_QUESTS_AVAILABLE:
            return false;  // Signal to caller: switch to grinding
    }

    return false;
}

void QuestingActivity::OnEnterCombat(Player* pBot)
{
    // If traveling, don't abort — resume after combat (same as TrainingStrategy)
    if (m_state == QuestActivityState::TRAVELING_TO_GIVER ||
        m_state == QuestActivityState::TRAVELING_TO_TURN_IN)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
            "[QuestingActivity] %s entered combat while traveling, will resume after",
            pBot ? pBot->GetName() : "unknown");
    }
}

void QuestingActivity::OnLeaveCombat(Player* pBot)
{
    if (!pBot || !m_pMovementMgr)
        return;

    // Resume travel after combat
    if (m_state == QuestActivityState::TRAVELING_TO_GIVER && m_targetGiver)
    {
        m_pMovementMgr->MoveTo(m_targetGiver->x, m_targetGiver->y, m_targetGiver->z,
                                MovementPriority::PRIORITY_NORMAL);
    }
    else if (m_state == QuestActivityState::TRAVELING_TO_TURN_IN && m_targetTurnIn)
    {
        m_pMovementMgr->MoveTo(m_targetTurnIn->x, m_targetTurnIn->y, m_targetTurnIn->z,
                                MovementPriority::PRIORITY_NORMAL);
    }
}

// ============================================================================
// State Handlers
// ============================================================================

void QuestingActivity::HandleCheckingQuestLog(Player* pBot)
{
    // Check if we have any completed quests to turn in first (highest priority)
    uint32 completedQuestId = FindCompletedQuest(pBot);
    if (completedQuestId != 0)
    {
        m_activeQuestId = completedQuestId;
        m_state = QuestActivityState::FINDING_TURN_IN;

        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL,
            "[QuestingActivity] %s has completed quest %u, finding turn-in",
            pBot->GetName(), completedQuestId);
        return;
    }

    // Check if we have incomplete quests to work on
    if (HasIncompleteQuests(pBot))
    {
        m_state = QuestActivityState::SELECTING_QUEST;
        return;
    }

    // No quests at all — find a quest giver
    // Apply search jitter to prevent all bots searching on same tick
    if (!m_searchDelayActive)
    {
        m_searchDelayTimer = urand(0, MAX_SEARCH_JITTER_MS);
        m_searchDelayActive = true;
    }

    if (m_searchDelayTimer > 0)
    {
        m_searchDelayTimer -= std::min(m_searchDelayTimer, (uint32)1000);
        return;
    }

    m_searchDelayActive = false;
    m_state = QuestActivityState::FINDING_QUEST_GIVER;
}

void QuestingActivity::HandleFindingQuestGiver(Player* pBot)
{
    m_targetGiver = BotQuestCache::FindNearestQuestGiver(pBot);

    if (!m_targetGiver)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL,
            "[QuestingActivity] %s could not find any quest giver on map %u",
            pBot->GetName(), pBot->GetMapId());
        m_state = QuestActivityState::NO_QUESTS_AVAILABLE;
        return;
    }

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
        "[QuestingActivity] %s traveling to quest giver (entry %u) at (%.1f, %.1f, %.1f)",
        pBot->GetName(), m_targetGiver->sourceEntry,
        m_targetGiver->x, m_targetGiver->y, m_targetGiver->z);

    // Start travel
    if (m_pMovementMgr)
        m_pMovementMgr->MoveTo(m_targetGiver->x, m_targetGiver->y, m_targetGiver->z,
                                MovementPriority::PRIORITY_NORMAL);

    m_stuckTimer = 0;
    m_lastDistanceCheckTime = 0;
    m_lastDistanceToTarget = FLT_MAX;
    m_state = QuestActivityState::TRAVELING_TO_GIVER;
}

void QuestingActivity::HandleTravelingToGiver(Player* pBot, uint32 diff)
{
    if (!m_targetGiver)
    {
        m_state = QuestActivityState::CHECKING_QUEST_LOG;
        return;
    }

    float dist = pBot->GetDistance(m_targetGiver->x, m_targetGiver->y, m_targetGiver->z);

    if (dist <= NPC_INTERACT_RANGE)
    {
        pBot->GetMotionMaster()->Clear();
        m_state = QuestActivityState::AT_GIVER_PICKING_UP;

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
            "[QuestingActivity] %s arrived at quest giver location",
            pBot->GetName());
        return;
    }

    // Stuck detection
    m_stuckTimer += diff;
    m_lastDistanceCheckTime += diff;

    if (m_lastDistanceCheckTime >= DISTANCE_CHECK_INTERVAL_MS)
    {
        if (std::abs(dist - m_lastDistanceToTarget) < 1.0f)
        {
            // Not making progress, re-issue movement
            if (m_pMovementMgr)
                m_pMovementMgr->MoveTo(m_targetGiver->x, m_targetGiver->y, m_targetGiver->z,
                                        MovementPriority::PRIORITY_NORMAL);
        }
        m_lastDistanceToTarget = dist;
        m_lastDistanceCheckTime = 0;
    }

    if (m_stuckTimer >= STUCK_TIMEOUT_MS)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
            "[QuestingActivity] %s stuck traveling to quest giver, aborting",
            pBot->GetName());
        m_targetGiver = nullptr;
        m_state = QuestActivityState::CHECKING_QUEST_LOG;
    }
}

void QuestingActivity::HandleAtGiverPickingUp(Player* pBot)
{
    // Accept quests from this quest giver and nearby ones (cluster behavior)
    AcceptAvailableQuestsFromCluster(pBot);

    // Move on to selecting a quest to work on
    m_targetGiver = nullptr;
    m_state = QuestActivityState::CHECKING_QUEST_LOG;
}

void QuestingActivity::HandleSelectingQuest(Player* pBot)
{
    // For now (Phase Q2), just check if any quests are complete
    // Phase Q3+ will add objective pursuit here
    uint32 completedQuestId = FindCompletedQuest(pBot);
    if (completedQuestId != 0)
    {
        m_activeQuestId = completedQuestId;
        m_state = QuestActivityState::FINDING_TURN_IN;
        return;
    }

    // Phase Q3+ will handle WORKING_ON_QUEST for incomplete quests
    // For now, if we have incomplete quests but can't work on them yet,
    // go look for more quests (maybe find some we CAN complete)
    if (GetFreeQuestSlots(pBot) > 5)
    {
        // Plenty of room, try to pick up more quests
        m_state = QuestActivityState::FINDING_QUEST_GIVER;
    }
    else
    {
        // Quest log is getting full but we can't complete anything yet
        // Phase Q3 will fix this — for now, signal no work available
        m_state = QuestActivityState::NO_QUESTS_AVAILABLE;
    }
}

void QuestingActivity::HandleWorkingOnQuest(Player* pBot, uint32 diff)
{
    // Phase Q3+ will implement objective pursuit (kill quests, collect, etc.)
    // For now, just check if any quest completed (e.g., auto-complete quests)
    uint32 completedQuestId = FindCompletedQuest(pBot);
    if (completedQuestId != 0)
    {
        m_activeQuestId = completedQuestId;
        m_state = QuestActivityState::FINDING_TURN_IN;
        return;
    }

    // Nothing to do yet — go back to checking
    m_state = QuestActivityState::SELECTING_QUEST;
}

void QuestingActivity::HandleFindingTurnIn(Player* pBot)
{
    if (m_activeQuestId == 0)
    {
        m_state = QuestActivityState::CHECKING_QUEST_LOG;
        return;
    }

    m_targetTurnIn = BotQuestCache::FindNearestTurnIn(pBot, m_activeQuestId);

    if (!m_targetTurnIn)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL,
            "[QuestingActivity] %s could not find turn-in for quest %u on map %u",
            pBot->GetName(), m_activeQuestId, pBot->GetMapId());
        m_activeQuestId = 0;
        m_state = QuestActivityState::CHECKING_QUEST_LOG;
        return;
    }

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
        "[QuestingActivity] %s traveling to turn-in (entry %u) for quest %u at (%.1f, %.1f, %.1f)",
        pBot->GetName(), m_targetTurnIn->targetEntry, m_activeQuestId,
        m_targetTurnIn->x, m_targetTurnIn->y, m_targetTurnIn->z);

    if (m_pMovementMgr)
        m_pMovementMgr->MoveTo(m_targetTurnIn->x, m_targetTurnIn->y, m_targetTurnIn->z,
                                MovementPriority::PRIORITY_NORMAL);

    m_stuckTimer = 0;
    m_lastDistanceCheckTime = 0;
    m_lastDistanceToTarget = FLT_MAX;
    m_state = QuestActivityState::TRAVELING_TO_TURN_IN;
}

void QuestingActivity::HandleTravelingToTurnIn(Player* pBot, uint32 diff)
{
    if (!m_targetTurnIn)
    {
        m_state = QuestActivityState::CHECKING_QUEST_LOG;
        return;
    }

    float dist = pBot->GetDistance(m_targetTurnIn->x, m_targetTurnIn->y, m_targetTurnIn->z);

    if (dist <= NPC_INTERACT_RANGE)
    {
        pBot->GetMotionMaster()->Clear();
        m_state = QuestActivityState::AT_TURN_IN;

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
            "[QuestingActivity] %s arrived at turn-in location",
            pBot->GetName());
        return;
    }

    // Stuck detection (same pattern as travel to giver)
    m_stuckTimer += diff;
    m_lastDistanceCheckTime += diff;

    if (m_lastDistanceCheckTime >= DISTANCE_CHECK_INTERVAL_MS)
    {
        if (std::abs(dist - m_lastDistanceToTarget) < 1.0f)
        {
            if (m_pMovementMgr)
                m_pMovementMgr->MoveTo(m_targetTurnIn->x, m_targetTurnIn->y, m_targetTurnIn->z,
                                        MovementPriority::PRIORITY_NORMAL);
        }
        m_lastDistanceToTarget = dist;
        m_lastDistanceCheckTime = 0;
    }

    if (m_stuckTimer >= STUCK_TIMEOUT_MS)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
            "[QuestingActivity] %s stuck traveling to turn-in, aborting",
            pBot->GetName());
        m_activeQuestId = 0;
        m_targetTurnIn = nullptr;
        m_state = QuestActivityState::CHECKING_QUEST_LOG;
    }
}

void QuestingActivity::HandleAtTurnIn(Player* pBot)
{
    if (m_activeQuestId == 0 || !m_targetTurnIn)
    {
        m_state = QuestActivityState::CHECKING_QUEST_LOG;
        return;
    }

    // Find the turn-in NPC nearby
    Creature* pNPC = FindNearbyQuestNPC(pBot, m_targetTurnIn->targetEntry);
    if (pNPC)
    {
        if (TurnInQuest(pBot, pNPC, m_activeQuestId))
        {
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
                "[QuestingActivity] %s turned in quest %u to %s",
                pBot->GetName(), m_activeQuestId, pNPC->GetName());
        }
        else
        {
            sLog.Out(LOG_BASIC, LOG_LVL_DETAIL,
                "[QuestingActivity] %s failed to turn in quest %u to %s",
                pBot->GetName(), m_activeQuestId, pNPC->GetName());
        }
    }
    else
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL,
            "[QuestingActivity] %s: No turn-in NPC found nearby (entry %u)",
            pBot->GetName(), m_targetTurnIn->targetEntry);
    }

    m_activeQuestId = 0;
    m_targetTurnIn = nullptr;
    m_state = QuestActivityState::CHECKING_QUEST_LOG;
}

// ============================================================================
// Quest Log Helpers
// ============================================================================

// Helper: check if quest is in the active quest log (not just status map)
// Replicates Player::FindQuestSlot which is private
static bool IsQuestInLog(Player* pBot, uint32 questId)
{
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 id = pBot->GetUInt32Value(PLAYER_QUEST_LOG_1_1 + slot * MAX_QUEST_OFFSET + QUEST_ID_OFFSET);
        if (id == questId)
            return true;
    }
    return false;
}

uint32 QuestingActivity::GetQuestCount(Player* pBot) const
{
    uint32 count = 0;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        if (pBot->GetUInt32Value(PLAYER_QUEST_LOG_1_1 + slot * MAX_QUEST_OFFSET + QUEST_ID_OFFSET) != 0)
            count++;
    }
    return count;
}

uint32 QuestingActivity::GetFreeQuestSlots(Player* pBot) const
{
    return MAX_QUEST_LOG_SIZE - GetQuestCount(pBot);
}

uint32 QuestingActivity::FindCompletedQuest(Player* pBot) const
{
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = pBot->GetUInt32Value(PLAYER_QUEST_LOG_1_1 + slot * MAX_QUEST_OFFSET + QUEST_ID_OFFSET);
        if (questId == 0)
            continue;

        if (pBot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE)
            return questId;
    }
    return 0;
}

bool QuestingActivity::HasIncompleteQuests(Player* pBot) const
{
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = pBot->GetUInt32Value(PLAYER_QUEST_LOG_1_1 + slot * MAX_QUEST_OFFSET + QUEST_ID_OFFSET);
        if (questId == 0)
            continue;

        if (pBot->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE)
            return true;
    }
    return false;
}

// ============================================================================
// NPC Interaction
// ============================================================================

Creature* QuestingActivity::FindNearbyQuestNPC(Player* pBot, uint32 entry, float range) const
{
    if (!pBot)
        return nullptr;

    std::list<Creature*> creatures;
    MaNGOS::AnyUnitInObjectRangeCheck check(pBot, range);
    MaNGOS::CreatureListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> searcher(creatures, check);
    Cell::VisitGridObjects(pBot, searcher, range);

    float closestDist = range;
    Creature* found = nullptr;

    for (Creature* pCreature : creatures)
    {
        if (!pCreature || !pCreature->IsAlive())
            continue;

        if (pCreature->GetEntry() != entry)
            continue;

        float dist = pBot->GetDistance(pCreature);
        if (dist < closestDist)
        {
            closestDist = dist;
            found = pCreature;
        }
    }

    return found;
}

void QuestingActivity::AcceptAvailableQuests(Player* pBot, Creature* pNPC)
{
    if (!pBot || !pNPC)
        return;

    uint32 acceptedCount = 0;

    // Iterate all quests this NPC offers
    QuestRelationsMapBounds bounds = sObjectMgr.GetCreatureQuestRelationsMapBounds(pNPC->GetEntry());
    for (auto it = bounds.first; it != bounds.second; ++it)
    {
        uint32 questId = it->second;

        Quest const* pQuest = sObjectMgr.GetQuestTemplate(questId);
        if (!pQuest)
            continue;

        // Skip if quest log is nearly full (keep some slots free)
        if (GetFreeQuestSlots(pBot) <= 4)
            break;

        // CanTakeQuest handles level, class, race, chain prerequisites
        if (!pBot->CanTakeQuest(pQuest, false))
            continue;

        // Check if bot can add it (not already in log, etc.)
        if (!pBot->CanAddQuest(pQuest, false))
            continue;

        // Accept the quest
        pBot->AddQuest(pQuest, pNPC);

        // If quest auto-completes on accept, mark it
        if (pBot->CanCompleteQuest(questId))
            pBot->CompleteQuest(questId);

        acceptedCount++;

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
            "[QuestingActivity] %s accepted quest: [%u] %s from %s",
            pBot->GetName(), questId, pQuest->GetTitle().c_str(), pNPC->GetName());
    }

    if (acceptedCount > 0)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
            "[QuestingActivity] %s accepted %u quests from %s",
            pBot->GetName(), acceptedCount, pNPC->GetName());
    }
}

void QuestingActivity::AcceptAvailableQuestsFromCluster(Player* pBot)
{
    if (!pBot || !m_targetGiver)
        return;

    // First, accept from the primary target
    if (!m_targetGiver->isGameObject)
    {
        Creature* pNPC = FindNearbyQuestNPC(pBot, m_targetGiver->sourceEntry);
        if (pNPC)
            AcceptAvailableQuests(pBot, pNPC);
    }

    // Then check for other quest givers in the cluster radius
    auto cluster = BotQuestCache::FindQuestGiverCluster(
        m_targetGiver->mapId, m_targetGiver->x, m_targetGiver->y, m_targetGiver->z,
        CLUSTER_RADIUS);

    for (QuestGiverInfo const* pGiver : cluster)
    {
        // Skip the one we already processed
        if (pGiver == m_targetGiver)
            continue;

        // Skip gameobjects for now (Phase Q5 adds object interaction)
        if (pGiver->isGameObject)
            continue;

        // Skip if quest log is nearly full
        if (GetFreeQuestSlots(pBot) <= 4)
            break;

        // Skip hostile NPCs
        if (!BotQuestCache::IsFactionFriendly(pBot, pGiver->factionTemplateId))
            continue;

        Creature* pNPC = FindNearbyQuestNPC(pBot, pGiver->sourceEntry, CLUSTER_RADIUS);
        if (pNPC)
            AcceptAvailableQuests(pBot, pNPC);
    }
}

bool QuestingActivity::TurnInQuest(Player* pBot, Creature* pNPC, uint32 questId)
{
    if (!pBot || !pNPC)
        return false;

    Quest const* pQuest = sObjectMgr.GetQuestTemplate(questId);
    if (!pQuest)
        return false;

    // Verify quest is actually complete
    if (pBot->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE)
        return false;

    // Check if we can reward
    if (!pBot->CanRewardQuest(pQuest, true))
        return false;

    // Select reward (simple class-based heuristic for now)
    // Phase Q11 will add BotStatWeights for better selection
    uint32 rewardChoice = 0;
    uint32 rewardCount = 0;
    for (uint32 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
    {
        if (pQuest->RewChoiceItemId[i] != 0)
            rewardCount++;
    }

    if (rewardCount > 0)
    {
        // For now, just pick the first available reward
        // Phase Q11 will score items properly
        for (uint32 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
        {
            if (pQuest->RewChoiceItemId[i] != 0)
            {
                rewardChoice = i;
                break;
            }
        }
    }

    // Turn in the quest
    pBot->RewardQuest(pQuest, rewardChoice, pNPC, true);

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
        "[QuestingActivity] %s completed quest: [%u] %s",
        pBot->GetName(), questId, pQuest->GetTitle().c_str());

    return true;
}

