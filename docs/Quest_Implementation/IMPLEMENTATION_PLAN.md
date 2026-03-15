# Master Plan: RandomBot Questing System

## Overview

Add comprehensive questing to RandomBot AI in 8 phases. Phase 0 builds reusable utilities. Phases 1-7 add quest functionality incrementally.

**Key Design:** QuestingStrategy is self-contained - it handles quest acquisition, objective completion, AND turn-in. It follows the same patterns as existing strategies (VendoringStrategy, TrainingStrategy, TravelingStrategy).

**Codebase Patterns Used:**
- Static cache: `static std::vector<T>` + `static bool s_cacheBuilt` + `static std::mutex` (same as vendor/trainer/grindspot caches)
- Cache building: `sObjectMgr.DoCreatureData()` for creature iteration, SQL for custom tables
- Cache init: Called from `PlayerBotMgr::Load()` before bots spawn
- Strategy interface: `IBotStrategy` with `Update()` returning `true` (busy) or `false` (yield)
- State machines: Enum-based states with switch dispatch in `Update()`
- Movement: All via `BotMovementManager` (set via `SetMovementManager()`)
- NPC interaction: Cache lookup → walk to NPC → nearby search fallback within 30yd → interact via game API

---

## QuestingStrategy Core Loop

```
QuestingStrategy Activated
    │
    ▼
Has quests in log?
    │
    ├── NO ──► Find nearest quest giver cluster (cache lookup)
    │              │
    │              ▼
    │          Travel to cluster
    │              │
    │              ▼
    │          Pick up all available quests from nearby givers
    │              │
    │              ▼
    │          (Loop back to "Has quests?")
    │
    └── YES ─► Select best quest to work on
                   │
                   ▼
               Work on objectives (kill/collect/explore/etc.)
                   │
                   ▼
               Quest complete?
                   │
                   ├── NO ──► Continue working
                   │
                   └── YES ─► Find turn-in target (separate cache, may differ from giver)
                                  │
                                  ▼
                              Travel to turn-in NPC/object
                                  │
                                  ▼
                              Turn in quest, select reward
                                  │
                                  ▼
                              (Loop back to "Has quests?")
```

---

## Integration with RandomBotAI

QuestingStrategy slots into `UpdateOutOfCombatAI()` in the existing priority chain. The weighted task system (future) will swap `m_strategy` between `GrindingStrategy` and `QuestingStrategy`. Until then, a simple config flag controls which strategy is active.

**Priority order (updated):**
```
Attackers > Training > Vendoring > Buffs > [Strategy: Questing OR Grinding] > Traveling
```

**Key: Universal behaviors still run regardless of active strategy:**
- Resting (checked before combat branch)
- Looting (checked before out-of-combat branch)
- Training (highest OOC priority)
- Vendoring (second OOC priority)
- Ghost walking (checked when dead)

**RandomBotAI initialization wiring (same pattern as existing strategies):**
```cpp
// In constructor:
m_questingStrategy(std::make_unique<QuestingStrategy>())

// In initialization block (after m_movementMgr created):
if (m_questingStrategy)
{
    m_questingStrategy->SetMovementManager(m_movementMgr.get());
    m_questingStrategy->SetAI(this);
}
```

---

## Phase 0: Prerequisites (Non-Questing Utilities)

### Phase 0A: Object Interaction System

**Files:**
- `src/game/PlayerBots/Utilities/BotObjectInteraction.h/.cpp`

**Purpose:** Bots currently cannot interact with gameobjects. This enables interacting with quest givers (wanted posters, signposts), lootable containers, and interaction triggers.

```cpp
class BotObjectInteraction
{
public:
    // Find nearby gameobject by entry ID
    // Uses Cell::VisitGridObjects() + MaNGOS grid visitor pattern
    static GameObject* FindNearbyObject(Player* pBot, uint32 entry, float range = 30.0f);

    // Find ANY nearby gameobject matching a type (e.g., GAMEOBJECT_TYPE_CHEST)
    static GameObject* FindNearbyObjectByType(Player* pBot, uint8 type, float range = 30.0f);

    // Check interaction prerequisites (range, LoS, flags, quest requirements)
    static bool CanInteractWith(Player* pBot, GameObject* pObject);

    // Interact with object - handles different types:
    //   QUESTGIVER (2): opens quest dialog
    //   CHEST (3): opens loot window
    //   GOOBER (10): triggers interaction
    static bool InteractWith(Player* pBot, GameObject* pObject);

    // Loot a gameobject (chest, herb node, etc.)
    // Similar to LootingBehavior::LootCorpse but for objects
    static bool LootObject(Player* pBot, GameObject* pObject);
};
```

