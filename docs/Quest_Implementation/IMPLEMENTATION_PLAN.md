# Questing System Implementation Plan

**Status**: Not Started
**Created**: 2026-03-14
**Depends On**: BRAINSTORM.md for design decisions and rationale

---

## Build Order

| # | Phase | Description | Prerequisites |
|---|-------|-------------|---------------|
| 1 | R1 | Architecture refactor: Activity interface + GrindingActivity | None |
| 2 | R2 | Verify behavior-unchanged refactor | R1 |
| 3 | Q1 | Quest caches (quest givers, turn-ins, item drops) | R2 |
| 4 | Q2 | QuestingActivity skeleton + quest acceptance/turn-in | Q1 |
| 5 | Q3 | Kill quests | Q2 |
| 6 | Q4 | Collect quests — mob drops | Q3 |
| 7 | Q5 | Object interaction + collect quests — world objects | Q4 |
| 8 | Q6 | Exploration quests | Q2 |
| 9 | Q7 | Item usage + item use quests | Q2 |
| 10 | Q8 | Quest log management (abandon grey/stale, soft timeout) | Q3 |
| 11 | Q9 | Multi-quest overlap (combined target lists) | Q3 |
| 12 | Q10 | Opportunistic features (item-started quests, nearby pickup) | Q4 |
| 13 | Q11 | Reward selection (class-based heuristic) | Q2 |
| 14 | Q12 | Weighted activity system (questing vs grinding assignment) | Q2 |

---

## Phase R1: Architecture Refactor

**Goal**: Extract the current hardwired grinding loop from RandomBotAI into an Activity system. After this phase, bots behave IDENTICALLY to today — no new features, no tweaks.

### New Files

**`src/game/PlayerBots/IBotActivity.h`** — Activity interface

```cpp
class IBotActivity
{
public:
    virtual ~IBotActivity() = default;

    // Called every tick when out of combat and no Tier 0/behavior interrupts
    // Returns true if the activity is busy (caller should not run fallbacks)
    virtual bool Update(Player* pBot, uint32 diff) = 0;

    // Combat state notifications
    virtual void OnEnterCombat(Player* pBot) = 0;
    virtual void OnLeaveCombat(Player* pBot) = 0;

    // Activity name for debugging (.bot status)
    virtual char const* GetName() const = 0;

    // Behavior permissions — activity declares what behaviors are allowed
    // RandomBotAI checks these before running optional behaviors
    virtual bool AllowsLooting() const = 0;
    virtual bool AllowsVendoring() const = 0;
    virtual bool AllowsTraining() const = 0;
};
```

**`src/game/PlayerBots/Activities/GrindingActivity.h/.cpp`** — Current grinding logic as an activity

```cpp
class GrindingActivity : public IBotActivity
{
public:
    GrindingActivity();

    // IBotActivity interface
    bool Update(Player* pBot, uint32 diff) override;
    void OnEnterCombat(Player* pBot) override;
    void OnLeaveCombat(Player* pBot) override;
    char const* GetName() const override { return "Grinding"; }

    // Behavior permissions
    bool AllowsLooting() const override { return true; }
    bool AllowsVendoring() const override { return true; }
    bool AllowsTraining() const override { return true; }

    // Wiring (same pattern as current strategies)
    void SetCombatMgr(BotCombatMgr* pMgr);
    void SetMovementManager(BotMovementManager* pMgr);
    void SetVendoringStrategy(VendoringStrategy* pVendoring);

    // Access to internal behaviors (needed by RandomBotAI for attacker handling)
    GrindingStrategy* GetGrindingStrategy();
    TravelingStrategy* GetTravelingStrategy();

private:
    // GrindingActivity owns these behaviors and coordinates them
    std::unique_ptr<GrindingStrategy> m_grinding;
    std::unique_ptr<TravelingStrategy> m_traveling;
};
```

GrindingActivity::Update() contains the logic currently in RandomBotAI::UpdateOutOfCombatAI() after the vendoring check:
- Call GrindingStrategy::UpdateGrinding()
- If NO_TARGETS and threshold hit, call TravelingStrategy

### Changes to Existing Files

**`RandomBotAI.h`** — Replace strategy + traveling with activity:

