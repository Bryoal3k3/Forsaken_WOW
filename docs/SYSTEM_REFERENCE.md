# RandomBot AI System Reference

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
    |
    +-> First Launch? -> Generate bot accounts/characters
    |
    +-> Build caches (vendors, grind spots)
    |
    +-> Spawn bots from playerbot table
            |
            v
        +-------------------------------------+
        |         BOT UPDATE LOOP             |
        |                                     |
        |  Dead? --> Ghost Walking            |
        |    |                                |
        |    v                                |
        |  Resting? --> Sit and regenerate    |
        |    |                                |
        |    v                                |
        |  In Combat? --> Combat AI           |
        |    |                                |
        |    v                                |
        |  Looting? --> Loot nearby corpses   |
        |    |                                |
        |    v                                |
        |  Bags full? --> Vendor strategy     |
        |    |                                |
        |    v                                |
        |  No mobs? --> Travel to new spot    |
        |    |                                |
        |    v                                |
        |  Default --> Grinding (find & kill) |
        +-------------------------------------+
```

---

## Architecture

### File Structure
```
src/game/PlayerBots/
+-- RandomBotAI.h/cpp           <- Main AI brain
+-- BotMovementManager.h/cpp    <- Centralized movement coordinator
+-- RandomBotGenerator.h/cpp    <- First-launch generation
+-- PlayerBotMgr.h/cpp          <- Bot lifecycle management
+-- BotCheats.h/cpp             <- Resting system
|
+-- Combat/
|   +-- IClassCombat.h          <- Interface for class handlers
|   +-- BotCombatMgr.h/cpp      <- Combat coordinator
|   +-- CombatHelpers.h         <- Shared helpers
|   +-- Classes/                <- 9 class-specific handlers
|       +-- WarriorCombat.h/cpp
|       +-- MageCombat.h/cpp
|       +-- ... (all 9 classes)
|
+-- Strategies/
    +-- IBotStrategy.h          <- Strategy interface
    +-- GrindingStrategy.h/cpp  <- Find and kill mobs
    +-- LootingBehavior.h/cpp   <- Loot corpses
    +-- GhostWalkingStrategy.h/cpp <- Death handling
    +-- VendoringStrategy.h/cpp <- Sell items, repair
    +-- TravelingStrategy.h/cpp <- Travel to grind spots
```

### Layer Responsibilities

| Layer | Responsibility |
|-------|---------------|
| **RandomBotAI** | Coordinates strategies, tracks state, handles resting |
| **BotMovementManager** | All movement goes through here (priority, stuck detection) |
| **BotCombatMgr** | Creates class handler, delegates combat calls |
| **IClassCombat** | Class-specific engagement, rotations, buffs |
| **Strategies** | High-level goals (grinding, traveling, vendoring) |

---

## Critical: Player Pointer Lifecycle

The bot AI object survives logout/login cycles, but the Player object does not. This requires careful pointer management.

### The Problem
```
1. Bot logs in first time -> BotMovementManager created with m_bot = Player A
2. Bot logs out -> Player A DESTROYED, but AI survives (owned by PlayerBotEntry)
3. Bot logs back in -> NEW Player B created, SetPlayer(B) updates 'me'
4. But m_initialized = true, so BotMovementManager is NOT recreated
5. m_bot still points to FREED Player A -> CRASH (use-after-free)
```

### The Solution
`BotMovementManager::SetBot()` is called in `OnPlayerLogin()` to sync pointers:
```cpp
void RandomBotAI::OnPlayerLogin()
{
    if (m_movementMgr)
        m_movementMgr->SetBot(me);  // Sync pointer after reconnect
}
```

---

## BotMovementManager

All movement commands go through `BotMovementManager` instead of calling MotionMaster directly.

### Features
| Feature | Purpose |
|---------|---------|
| Priority system | IDLE < WANDER < NORMAL < COMBAT < FORCED |
| Duplicate detection | Prevents MoveChase spam to same target |
| CC validation | Won't move while stunned/rooted/confused |
| Multi-Z search | Tries 5 heights for caves/multi-story buildings |
| Path smoothing | Skips waypoints when LoS exists |
| 5-sec stuck detection | Micro-recovery then emergency teleport |

### Key Methods
| Method | Purpose |
|--------|---------|
| `MoveTo(x, y, z, priority)` | Point movement with Z validation |
| `Chase(target, distance, priority)` | Combat movement with duplicate detection |
| `MoveNear(x, y, z, maxDist, priority)` | 8-angle search to prevent stacking |
| `MoveAway(threat, distance, priority)` | Flee mechanism |
| `Update(diff)` | Stuck detection (every 1 sec) |

---

## Update Loop (Every 1000ms)

```
UpdateAI(diff)
|
+-- [DEAD?] --> GhostWalkingStrategy::Update()
|               +-- Walk to corpse
|               +-- Resurrect when close
|               +-- Spirit healer if death loop (3 deaths/10min)
|               RETURN
|
+-- [COMBAT ENDED?] --> LootingBehavior::OnCombatEnded()
|   (wasInCombat && !inCombat)
|               +-- Enable looting mode for next tick
|
+-- [RESTING?] --> BotCheats::HandleResting()
|               +-- Sit and regen 5% HP/mana per 2 sec
|               +-- Stand up when HP >= 90% AND mana >= 90%
|               RETURN
|
+-- [IN COMBAT or HAS VICTIM?] --> UpdateInCombatAI()
|               +-- If victim dead + no attackers -> AttackStop()
|               +-- BotCombatMgr::UpdateCombat()
|               RETURN
|
+-- [OUT OF COMBAT]
    +-- Being attacked? -> Attack() -> RETURN
    +-- LootingBehavior::Update() -> RETURN if busy
    +-- VendoringStrategy::Update() -> RETURN if busy
    +-- BotCombatMgr::UpdateOutOfCombat() <- BUFFS
    +-- GrindingStrategy::UpdateGrinding()
        +-- NO_TARGETS (5x) -> TravelingStrategy