**Implementation Notes:**
- `FindNearbyObject()`: Use `NearestGameObjectEntryInObjectRangeCheck` visitor (exists in MaNGOS)
- `InteractWith()`: Call `GameObject::Use(player)` - this is what the client does on right-click
- `LootObject()`: `SendLoot(guid, LOOT_SKINNING)` → `AutoStoreLoot()` → `DoLootRelease()` (same pattern as LootingBehavior)

---

### Phase 0B: Item Usage System

**Files:**
- `src/game/PlayerBots/Utilities/BotItemUsage.h/.cpp`

**Purpose:** Bots cannot use inventory items. Needed for quest items that must be "used" on targets, locations, or self.

```cpp
class BotItemUsage
{
public:
    // Find item in bags by entry ID
    // Iterates INVENTORY_SLOT_BAG_START..BAG_END, then each bag's slots
    static Item* FindItemInBags(Player* pBot, uint32 itemEntry);

    // Check if item is usable (has spell, not on cooldown)
    static bool CanUseItem(Player* pBot, Item* pItem);

    // Use item variants (maps to Player::CastItemUseSpell with appropriate SpellCastTargets)
    static bool UseItemOnSelf(Player* pBot, Item* pItem);
    static bool UseItemOnTarget(Player* pBot, Item* pItem, WorldObject* pTarget);
    static bool UseItemAtLocation(Player* pBot, Item* pItem, float x, float y, float z);
};
```

**Implementation Notes:**
- Item spell info in `item_template.Spells[0-4]` (SpellId, SpellTrigger)
- Build `SpellCastTargets` struct, then call `Player::CastItemUseSpell()`
- Check `Player::CanUseItem()` for level/class/cooldown restrictions

---

### Phase 0C: Spec Detection System

**Files:**
- `src/game/PlayerBots/Utilities/BotSpecDetection.h/.cpp`

**Purpose:** Determine bot spec from talent distribution. Used for reward selection. Built as infrastructure - not actively used until higher levels where gear choices matter.

```cpp
enum class BotRole { TANK, HEALER, MELEE_DPS, RANGED_DPS, UNKNOWN };

class BotSpecDetection
{
public:
    // Get dominant talent tree (0, 1, or 2) - returns tree with most points
    static uint8 GetDominantTree(Player* pBot);

    // Count talent points per tree
    static void GetTalentDistribution(Player* pBot, uint32& tree0, uint32& tree1, uint32& tree2);

    // Categorize role from class + dominant tree
    static BotRole GetRole(Player* pBot);

    // Simple class-based fallback for low levels (no talents)
    static BotRole GetDefaultRole(uint8 classId);
};
```

---

### Phase 0D: Stat Weight System (for Rewards)

**Files:**
- `src/game/PlayerBots/Utilities/BotStatWeights.h/.cpp`

**Purpose:** Score quest reward items so bots pick useful gear. Starts with simple class-based heuristic, upgradable to spec-aware later.

```cpp
class BotStatWeights
{
public:
    // Score an item for a given class (simple heuristic)
    // Warrior/Paladin: STR+STA, Rogue/Hunter: AGI+STA, Casters: INT+STA
    static float ScoreItemForClass(ItemPrototype const* pProto, uint8 classId);

    // Score an item using spec-aware weights (future refinement)
    static float ScoreItemForRole(ItemPrototype const* pProto, BotRole role);

    // Pick best reward from quest reward choices
    // Returns reward index (0-based) for Player::RewardQuest()
    static uint32 SelectBestReward(Quest const* pQuest, Player* pBot);
};
```

**Phase 1 uses `ScoreItemForClass()` (simple). Phase 0C's spec detection enables `ScoreItemForRole()` later.**

---

## Phase 1: Quest Infrastructure

**Files:**
- `src/game/PlayerBots/Utilities/BotQuestCache.h/.cpp`
- `src/game/PlayerBots/Strategies/QuestingStrategy.h/.cpp`

### BotQuestCache - Quest Giver & Turn-in Caches

Follows the proven vendor/trainer/grindspot cache pattern: static vectors built at server startup, shared across all bots, immutable at runtime.

**Two separate caches:**

