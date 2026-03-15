/*
 * QuestingActivity.cpp
 *
 * Tier 1 Activity: autonomous questing.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#include "QuestingActivity.h"
#include "PlayerBots/Utilities/BotObjectInteraction.h"
#include "PlayerBots/BotMovementManager.h"
#include "Player.h"
#include "Creature.h"
#include "GameObject.h"
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
#include "ObjectDefines.h"
#include "Formulas.h"
#include "ItemPrototype.h"
#include <cmath>
#include <cfloat>

// ============================================================================
// Constructor / Destructor
// ============================================================================

QuestingActivity::QuestingActivity()
    : m_grindingHelper(std::make_unique<GrindingStrategy>())
{
}

QuestingActivity::~QuestingActivity() = default;

void QuestingActivity::SetCombatMgr(BotCombatMgr* pMgr)
{
    m_pCombatMgr = pMgr;
    if (m_grindingHelper)
        m_grindingHelper->SetCombatMgr(pMgr);
}

void QuestingActivity::SetMovementManager(BotMovementManager* pMgr)
{
    m_pMovementMgr = pMgr;
    if (m_grindingHelper)
        m_grindingHelper->SetMovementManager(pMgr);
}

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
        {
            // Periodically re-check in case quests became available (leveled up, etc.)
            m_noQuestsTimer += diff;
            if (m_noQuestsTimer >= NO_QUESTS_RECHECK_MS)
            {
                m_noQuestsTimer = 0;
                m_state = QuestActivityState::CHECKING_QUEST_LOG;
                return true;
            }
            return false;  // Idle — let bot do nothing (or grinding if wired up)
        }
    }

    return false;
}

void QuestingActivity::OnEnterCombat(Player* pBot)
{
    if (m_state == QuestActivityState::WORKING_ON_QUEST && m_grindingHelper)
        m_grindingHelper->OnEnterCombat(pBot);

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
    if (m_state == QuestActivityState::WORKING_ON_QUEST && m_grindingHelper)
        m_grindingHelper->OnLeaveCombat(pBot);

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
    // Manage quest log first — abandon grey/stale quests
    ManageQuestLog(pBot);

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
    // First: check quest completion and mark any completed quests
    UpdateQuestCompletion(pBot);

    // Check if any quests are ready to turn in
    uint32 completedQuestId = FindCompletedQuest(pBot);
    if (completedQuestId != 0)
    {
        m_activeQuestId = completedQuestId;
        m_grindingHelper->ClearQuestTargetFilter();
        m_state = QuestActivityState::FINDING_TURN_IN;
        return;
    }

    // Build kill target list from ALL active quest objectives
    std::vector<uint32> killTargets = BuildKillTargetList(pBot);

    if (!killTargets.empty())
    {
        // Set the filter on our grinding helper
        m_grindingHelper->SetQuestTargetFilter(killTargets);
        m_travelingToMobArea = false;

        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL,
            "[QuestingActivity] %s working on kill objectives (%zu target entries)",
            pBot->GetName(), killTargets.size());

        m_state = QuestActivityState::WORKING_ON_QUEST;
        return;
    }

    // No kill objectives — check for gameobject objectives
    std::vector<uint32> goTargets = BuildGameObjectTargetList(pBot);
    if (!goTargets.empty())
    {
        m_travelingToMobArea = false;
        m_state = QuestActivityState::WORKING_ON_QUEST;
        return;
    }

    // Check for items that come from gameobjects (e.g., Cactus Apples)
    std::vector<uint32> itemGoTargets = BuildItemGameObjectList(pBot);
    if (!itemGoTargets.empty())
    {
        m_travelingToMobArea = false;
        m_travelingToGameObject = false;

        // Determine minimum search distance — if we've been stuck, skip nearby spawns
        float minDist = (m_goNoProgressTimer >= GO_RELOCATE_MS) ? GO_RELOCATE_MIN_DIST : 0.0f;

        float bestDist = FLT_MAX;
        float bestX = 0, bestY = 0, bestZ = 0;
        uint32 bestEntry = 0;

        for (uint32 goEntry : itemGoTargets)
        {
            float spawnX, spawnY, spawnZ;
            if (BotQuestCache::FindGameObjectSpawnLocation(goEntry, pBot->GetMapId(),
                    pBot->GetPositionX(), pBot->GetPositionY(),
                    spawnX, spawnY, spawnZ))
            {
                float dx = spawnX - pBot->GetPositionX();
                float dy = spawnY - pBot->GetPositionY();
                float dist = std::sqrt(dx * dx + dy * dy);

                if (dist < minDist)
                    continue;  // Skip nearby spawns when relocating

                if (dist < bestDist)
                {
                    bestDist = dist;
                    bestX = spawnX;
                    bestY = spawnY;
                    bestZ = spawnZ;
                    bestEntry = goEntry;
                }
            }
        }

        if (bestEntry != 0)
        {
            m_goTargetX = bestX;
            m_goTargetY = bestY;
            m_goTargetZ = bestZ;
            m_goTargetEntry = bestEntry;
            m_travelingToGameObject = true;
            m_stuckTimer = 0;
            m_lastDistanceCheckTime = 0;
            m_lastDistanceToTarget = FLT_MAX;

            // Snapshot current item count for progress tracking
            m_lastGoItemCount = 0;
            for (int i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
            {
                Quest const* pQ = nullptr;
                for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
                {
                    uint32 qId = pBot->GetUInt32Value(PLAYER_QUEST_LOG_1_1 + slot * MAX_QUEST_OFFSET + QUEST_ID_OFFSET);
                    if (qId == 0) continue;
                    Quest const* q = sObjectMgr.GetQuestTemplate(qId);
                    if (q)
                    {
                        for (int j = 0; j < QUEST_ITEM_OBJECTIVES_COUNT; ++j)
                        {
                            if (q->ReqItemId[j] != 0)
                                m_lastGoItemCount += pBot->GetItemCount(q->ReqItemId[j]);
                        }
                    }
                }
                break;  // Only need to count once
            }

            if (m_goNoProgressTimer >= GO_RELOCATE_MS)
            {
                sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
                    "[QuestingActivity] %s relocating to different GO spawn (entry %u) at (%.1f, %.1f, %.1f) dist %.0f",
                    pBot->GetName(), bestEntry, bestX, bestY, bestZ, bestDist);
                m_goNoProgressTimer = 0;
            }
            else
            {
                sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
                    "[QuestingActivity] %s traveling to loot gameobject (entry %u) at (%.1f, %.1f, %.1f)",
                    pBot->GetName(), bestEntry, bestX, bestY, bestZ);
            }

            if (m_pMovementMgr)
                m_pMovementMgr->MoveTo(m_goTargetX, m_goTargetY, m_goTargetZ,
                                        MovementPriority::PRIORITY_NORMAL);

            m_state = QuestActivityState::WORKING_ON_QUEST;
            return;
        }
        else if (m_goNoProgressTimer >= GO_RELOCATE_MS)
        {
            // Tried relocating but no other spawn found — reset and try closest again
            m_goNoProgressTimer = 0;
        }
    }

    // Check for exploration quests
    uint32 exploreQuestId = FindExplorationQuest(pBot);
    if (exploreQuestId != 0)
    {
        if (BotQuestCache::FindQuestAreaTrigger(exploreQuestId, pBot->GetMapId(),
                m_exploreTargetX, m_exploreTargetY, m_exploreTargetZ))
        {
            m_exploreQuestId = exploreQuestId;
            m_travelingToExploreTarget = true;
            m_travelingToMobArea = false;
            m_travelingToGameObject = false;
            m_stuckTimer = 0;
            m_lastDistanceCheckTime = 0;
            m_lastDistanceToTarget = FLT_MAX;

            Quest const* pQuest = sObjectMgr.GetQuestTemplate(exploreQuestId);
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
                "[QuestingActivity] %s traveling to explore area for [%u] %s at (%.1f, %.1f, %.1f)",
                pBot->GetName(), exploreQuestId,
                pQuest ? pQuest->GetTitle().c_str() : "unknown",
                m_exploreTargetX, m_exploreTargetY, m_exploreTargetZ);

            if (m_pMovementMgr)
                m_pMovementMgr->MoveTo(m_exploreTargetX, m_exploreTargetY, m_exploreTargetZ,
                                        MovementPriority::PRIORITY_NORMAL);

            m_state = QuestActivityState::WORKING_ON_QUEST;
            return;
        }
    }

    // No actionable objectives at all — if bot has no quests, try finding a quest giver
    // If bot HAS quests but can't work on them, fall back to grinding (don't bounce)
    if (!HasIncompleteQuests(pBot) && GetFreeQuestSlots(pBot) > 5)
    {
        m_state = QuestActivityState::FINDING_QUEST_GIVER;
    }
    else
    {
        // Have quests but none are actionable (need quest types we don't support yet)
        // Fall back to grinding instead of bouncing between quest givers
        m_state = QuestActivityState::NO_QUESTS_AVAILABLE;
    }
}

void QuestingActivity::HandleWorkingOnQuest(Player* pBot, uint32 diff)
{
    // Check quest completion
    UpdateQuestCompletion(pBot);

    uint32 completedQuestId = FindCompletedQuest(pBot);
    if (completedQuestId != 0)
    {
        m_activeQuestId = completedQuestId;
        m_grindingHelper->ClearQuestTargetFilter();
        m_travelingToMobArea = false;
        m_state = QuestActivityState::FINDING_TURN_IN;

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
            "[QuestingActivity] %s quest %u objectives complete, finding turn-in",
            pBot->GetName(), completedQuestId);
        return;
    }

    // Try interacting with nearby quest gameobjects first (cheap check)
    if (TryInteractWithQuestObjects(pBot))
        return;

    // Handle gameobject travel if active
    if (m_travelingToGameObject)
    {
        float dist = pBot->GetDistance(m_goTargetX, m_goTargetY, m_goTargetZ);
        if (dist <= INTERACTION_DISTANCE + 2.0f)
        {
            // Arrived at gameobject location — try to interact/loot
            m_travelingToGameObject = false;
            GameObject* pGo = BotObjectInteraction::FindNearbyObject(pBot, m_goTargetEntry, 15.0f);
            if (pGo)
            {
                BotObjectInteraction::LootObject(pBot, pGo);
                sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
                    "[QuestingActivity] %s interacted with quest object %s (entry %u)",
                    pBot->GetName(), pGo->GetGOInfo()->name.c_str(), m_goTargetEntry);
            }
            else
            {
                // GO not here (despawned) — force relocate to a different spawn immediately
                sLog.Out(LOG_BASIC, LOG_LVL_DETAIL,
                    "[QuestingActivity] %s: GO entry %u not found at destination, relocating",
                    pBot->GetName(), m_goTargetEntry);
                m_goNoProgressTimer = GO_RELOCATE_MS;  // Trigger relocate in SelectingQuest
                m_state = QuestActivityState::SELECTING_QUEST;
            }
            return;
        }

        // Stuck detection
        m_stuckTimer += diff;
        m_lastDistanceCheckTime += diff;
        if (m_lastDistanceCheckTime >= DISTANCE_CHECK_INTERVAL_MS)
        {
            if (std::abs(dist - m_lastDistanceToTarget) < 1.0f)
            {
                if (m_pMovementMgr)
                    m_pMovementMgr->MoveTo(m_goTargetX, m_goTargetY, m_goTargetZ,
                                            MovementPriority::PRIORITY_NORMAL);
            }
            m_lastDistanceToTarget = dist;
            m_lastDistanceCheckTime = 0;
        }
        if (m_stuckTimer >= STUCK_TIMEOUT_MS)
        {
            m_travelingToGameObject = false;
            m_state = QuestActivityState::SELECTING_QUEST;
        }
        return;
    }

    // Handle exploration quest travel
    if (m_travelingToExploreTarget && m_exploreQuestId != 0)
    {
        // Check if quest completed (areatrigger fires automatically on arrival)
        if (pBot->GetQuestStatus(m_exploreQuestId) == QUEST_STATUS_COMPLETE)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
                "[QuestingActivity] %s exploration quest %u complete!",
                pBot->GetName(), m_exploreQuestId);
            m_travelingToExploreTarget = false;
            m_exploreQuestId = 0;
            m_state = QuestActivityState::CHECKING_QUEST_LOG;
            return;
        }

        float dist = pBot->GetDistance(m_exploreTargetX, m_exploreTargetY, m_exploreTargetZ);
        if (dist <= 5.0f)
        {
            // Arrived but quest not complete yet — areatrigger might have a larger radius
            // Wait a tick, completion should fire from the server side
            m_travelingToExploreTarget = false;
            m_exploreQuestId = 0;
            return;
        }

        // Stuck detection
        m_stuckTimer += diff;
        m_lastDistanceCheckTime += diff;
        if (m_lastDistanceCheckTime >= DISTANCE_CHECK_INTERVAL_MS)
        {
            if (std::abs(dist - m_lastDistanceToTarget) < 1.0f)
            {
                if (m_pMovementMgr)
                    m_pMovementMgr->MoveTo(m_exploreTargetX, m_exploreTargetY, m_exploreTargetZ,
                                            MovementPriority::PRIORITY_NORMAL);
            }
            m_lastDistanceToTarget = dist;
            m_lastDistanceCheckTime = 0;
        }
        if (m_stuckTimer >= STUCK_TIMEOUT_MS)
        {
            m_travelingToExploreTarget = false;
            m_exploreQuestId = 0;
            m_state = QuestActivityState::SELECTING_QUEST;
        }
        return;
    }

    // Refresh kill targets (in case a quest objective was completed)
    std::vector<uint32> killTargets = BuildKillTargetList(pBot);
    if (killTargets.empty())
    {
        // No kill objectives — check for gameobject objectives
        std::vector<uint32> goTargets = BuildGameObjectTargetList(pBot);
        if (!goTargets.empty())
        {
            // Find closest gameobject spawn
            for (uint32 goEntry : goTargets)
            {
                if (BotQuestCache::FindGameObjectSpawnLocation(goEntry, pBot->GetMapId(),
                        pBot->GetPositionX(), pBot->GetPositionY(),
                        m_goTargetX, m_goTargetY, m_goTargetZ))
                {
                    m_goTargetEntry = goEntry;
                    m_travelingToGameObject = true;
                    m_stuckTimer = 0;
                    m_lastDistanceCheckTime = 0;
                    m_lastDistanceToTarget = FLT_MAX;

                    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
                        "[QuestingActivity] %s traveling to quest object (entry %u) at (%.1f, %.1f, %.1f)",
                        pBot->GetName(), goEntry, m_goTargetX, m_goTargetY, m_goTargetZ);

                    if (m_pMovementMgr)
                        m_pMovementMgr->MoveTo(m_goTargetX, m_goTargetY, m_goTargetZ,
                                                MovementPriority::PRIORITY_NORMAL);
                    return;
                }
            }
        }

        // No kill or gameobject objectives — go back to selecting
        m_grindingHelper->ClearQuestTargetFilter();
        m_travelingToMobArea = false;
        m_state = QuestActivityState::SELECTING_QUEST;
        return;
    }
    m_grindingHelper->SetQuestTargetFilter(killTargets);

    // Try looting nearby item-source gameobjects (e.g., Cactus Apples)
    {
        std::vector<uint32> itemGoTargets = BuildItemGameObjectList(pBot);
        for (uint32 goEntry : itemGoTargets)
        {
            GameObject* pGo = BotObjectInteraction::FindNearbyObject(pBot, goEntry, 10.0f);
            if (pGo && BotObjectInteraction::CanInteractWith(pBot, pGo))
            {
                BotObjectInteraction::LootObject(pBot, pGo);
                m_goNoProgressTimer = 0;
                sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
                    "[QuestingActivity] %s looted quest item from %s (entry %u)",
                    pBot->GetName(), pGo->GetGOInfo()->name.c_str(), goEntry);
                return;
            }
        }
    }

    // If traveling to mob area, handle travel (don't scan for mobs during travel)
    if (m_travelingToMobArea)
    {
        float dist = pBot->GetDistance(m_mobAreaX, m_mobAreaY, m_mobAreaZ);
        if (dist <= 30.0f)
        {
            // Arrived at mob area — reset grinding helper state and start scanning
            m_travelingToMobArea = false;
            m_grindingHelper->Reset(pBot);
            m_grindingHelper->ResetNoMobsCount();

            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
                "[QuestingActivity] %s arrived at quest mob area, scanning for targets",
                pBot->GetName());
            return;
        }

        // Stuck detection while traveling
        m_stuckTimer += diff;
        m_lastDistanceCheckTime += diff;

        if (m_lastDistanceCheckTime >= DISTANCE_CHECK_INTERVAL_MS)
        {
            if (std::abs(dist - m_lastDistanceToTarget) < 1.0f)
            {
                if (m_pMovementMgr)
                    m_pMovementMgr->MoveTo(m_mobAreaX, m_mobAreaY, m_mobAreaZ,
                                            MovementPriority::PRIORITY_NORMAL);
            }
            m_lastDistanceToTarget = dist;
            m_lastDistanceCheckTime = 0;
        }

        if (m_stuckTimer >= STUCK_TIMEOUT_MS)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
                "[QuestingActivity] %s stuck traveling to mob area, aborting",
                pBot->GetName());
            m_grindingHelper->ClearQuestTargetFilter();
            m_travelingToMobArea = false;
            m_state = QuestActivityState::SELECTING_QUEST;
        }
        return;
    }

    // At mob area — scan and fight quest mobs
    GrindingResult result = m_grindingHelper->UpdateGrinding(pBot, 0);

    if (result == GrindingResult::ENGAGED)
    {
        m_noMobsAtAreaTimer = 0;  // Reset — we found something
        return;
    }

    if (result == GrindingResult::NO_TARGETS)
    {
        m_noMobsAtAreaTimer += diff;

        // Find closest spawn across ALL target entries
        // Use minimum distance threshold to skip spawns we're already at
        float minSearchDist = (m_noMobsAtAreaTimer >= NO_MOBS_RELOCATE_MS) ? 75.0f : 0.0f;

        float bestDist = FLT_MAX;
        float bestX = 0, bestY = 0, bestZ = 0;
        uint32 bestEntry = 0;

        for (uint32 entry : killTargets)
        {
            float spawnX, spawnY, spawnZ;
            if (BotQuestCache::FindCreatureSpawnLocation(entry, pBot->GetMapId(),
                    pBot->GetPositionX(), pBot->GetPositionY(),
                    spawnX, spawnY, spawnZ))
            {
                float dx = spawnX - pBot->GetPositionX();
                float dy = spawnY - pBot->GetPositionY();
                float dist = std::sqrt(dx * dx + dy * dy);

                // When relocating, skip spawns we're already near
                if (dist < minSearchDist)
                    continue;

                if (dist < bestDist)
                {
                    bestDist = dist;
                    bestX = spawnX;
                    bestY = spawnY;
                    bestZ = spawnZ;
                    bestEntry = entry;
                }
            }
        }

        if (bestEntry != 0 && bestDist > 75.0f)
        {
            m_mobAreaX = bestX;
            m_mobAreaY = bestY;
            m_mobAreaZ = bestZ;
            m_noMobsAtAreaTimer = 0;

            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
                "[QuestingActivity] %s traveling to quest mob area (entry %u) at (%.1f, %.1f, %.1f) dist %.0f",
                pBot->GetName(), bestEntry, m_mobAreaX, m_mobAreaY, m_mobAreaZ, bestDist);

            if (m_pMovementMgr)
                m_pMovementMgr->MoveTo(m_mobAreaX, m_mobAreaY, m_mobAreaZ,
                                        MovementPriority::PRIORITY_NORMAL);

            m_travelingToMobArea = true;
            m_stuckTimer = 0;
            m_lastDistanceCheckTime = 0;
            m_lastDistanceToTarget = FLT_MAX;
            return;
        }

        // No further spawn found — if timer hasn't expired, keep scanning
        if (m_noMobsAtAreaTimer < NO_MOBS_RELOCATE_MS)
            return;

        // Timed out and no other spawns — go back to selecting
        m_noMobsAtAreaTimer = 0;
        m_grindingHelper->ClearQuestTargetFilter();
        m_state = QuestActivityState::SELECTING_QUEST;
    }
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

    // Select best reward based on class stat preferences
    uint32 rewardChoice = 0;
    float bestScore = -1.0f;
    uint8 botClass = pBot->GetClass();

    for (uint32 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
    {
        uint32 itemId = pQuest->RewChoiceItemId[i];
        if (itemId == 0)
            continue;

        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
        if (!proto)
            continue;

        // Score item based on class stat preferences
        float score = 0.0f;
        for (int s = 0; s < MAX_ITEM_PROTO_STATS; ++s)
        {
            int32 value = proto->ItemStat[s].ItemStatValue;
            if (value <= 0)
                continue;

            switch (proto->ItemStat[s].ItemStatType)
            {
                case ITEM_MOD_STRENGTH:
                    if (botClass == CLASS_WARRIOR || botClass == CLASS_PALADIN)
                        score += value * 2.0f;
                    break;
                case ITEM_MOD_AGILITY:
                    if (botClass == CLASS_ROGUE || botClass == CLASS_HUNTER)
                        score += value * 2.0f;
                    else if (botClass == CLASS_DRUID || botClass == CLASS_SHAMAN)
                        score += value * 1.0f;
                    break;
                case ITEM_MOD_INTELLECT:
                    if (botClass == CLASS_MAGE || botClass == CLASS_WARLOCK || botClass == CLASS_PRIEST)
                        score += value * 2.0f;
                    else if (botClass == CLASS_DRUID || botClass == CLASS_SHAMAN || botClass == CLASS_PALADIN)
                        score += value * 1.0f;
                    break;
                case ITEM_MOD_STAMINA:
                    score += value * 0.5f;  // Everyone likes stamina
                    break;
                default:
                    break;
            }
        }

        // Fallback: if no stats, use sell price as tiebreaker
        if (score == 0.0f)
            score = static_cast<float>(proto->SellPrice) * 0.001f;

        if (score > bestScore)
        {
            bestScore = score;
            rewardChoice = i;
        }
    }

    // Turn in the quest
    pBot->RewardQuest(pQuest, rewardChoice, pNPC, true);

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
        "[QuestingActivity] %s completed quest: [%u] %s",
        pBot->GetName(), questId, pQuest->GetTitle().c_str());

    return true;
}

// ============================================================================
// Quest Objective Helpers
// ============================================================================

// Helper to add a creature entry to target list without duplicates
static void AddTargetEntry(std::vector<uint32>& targets, uint32 entry)
{
    for (uint32 existing : targets)
    {
        if (existing == entry)
            return;
    }
    targets.push_back(entry);
}

std::vector<uint32> QuestingActivity::BuildKillTargetList(Player* pBot) const
{
    std::vector<uint32> targets;

    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = pBot->GetUInt32Value(PLAYER_QUEST_LOG_1_1 + slot * MAX_QUEST_OFFSET + QUEST_ID_OFFSET);
        if (questId == 0)
            continue;

        // Only consider incomplete quests
        if (pBot->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
            continue;

        Quest const* pQuest = sObjectMgr.GetQuestTemplate(questId);
        if (!pQuest)
            continue;

        QuestStatusData const* statusData = pBot->GetQuestStatusData(questId);

        // --- Kill objectives: ReqCreatureOrGOId1-4 (positive = creature entry) ---
        for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        {
            int32 reqId = pQuest->ReqCreatureOrGOId[i];
            if (reqId <= 0)
                continue;  // 0 = none, negative = gameobject (Phase Q5)

            uint32 reqCount = pQuest->ReqCreatureOrGOCount[i];
            if (reqCount == 0)
                continue;

            if (statusData && statusData->m_creatureOrGOcount[i] >= reqCount)
                continue;  // Already met this objective

            AddTargetEntry(targets, static_cast<uint32>(reqId));
        }

        // --- Collect objectives: ReqItemId1-4 (items that drop from mobs) ---
        for (int i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        {
            uint32 itemId = pQuest->ReqItemId[i];
            if (itemId == 0)
                continue;

            uint32 reqCount = pQuest->ReqItemCount[i];
            if (reqCount == 0)
                continue;

            // Check if already have enough items
            uint32 currentCount = pBot->GetItemCount(itemId);
            if (currentCount >= reqCount)
                continue;

            // Look up which creatures drop this item (from startup cache)
            std::vector<uint32> const* dropSources = BotQuestCache::GetCreaturesDropping(itemId);
            if (!dropSources || dropSources->empty())
                continue;  // Item doesn't drop from any creature (might be from objects)

            // Add all creatures that drop this item to the target list
            for (uint32 creatureEntry : *dropSources)
                AddTargetEntry(targets, creatureEntry);
        }

        // --- Source item objectives: ReqSourceId1-4 (items needed before GO interaction) ---
        for (int i = 0; i < QUEST_SOURCE_ITEM_IDS_COUNT; ++i)
        {
            uint32 srcItemId = pQuest->ReqSourceId[i];
            uint32 srcItemCount = pQuest->ReqSourceCount[i];
            if (srcItemId == 0 || srcItemCount == 0)
                continue;

            // Check if already have the source item
            if (pBot->GetItemCount(srcItemId) >= srcItemCount)
                continue;

            // Look up which creatures drop this source item
            std::vector<uint32> const* dropSources = BotQuestCache::GetCreaturesDropping(srcItemId);
            if (!dropSources || dropSources->empty())
                continue;

            for (uint32 creatureEntry : *dropSources)
                AddTargetEntry(targets, creatureEntry);
        }
    }

    return targets;
}

void QuestingActivity::UpdateQuestCompletion(Player* pBot)
{
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = pBot->GetUInt32Value(PLAYER_QUEST_LOG_1_1 + slot * MAX_QUEST_OFFSET + QUEST_ID_OFFSET);
        if (questId == 0)
            continue;

        if (pBot->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE)
        {
            Quest const* pQuest = sObjectMgr.GetQuestTemplate(questId);
            QuestStatusData const* statusData = pBot->GetQuestStatusData(questId);
            if (pQuest && statusData)
            {
                // Sum total progress across kill + item objectives
                uint32 totalProgress = 0;
                for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
                    totalProgress += statusData->m_creatureOrGOcount[i];
                for (int i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
                {
                    if (pQuest->ReqItemId[i] != 0)
                        totalProgress += pBot->GetItemCount(pQuest->ReqItemId[i]);
                }

                uint32& lastKnown = m_lastKnownKillCounts[questId];
                if (totalProgress > lastKnown)
                {
                    lastKnown = totalProgress;
                    m_questLastProgressTime[questId] = WorldTimer::getMSTime();

                    // Log kill objective progress
                    for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
                    {
                        int32 reqId = pQuest->ReqCreatureOrGOId[i];
                        uint32 reqCount = pQuest->ReqCreatureOrGOCount[i];
                        if (reqId > 0 && reqCount > 0)
                        {
                            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
                                "[QuestingActivity] %s [%u] %s: kill %u/%u (entry %u)",
                                pBot->GetName(), questId, pQuest->GetTitle().c_str(),
                                statusData->m_creatureOrGOcount[i], reqCount, (uint32)reqId);
                        }
                    }

                    // Log item collection progress
                    for (int i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
                    {
                        uint32 itemId = pQuest->ReqItemId[i];
                        uint32 reqCount = pQuest->ReqItemCount[i];
                        if (itemId != 0 && reqCount > 0)
                        {
                            uint32 current = pBot->GetItemCount(itemId);
                            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
                                "[QuestingActivity] %s [%u] %s: item %u/%u (itemId %u)",
                                pBot->GetName(), questId, pQuest->GetTitle().c_str(),
                                current, reqCount, itemId);
                        }
                    }
                }
            }

            if (pBot->CanCompleteQuest(questId))
            {
                sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
                    "[QuestingActivity] %s [%u] %s: ALL OBJECTIVES COMPLETE!",
                    pBot->GetName(), questId, pQuest ? pQuest->GetTitle().c_str() : "unknown");
                pBot->CompleteQuest(questId);
                m_lastKnownKillCounts.erase(questId);
            }
        }
    }
}

// ============================================================================
// Gameobject Quest Helpers
// ============================================================================

std::vector<uint32> QuestingActivity::BuildGameObjectTargetList(Player* pBot) const
{
    std::vector<uint32> targets;

    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = pBot->GetUInt32Value(PLAYER_QUEST_LOG_1_1 + slot * MAX_QUEST_OFFSET + QUEST_ID_OFFSET);
        if (questId == 0)
            continue;

        if (pBot->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
            continue;

        Quest const* pQuest = sObjectMgr.GetQuestTemplate(questId);
        if (!pQuest)
            continue;

        QuestStatusData const* statusData = pBot->GetQuestStatusData(questId);

        for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        {
            int32 reqId = pQuest->ReqCreatureOrGOId[i];
            if (reqId >= 0)
                continue;  // Positive = creature (handled by kill targets), 0 = none

            uint32 goEntry = static_cast<uint32>(-reqId);

            uint32 reqCount = pQuest->ReqCreatureOrGOCount[i];
            if (reqCount == 0)
                continue;

            if (statusData && statusData->m_creatureOrGOcount[i] >= reqCount)
                continue;

            // Check if this objective requires a source item the bot doesn't have yet
            // ReqSourceId maps to objectives — check if any source item is missing
            bool missingSourceItem = false;
            for (int s = 0; s < QUEST_SOURCE_ITEM_IDS_COUNT; ++s)
            {
                uint32 srcItemId = pQuest->ReqSourceId[s];
                uint32 srcItemCount = pQuest->ReqSourceCount[s];
                if (srcItemId != 0 && srcItemCount > 0)
                {
                    if (pBot->GetItemCount(srcItemId) < srcItemCount)
                    {
                        missingSourceItem = true;
                        break;
                    }
                }
            }

            if (missingSourceItem)
                continue;  // Need source item first — skip this GO objective

            AddTargetEntry(targets, goEntry);
        }
    }

    return targets;
}

bool QuestingActivity::TryInteractWithQuestObjects(Player* pBot)
{
    std::vector<uint32> goTargets = BuildGameObjectTargetList(pBot);
    if (goTargets.empty())
        return false;

    uint32 now = WorldTimer::getMSTime();

    // If we recently interacted, wait for the cast/channel to finish before doing anything
    for (auto const& cooldown : m_goInteractCooldowns)
    {
        if (WorldTimer::getMSTimeDiff(cooldown.second, now) < GO_INTERACT_COOLDOWN_MS)
            return false;  // Still waiting — don't spam, let bot idle
    }

    for (uint32 goEntry : goTargets)
    {
        GameObject* pGo = BotObjectInteraction::FindNearbyObject(pBot, goEntry, 10.0f);
        if (pGo && BotObjectInteraction::CanInteractWith(pBot, pGo))
        {
            BotObjectInteraction::InteractWith(pBot, pGo);

            // Set cooldown — wait before trying again (goobers may need cast time)
            m_goInteractCooldowns[goEntry] = now;

            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
                "[QuestingActivity] %s interacted with quest object %s (entry %u)",
                pBot->GetName(), pGo->GetGOInfo()->name.c_str(), goEntry);
            return true;
        }
    }

    return false;
}

// ============================================================================
// Item-from-Gameobject Helpers
// ============================================================================

std::vector<uint32> QuestingActivity::BuildItemGameObjectList(Player* pBot) const
{
    std::vector<uint32> targets;

    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = pBot->GetUInt32Value(PLAYER_QUEST_LOG_1_1 + slot * MAX_QUEST_OFFSET + QUEST_ID_OFFSET);
        if (questId == 0)
            continue;

        if (pBot->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
            continue;

        Quest const* pQuest = sObjectMgr.GetQuestTemplate(questId);
        if (!pQuest)
            continue;

        for (int i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        {
            uint32 itemId = pQuest->ReqItemId[i];
            if (itemId == 0)
                continue;

            uint32 reqCount = pQuest->ReqItemCount[i];
            if (reqCount == 0)
                continue;

            if (pBot->GetItemCount(itemId) >= reqCount)
                continue;

            // Skip items that drop from creatures (handled by BuildKillTargetList)
            std::vector<uint32> const* creatureSources = BotQuestCache::GetCreaturesDropping(itemId);
            if (creatureSources && !creatureSources->empty())
                continue;

            // Check if item comes from a gameobject
            std::vector<uint32> const* goSources = BotQuestCache::GetGameObjectsDropping(itemId);
            if (!goSources || goSources->empty())
                continue;

            for (uint32 goEntry : *goSources)
                AddTargetEntry(targets, goEntry);
        }
    }

    return targets;
}

// ============================================================================
// Exploration Quest Helpers
// ============================================================================

uint32 QuestingActivity::FindExplorationQuest(Player* pBot) const
{
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = pBot->GetUInt32Value(PLAYER_QUEST_LOG_1_1 + slot * MAX_QUEST_OFFSET + QUEST_ID_OFFSET);
        if (questId == 0)
            continue;

        if (pBot->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
            continue;

        Quest const* pQuest = sObjectMgr.GetQuestTemplate(questId);
        if (!pQuest)
            continue;

        if (pQuest->HasSpecialFlag(QUEST_SPECIAL_FLAG_EXPLORATION_OR_EVENT))
        {
            // Check it has no kill/collect objectives (pure exploration)
            bool hasOtherObjectives = false;
            for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            {
                if (pQuest->ReqCreatureOrGOId[i] != 0)
                    hasOtherObjectives = true;
            }
            for (int i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
            {
                if (pQuest->ReqItemId[i] != 0)
                    hasOtherObjectives = true;
            }

            // If it has other objectives, let the kill/collect handlers deal with it
            // Only handle pure exploration quests here
            if (!hasOtherObjectives)
                return questId;
        }
    }
    return 0;
}

// ============================================================================
// Quest Log Management
// ============================================================================

void QuestingActivity::ManageQuestLog(Player* pBot)
{
    uint32 now = WorldTimer::getMSTime();
    uint32 playerLevel = pBot->GetLevel();
    uint32 grayLevel = MaNGOS::XP::GetGrayLevel(playerLevel);

    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = pBot->GetUInt32Value(PLAYER_QUEST_LOG_1_1 + slot * MAX_QUEST_OFFSET + QUEST_ID_OFFSET);
        if (questId == 0)
            continue;

        QuestStatus status = pBot->GetQuestStatus(questId);
        if (status != QUEST_STATUS_INCOMPLETE)
            continue;

        Quest const* pQuest = sObjectMgr.GetQuestTemplate(questId);
        if (!pQuest)
            continue;

        // Abandon grey quests — XP is worthless
        uint32 questLevel = pQuest->GetQuestLevel();
        if (questLevel > 0 && questLevel <= grayLevel)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
                "[QuestingActivity] %s abandoning grey quest [%u] %s (quest level %u, grey at %u)",
                pBot->GetName(), questId, pQuest->GetTitle().c_str(), questLevel, grayLevel);

            pBot->RemoveQuest(questId);
            m_lastKnownKillCounts.erase(questId);
            m_questLastProgressTime.erase(questId);
            continue;
        }

        // Soft timeout — only applies to quests we've been actively working on
        // A quest gets a timestamp when it first makes progress (kill, loot, etc.)
        // If no progress for 15 min AFTER first progress, it's stale
        auto progressIt = m_questLastProgressTime.find(questId);
        if (progressIt != m_questLastProgressTime.end())
        {
            // Only abandon if we've been trying and failing — not new quests
            if (WorldTimer::getMSTimeDiff(progressIt->second, now) >= QUEST_SOFT_TIMEOUT_MS)
            {
                sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
                    "[QuestingActivity] %s abandoning stale quest [%u] %s (no progress for 15 min)",
                    pBot->GetName(), questId, pQuest->GetTitle().c_str());

                pBot->RemoveQuest(questId);
                m_lastKnownKillCounts.erase(questId);
                m_questLastProgressTime.erase(questId);
                break;  // Only abandon one per check cycle
            }
        }
        // Quests without a progress timestamp haven't been worked on yet — leave them alone
    }
}
