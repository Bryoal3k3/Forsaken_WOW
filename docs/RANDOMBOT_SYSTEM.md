# RandomBot AI System Documentation

## Overview

RandomBots are autonomous AI-controlled player characters that grind mobs, level up, and behave like real players. They require no human input after server startup.

### What They Do
- Auto-generate on first server launch (accounts + characters)
- Spawn in the world and fight appropriate-level mobs
- Loot corpses, rest when low, sell items at vendors
- Travel to new areas when they out-level their current zone
- Handle death (ghost walk to corpse, resurrect)

### High-Level Flow
```
Server Start
    │
    ├─► First Launch? ─► Generate bot accounts/characters
    │
    ├─► Build caches (vendors, grind spots)
    │
    └─► Spawn bots from playerbot table
            │
            ▼
        ┌─────────────────────────────────────┐
        │         BOT UPDATE LOOP             │
        │                                     │
        │  Dead? ──► Ghost Walking            │
        │    │                                │
        │    ▼                                │
        │  Resting? ──► Sit and regenerate    │
        │    │                                │
        │    ▼                                │
        │  In Combat? ──► Combat AI           │
        │    │                                │
        │    ▼                                │
        │  Looting? ──► Loot nearby corpses   │
        │    │                                │
        │    ▼                                │
        │  Bags full? ──► Vendor strategy     │
        │    │                                │
        │    ▼                                │
        │  No mobs? ──► Travel to new spot    │
        │    │                                │
        │    ▼                                │
        │  Default ──► Grinding (find & kill) │
        └─────────────────────────────────────┘
```

---

## System Architecture

### Layer Diagram
```
┌─────────────────────────────────────────────────────────────┐
│                      RandomBotAI                            │
│         (Main brain - coordinates all systems)              │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │  Strategies │  │  Behaviors  │  │    Combat System    │  │
│  ├─────────────┤  ├─────────────┤  ├─────────────────────┤  │
│  │ Grinding    │  │ Looting     │  │ BotCombatMgr        │  │
│  │ Traveling   │  │             │  │   ├─ WarriorCombat  │  │
│  │ Vendoring   │  │             │  │   ├─ MageCombat     │  │
│  │ GhostWalking│  │             │  │   ├─ HunterCombat   │  │
│  └─────────────┘  └─────────────┘  │   └─ ... (9 total)  │  │
│                                    └─────────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│                    CombatBotBaseAI                          │
│            (Spell data, casting utilities)                  │
├─────────────────────────────────────────────────────────────┤
│                      PlayerBotAI                            │
│              (Base class, session handling)                 │
└─────────────────────────────────────────────────────────────┘
```

### File Structure
```
src/game/PlayerBots/
├── RandomBotAI.h/cpp           ← Main AI brain
├── BotMovementManager.h/cpp    ← Centralized movement coordinator
├── RandomBotGenerator.h/cpp    ← First-launch generation
├── PlayerBotMgr.h/cpp          ← Bot lifecycle management
├── BotCheats.h/cpp             ← Resting (no consumables needed)
│
├── Combat/
│   ├── IClassCombat.h          ← Interface for class handlers
│   ├── BotCombatMgr.h/cpp      ← Combat coordinator
│   ├── CombatHelpers.h         ← Shared helpers (uses BotMovementManager)
│   └── Classes/                ← 9 class-specific handlers
│       ├── WarriorCombat.h/cpp
│       ├── MageCombat.h/cpp
│       └── ... (all 9 classes)
│
└── Strategies/                 ← All use BotMovementManager for movement
    ├── IBotStrategy.h          ← Strategy interface
    ├── GrindingStrategy.h/cpp  ← Find and kill mobs
    ├── LootingBehavior.h/cpp   ← Loot corpses
    ├── GhostWalkingStrategy.h/cpp ← Death handling
    ├── VendoringStrategy.h/cpp ← Sell items, repair
    └── TravelingStrategy.h/cpp ← Travel to grind spots
```

### Layer Responsibilities