```cpp
// ============================================================
// Quest Giver Cache - WHO gives quests and WHERE
// ============================================================

struct QuestGiverInfo
{
    float x, y, z;
    uint32 mapId;
    uint32 sourceEntry;         // Creature entry or gameobject entry
    uint32 sourceGuid;          // Spawn GUID (for world lookup)
    bool isGameObject;          // false=creature, true=gameobject
    uint32 factionTemplateId;   // For faction filtering (creatures only, 0 for objects)
    std::vector<uint32> questIds;  // Quests this NPC/object offers

    // ---- Pre-filter fields (computed at cache build time) ----
    // These allow skipping CanTakeQuest() entirely for ~95% of entries.
    // Without these, 3000 bots × 5000 givers × 3 quests = 45M CanTakeQuest() calls.
    uint8 minQuestLevel;        // Lowest MinLevel across all quests this NPC offers
    uint8 maxQuestLevel;        // Highest MinLevel across all quests this NPC offers
    uint32 classesMask;         // OR'd RequiredClasses from all quests (0 = any class)
    uint32 racesMask;           // OR'd RequiredRaces from all quests (0 = any race)
    uint32 objectiveMapMask;    // Bitmask of maps where objectives exist (bit 0=map 0, bit 1=map 1)
                                // Pre-computed from ReqCreatureOrGOId spawn locations
};

// ============================================================
// Quest Turn-in Cache - WHO accepts completed quests
// ============================================================

struct QuestTurnInInfo
{
    float x, y, z;
    uint32 mapId;
    uint32 targetEntry;         // Creature entry or gameobject entry
    uint32 targetGuid;          // Spawn GUID
    bool isGameObject;          // false=creature, true=gameobject
    uint32 factionTemplateId;   // For faction filtering
    uint32 questId;             // Which quest this NPC/object accepts
};

class BotQuestCache
{
public:
    // ---- Cache building (called from PlayerBotMgr::Load) ----
    static void BuildQuestGiverCache();   // creature_questrelation + gameobject_questrelation
    static void BuildTurnInCache();       // creature_involvedrelation + gameobject_involvedrelation
    static void BuildItemDropCache();     // creature_loot_template → reverse lookup (item → creatures)

    // ---- Quest giver lookups (indexed by map for O(N/maps) instead of O(N)) ----

    // Find nearest quest giver(s) with quests available for this bot
    // Fast path: map partition → pre-filter level/class/race/faction → CanTakeQuest() only on survivors
    // With pre-filtering, ~5000 entries → ~100-200 candidates → CanTakeQuest() on ~10-20
    static QuestGiverInfo const* FindNearestQuestGiver(Player* pBot);

    // Find nearby quest givers within a cluster radius (pick up multiple quests per trip)
    // Returns all givers within ~100 yards of the nearest one
    static std::vector<QuestGiverInfo const*> FindQuestGiverCluster(Player* pBot);

    // ---- Turn-in lookups (indexed by quest ID for O(1)) ----

    // Find turn-in target for a specific completed quest
    // Returns the nearest NPC/object that accepts this quest on the bot's current map
    static QuestTurnInInfo const* FindTurnInTarget(Player* pBot, uint32 questId);

    // ---- Item drop lookups (reverse cache, O(1) by item entry) ----

    // Get creature entries that drop a specific item (for collect quests)
    // Pre-built from creature_loot_template at startup - zero runtime DB queries
    static std::vector<uint32> const* GetCreaturesDropping(uint32 itemEntry);

    // ---- Quest filtering helpers ----

    // Check if quest objectives are completable on the bot's current map
    // Uses pre-computed objectiveMapMask for O(1) bitwise check
    static bool AreObjectivesOnMap(Quest const* pQuest, uint32 mapId);

    // Check if a quest should be abandoned (grey, stale, log management)
    static bool ShouldAbandonQuest(Player* pBot, uint32 questId);

    // Get quests available from a specific NPC that this bot can take
    // Calls Player::CanTakeQuest() for each quest (handles level, class, race, chains)
    static std::vector<uint32> GetAvailableQuests(Player* pBot, WorldObject* pQuestGiver);

    // ---- Opportunistic scanning helpers (O(1) lookups) ----

    // Check if a creature entry is a quest giver (for Phase 7B nearby scanning)
    // O(1) unordered_set lookup instead of searching the full cache
    static bool IsQuestGiverEntry(uint32 creatureEntry);
    static bool IsQuestGiverObjectEntry(uint32 gameobjectEntry);

private:
    // ---- Primary caches (partitioned by map for scalability) ----
    // Flat vector would be O(5000) per lookup × 3000 bots = too slow.
    // Map partition reduces to O(~2500) per continent, pre-filters cut to O(~100-200).
    static std::unordered_map<uint32 /*mapId*/, std::vector<QuestGiverInfo>> s_questGiversByMap;
    static std::unordered_map<uint32 /*questId*/, std::vector<QuestTurnInInfo>> s_turnInsByQuestId;
    static bool s_giverCacheBuilt;
    static bool s_turnInCacheBuilt;
    static std::mutex s_giverCacheMutex;
    static std::mutex s_turnInCacheMutex;

    // ---- Reverse lookup: item entry → creature entries that drop it ----
    // Built from creature_loot_template at startup. Eliminates all runtime DB queries
    // for "which mobs drop this quest item?" (Phase 3 collect quests).
    static std::unordered_map<uint32 /*itemEntry*/, std::vector<uint32> /*creatureEntries*/> s_itemDropSources;
    static bool s_itemDropCacheBuilt;
    static std::mutex s_itemDropCacheMutex;

    // ---- O(1) "is this NPC a quest giver?" sets (for opportunistic scanning) ----
    // Phase 7B scans nearby NPCs every 10-15 seconds. Without these sets,
    // each scan would search the full cache per NPC. With 3000 bots × 240 scans/sec
    // × multiple nearby NPCs, this must be O(1).
    static std::unordered_set<uint32> s_questGiverCreatureEntries;
    static std::unordered_set<uint32> s_questGiverObjectEntries;
};
```