```

---

## Strategy Priority Order

| Priority | Strategy | Trigger Condition |
|----------|----------|-------------------|
| 1 | **GhostWalking** | Bot is dead |
| 2 | **Resting** | HP < 35% OR mana < 45% |
| 3 | **Combat** | In combat OR has attack victim |
| 4 | **Looting** | Combat just ended, lootable corpses nearby |
| 5 | **Vendoring** | Bags 100% full OR gear 0% durability |
| 6 | **Buffing** | Missing self-buffs |
| 7 | **Traveling** | No valid mobs for 5 consecutive ticks |
| 8 | **Grinding** | Default - find and kill mobs |

---

## Key Constants & Thresholds

### Resting (BotCheats.h)
| Constant | Value | Purpose |
|----------|-------|---------|
| `RESTING_HP_START_THRESHOLD` | 35% | Start resting below this HP |
| `RESTING_MANA_START_THRESHOLD` | 45% | Start resting below this mana |
| `RESTING_STOP_THRESHOLD` | 90% | Stop resting above this |
| `RESTING_REGEN_PERCENT` | 5% | Regen per tick |
| `RESTING_TICK_INTERVAL` | 2000ms | Time between regen ticks |

### Grinding (GrindingStrategy.h)
| Constant | Value | Purpose |
|----------|-------|---------|
| `SEARCH_RANGE` | 75 yards | Scan radius for mobs |
| `LEVEL_RANGE` | 2 | Attack same level or up to 2 below |
| `APPROACH_TIMEOUT_MS` | 30 seconds | Give up if can't reach target |

### Traveling (TravelingStrategy.h)
| Constant | Value | Purpose |
|----------|-------|---------|
| `NO_MOBS_THRESHOLD` | 5 ticks | Consecutive failures before travel |
| `ARRIVAL_COOLDOWN_MS` | 90000ms | Minimum stay at new spot |
| `ARRIVAL_DISTANCE` | 30 yards | "Arrived" threshold |
| `WAYPOINT_SEGMENT_SIZE` | 200 yards | Max distance per segment |

### Death Handling (GhostWalkingStrategy.h)
| Constant | Value | Purpose |
|----------|-------|---------|
| `DEATH_LOOP_COUNT` | 3 deaths | Triggers spirit healer |
| `DEATH_LOOP_WINDOW` | 600 sec | Time window for death loop |

---

## Combat System

### Class Engagement Types

| Type | Classes | Behavior |
|------|---------|----------|
| Melee | Warrior, Rogue, Paladin, Shaman, Druid | `Attack(true)` + `MoveChase()` |
| Caster | Mage, Priest, Warlock | `Attack(false)` + `MoveChase()` + spell pull |
| Hunter | Hunter | `Attack(false)` + `MoveChase()` + Auto Shot |

### IClassCombat Interface
```cpp
class IClassCombat
{
public:
    virtual bool Engage(Player* pBot, Unit* pTarget) = 0;      // How to pull
    virtual void UpdateCombat(Player* pBot, Unit* pVictim) = 0; // Combat rotation
    virtual void UpdateOutOfCombat(Player* pBot) = 0;           // Buffs, pets
    virtual char const* GetName() const = 0;
};
```

---

## Configuration (mangosd.conf)

| Option | Default | Description |
|--------|---------|-------------|
| `RandomBot.Enable` | 0 | Enable the RandomBot system |
| `RandomBot.Purge` | 0 | Delete all bots on startup |
| `RandomBot.MinBots` | 0 | Minimum bots to keep online |
| `RandomBot.MaxBots` | 0 | Maximum bots to generate/spawn |
| `RandomBot.Refresh` | 60000 | Add/remove check interval (ms) |
| `RandomBot.DebugGrindSelection` | 0 | Log grind spot selection |

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
[Victim] Scarlet Convert (100% HP) | Dist: 47.1 | LoS: YES
[Motion] Type: CHASE
```

### Common Issues

| Symptom | Likely Cause |
|---------|--------------|
| Bot stands idle | No mobs in range, backoff active, or travel not triggering |
| Bot keeps sitting | Resting threshold not met (check HP/mana %) |
| Bot won't vendor | Bags not 100% full, gear not broken |
| Bot stuck traveling | BotMovementManager detects in 5 sec |
| `startPoly=0` errors | Bot at invalid position (falling through floor) |

---

## Database Tables

### characters.playerbot
Links character GUID to AI type.

| Column | Type | Description |
|--------|------|-------------|
| `char_guid` | INT | Character GUID |
| `ai` | VARCHAR | AI class name ("RandomBotAI") |

### characters.grind_spots
2,684 grind locations auto-generated from creature spawns.

| Column | Type | Description |
|--------|------|-------------|
| `id` | INT | Spot ID |
| `map_id` | INT | Map (0=EK, 1=Kalimdor) |
| `x`, `y`, `z` | FLOAT | Position |
| `min_level`, `max_level` | TINYINT | Bot level range |
| `faction` | TINYINT | 0=both, 1=alliance, 2=horde |

---

## Known Limitations

| Limitation | Reason |
|------------|--------|
| Cross-continent travel | No boat/zeppelin/flight path support |
| Dungeon/raid content | No group coordination AI |
| PvP combat | No player-vs-player logic |
| Professions | Not implemented |
| Questing | No quest tracking |
| Use consumables | Resting uses cheat regen |

---

*Last Updated: 2026-01-31*