| Layer | Responsibility |
|-------|---------------|
| **RandomBotAI** | Coordinates strategies, tracks state, handles resting |
| **BotCombatMgr** | Creates class handler, delegates combat calls |
| **IClassCombat** | Class-specific engagement, rotations, buffs |
| **Strategies** | High-level goals (grinding, traveling, vendoring) |
| **Behaviors** | Universal actions (looting runs for all bots) |

---

## Bot Lifecycle

### Generation (First Launch Only)

Triggered when `RandomBot.Enable = 1` and no bots exist in the database.

```
PlayerBotMgr::Load()
    │
    └─► sRandomBotGenerator.GenerateIfNeeded()
            │
            ├─► Check: playerbot table empty?
            ├─► Check: No RNDBOT accounts exist?
            │
            └─► If both true: GenerateRandomBots(count)
                    │
                    ├─► Create accounts (RNDBOT001, RNDBOT002...)
                    ├─► Create characters (race-appropriate names)
                    └─► Insert into playerbot table with ai='RandomBotAI'
```

**Account/Character Distribution:**
- 6 accounts created (RNDBOT001-006)
- Each account holds ~8-9 characters (different race/class combos)
- Total: 50 bots by default (configurable)

### Spawning (Every Launch)

```
PlayerBotMgr::Load()
    │
    ├─► LoadConfig() - Read mangosd.conf settings
    │
    ├─► Query playerbot table - Load all bot entries into m_bots map
    │
    ├─► Build caches:
    │       ├─► VendoringStrategy::BuildVendorCache()
    │       └─► TravelingStrategy::BuildGrindSpotCache()
    │
    └─► AddRandomBot() - Spawn MinBots immediately
            │
            └─► For each bot:
                    ├─► CreatePlayerBotAI("RandomBotAI")
                    ├─► Create WorldSession with fake account ID
                    └─► LoginPlayer() - Bot enters world
```

### Update Loop

Each bot's `UpdateAI()` runs every 1 second (configurable via `RB_UPDATE_INTERVAL`).

```cpp
RandomBotAI::UpdateAI(diff)
{
    // 1. Throttle updates (1 second interval)
    // 2. Handle teleport acks
    // 3. Invalid position detection (fell through floor?)

    // 4. One-time initialization (first tick only)
    //    - Assign role, learn spells, initialize combat manager

    // 5. Dead? → GhostWalkingStrategy

    // 6. Track combat state transitions (for looting trigger)

    // 7. Resting? → BotCheats::HandleResting()

    // 8. In combat OR have victim? → UpdateInCombatAI()
    //    Else:
    //        - Looting behavior
    //        - UpdateOutOfCombatAI()
}
```

---

## Core Systems

### 0. Movement System (BotMovementManager)

All bot movement is centralized through `BotMovementManager`. Strategies and combat handlers never call MotionMaster directly.

**Key Features:**
| Feature | Purpose |
|---------|---------|
| Priority system | IDLE < WANDER < NORMAL < COMBAT < FORCED |
| Duplicate detection | Won't spam MoveChase to same target |
| CC validation | Won't move while stunned/rooted/confused |
| Multi-Z search | Tries 5 heights for caves/multi-story buildings |
| Path smoothing | Skips waypoints when LoS exists |
| 5-sec stuck detection | Micro-recovery (step back/sideways) before emergency teleport |

**Wiring:**
Each strategy receives movement manager via `SetMovementManager()` during RandomBotAI initialization.

---

### 1. Combat System

**Architecture:**
- `BotCombatMgr` creates the appropriate class handler based on `GetClass()`
- Each class handler implements `IClassCombat` interface
- `CombatHelpers.h` provides shared functions (engagement, movement)

**Class Engagement Types:**

| Type | Classes | Behavior |
|------|---------|----------|
| Melee | Warrior, Rogue, Paladin, Shaman, Druid | `Attack(true)` + `MoveChase()` |
| Caster | Mage, Priest, Warlock | `Attack(false)` + `MoveChase()` + spell pull |
| Hunter | Hunter | `Attack(false)` + `MoveChase()` + Auto Shot |