```
REMOVE:
  std::unique_ptr<IBotStrategy> m_strategy;
  std::unique_ptr<TravelingStrategy> m_travelingStrategy;

ADD:
  std::unique_ptr<IBotActivity> m_currentActivity;
```

Keep as-is (these are behaviors checked at the RandomBotAI level):
- `m_vendoringStrategy` — checked before activity, gated by `m_currentActivity->AllowsVendoring()`
- `m_trainingStrategy` — checked before activity, gated by `m_currentActivity->AllowsTraining()`
- `m_looting` — checked before activity, gated by `m_currentActivity->AllowsLooting()`
- `m_ghostStrategy` — Tier 0, always runs
- `m_combatMgr` — always runs

**`RandomBotAI.cpp`** — Refactor UpdateOutOfCombatAI():

```
BEFORE (current):
  attackers check
  training check
  vendoring check
  buffs
  grinding → traveling (hardwired)

AFTER:
  attackers check
  if activity allows training → training check
  if activity allows vendoring → vendoring check
  buffs
  m_currentActivity->Update()  // GrindingActivity handles grinding + traveling
```

Constructor: create `GrindingActivity` instead of `GrindingStrategy` + `TravelingStrategy`.

Initialization block: wire up GrindingActivity with combat mgr, movement mgr, etc.

**`RandomBotAI::UpdateAI()`** — Looting gate:
```
BEFORE:
  if (m_looting.Update(me, diff))
      return;

AFTER:
  if (m_currentActivity->AllowsLooting() && m_looting.Update(me, diff))
      return;
```

**`BotAction` enum** — No changes needed yet. GrindingActivity reports as "Grinding" same as today.

**`GetStatusInfo()`** — Update to use `m_currentActivity->GetName()` instead of `m_strategy->GetName()`.

**`MovementInform()`** — TravelingStrategy is now inside GrindingActivity. Need to route waypoint callbacks through the activity, or give RandomBotAI access to the internal TravelingStrategy via `GrindingActivity::GetTravelingStrategy()`.

**`CMakeLists.txt`** — Add new files.

### What Does NOT Change
- IBotStrategy interface — behaviors still use it
- GrindingStrategy — unchanged, just owned by GrindingActivity instead of RandomBotAI
- TravelingStrategy — unchanged, just owned by GrindingActivity instead of RandomBotAI
- VendoringStrategy — unchanged, still owned by RandomBotAI
- TrainingStrategy — unchanged, still owned by RandomBotAI
- LootingBehavior — unchanged, still owned by RandomBotAI
- GhostWalkingStrategy — unchanged, still owned by RandomBotAI
- BotCheats — unchanged
- BotCombatMgr — unchanged
- BotMovementManager — unchanged
- All 9 class combat handlers — unchanged

---

## Phase R2: Verify Refactor

**Goal**: Confirm bots behave identically to before the refactor.

### Verification Steps
1. Build: `cd ~/Desktop/Forsaken_WOW/core/build && make -j$(nproc) && make install`
2. Run server with 50+ bots for 30+ minutes
3. Verify with `.bot status`:
   - Bots show "Grinding" as activity
   - Bots grind, loot, rest, vendor, train, travel — all working
   - Combat still functions for all classes
4. Check console for errors/warnings — should be identical to pre-refactor
5. Verify ghost walking still works (kill a bot, watch it corpse walk)
6. Verify level-up training trigger still fires
7. Verify vendoring still triggers on full bags

**Critical**: No new features in this phase. If anything behaves differently, it's a bug.

---

## Phase Q1: Quest Caches

**Goal**: Build the data layer. All quest-related lookups available as fast in-memory operations.

### New Files

**`src/game/PlayerBots/Utilities/BotQuestCache.h/.cpp`**

Three caches, same pattern as existing vendor/trainer caches:

**Quest Giver Cache:**
- Source: `creature_questrelation` + `gameobject_questrelation` + spawn tables
- Structure: `QuestGiverInfo` — position, entry, quest IDs, pre-filter fields
- Partitioned by map ID: `unordered_map<mapId, vector<QuestGiverInfo>>`
- Pre-filter fields per entry: min/max quest level, class mask, race mask, objective map mask
- O(1) sets: `unordered_set<uint32>` for creature entries and gameobject entries that are quest givers
- Locality search: filter by bot's current zone/area first, then expand

