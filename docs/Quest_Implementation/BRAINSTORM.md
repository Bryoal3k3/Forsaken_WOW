# Questing System Brainstorm

**Status**: Planning / Research (Revised)
**Created**: 2026-02-02
**Last Updated**: 2026-02-09

---

## Overview

Adding questing to bots would make them feel more alive and level more naturally. This document captures our brainstorming on how to approach this incrementally.

---

## Quest Types Analysis

### Kill Quest (Priority: HIGH)
**User Take**: Easy lookup required creature, make bot run towards known "crowded" areas.

**Technical Notes**:
- `quest_template.ReqCreatureOrGOId1-4` contains target creature entries (positive values = creatures)
- `quest_template.ReqCreatureOrGOCount1-4` contains kill counts
- Extends existing `GrindingStrategy` - just filter by creature entry instead of "anything nearby"
- Use `creature` spawn table to find where required mobs spawn (has exact position per creature entry)
- Player quest progress tracked via `Player::GetQuestSlotQuestId()`, `Player::GetQuestSlotCounter()`

**Multi-objective note**: Many quests have multiple kill targets (e.g., "Kill 10 Boars AND 8 Wolves"). `ReqCreatureOrGOId1-4` supports up to 4 separate kill targets per quest. QuestingStrategy must track ALL objectives simultaneously, not just one.

**Complexity**: Low - builds on existing systems

---

### Collect Quest (Priority: HIGH)
**User Take**: Similar path as kill quest.

**Technical Notes**:
- Two sub-types:
  1. **Mob drops**: Kill mobs, loot items (already works via LootingBehavior)
  2. **World objects**: Interact with gameobjects to collect (needs new capability)
- `quest_template.ReqItemId1-6` and `ReqItemCount1-6` define requirements
- `creature_loot_template` links creatures to loot items
- For world objects: `gameobject_loot_template`
- Note: Many quest item drops only occur when the quest is active in the player's log
- **Multi-objective**: Quests can require BOTH kills and item collection simultaneously (e.g., "Kill 10 Gnolls AND collect 5 Gnoll Paws"). Up to 4 kill/GO targets + 6 item requirements per quest. QuestingStrategy must handle mixed-type objectives in a single quest.

**Complexity**: Low for mob drops, Medium for world objects (needs object interaction)

---

### Go to Location / Exploration Quest (Priority: MEDIUM)
**User Take**: Find the completion trigger, tell bot to path.

**Technical Notes**:
- Exploration quests use a **separate system** from kill/collect quests
- `areatrigger_involvedrelation` maps areatrigger IDs to quest IDs
- Quest must have `QUEST_SPECIAL_FLAG_EXPLORATION_OR_EVENT` flag set
- When player enters the areatrigger, `Player::AreaExploredOrEventHappens()` fires automatically
- Bot just needs to travel to the areatrigger coordinates - completion is automatic
- Note: `ReqCreatureOrGOId` negative values are gameobject entries, NOT areatrigger IDs
- Some "go to" quests are simply "talk to NPC at location X" (talk quest variant)
- `TravelingStrategy` already handles long-distance movement

**Complexity**: Low - mostly just travel

---

### Escort Quest (Priority: DEFERRED)
**User Take**: Make sure bot doesn't travel X yards away from NPC, clear upon completion.

**Technical Notes**:
- Escort quests use scripted NPCs that path on their own
- Bot needs to:
  1. Accept quest (triggers NPC movement)
  2. Stay within ~30 yards of escort NPC
  3. Kill any mobs that aggro the NPC
  4. Detect quest completion/failure
- Could track escort NPC via `GetQuestNpcGuid()` or scan for friendly NPCs with matching entry
- Combat priority: attackers of escort NPC > normal targets

**Complexity**: Medium-High - needs reactive behavior, escort NPCs are fragile

---

### Interact with Object (Priority: HIGH - PREREQUISITE)
**User Take**: Very important, one of the key things we need bots to be able to do.

**Technical Notes**:
- **This is NEW functionality** - bots currently cannot interact with gameobjects
- `gameobject` table has spawn locations
- `gameobject_template` has type, flags, interaction data
- Relevant types:
  - `GAMEOBJECT_TYPE_QUESTGIVER` (2) - quest givers
  - `GAMEOBJECT_TYPE_CHEST` (3) - lootable containers
  - `GAMEOBJECT_TYPE_GOOBER` (10) - interaction triggers