**Combat Flow:**
```
Engage(target)
    │
    ├─► Face target (SetFacingToObject)
    ├─► Attack(target, melee?) - Establish combat state
    └─► MoveChase(target) - Path to target
            │
            ▼
UpdateCombat(victim) [called every tick while in combat]
    │
    ├─► HandleRangedMovement() or HandleMeleeMovement()
    ├─► Class rotation (spells/abilities)
    └─► HandleCasterFallback() - Wand/melee if all spells fail
```

**Movement Helpers:**
- `HandleRangedMovement()` - Stop at 30 yards if LoS, chase if not
- `HandleMeleeMovement()` - Re-issue MoveChase if interrupted
- `HandleCasterFallback()` - Wand (spell 5019) → melee auto-attack

### 2. Grinding Strategy

**Purpose:** Find appropriate mobs and engage them.

**Target Selection Criteria:**
- Level within bot level ± 2
- Not elite, not critter, not totem
- Not tapped by others
- Visible to bot
- Hostile or neutral (attackable)
- Reachable via pathfinding

**Search Tiers:**
1. **Close range (50 yards)** - Fast local search
2. **Far range (150 yards)** - Only if tier 1 empty

**Exponential Backoff:**
When no mobs found, search frequency reduces:
- Level 1: Skip 1 tick (1 second)
- Level 2: Skip 3 ticks
- Level 3: Skip 7 ticks (max)

Resets immediately when a mob is found.

**Result Signaling:**
```cpp
enum class GrindingResult {
    ENGAGED,     // Found target, attacking
    NO_TARGETS,  // No valid mobs found
    BUSY         // In combat, looting, etc.
};
```

### 3. Looting Behavior

**Trigger:** Combat ends (tracked via `m_wasInCombat` flag)

**Flow:**
```
OnCombatEnded()
    │
    └─► Scan for lootable corpses within 30 yards
            │
            └─► For each corpse:
                    ├─► MovePoint() to corpse
                    ├─► SendLoot() packet
                    └─► AutoStoreLoot() - Items to bags
```

**Notes:**
- Uses pathfinding (`MOVE_PATHFINDING` flag)
- Only loots corpses the bot has loot rights to
- Gold auto-collected

### 4. Resting System

**Purpose:** Regenerate HP/mana without consumables.

**Thresholds:**
- Start resting: HP < 35% OR mana < 45%
- Stop resting: HP ≥ 90% AND mana ≥ 90%

**Implementation (BotCheats):**
```cpp
BotCheats::HandleResting(bot, diff, isResting, tickTimer)
{
    // If should rest and not already resting:
    //   - Stop movement
    //   - Set standing state to SIT
    //   - isResting = true

    // While resting (every 2 seconds):
    //   - Add 5% max HP
    //   - Add 5% max mana

    // If recovered enough:
    //   - Set standing state to STAND
    //   - isResting = false
}
```

**Notes:**
- No food/drink items consumed
- Bot sits (visual feedback)
- Interrupted by combat (attackers wake bot up)

### 5. Vendoring Strategy

**Triggers:**
- Bags 100% full (no free slots)
- Any equipped gear broken (0% durability)

**Pre-travel check (softer thresholds):**
- Bags > 60% full
- Any equipped gear < 50% durability

**Vendor Cache:**
Built once at startup from `creature_template` + `npc_vendor` tables.

**Flow:**
```
ShouldVendor()?
    │
    ├─► FindNearestVendor() - Query cache, filter by faction
    │
    ├─► MovePoint() to vendor (with pathfinding)
    │
    ├─► At vendor:
    │       ├─► SellAllItems() - Sell everything except equipped
    │       └─► RepairAllItems() - Repair all gear
    │
    └─► Return to grinding
```

**Vendor Selection:**
- Same faction only (or neutral)
- Closest vendor wins
- Cache stores: GUID, position, faction, vendor type

### 6. Traveling Strategy

**Purpose:** Move to level-appropriate grind spots when current area is depleted.