**Turn-in Cache:**
- Source: `creature_involvedrelation` + `gameobject_involvedrelation` + spawn tables
- Indexed by quest ID: `unordered_map<questId, vector<QuestTurnInInfo>>`
- O(1) lookup when a quest is completed

**Item Drop Cache:**
- Source: `creature_loot_template`
- Reverse map: `unordered_map<itemEntry, vector<uint32 creatureEntries>>`
- Zero runtime DB queries for "which mobs drop this item?"

### Changes to Existing Files

**`PlayerBotMgr.cpp`** — Add cache init calls:
```cpp
BotQuestCache::BuildQuestGiverCache();
BotQuestCache::BuildTurnInCache();
BotQuestCache::BuildItemDropCache();
```

**`CMakeLists.txt`** — Add new files.

### Verification
- Server boots without errors
- Console logs show cache counts: "Quest giver cache: X creature givers, Y gameobject givers..."
- No impact on existing bot behavior (caches are passive data)

---

## Phase Q2: QuestingActivity Skeleton

**Goal**: Bots can travel to quest givers, accept quests, travel to turn-in NPCs, and turn in quests. No objective completion yet — just the pickup/turn-in loop.

### New Files

**`src/game/PlayerBots/Activities/QuestingActivity.h/.cpp`**

```
State Machine:
  CHECKING_QUEST_LOG → evaluate what bot has, what to do next
  FINDING_QUEST_GIVER → cache lookup (locality-first)
  TRAVELING_TO_GIVER → walking to quest giver
  AT_GIVER_PICKING_UP → accepting available quests (cluster behavior)
  SELECTING_QUEST → pick which quest to work on
  WORKING_ON_QUEST → pursuing objectives (Phase Q3+)
  FINDING_TURN_IN → cache lookup for turn-in target
  TRAVELING_TO_TURN_IN → walking to turn-in NPC
  AT_TURN_IN → turning in quest
  NO_QUESTS_AVAILABLE → signal fallback to grinding
```

Behavior permissions:
- AllowsLooting: true
- AllowsVendoring: true
- AllowsTraining: true

Cluster behavior: when at a quest giver, scan for other givers within ~100 yards, accept quests from all of them before leaving. Same for turn-ins.

### Changes to Existing Files

**`RandomBotAI.h/.cpp`** — Add QuestingActivity as an option. For now, controlled by a simple flag or config. The weighted system comes in Phase Q12.

**`CMakeLists.txt`** — Add new files.