**Cache building details:**

Quest giver cache (`BuildQuestGiverCache()`):
1. Iterate `sObjectMgr.DoCreatureData()` - find creatures with entries in `creature_questrelation`
2. Query `SELECT entry, quest FROM gameobject_questrelation` + join with `gameobject` spawn table for gameobject quest givers
3. For each quest giver, store position + all quest IDs they offer
4. **Pre-compute filter fields** for each entry:
   - `minQuestLevel` / `maxQuestLevel`: scan all quest IDs, read `quest_template.MinLevel`
   - `classesMask`: OR all `quest_template.RequiredClasses` together
   - `racesMask`: OR all `quest_template.RequiredRaces` together
   - `objectiveMapMask`: for each quest's `ReqCreatureOrGOId1-4`, look up spawn map from `creature`/`gameobject` table, set bit
5. **Partition by map ID** into `s_questGiversByMap[mapId]`
6. **Populate O(1) sets**: `s_questGiverCreatureEntries`, `s_questGiverObjectEntries`
7. Log counts: `">> Quest giver cache: X creature givers, Y gameobject givers, Z total quests (partitioned across N maps)"`

Turn-in cache (`BuildTurnInCache()`):
1. Same approach but with `creature_involvedrelation` and `gameobject_involvedrelation`
2. **Indexed by quest ID** into `s_turnInsByQuestId[questId]` for O(1) lookup when a quest is complete
3. Log counts: `">> Turn-in cache: X creature targets, Y gameobject targets"`

Item drop cache (`BuildItemDropCache()`):
1. Query `SELECT entry, item FROM creature_loot_template` (one query at startup)
2. Build reverse map: `s_itemDropSources[itemEntry] → {creatureEntry1, creatureEntry2, ...}`
3. Eliminates ALL runtime DB queries for Phase 3 collect quests
4. Log counts: `">> Item drop cache: X items mapped to Y creature sources"`

**Scalability at 3000 bots — lookup flow:**
```
FindNearestQuestGiver(bot):
  1. s_questGiversByMap[bot->GetMapId()]         // O(1) map partition: 5000 → ~2500
  2. Skip if botLevel outside minQuestLevel-4..maxQuestLevel+4  // ~2500 → ~300
  3. Skip if classesMask set and bot class not in mask           // ~300 → ~200
  4. Skip if racesMask set and bot race not in mask              // ~200 → ~150
  5. Skip if !IsFactionFriendly()                                // ~150 → ~80
  6. Skip if !(objectiveMapMask & (1 << botMapId))               // ~80 → ~60
  7. NOW call CanTakeQuest() on individual quests for ~60 givers // expensive but rare
```

### QuestingStrategy State Machine