**Trigger:** 5 consecutive "no mobs found" results from GrindingStrategy

**Grind Spot Cache:**
- 2,684 auto-generated spots from creature spawn data
- Loaded at startup from `characters.grind_spots` table
- Covers levels 1-60, both factions, both continents

**Spot Selection:**
```
FindGrindSpot()
    │
    ├─► Filter by: map, level range, faction
    │
    ├─► Categorize:
    │       ├─► Nearby (< 800 yards) - Same zone
    │       └─► Distant (> 800 yards) - Different zone
    │
    ├─► If nearby spots exist:
    │       └─► Random pick (prevents "train" behavior)
    │
    └─► Else if distant spots:
            └─► Weighted random (closer spots more likely)
```

**Waypoint Navigation:**
Long journeys are segmented into ~200 yard waypoints:
```
GenerateWaypoints()
    │
    ├─► PathFinder calculates full path
    │
    ├─► Split into segments (MAX 200 yards each)
    │
    └─► MovePoint() to each waypoint sequentially
            │
            └─► OnWaypointReached() chains to next
```

**Stuck Detection:**
- 30 second timeout without progress
- Resets travel, picks new destination

**Anti-Thrashing:**
- 90 second cooldown after arriving at a spot
- Prevents constant spot-hopping

### 7. Death Handling

**GhostWalkingStrategy States:**
```
ALIVE → DEAD → RELEASING → GHOST_WALKING → RESURRECTING → ALIVE
```

**Flow:**
```
Bot dies
    │
    ├─► RepopAtGraveyard() - Become ghost at nearest graveyard
    │
    ├─► Find corpse position
    │
    ├─► MovePoint() to corpse (ghost can walk through walls)
    │
    ├─► At corpse:
    │       └─► RetrieveCorpse() - Resurrect
    │
    └─► Reset behaviors, resume grinding
```

**Death Loop Detection:**
- Track deaths in short time window
- If dying too often (same area, high-level mobs):
  - Use spirit healer instead of corpse run
  - Triggers travel to new area

---

## Configuration

### mangosd.conf Options

| Option | Default | Description |
|--------|---------|-------------|
| `RandomBot.Enable` | 0 | Enable the RandomBot system |
| `RandomBot.Purge` | 0 | Delete all bots on startup (set back to 0 after!) |
| `RandomBot.MinBots` | 0 | Minimum bots to keep online |
| `RandomBot.MaxBots` | 0 | Maximum bots to generate/spawn |
| `RandomBot.Refresh` | 60000 | Add/remove check interval (ms) |
| `RandomBot.DebugGrindSelection` | 0 | Log grind spot selection to console |
| `PlayerBot.AllowSaving` | 0 | Save bot progress to DB |
| `PlayerBot.Debug` | 0 | Extra debug logging |
| `PlayerBot.UpdateMs` | 1000 | AI update interval (ms) |

### Recommended Settings

**Development/Testing:**
```conf
RandomBot.Enable = 1
RandomBot.MinBots = 5
RandomBot.MaxBots = 10
RandomBot.DebugGrindSelection = 1
```

**Production:**
```conf
RandomBot.Enable = 1
RandomBot.MinBots = 50
RandomBot.MaxBots = 100
RandomBot.DebugGrindSelection = 0
```

---

## Debug Tools

### `.bot status` Command

**Usage:** Target a bot player, run `.bot status`

**Output:**
```
=== Bot Status: Graveleth ===
Level 2 Warlock | HP: 78/78 (100%)
Mana: 133/133 (100%)
Position: (1737.9, 1433.9, 114.0) Map 0
[State] Action: GRINDING | Strategy: Grinding
Moving: NO | Casting: NO
[Victim] Scarlet Convert (100% HP) | Dist: 47.1 | LoS: YES | InCombat: NO
[Motion] Type: CHASE
```

**Diagnostic Fields:**