### Verification
- Set a bot to questing mode
- Bot should find nearest quest giver, travel there, accept quests
- Bot should find turn-in NPC for completed quests (won't have any yet — this verifies the travel/interaction works)
- `.bot status` shows "Questing" with state info

---

## Phase Q3: Kill Quests

**Goal**: Bots complete "Kill X of creature Y" quests.

### Changes

**GrindingStrategy** (or internal GrindingBehavior used by QuestingActivity):
- Add `SetQuestTargetFilter(vector<uint32> creatureEntries)` / `ClearQuestTargetFilter()`
- When filter is set, only target matching creature entries
- Level range check relaxed (quest mobs may be outside normal ±2 range)

**QuestingActivity — WORKING_ON_QUEST state:**
- Parse `ReqCreatureOrGOId1-4` (positive values) and `ReqCreatureOrGOCount1-4`
- Handle multi-objective: up to 4 different kill targets
- Build combined target list from ALL active quests (multi-quest overlap)
- If quest mobs aren't nearby, use `creature` spawn table to find spawn location, travel there
- Set creature filter on GrindingBehavior, let it handle scanning/approach/combat
- Monitor progress via quest slot counters
- When all objectives complete → transition to FINDING_TURN_IN

### Verification
- Bot picks up a kill quest, travels to mob area, kills correct creatures
- Bot tracks progress, stops killing when objective complete
- Bot travels to turn-in NPC (may differ from quest giver)
- Bot turns in quest

---

## Phase Q4: Collect Quests — Mob Drops

**Goal**: Bots complete "Collect X of item Y" quests where items drop from mobs.

### Changes

**QuestingActivity — WORKING_ON_QUEST state:**
- Parse `ReqItemId1-6` and `ReqItemCount1-6`
- Use `BotQuestCache::GetCreaturesDropping(itemEntry)` for O(1) reverse lookup
- Set GrindingBehavior filter to creatures that drop needed items
- Track progress via item count in inventory
- Existing LootingBehavior handles actual looting — no changes needed
- Handle mixed quests: combine creature entries from kill objectives AND item drop sources

### Verification
- Bot picks up a collect quest, kills correct mobs, loots items
- Progress tracked correctly even with low drop rates
- Mixed quests (kills + items) work simultaneously

---

## Phase Q5: Object Interaction + World Object Quests

**Goal**: Bots can interact with gameobjects. Bots complete quests requiring gameobject interaction.

### New Files

**`src/game/PlayerBots/Utilities/BotObjectInteraction.h/.cpp`**
- `FindNearbyObject(Player*, uint32 entry, float range)` — grid visitor search
- `FindNearbyObjectByType(Player*, uint8 type, float range)`
- `CanInteractWith(Player*, GameObject*)` — range, LoS, flag checks
- `InteractWith(Player*, GameObject*)` — calls `GameObject::Use(player)`
- `LootObject(Player*, GameObject*)` — for lootable containers

### Changes

**QuestingActivity — WORKING_ON_QUEST state:**
- Parse `ReqCreatureOrGOId1-4` negative values (negate to get gameobject entry)
- Lookup gameobject spawn locations from `gameobject` table (can be part of quest cache)
- Travel to object location, interact, loot if needed

### Verification
- Bot travels to gameobject, interacts, collects quest item
- Mixed quests with both creature kills and object interactions work

---

## Phase Q6: Exploration Quests

**Goal**: Bots complete "Go to location X" quests.

### Changes

**QuestingActivity — WORKING_ON_QUEST state:**
- Check `SpecialFlags` for `QUEST_SPECIAL_FLAG_EXPLORATION_OR_EVENT`
- Lookup areatrigger coordinates from `areatrigger_involvedrelation` + `areatrigger` table
- Travel to location — completion is automatic when bot enters areatrigger
- "Talk to NPC" variants: travel to NPC, interact (same as turn-in flow)

### Verification
- Bot travels to exploration point, quest auto-completes

---

## Phase Q7: Item Usage + Item Use Quests

**Goal**: Bots can use inventory items. Bots complete quests requiring item usage.

### New Files

**`src/game/PlayerBots/Utilities/BotItemUsage.h/.cpp`**
- `FindItemInBags(Player*, uint32 itemEntry)` — bag iteration
- `CanUseItem(Player*, Item*)` — spell check, cooldown check
- `UseItemOnSelf(Player*, Item*)`
- `UseItemOnTarget(Player*, Item*, WorldObject*)`
- `UseItemAtLocation(Player*, Item*, float x, float y, float z)`

### Changes

**QuestingActivity — WORKING_ON_QUEST state:**
- Identify quest items with spells in `item_template.Spells[]`
- Determine target type from spell data
- Use appropriate BotItemUsage function

### Verification
- Bot uses quest item on self/target/location, quest progresses

---

## Phase Q8: Quest Log Management

**Goal**: Bots actively manage their 20-quest log.

### Changes

**QuestingActivity — CHECKING_QUEST_LOG state:**
- Abandon grey quests (quest level too far below bot level)
- Abandon stale quests (soft timeout: 15 min since last meaningful progress event)
- Track meaningful events per quest (see BRAINSTORM.md for event types)
- Keep ~4-5 free slots for opportunistic pickups
- Prioritize turning in completed quests to free slots

### Verification
- Bot abandons grey quests as it levels
- Bot abandons quests with no progress after timeout
- Quest log stays manageable, doesn't hit 20/20

---

## Phase Q9: Multi-Quest Overlap

**Goal**: Bots work on multiple quests simultaneously when objectives overlap.

### Changes

**QuestingActivity — SELECTING_QUEST / WORKING_ON_QUEST:**
- Instead of "pick one quest, work it" → "build target list from ALL active quests"
- Prioritize creature entries that satisfy multiple quests at once
- Example: "Kill 10 Defias" quest + "Collect 8 Red Bandanas" quest → target Defias mobs (satisfies both)
- Quest selection heuristic: lowest level → same-level tiebreak → closest → most overlap

### Verification
- Bot with overlapping kill/collect quests targets the right mobs
- Multiple quests progress simultaneously

---

## Phase Q10: Opportunistic Features

**Goal**: Quest discovery feels natural, not just intentional.

### Changes

**LootingBehavior** — Item-started quests:
- After looting, check if any items have `item_template.StartQuest > 0`
- If bot is in questing mode and `Player::CanTakeQuest()` passes → auto-accept
- Grinding-only bots ignore quest-starting items

**QuestingActivity** — Nearby quest pickup:
- During WORKING_ON_QUEST or TRAVELING states, scan every ~10-15 seconds
- Check nearby NPCs against O(1) `IsQuestGiverEntry()` set
- If quest giver found and bot has free slots → brief detour to accept

### Verification
- Bot loots quest-starting item, quest appears in log
- Bot traveling past quest giver picks up available quests

---

## Phase Q11: Reward Selection

**Goal**: Bots pick appropriate quest rewards.

### New Files

**`src/game/PlayerBots/Utilities/BotStatWeights.h/.cpp`**
- `ScoreItemForClass(ItemPrototype const*, uint8 classId)` — simple heuristic
- `SelectBestReward(Quest const*, Player*)` — returns reward index

### Changes

**QuestingActivity — AT_TURN_IN state:**
- Use `BotStatWeights::SelectBestReward()` when turning in
- Pass reward index to quest completion API

### Verification
- Warriors pick STR/STA gear, mages pick INT/STA gear, etc.

---

## Phase Q12: Weighted Activity System

**Goal**: Bots are assigned activities via weighted random selection.

### Changes

**RandomBotAI or new ActivityManager:**
- On bot initialization (or periodically), roll weighted random to assign activity
- Default weights: Questing 70%, Grinding 30%
- Could be configurable via mangosd.conf or database table
- Bot keeps activity until it signals a switch (e.g., QuestingActivity runs out of quests → fallback to grinding)

### Verification
- ~70% of bots quest, ~30% grind (verify by sampling `.bot status`)
- Bots that run out of quests fall back to grinding
- Mix feels natural — not all bots doing the same thing

---

## Files Summary

### New Files:
```
src/game/PlayerBots/IBotActivity.h                          (Phase R1)
src/game/PlayerBots/Activities/GrindingActivity.h/.cpp       (Phase R1)
src/game/PlayerBots/Activities/QuestingActivity.h/.cpp       (Phase Q2)
src/game/PlayerBots/Utilities/BotQuestCache.h/.cpp           (Phase Q1)
src/game/PlayerBots/Utilities/BotObjectInteraction.h/.cpp    (Phase Q5)
src/game/PlayerBots/Utilities/BotItemUsage.h/.cpp            (Phase Q7)
src/game/PlayerBots/Utilities/BotStatWeights.h/.cpp          (Phase Q11)
```

### Modified Files:
```
src/game/PlayerBots/RandomBotAI.h/.cpp                       (Phase R1)
src/game/PlayerBots/Strategies/GrindingStrategy.h/.cpp       (Phase Q3 — target filter)
src/game/PlayerBots/Strategies/LootingBehavior.h/.cpp        (Phase Q10 — item-started quests)
src/game/PlayerBots/PlayerBotMgr.cpp                         (Phase Q1 — cache init)
src/game/CMakeLists.txt                                      (All phases)
```

### Unchanged Files:
```
All 9 class combat handlers
BotCombatMgr
BotMovementManager
VendoringStrategy
TrainingStrategy
TravelingStrategy (moved to GrindingActivity, but code unchanged)
GhostWalkingStrategy
BotCheats
IBotStrategy (behaviors still use this interface)
```

---

## Verification After Each Phase

1. Build: `cd ~/Desktop/Forsaken_WOW/core/build && make -j$(nproc) && make install`
2. Run server and observe bot behavior
3. Use `.bot status` to verify activity name and state
4. Check console logs for `[QuestingActivity]` / `[BotQuestCache]` messages
5. Verify no regressions in existing behavior

---

*Last Updated: 2026-03-14*