```cpp
enum class QuestState
{
    // Quest acquisition
    CHECKING_QUEST_LOG,       // Evaluate quest log: have quests? need to abandon any?
    FINDING_QUEST_GIVER,      // Cache lookup for nearest giver cluster
    TRAVELING_TO_GIVER,       // Walking to quest giver (with stuck detection)
    AT_GIVER_PICKING_UP,      // At giver, accepting quests from this + nearby givers

    // Quest execution
    SELECTING_QUEST,          // Pick which quest to work on (priority: turn-in ready > closest objective)
    WORKING_ON_QUEST,         // Pursuing objectives (delegated to sub-handlers)

    // Quest turn-in
    FINDING_TURN_IN,          // Cache lookup for turn-in NPC/object (may differ from giver!)
    TRAVELING_TO_TURN_IN,     // Walking to turn-in target
    AT_TURN_IN,               // Turning in quest, selecting reward

    // Fallback
    NO_QUESTS_AVAILABLE,      // No quests found - signal strategy switch to grinding
};

class QuestingStrategy : public IBotStrategy
{
public:
    QuestingStrategy();

    // IBotStrategy interface
    bool Update(Player* pBot, uint32 diff) override;
    void OnEnterCombat(Player* pBot) override;
    void OnLeaveCombat(Player* pBot) override;
    char const* GetName() const override { return "QuestingStrategy"; }

    // Wiring (called by RandomBotAI during initialization)
    void SetMovementManager(BotMovementManager* pMgr) { m_pMovementMgr = pMgr; }
    void SetAI(RandomBotAI* pAI) { m_pAI = pAI; }

    // Query
    bool HasActiveQuest() const { return m_activeQuestId != 0; }
    uint32 GetActiveQuestId() const { return m_activeQuestId; }
    bool IsActive() const { return m_state != QuestState::CHECKING_QUEST_LOG; }

    // Signal: no quests available, caller should switch to grinding
    bool WantsStrategySwitch() const { return m_state == QuestState::NO_QUESTS_AVAILABLE; }

private:
    QuestState m_state = QuestState::CHECKING_QUEST_LOG;
    uint32 m_activeQuestId = 0;

    // References (set by RandomBotAI)
    BotMovementManager* m_pMovementMgr = nullptr;
    RandomBotAI* m_pAI = nullptr;

    // Internal GrindingStrategy for kill sub-tasks
    // Composed, not inherited - QuestingStrategy sets a creature filter and lets
    // GrindingStrategy handle scanning, target selection, approach, and combat
    std::unique_ptr<GrindingStrategy> m_pGrindingHelper;

    // Target tracking
    QuestGiverInfo const* m_targetGiver = nullptr;
    QuestTurnInInfo const* m_targetTurnIn = nullptr;

    // Stuck detection (same pattern as VendoringStrategy/TrainingStrategy)
    uint32 m_stuckTimer = 0;
    uint32 m_lastDistanceCheckTime = 0;
    float m_lastDistanceToTarget = 0.0f;

    // State timeout (prevent infinite stuck in any state)
    uint32 m_stateTimer = 0;
    static constexpr uint32 STATE_TIMEOUT_MS = 300000;  // 5 minutes

    // Search jitter (prevents 3000 bots from hitting FindNearestQuestGiver on same tick)
    // Random delay 0-5 seconds before entering FINDING_QUEST_GIVER
    uint32 m_searchDelayTimer = 0;
    static constexpr uint32 MAX_SEARCH_JITTER_MS = 5000;

    // State handlers (switch dispatch in Update)
    void HandleCheckingQuestLog(Player* pBot);
    void HandleFindingQuestGiver(Player* pBot);
    void HandleTravelingToGiver(Player* pBot, uint32 diff);
    void HandleAtGiverPickingUp(Player* pBot);
    void HandleSelectingQuest(Player* pBot);
    void HandleWorkingOnQuest(Player* pBot, uint32 diff);
    void HandleFindingTurnIn(Player* pBot);
    void HandleTravelingToTurnIn(Player* pBot, uint32 diff);
    void HandleAtTurnIn(Player* pBot);

    // Quest log management
    void AbandonGreyQuests(Player* pBot);    // Drop quests that went grey
    void AbandonStaleQuests(Player* pBot);   // Drop quests with no progress for too long
    uint32 GetFreeQuestSlots(Player* pBot) const;  // 20 - current count

    // Quest acceptance
    bool AcceptQuestFromNPC(Player* pBot, Creature* pNPC, uint32 questId);
    bool AcceptQuestFromObject(Player* pBot, GameObject* pObject, uint32 questId);

    // Quest turn-in
    bool TurnInQuestToNPC(Player* pBot, Creature* pNPC, uint32 questId);
    bool TurnInQuestToObject(Player* pBot, GameObject* pObject, uint32 questId);

    // Nearby NPC/object search (fallback when at cached position but NPC has moved)
    Creature* FindNearbyQuestNPC(Player* pBot, uint32 entry, float range = 30.0f);
};
```