- To interact: `GameObject::Use(player)` or send `CMSG_GAMEOBJ_USE` packet
- Need to find nearby objects: `Cell::VisitGridObjects()` + `NearestGameObjectEntryInObjectRangeCheck`

**Complexity**: Medium - new system but mechanics are straightforward

---

### Use Item (Priority: HIGH - PREREQUISITE)
**User Take**: Have bots lookup item in inventory and use it.

**Technical Notes**:
- **This is NEW functionality** - bots don't use inventory items currently
- `Player::GetItemByEntry(itemId)` finds item in bags
- `Player::UseItem(item, targets)` or `CastItemUseSpell()`
- Some items need targets (use on mob, use on object, use at location)
- `item_template.Spells[0-4]` defines what spell the item casts
- Quest items often have `ITEM_FLAG_CONJURED` or specific spell triggers

**Complexity**: Medium - need to handle different target types

---

### Talk to NPC (Priority: MEDIUM)
**User Take**: Have bot target and interact.

**Technical Notes**:
- Similar to existing vendoring/training - move to NPC, interact
- `Creature::HandleGossipOption()` for gossip menu navigation
- Some NPCs require specific gossip choices (menu navigation)
- Simple case: just right-click NPC to trigger quest completion
- **Important**: Turn-in NPC is often a DIFFERENT NPC than the quest giver
  - `creature_questrelation` = who gives the quest
  - `creature_involvedrelation` = who accepts the turn-in
  - Travel logic must handle going to a completely different location for turn-in

**Complexity**: Low for simple cases, Medium if gossip menus need navigation

---

### Dungeon Quests (Priority: DEFERRED)
**User Take**: Handle at a later time.

**Technical Notes**:
- Requires party coordination (existing PartyBotAI handles this)
- Requires dungeon navigation (no navmesh inside instances?)
- Requires role awareness (tank, healer, DPS)
- Much more complex - defer until open world questing works

**Complexity**: Very High - future phase

---

### Group/Elite Quests (Priority: DEFERRED)
**User Take**: Will be handled when bot grouping system is implemented.

**Technical Notes**:
- `quest_template.SuggestedPlayers` indicates group quests
- Some quests require killing elite mobs that solo bots cannot handle
- Bots will need to form groups and coordinate to complete these
- Defer to future grouping system - not filtered out permanently, just not attempted solo

**Complexity**: High - requires grouping AI

---

## Design Decisions

### Quest Selection
- **Level range**: Pick quests equal to bot level, down to 4 levels below
- **Zone**: Same zone preferred (already near quest objectives)
- **Type**: Solo-completable only until grouping system is built
- **Class/Race filtering**: Must check `quest_template.RequiredClasses` and `quest_template.RequiredRaces` - a Warrior should never try to pick up a Warlock pet quest
- **Server-side validation**: `Player::CanTakeQuest()` handles level, class, race, and chain prerequisites - always use this as the final gate

### Quest Log Management & Abandonment
- **Vanilla limit**: 20 quests maximum in the quest log
- **Active management**: Don't hoard quests the bot can't currently work on
- **Prioritize turn-ins**: Completed quests free up log slots for new ones
- **Abandon grey quests**: When a quest becomes grey (too far below bot level), abandon it - minimal XP, not worth completing
- **Abandon stale quests**: If a quest has been in the log too long without progress, consider abandoning to free slots
- **Goal**: Keep the quest log lean so there's always room for new quests

### Quest Chains
- **Key fields**: `quest_template.PrevQuestId` and `NextQuestId` define chain order
- **Server-side handling**: `Player::CanTakeQuest()` already validates chain prerequisites
- **Bot behavior**: If a quest giver has no available quests (all locked behind chains), the bot should move on rather than getting stuck
- **Breadcrumb quests**: Some quests just send you to another NPC/zone - these are essentially "talk to NPC" quests that serve as travel triggers and should be handled naturally

### Reward Selection
- **Phase 1 approach**: Simple class-based heuristic (good enough for leveling gear)

  | Class | Pick gear with... |
  |-------|-------------------|
  | Warrior/Paladin | STR, STA |
  | Rogue/Hunter | AGI, STA |
  | Mage/Warlock/Priest | INT, STA |
  | Druid/Shaman | INT or AGI (coin flip) |

- **Future refinement**: Full spec detection + stat weights will be built as Phase 0 prerequisites
- **Why both**: Simple heuristic gets questing working fast; spec-aware system improves quality at higher levels where gear choices matter more