| Field | Meaning |
|-------|---------|
| Action | What bot thinks it's doing (IDLE, GRINDING, COMBAT, RESTING, TRAVELING, VENDORING, GHOST_WALKING) |
| Strategy | Active strategy name |
| Moving | Is bot physically moving? |
| Casting | Is bot casting a spell? |
| Victim | Current target (even if not "in combat") |
| Dist | Distance to victim in yards |
| LoS | Line of sight to victim? |
| InCombat | Game's combat flag |
| Motion Type | Movement generator (IDLE, CHASE, POINT, etc.) |

### Grind Selection Logging

**Enable:** `RandomBot.DebugGrindSelection = 1`

**Console Output:**
```
[GRIND] Graveleth selected 'Auto_L2 Tirisfal' from 3 nearby, 12 distant spots
[GRIND] Linzzi traveling to 'Auto_L5 Durotar' (8 distant spots, no nearby)
```

---

## Database Tables

### realmd.account
Bot accounts with usernames `RNDBOT001`, `RNDBOT002`, etc.

### characters.characters
Bot character data (name, class, level, position, inventory).

### characters.playerbot
Links character GUID to AI type string.

| Column | Type | Description |
|--------|------|-------------|
| `char_guid` | INT | Character GUID |
| `ai` | VARCHAR | AI class name ("RandomBotAI") |

### characters.grind_spots
2,684 grind locations auto-generated from creature spawns.

| Column | Type | Description |
|--------|------|-------------|
| `id` | INT | Spot ID |
| `map_id` | INT | Map (0=Eastern Kingdoms, 1=Kalimdor) |
| `x`, `y`, `z` | FLOAT | Position |
| `min_level`, `max_level` | TINYINT | Bot level range |
| `faction` | TINYINT | 0=both, 1=alliance, 2=horde |
| `priority` | TINYINT | Selection weight |
| `name` | VARCHAR | Spot name for logging |

### characters.zone_levels
Zone boundaries for grind spot validation.

---

## Known Limitations

**What bots CAN'T do (yet):**

| Limitation | Reason |
|------------|--------|
| Cross-continent travel | No boat/zeppelin/flight path support |
| Dungeon/raid content | No group coordination AI |
| PvP combat | No player-vs-player logic |
| Professions | Not implemented |
| Questing | No quest tracking/objectives |
| Auction house | Not implemented |
| Smart threat avoidance | Danger zone system disabled pending testing |
| Use consumables | Resting uses cheat regen instead |

**Known Edge Cases:**
- Bots may path through hostile camps when traveling
- Very steep terrain can still cause stuck issues (rare)
- Some cave/building entrances may be problematic

---

## Appendix: Key Constants

### RandomBotAI
- `RB_UPDATE_INTERVAL`: 1000ms (1 second between AI ticks)
- `INVALID_POS_THRESHOLD`: 15 ticks before recovery teleport

### GrindingStrategy
- `SEARCH_RANGE_CLOSE`: 50 yards (tier 1)
- `SEARCH_RANGE_FAR`: 150 yards (tier 2)
- `LEVEL_RANGE`: ±2 levels from bot
- `BACKOFF_MAX_LEVEL`: 3 (max 8 second delay)

### TravelingStrategy
- `ARRIVAL_DISTANCE`: 30 yards (consider "arrived")
- `STUCK_TIMEOUT_MS`: 30000ms (30 seconds)
- `WAYPOINT_SEGMENT_DISTANCE`: 200 yards
- `ARRIVAL_COOLDOWN_MS`: 90000ms (90 seconds)
- `NO_MOBS_THRESHOLD`: 5 ticks before travel

### VendoringStrategy
- Bag threshold: 100% full (no free slots)
- Durability threshold: 0% (gear broken)
- Pre-travel bag threshold: 60%
- Pre-travel durability threshold: 50%

### BotCheats (Resting)
- Start HP threshold: 35%
- Start mana threshold: 45%
- Stop threshold: 90% (both HP and mana)
- Regen per tick: 5% of max
- Tick interval: 2 seconds

---

*Last Updated: 2026-01-30 (Added BotMovementManager)*