### Integration Changes:

**PlayerBotMgr::Load()** (add after existing cache builds):
```cpp
BotQuestCache::BuildQuestGiverCache();   // Quest giver locations + pre-filter fields + O(1) sets
BotQuestCache::BuildTurnInCache();       // Turn-in locations indexed by quest ID
BotQuestCache::BuildItemDropCache();     // Reverse lookup: item → creature entries (Phase 3)
```

**RandomBotAI.h** - Add member:
```cpp
std::unique_ptr<QuestingStrategy> m_questingStrategy;
bool m_questingMode = false;  // Config flag (later: weighted task system)
```

**RandomBotAI::UpdateOutOfCombatAI()** - Replace grinding block:
```cpp
// Priority 5: Active strategy (Questing OR Grinding)
if (m_questingMode && m_questingStrategy)
{
    if (m_questingStrategy->Update(me, RB_UPDATE_INTERVAL))
        return;  // Busy questing

    // If questing has no quests available, fall through to grinding
    if (m_questingStrategy->WantsStrategySwitch())
    {
        // Grinding as fallback
        GrindingResult grindResult = pGrinding->UpdateGrinding(me, 0);
        // ... existing travel logic ...
    }
}
else
{
    // Existing grinding + travel logic (unchanged)
    GrindingResult grindResult = pGrinding->UpdateGrinding(me, 0);
    // ...
}
```

**CMakeLists.txt** - Add new files

---

## Phase 2: Kill Quests

**Goal:** Bots can complete "Kill X of creature Y" quests.

**Changes to QuestingStrategy (`HandleWorkingOnQuest`):**
- Parse `quest_template.ReqCreatureOrGOId1-4` (positive values = creature entries)
- Parse `quest_template.ReqCreatureOrGOCount1-4` for required counts
- Handle multi-objective: up to 4 different kill targets per quest
- Set creature filter on internal `m_pGrindingHelper`:
  ```cpp
  std::vector<uint32> targetEntries;
  // Collect all creature entries from quest objectives that aren't complete yet
  m_pGrindingHelper->SetQuestTargetFilter(targetEntries);
  ```
- Internal GrindingStrategy handles scanning, approach, combat engagement
- QuestingStrategy monitors progress via `Player::GetQuestSlotQuestId()` / counter methods
- When all kill objectives complete → transition to `FINDING_TURN_IN`

**Changes to GrindingStrategy:**
```cpp
// New methods for quest target filtering
void SetQuestTargetFilter(std::vector<uint32> const& creatureEntries);
void ClearQuestTargetFilter();

// Modified IsValidGrindTarget() - when filter is set:
//   ONLY targets matching filter entries are valid
//   Level range check relaxed (quest mobs may be higher/lower than ±2)
```

**Mob Location:**
- Use `creature` spawn table to find where quest mobs spawn
- If quest mobs aren't within GrindingStrategy's SEARCH_RANGE (75yd), QuestingStrategy
  travels to a known spawn location first, then lets GrindingStrategy do the killing

---

## Phase 3: Collect Quests (Mob Drops)

**Goal:** Bots can complete "Collect X of item Y" quests where items drop from mobs.

**Changes to QuestingStrategy (`HandleWorkingOnQuest`):**
- Parse `quest_template.ReqItemId1-6` and `ReqItemCount1-6`
- Use `BotQuestCache::GetCreaturesDropping(itemEntry)` to find which mobs drop required items
  - This is an O(1) lookup into the pre-built reverse cache (`s_itemDropSources`)
  - **Zero runtime DB queries** — the reverse map was built at startup from `creature_loot_template`
- Set GrindingStrategy filter to those creature entries
- Track progress via item count in bot's inventory
- Loot handling automatic via existing `LootingBehavior` (no changes needed)
- Handle mixed quests: if quest has BOTH kill AND collect objectives, combine creature
  entries from both into the filter

---

## Phase 4: Collect Quests (World Objects)

**Goal:** Bots can interact with gameobjects to collect quest items (mining nodes, chests, herb gathers for quests).