### Spec Detection
- **Source**: Database stores talent data
- **Method**: Query talent tables to determine which tree has the most points
- **Fallback**: If no talents yet (low level), use class defaults
- **Note**: Meaningless below ~level 20 when bots have few talent points

### Strategy Integration
- **Weighted task system**: An external system (built separately, NOT part of QuestingStrategy) assigns bots their current task: questing, grinding, gathering, etc.
- **Implementation**: The weighted task system swaps `RandomBotAI::m_strategy` between `GrindingStrategy`, `QuestingStrategy`, etc. The `IBotStrategy::Update()` interface stays the same
- **Universal behaviors still run**: Vendoring, training, resting, looting all run regardless of which strategy is active (same as current architecture)
- **Fallback**: If QuestingStrategy runs out of quests and can't find a quest giver, it can signal for a strategy switch (similar to how GrindingStrategy signals TravelingStrategy when no mobs are found)

### Kill Sub-Tasks (QuestingStrategy + GrindingStrategy)
- When QuestingStrategy has a kill objective, it needs GrindingStrategy's mob-finding and engagement capabilities
- **Approach**: QuestingStrategy composes a `GrindingStrategy` instance internally as a member
- QuestingStrategy sets a creature entry filter on its internal GrindingStrategy (via `SetQuestTargetFilter()`)
- The internal GrindingStrategy handles scanning, target selection, approach, and combat engagement
- QuestingStrategy monitors quest progress and clears the filter / switches objectives when done
- This avoids duplicating mob-scanning logic and reuses the proven GrindingStrategy code

### Timed Quests
- **Decision**: Avoid initially
- **Rationale**: `quest_template.LimitTime` quests add complexity (priority scheduling, failure handling) with low reward - very few quests use timers
- **Future**: Can add support later when core questing is solid

---

## Quest Discovery

### Quest Giver Cache (Built at Server Startup)

Follows the same proven pattern as `VendorLocation`, `TrainerLocation`, and `GrindSpotData` caches:

**How it works:**
1. At server boot, build from TWO sources:
   - **Creature quest givers**: `creature_questrelation` + `quest_template` + `creature` spawn table
   - **Gameobject quest givers**: `gameobject_questrelation` + `quest_template` + `gameobject` spawn table (wanted posters, signposts, etc.)
2. Build an in-memory index of ALL quest givers (NPCs and objects): position, quests offered, requirements (level, class, race, faction, prevQuestId)
3. Group nearby quest givers into **clusters** (within ~100 yards of each other)
4. Static vector + mutex, shared across all bots - zero runtime DB queries

**Cluster concept:**
- A cluster is just a grouping of nearby quest givers (a town, a camp, a hub)
- Solo NPCs/objects in the wilderness are clusters of size 1
- When a bot needs quests, find the nearest cluster with available quests **filtered by the bot's level, class, race, and faction**
- At a cluster, the bot picks up ALL available quests from quest givers in that cluster (efficient)

**Cross-map filtering:**
- Quest selection must check that objectives are completable on the bot's current map
- If a quest's kill/collect targets only spawn on a different continent, skip it (no boat/zeppelin support)
- Filter during quest acceptance, not during cache building (cache is shared, objective locations are per-quest)

**Scalability (2500-3000 bots):**
- Cache built once, shared by all bots (static singleton pattern)
- Per-bot quest state tracked individually (quest log, progress)
- All lookups are fast in-memory searches (indexed by zone, level range, faction)
- No per-tick database queries

### Turn-in NPC/Object Cache

Separate cache for turn-in targets since they are often DIFFERENT from quest givers:
- Built from TWO sources:
  - **Creature turn-ins**: `creature_involvedrelation` + `creature` spawn table
  - **Gameobject turn-ins**: `gameobject_involvedrelation` + `gameobject` spawn table
- Indexed by quest ID for fast lookup when a quest is complete
- Same static cache pattern

### Intentional Discovery
- Bot has no quests or completed all current quests
- Query cache: find nearest quest giver(s) with available quests for this bot
- Travel there, pick up quests
- Prefer clusters (more quests per trip) but don't skip solo NPCs if they're closer

### Opportunistic Discovery
- Bot is traveling, grinding, or doing other activities
- Detect quest giver NPCs within a detection radius (e.g., 30-50 yards)
- If the NPC has available quests for this bot, grab them on the way
- Hooks into the existing update loop with a cooldown (check every ~10-15 seconds, not every tick)
- Track "already checked" NPCs to avoid re-scanning the same quest giver repeatedly
- Makes bots feel natural: grinding, stumbles across an NPC with a `!`, picks up the quest

