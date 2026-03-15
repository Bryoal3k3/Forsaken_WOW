# Questing System Brainstorm

**Status**: Planning
**Created**: 2026-03-14

---

## Overview

Adding questing to RandomBots to make them level more naturally. This also requires an architecture refactor — introducing a tiered Activity system that cleanly supports questing, grinding, and all future bot activities (crafting, gathering, dungeons, RP).

---

## Architecture: Three-Tier System

The current architecture has grinding hardwired into RandomBotAI's update loop. Questing (and future activities) needs a cleaner model. The solution is three tiers:

### Tier 0: Survival (ALWAYS runs, non-negotiable)
These run for every bot, every tick, regardless of activity. No activity can disable them.

- **Ghost walking** — bot is dead, must walk to corpse or use spirit healer
- **React to attackers** — self preservation, fight back when attacked
- **Resting** — low HP/mana, must recover before doing anything else

### Tier 1: Activity Layer (weighted selection, one active per bot)
An activity is a **coordinator** — it decides what the bot should be doing and delegates execution to Tier 2 behaviors. Each activity declares which behaviors it allows.

- **GrindingActivity** — find mobs, kill them, travel when area depleted
- **QuestingActivity** — find quest givers, accept quests, complete objectives, turn in
- **Future**: CraftingActivity, GatheringActivity, DungeonActivity, RPActivity

The **weighted activity system** assigns each bot an activity. Weighting favors questing (~70/30 over grinding), so most bots quest but some pure grinders still exist for variety.

### Tier 2: Behaviors (reusable building blocks)
Behaviors are the actual "doers" — they know how to perform a specific action but don't decide when to perform it. Any activity can use any behavior. Behaviors don't know which activity is using them.

- **GrindingBehavior** — scan for mobs, approach, engage (currently GrindingStrategy)
- **TravelingBehavior** — navigate to coordinates (currently TravelingStrategy)
- **LootingBehavior** — loot nearby corpses (already exists)
- **VendoringBehavior** — find vendor, sell items, repair (currently VendoringStrategy)
- **TrainingBehavior** — find trainer, learn spells (currently TrainingStrategy)
- **NPCInteractionBehavior** — generalized NPC interaction (new, extracted from vendor/trainer pattern)
- **ObjectInteractionBehavior** — interact with gameobjects (new, needed for questing)
- **ItemUsageBehavior** — use items from inventory (new, needed for quest items)

### Activity → Behavior Permissions

Each activity declares which behaviors it allows. RandomBotAI checks these before running optional behaviors (vendoring, training, looting).

| Behavior | Grinding | Questing | Dungeons | RP |
|----------|----------|----------|----------|------|
| Looting | Yes | Yes | Yes | No |
| Vendoring | Yes | Yes | No | No |
| Training | Yes | Yes | No | No |
| Traveling | Yes | Yes | No | Yes |
| GrindingBehavior | Yes | Yes (filtered) | No | No |
| NPC Interaction | No | Yes | No | Yes |
| Object Interaction | No | Yes | No | No |
| Item Usage | No | Yes | No | No |

### Update Flow (after refactor)

```
RandomBotAI::UpdateAI():
  Movement manager update
  Level-up detection

  // Tier 0: Survival (always, no exceptions)
  if dead → ghost walk → return
  if resting → rest → return

  // Combat (always, no exceptions)
  Track combat state transitions (for post-combat looting trigger)
  if in combat or has victim → UpdateInCombatAI() → return

  // Tier 2: Activity-allowed behaviors (gated by activity permissions)
  if activity allows LOOTING → looting check → return if busy
  if activity allows TRAINING → training check → return if busy
  if activity allows VENDORING → vendoring check → return if busy

  // Out-of-combat buffs (always)
  m_combatMgr->UpdateOutOfCombat()

  // Tier 1: Activity update (the main coordination)
  m_currentActivity->Update()
```

### Performance at 2500+ Bots

The tier system adds **zero meaningful overhead**. Per bot, per tick, it's one extra virtual function call (activity's Update) and a bitmask check for behavior permissions. At 2500 bots that's nanoseconds. The expensive operations (pathfinding, cache lookups, grid scans, spell casts) are identical regardless of architecture.

Memory: ~100-200 bytes per activity instance. 2500 bots = ~500KB. Negligible.

The weighted activity selection runs once when assigning/switching activities, not every tick.

---

## Quest Design Decisions

### Quest Approach: Generic with Blacklist
Bots attempt ANY quest they can accept. No curated whitelist. If specific quests cause problems during testing, add them to a blacklist table. Most vanilla quests are straightforward kill/collect and will work fine.

### Quest Discovery: Hybrid (Cache + Locality Preference)
A cache of all quest givers is built at server startup (same pattern as vendor/trainer caches). However, bots prefer quest givers **in their current area first**:

1. Quests in current area/subzone
2. Quests elsewhere in current zone
3. Neighboring zones (only when current zone is dry)
4. Never cross continents (no boats/zeppelins)

This prevents the robotic "beeline across the map to optimal quest giver" behavior while still keeping bots from getting stuck.

### Quest Selection Heuristic
When a bot has multiple quests in its log, prioritize which to work on:

1. **Lowest level quest first** — clear out easy quests
2. **Same-level tiebreak** — if multiple quests at same level, pick closest objective
3. **Multi-quest overlap** — build a target list from ALL active quest objectives. Prioritize mobs that satisfy multiple quests simultaneously (e.g., killing Defias Bandits for a kill quest AND a bandana drop quest at the same time)

### Cluster Behavior
When a bot arrives at a quest hub, it does ALL business before leaving:
- Accept all available quests from that NPC
- Check for other quest givers within ~50-100 yards, grab those quests too
- Same for turn-ins: if multiple completed quests have turn-in NPCs in the same area, turn them all in

### Quest Log Management
Vanilla limit: 20 quests. Bots should manage this actively:
- **Accept freely** up to ~15-16, keeping slots open for opportunistic pickups
- **Abandon grey quests** — too far below bot level, XP is worthless
- **Abandon after soft timeout** — 15 min with no meaningful progress event
- **Prioritize turn-ins** — completed quests waste log slots, turn in ASAP

### Progress Tracking: Soft Timeout
A bot tracks "time since last meaningful event" per quest. What counts as meaningful depends on quest type:

| Quest Type | Meaningful Event |
|------------|-----------------|
| Kill quest | Killed a quest target creature |
| Collect quest (mob drops) | Killed a relevant mob (even if no drop — bad RNG isn't "stuck") |
| Collect quest (world objects) | Interacted with a quest object |
| Exploration/travel | Significant distance moved toward objective |
| Turn-in travel | Significant distance moved toward turn-in NPC |

If 15 minutes pass with no meaningful event, the bot abandons the quest and moves on. No hard ceiling — if a bot is making slow but steady progress, it persists.

This naturally handles impossible quests (can't path to mobs, scripted event required, etc.) without needing to identify them upfront.

### Quest Chains
- `quest_template.PrevQuestId` / `NextQuestId` define chains
- `Player::CanTakeQuest()` validates prerequisites server-side — always use as final gate
- If a quest giver has no available quests (all chain-locked), bot moves on
- Breadcrumb quests ("go talk to NPC in other zone") handled naturally as travel-to-NPC quests
- Cross-continent breadcrumbs will soft-timeout and get abandoned (which is fine)

### Reward Selection
Phase 1: Simple class-based heuristic:

| Class | Prefer stats |
|-------|-------------|
| Warrior, Paladin | STR, STA |
| Rogue, Hunter | AGI, STA |
| Mage, Warlock, Priest | INT, STA |
| Druid, Shaman | INT or AGI (coin flip) |

Future: Spec-aware stat weights using talent tree analysis. Built as-needed, not upfront.

### Bot Competition at Quest Hubs
3000 bots at the same level wanting the same quests is **authentic fresh-server behavior**. This is not a bug — it makes the world feel alive. No special handling needed.

### Combat
Combat stays exactly where it is — in RandomBotAI as a first-check before anything else. It's not a behavior, not an activity. If future activities need role-aware combat (dungeon tank/healer/DPS), that's a problem inside the combat system, not the architecture.

---

## Quest Types (Implementation Priority)

### Kill Quest (Priority: HIGH)
- `quest_template.ReqCreatureOrGOId1-4` (positive = creature entries)
- `quest_template.ReqCreatureOrGOCount1-4` for counts
- Up to 4 kill targets per quest (multi-objective)
- GrindingBehavior with creature entry filter — reuse existing mob scanning code
- Use `creature` spawn table to find where required mobs spawn

### Collect Quest — Mob Drops (Priority: HIGH)
- `quest_template.ReqItemId1-6` and `ReqItemCount1-6`
- Reverse lookup cache: item entry → creature entries that drop it (built from `creature_loot_template` at startup)
- GrindingBehavior with filter for mobs that drop needed items
- Existing LootingBehavior handles the actual looting — no changes needed
- Mixed quests (kills AND items) combine creature entries from both objectives

### Collect Quest — World Objects (Priority: MEDIUM)
- `ReqCreatureOrGOId1-4` negative values = gameobject entries (negate to get entry)
- Requires ObjectInteractionBehavior (new)
- Find gameobject spawns from `gameobject` table, travel to location, interact

### Exploration Quest (Priority: MEDIUM)
- `QUEST_SPECIAL_FLAG_EXPLORATION_OR_EVENT` flag
- `areatrigger_involvedrelation` maps areatrigger → quest
- Bot just travels to areatrigger coordinates — completion is automatic
- "Talk to NPC" variants: travel to NPC, interact (same as turn-in flow)

### Use Item Quest (Priority: MEDIUM)
- Quest items with spells in `item_template.Spells[0-4]`
- Requires ItemUsageBehavior (new)
- Handle target types: self-cast, target-cast (on mob/NPC), ground-target (at location)

### Escort Quest (Priority: DEFERRED)
- Requires reactive behavior, escort NPCs are fragile
- Defer until core questing is solid

### Dungeon/Group/Elite Quests (Priority: DEFERRED)
- Requires party system and dungeon AI
- Not filtered out — just not attempted solo

---

## Quest Infrastructure

### Quest Caches (Built at Server Startup)

Same proven pattern as vendor/trainer/grindspot caches: static vectors, built once, shared across all bots, immutable at runtime. Zero runtime DB queries.

**Quest Giver Cache:**
- Built from `creature_questrelation` + `gameobject_questrelation` + spawn tables
- Stores position, quest IDs offered, pre-computed filter fields (level range, class/race masks, objective map mask)
- Partitioned by map ID for fast lookup
- O(1) "is this NPC a quest giver?" set for opportunistic scanning

**Turn-in Cache:**
- Built from `creature_involvedrelation` + `gameobject_involvedrelation` + spawn tables
- Indexed by quest ID for O(1) lookup when a quest is complete
- Turn-in NPC is often DIFFERENT from quest giver — never assume they're the same

**Item Drop Cache:**
- Built from `creature_loot_template`
- Reverse map: item entry → creature entries that drop it
- Eliminates all runtime DB queries for collect quests

### Quest Blacklist
Simple table or config list of quest IDs bots should never attempt. Populated during testing when specific quests are found to cause problems. Empty by default.

---

## Database Tables Reference

| Table | Purpose |
|-------|---------|
| `quest_template` | Quest definitions, objectives, rewards, chain links |
| `creature_questrelation` | NPCs that give quests |
| `creature_involvedrelation` | NPCs that accept turn-ins |
| `gameobject_questrelation` | Objects that give quests |
| `gameobject_involvedrelation` | Objects that accept turn-ins |
| `creature_loot_template` | What items drop from mobs |
| `gameobject_loot_template` | What items come from objects |
| `areatrigger_involvedrelation` | Exploration quest triggers |
| `item_template` | Item data, `StartQuest` field for item-started quests |
| `creature` | NPC spawn positions |
| `gameobject` | Object spawn positions |

### Key quest_template Fields

| Field | Purpose |
|-------|---------|
| `RequiredClasses` | Bitmask — which classes can take this quest |
| `RequiredRaces` | Bitmask — which races can take this quest |
| `PrevQuestId` | Previous quest in chain (must be completed first) |
| `NextQuestId` | Next quest in chain |
| `SuggestedPlayers` | Group size hint (>1 = group quest) |
| `LimitTime` | Time limit in seconds (avoid these initially) |
| `ReqCreatureOrGOId1-4` | Kill targets (positive=creature, negative=gameobject) |
| `ReqCreatureOrGOCount1-4` | Required kill/interaction counts |
| `ReqItemId1-6` | Required items to collect |
| `ReqItemCount1-6` | Required item counts |
| `ExclusiveGroup` | Mutually exclusive quests |
| `QuestFlags` | SHARABLE, STAY_ALIVE, etc. |
| `SpecialFlags` | EXPLORATION_OR_EVENT = areatrigger completion |

---

## Opportunistic Features (Later Phase)

### Item-Started Quests
- Many vanilla quests start from item drops (`item_template.StartQuest`)
- Hook in LootingBehavior: after looting, check if any items have `StartQuest > 0`
- If bot is in questing mode and `Player::CanTakeQuest()` passes, auto-accept
- Only accept if questing — grinding-only bots ignore to avoid filling quest log

### Opportunistic Quest Pickup
- While working on objectives or traveling, scan for nearby quest giver NPCs every ~10-15 seconds
- O(1) check via `IsQuestGiverEntry()` set lookup
- If found and bot has free quest log slots, detour briefly to pick up quests
- Makes bots feel natural: grinding for a quest, stumbles across NPC with `!`, grabs quest

---

## Notes

- Prerequisites (object interaction, item usage, spec detection) built as-needed alongside the phases that require them, not upfront
- Refactor into tier system FIRST before building questing — verify existing behavior is unchanged
- `Player::CanTakeQuest()` is the ultimate gate for quest eligibility — always defer to it
- All caches follow the proven pattern: static, built once at startup, shared across all bots
- Design for 2500-3000 concurrent bots — all lookups must be fast in-memory operations

---

*Last Updated: 2026-03-14*