**Changes to QuestingStrategy (`HandleWorkingOnQuest`):**
- Parse `quest_template.ReqCreatureOrGOId1-4` (negative values = gameobject entries, negate to get entry)
- Add sub-states within WORKING_ON_QUEST:
  - `TRAVELING_TO_OBJECT`: Move to known gameobject spawn location
  - `INTERACTING_WITH_OBJECT`: Use `BotObjectInteraction::InteractWith()` from Phase 0A
- Build gameobject spawn location lookup from `gameobject` table (can be part of quest cache or separate)
- Handle mixed quests: some objectives may be kills, others may be object interactions

---

## Phase 5: Exploration Quests

**Goal:** Bots can complete "Go to location X" quests.

**Changes to QuestingStrategy (`HandleWorkingOnQuest`):**
- Check `quest_template.QuestFlags` for `QUEST_SPECIAL_FLAG_EXPLORATION_OR_EVENT`
- Look up areatrigger coordinates from `areatrigger_involvedrelation` + `areatrigger` table
- Travel to areatrigger location using `BotMovementManager`
- Completion is automatic: when bot enters areatrigger, `Player::AreaExploredOrEventHappens()` fires
- Also handle "talk to NPC" variants: travel to NPC, interact (similar to turn-in flow)

---

## Phase 6: Item Usage Quests

**Goal:** Bots can use quest items on targets, locations, or self.

**Changes to QuestingStrategy (`HandleWorkingOnQuest`):**
- Identify quests requiring item use (quest items with spells in `item_template.Spells[]`)
- Add sub-state: `USING_QUEST_ITEM`
- Use `BotItemUsage` from Phase 0B
- Determine target type from spell data:
  - Self-cast: `UseItemOnSelf()`
  - Target-cast (on mob or NPC): `UseItemOnTarget()`
  - Ground-target (at location): `UseItemAtLocation()`

---

## Phase 7: Opportunistic Features

**Goal:** Make quest discovery feel natural instead of purely intentional.

### 7A: Item-Started Quests

**Hook point:** `LootingBehavior` (already runs universally after combat)