### Item-Started Quests
- **Completely missed in original brainstorm** - many vanilla quests start from item drops
- Examples: head/trophy items from named mobs, mysterious notes, rare drops that begin chains
- `item_template.StartQuest` field - if non-zero, using the item starts that quest
- **Hook point**: `LootingBehavior` - after looting a corpse, check if any looted items have `StartQuest > 0`
- If `Player::CanTakeQuest()` passes for that quest, auto-accept it
- No special routing needed - happens organically during normal play
- This is one of the most natural quest behaviors: bot kills mob, loots item, quest appears in log
- **Only accept if bot is in questing mode** - grinding-only bots should ignore quest-starting items to avoid wasting quest log slots on quests they won't turn in

---

## Prerequisites Before Questing

Before implementing full questing, we need these foundational capabilities. These are useful standalone features even without questing.

### 1. Object Interaction System
```
New files needed:
- Utilities/BotObjectInteraction.h/cpp

Capabilities:
- Find nearby gameobjects by entry ID
- Move to object position
- Interact with object (Use)
- Handle loot window if object is lootable
```

### 2. Item Usage System
```
New files needed:
- Utilities/BotItemUsage.h/cpp

Capabilities:
- Find item in bags by entry ID
- Determine if item is usable (has spell, not on cooldown)
- Use item with appropriate target:
  - Self-cast items
  - Target-cast items (on mob, on NPC)
  - Ground-target items (at location)
  - Object-target items
```

### 3. Spec Detection System
```
New files needed:
- Utilities/BotSpecDetection.h/cpp

Capabilities:
- Determine bot spec from talent distribution
- Categorize as healer/tank/dps
- Used for reward selection (refined version)
- Fallback to class defaults at low levels
```

### 4. Stat Weight System (for rewards)
```
New files needed:
- Utilities/BotStatWeights.h/cpp

Capabilities:
- Define stat priorities per class/spec
- Score items based on stat weights
- Compare quest reward options
- Pick best reward for bot's spec
```

**Note on Phase 0C/0D**: These systems are built as infrastructure but NOT actively used until higher levels. During early questing phases, reward selection uses the simple class-based heuristic from Design Decisions > Reward Selection. Once bots reach levels where gear choices matter (40+), the spec-aware stat weight system kicks in.

---

## Database Tables Reference

| Table | Purpose |
|-------|---------|
| `quest_template` | Quest definitions, objectives, rewards, chain links, class/race requirements |
| `creature_questrelation` | NPCs that **give** quests |
| `creature_involvedrelation` | NPCs that **accept turn-ins** (often different from giver!) |
| `gameobject_questrelation` | Objects that give quests |
| `gameobject_involvedrelation` | Objects that accept turn-ins |
| `creature_loot_template` | What items drop from mobs |
| `gameobject_loot_template` | What items come from objects |
| `areatrigger_involvedrelation` | Exploration quest triggers |
| `item_template` | Item data - `StartQuest` field for item-started quests |
| `creature` | NPC spawn table - positions, entries (used for cache building + mob location lookup) |
| `gameobject` | Object spawn table - positions, entries (used for cache building + world object quests) |

### Key quest_template Fields
| Field | Purpose |
|-------|---------|
| `RequiredClasses` | Bitmask - which classes can take this quest |
| `RequiredRaces` | Bitmask - which races can take this quest |
| `PrevQuestId` | Previous quest in chain (must be completed first) |
| `NextQuestId` | Next quest in chain (unlocked after this one) |
| `SuggestedPlayers` | Suggested group size (>1 = group quest, defer) |
| `LimitTime` | Time limit in seconds (0 = no limit, avoid these initially) |
| `ReqCreatureOrGOId1-4` | Kill targets (positive = creature, negative = gameobject) |
| `ReqCreatureOrGOCount1-4` | Required kill/interaction counts |
| `ReqItemId1-6` | Required items to collect |
| `ReqItemCount1-6` | Required item counts |
| `ExclusiveGroup` | Mutually exclusive quests - completing one makes others in group unavailable |
| `QuestFlags` | Flags like SHARABLE, STAY_ALIVE (fail on death), etc. |
| `SpecialFlags` | `QUEST_SPECIAL_FLAG_EXPLORATION_OR_EVENT` = area trigger completion |

---

## Proposed Implementation Order

### Phase 0: Prerequisites (Non-Questing)
1. **Object Interaction** - Ability to interact with gameobjects
2. **Item Usage** - Ability to use items from inventory
3. **Spec Detection** - Determine bot spec from talents
4. **Stat Weights** - For reward selection (starts with class-based heuristic)

### Phase 1: Quest Infrastructure
1. Quest giver cache - creatures AND gameobjects (startup, static singleton - same pattern as vendor/trainer caches)
2. Turn-in cache - creatures AND gameobjects (separate from quest giver cache, indexed by quest ID)
3. Quest acceptance from NPCs/objects
4. Quest turn-in to NPCs/objects (may require traveling to a different location than quest giver)
5. Cross-map objective filtering (skip quests with objectives on another continent)
6. Quest log management (20 quest limit, grey/stale quest abandonment)
7. Class/race/faction/chain filtering via `Player::CanTakeQuest()`
8. QuestingStrategy skeleton with state machine

### Phase 2: Kill Quests
1. Parse kill objectives from `quest_template`
2. Filter GrindingStrategy to target specific creatures
3. Track kill progress
4. Return to turn-in NPC when complete (may differ from quest giver)

### Phase 3: Collect Quests (Mob Drops)
1. Parse item objectives
2. Identify which mobs drop required items
3. Grind those mobs, loot as normal
4. Track item collection progress

### Phase 4: Collect Quests (World Objects)
1. Find gameobjects that contain quest items
2. Travel to and interact with objects (uses Phase 0 Object Interaction)
3. Loot items from objects

### Phase 5: Exploration Quests
1. Parse destination from quest data
2. Travel to location
3. Trigger area-based completion

### Phase 6: Item Usage Quests
1. Identify quest items that need to be used (uses Phase 0 Item Usage)
2. Determine target type (self, mob, object, location)
3. Use item appropriately

### Phase 7: Opportunistic Features
1. Item-started quests (loot hook in LootingBehavior)
2. Opportunistic quest pickup from nearby NPCs while traveling/grinding

### Future: Escort Quests
- Requires more sophisticated reactive behavior
- Defer until core questing is solid

### Future: Group/Elite Quests
- Requires bot grouping system
- Bots form parties to tackle elite quests together

---

## Architecture Vision

```
External Weighted Task System (separate from QuestingStrategy):
  Assigns each bot a primary task based on weights:
    - Questing (weight: X)
    - Grinding (weight: Y)
    - Gathering (weight: Z, future)
    - etc.
  Swaps RandomBotAI::m_strategy pointer accordingly

GRINDING mode (current system, m_strategy = GrindingStrategy):
  GrindingStrategy -> LootingBehavior -> VendoringStrategy -> TrainingStrategy

QUESTING mode (new, m_strategy = QuestingStrategy):
  QuestingStrategy (self-contained)
    +-- No quests? -> Find nearest quest giver(s) -> Travel -> Pick up quests
    +-- Has quests? -> Select one -> Work on objectives
    |     +-- Kill objective -> GrindingStrategy with creature filter
    |     +-- Collect (mob) -> kill specific mobs, loot as normal
    |     +-- Collect (object) -> travel to gameobject, interact
    |     +-- Explore objective -> travel to location
    |     +-- Use item objective -> use quest item on target
    +-- Quest complete? -> Travel to turn-in NPC -> Turn in -> Repeat
    +-- No quests available? -> Signal for strategy switch (fallback to grinding)
  (vendoring, training, resting, looting all still run - universal behaviors)

Opportunistic (only when QuestingStrategy is active):
  - Detect quest giver NPCs nearby while working on other objectives
  - Check looted items for StartQuest field
  - Accept available quests organically
```

---

## Notes

- Work incrementally - one quest type at a time
- Focus on prerequisites first (object interaction, item usage, spec detection, stat weights)
- These are useful standalone features even without questing
- Kill quests alone would be a huge improvement over pure grinding
- QuestingStrategy is self-contained - handles acquisition, execution, and turn-in
- All caches follow the same proven pattern: static vector, built at startup, shared across all bots
- Scalability is critical - design everything for 2500-3000 concurrent bots
- `Player::CanTakeQuest()` is the ultimate gate for quest eligibility - always defer to it
- Turn-in NPCs and quest givers are often different - never assume they're the same

---

*Last Updated: 2026-02-09*