**Changes to LootingBehavior:**
- After looting a corpse, check if any looted items have `item_template.StartQuest > 0`
- If bot is in questing mode AND `Player::CanTakeQuest()` passes → auto-accept the quest
- No special routing needed - happens organically during normal play
- Grinding-only bots ignore quest-starting items (avoid filling quest log they won't use)

### 7B: Opportunistic Quest Pickup

**Changes to QuestingStrategy:**
- During `WORKING_ON_QUEST` or `TRAVELING_*` states, periodically check for nearby quest givers
- Cooldown: every ~10-15 seconds, scan within 40 yards for NPCs/objects with available quests
- **O(1) "is quest giver?" check**: For each nearby NPC found by grid visitor, call
  `BotQuestCache::IsQuestGiverEntry(npc->GetEntry())` — this is an `unordered_set::count()`
  lookup, not a cache search. Critical at 3000 bots × 240 scans/sec.
- Only call `GetAvailableQuests()` + `CanTakeQuest()` on confirmed quest givers (rare)
- If found and bot has free quest log slots → detour briefly to pick up quests
- Track "already checked" GUIDs to avoid re-scanning the same quest giver repeatedly
- Makes bots feel natural: grinding for quest, stumbles across NPC with `!`, grabs the quest

---

## Files Summary

### New Files (12):
```
src/game/PlayerBots/Utilities/BotObjectInteraction.h/.cpp   (Phase 0A)
src/game/PlayerBots/Utilities/BotItemUsage.h/.cpp           (Phase 0B)
src/game/PlayerBots/Utilities/BotSpecDetection.h/.cpp       (Phase 0C)
src/game/PlayerBots/Utilities/BotStatWeights.h/.cpp         (Phase 0D)
src/game/PlayerBots/Utilities/BotQuestCache.h/.cpp          (Phase 1)
src/game/PlayerBots/Strategies/QuestingStrategy.h/.cpp      (Phase 1)
```

### Modified Files:
```
src/game/CMakeLists.txt                                     (add new files)
src/game/PlayerBots/RandomBotAI.h/.cpp                      (add QuestingStrategy, wiring, priority)
src/game/PlayerBots/Strategies/GrindingStrategy.h/.cpp      (quest target filter)
src/game/PlayerBots/Strategies/LootingBehavior.h/.cpp       (item-started quests, Phase 7A)
src/game/PlayerBots/PlayerBotMgr.cpp                        (cache init)
```

---

## Implementation Order

| # | Phase | Description | Est. Complexity |
|---|-------|-------------|-----------------|
| 1 | 0A | Object Interaction | Low - utility class, grid visitors exist |
| 2 | 0B | Item Usage | Medium - multiple target types |
| 3 | 0C | Spec Detection | Low - talent iteration |
| 4 | 0D | Stat Weights | Low - lookup tables |
| 5 | 1 | Quest Infrastructure (caches, strategy skeleton, AI integration) | High - largest phase |
| 6 | 2 | Kill Quests | Medium - GrindingStrategy filter + mob location |
| 7 | 3 | Collect Quests (Mob Drops) | Low - extends Phase 2 with loot lookup |
| 8 | 4 | Collect Quests (World Objects) | Medium - uses Phase 0A |
| 9 | 5 | Exploration Quests | Low - mostly just travel |
| 10 | 6 | Item Usage Quests | Medium - uses Phase 0B |
| 11 | 7 | Opportunistic Features (item-start + nearby pickup) | Low - hooks into existing systems |

---

## Verification

**After each phase:**
1. Build: `cd ~/Desktop/Forsaken_WOW/core/build && make -j$(nproc) && make install`
2. Run server and observe bot behavior
3. Use `.bot status` to verify strategy name and state
4. Check console logs for `[QuestingStrategy]` / `[BotQuestCache]` messages

**Phase 1 end-to-end test:**
1. Enable questing mode on a bot
2. Bot should find nearest quest giver cluster
3. Bot travels to quest giver(s)
4. Bot picks up available quests (filtered by level, class, race, faction, chains)
5. Bot works on quest objectives
6. Bot finds turn-in NPC (may be different from giver)
7. Bot turns in quest, selects best reward
8. Bot repeats cycle
9. When no quests available, bot falls back to grinding

**Quest log management test:**
1. Bot with 18+ quests should not hoard more
2. Grey quests get abandoned to free slots
3. Stale quests get abandoned after extended no-progress

---

## Database Tables Reference

| Table | Used By | Purpose |
|-------|---------|---------|
| `quest_template` | QuestingStrategy | Quest definitions, objectives, rewards, chains |
| `creature_questrelation` | BotQuestCache | NPCs that give quests |
| `creature_involvedrelation` | BotQuestCache | NPCs that accept turn-ins |
| `gameobject_questrelation` | BotQuestCache | Objects that give quests |
| `gameobject_involvedrelation` | BotQuestCache | Objects that accept turn-ins |
| `creature_loot_template` | QuestingStrategy (Phase 3) | Which mobs drop quest items |
| `gameobject_loot_template` | QuestingStrategy (Phase 4) | Which objects contain quest items |
| `areatrigger_involvedrelation` | QuestingStrategy (Phase 5) | Exploration quest triggers |
| `item_template` | LootingBehavior (Phase 7A) | `StartQuest` field for item-started quests |
| `creature` | BotQuestCache | NPC spawn positions (via sObjectMgr) |
| `gameobject` | BotQuestCache | Object spawn positions (SQL query) |

---

## Scalability (2500-3000 Bots)

Design reviewed for 3000 concurrent bots. Key measures:

| Concern | Solution | Impact |
|---------|----------|--------|
| Quest giver search O(5000) per bot | Map-partitioned cache + pre-filter fields (level/class/race/faction/objectiveMap) | 5000 → ~60 candidates before CanTakeQuest() |
| Turn-in lookup O(N) per quest | Indexed by quest ID (`unordered_map`) | O(1) lookup |
| "Which mobs drop item X?" DB query | Startup reverse cache (`s_itemDropSources`) | Zero runtime DB queries |
| Opportunistic "is quest giver?" check | `unordered_set<creatureEntry>` | O(1) per NPC |
| 3000 bots searching simultaneously | Random jitter (0-5sec) before FINDING_QUEST_GIVER | Spreads load across ticks |
| AreObjectivesOnMap per-quest | Pre-computed `objectiveMapMask` bitmask | O(1) bitwise AND |
| Cache thread safety | Immutable after startup (same as vendor/trainer) | Zero locks at runtime |
| Memory: 3000 GrindingHelper instances | ~100 bytes each = 300KB total | Negligible |

**Zero runtime DB queries** — all data pre-loaded at startup. Every lookup is an in-memory operation.

---

*Last Updated: 2026-02-09*
