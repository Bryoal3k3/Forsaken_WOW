# RandomBot Decision Flow

This document describes how RandomBotAI coordinates strategies and makes decisions each update tick.

---

## Player Pointer Lifecycle (Critical)

The bot AI object survives logout/login cycles, but the Player object does not. This requires careful pointer management.

### The Problem
```
1. Bot logs in first time → BotMovementManager created with m_bot = Player A
2. Bot logs out → Player A DESTROYED, but AI survives (owned by PlayerBotEntry)
3. Bot logs back in → NEW Player B created, SetPlayer(B) updates 'me'
4. But m_initialized = true, so BotMovementManager is NOT recreated
5. m_bot still points to FREED Player A → CRASH (use-after-free)
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

Additionally, `IsValid()` guards on all movement entry points provide a safety net:
```cpp
bool BotMovementManager::IsValid() const
{
    if (!m_bot || m_botGuid.IsEmpty())
        return false;
    Player* pPlayer = ObjectAccessor::FindPlayer(m_botGuid);
    return pPlayer && pPlayer == m_bot && pPlayer->IsInWorld();
}
```

---

## BotMovementManager (Centralized Movement)

All movement commands go through `BotMovementManager` instead of calling MotionMaster directly.

### Architecture
```
RandomBotAI
├── BotMovementManager (single point of control)
│   ├── Priority system (prevents interruptions)
│   ├── Duplicate detection (no MoveChase spam)
│   ├── CC validation (won't move while stunned)
│   ├── Multi-Z search (caves/buildings)
│   ├── Path smoothing (skip unnecessary waypoints)
│   └── 5-sec stuck detection + micro-recovery
│   ↓
└── MotionMaster (engine calls)
```

### Movement Priority
| Priority | Value | Used For |
|----------|-------|----------|
| `PRIORITY_IDLE` | 0 | Standing still |
| `PRIORITY_WANDER` | 1 | Random exploration (future) |
| `PRIORITY_NORMAL` | 2 | Travel, vendor, loot, ghost walk |
| `PRIORITY_COMBAT` | 3 | Chase, engage, positioning |
| `PRIORITY_FORCED` | 4 | Flee, emergency (cannot be overridden) |

### Key Methods
| Method | Purpose |
|--------|---------|
| `MoveTo(x, y, z, priority)` | Point movement with Z validation |
| `Chase(target, distance, priority)` | Combat movement with duplicate detection |
| `MoveNear(x, y, z, maxDist, priority)` | 8-angle search to prevent stacking |
| `MoveAway(threat, distance, priority)` | Flee mechanism |
| `SmoothPath(path)` | Skip waypoints when LoS exists |
| `Update(diff)` | Stuck detection (every 1 sec) |

### Wiring Pattern
Each strategy receives movement manager via `SetMovementManager()` during initialization:
```cpp
// In RandomBotAI one-time init:
m_movementMgr = std::make_unique<BotMovementManager>(me);
m_combatMgr->SetMovementManager(m_movementMgr.get());
m_strategy->SetMovementManager(m_movementMgr.get());
// ... etc for all strategies
```

---

## Update Loop (Every 1000ms)

```
UpdateAI(diff)
│
├── [DEAD?] ───────────────────────────────────────────────┐
│   YES → GhostWalkingStrategy::Update()                   │
│         ├── Walk to corpse                               │
│         ├── Resurrect when close                         │
│         └── Spirit healer if death loop (3 deaths/10min) │
│         RETURN                                           │
│                                                          │
├── [COMBAT ENDED?] ──────────────────────────────────────┤
│   (wasInCombat && !inCombat)                             │
│   YES → LootingBehavior::OnCombatEnded()                 │
│         └── Enable looting mode for next tick            │
│                                                          │
├── [RESTING?] ────────────────────────────────────────────┤
│   YES → BotCheats::HandleResting()                       │
│         ├── Sit and regen 5% HP/mana per 2 sec           │
│         └── Stand up when HP ≥90% AND mana ≥90%          │
│         RETURN                                           │
│                                                          │
├── [IN COMBAT or HAS VICTIM?] ────────────────────────────┤
│   YES → UpdateInCombatAI()                               │
│         ├── If victim dead + no attackers → AttackStop() │
│         └── BotCombatMgr::UpdateCombat()                 │
│             └── Class handler rotation                   │
│         RETURN                                           │
│                                                          │
└── [OUT OF COMBAT] ───────────────────────────────────────┘
    │
    ├── Being attacked? → Attack() → RETURN
    │
    ├── LootingBehavior::Update()
    │   └── Walk to corpse, loot, RETURN if busy
    │
    ├── VendoringStrategy::Update()
    │   └── Bags 100% full OR gear broken → vendor
    │       RETURN if busy
    │
    ├── BotCombatMgr::UpdateOutOfCombat()   ← BUFFS FIRST!
    │   └── Apply self-buffs (Ice Armor, etc.)
    │
    ├── GrindingStrategy::UpdateGrinding()
    │   ├── ENGAGED → Found mob, attacking → RETURN
    │   └── NO_TARGETS (5x consecutive) → TravelingStrategy
    │       └── Find new grind spot, travel there
    │           RETURN if busy
    │
    └── (end of out-of-combat loop)
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
| 6 | **Buffing** | Missing self-buffs (Ice Armor, Demon Skin, etc.) |
| 7 | **Traveling** | No valid mobs for 5 consecutive ticks |
| 8 | **Grinding** | Default - find and kill mobs |

---

## Strategy Communication

Strategies communicate via return values and explicit signaling:

```cpp
// GrindingResult enum - explicit outcome signaling
enum class GrindingResult {
    ENGAGED,      // Found target, attacking
    NO_TARGETS,   // No valid mobs in area
    BUSY          // Doing something else
};

// TravelingStrategy listens for NO_TARGETS
if (grindResult == NO_TARGETS && noMobsCount >= 5)
    travelingStrategy->SignalNoMobs();
```

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
| `LEVEL_RANGE` | 2 | Attack same level or up to 2 below (no higher) |
| `APPROACH_TIMEOUT_MS` | 30 seconds | Give up if can't reach target |
| `BACKOFF_MAX_LEVEL` | 3 | Max backoff level (8 second interval) |

**State Machine:**
```
IDLE → Scan mobs → Pick random → Validate path → APPROACHING → IN_COMBAT → IDLE
                                                       |
                                                TIMEOUT (30s) → Clear target → IDLE
```

**Target Selection:**
- Scans ALL valid mobs in 75 yard radius
- Validates path BEFORE committing (rejects `endPoly=0` targets)
- Picks randomly from valid candidates (not just nearest)
- Only attacks same level or lower (up to 2 levels below)

**Adaptive Search Behavior:**
- When no mobs found, applies exponential backoff: 1s → 2s → 4s → 8s
- Resets to fast searching when target found or combat ends

### Looting (LootingBehavior.h)

| Constant | Value | Purpose |
|----------|-------|---------|
| `LOOT_RANGE` | 40 yards | Max distance to travel for loot |
| `INTERACT_RANGE` | 5 yards | Distance needed to loot |
| `LOOT_TIMEOUT_MS` | 12000ms | Give up after this time |

### Vendoring (VendoringStrategy.h)

| Constant | Value | Purpose |
|----------|-------|---------|
| `VENDOR_INTERACT_RANGE` | 5 yards | Distance to interact |
| `STUCK_TIMEOUT` | 30000ms | Give up if stuck |

### Traveling (TravelingStrategy.h)

| Constant | Value | Purpose |
|----------|-------|---------|
| `NO_MOBS_THRESHOLD` | 5 ticks | Consecutive failures before travel |
| `ARRIVAL_COOLDOWN_MS` | 90000ms | Minimum stay at new spot |
| `ARRIVAL_DISTANCE` | 30 yards | "Arrived" threshold |
| `STUCK_TIMEOUT_MS` | 30000ms | Reset if no progress |
| `BAG_FULL_THRESHOLD` | 60% | Pre-travel vendor check |
| `DURABILITY_THRESHOLD` | 50% | Pre-travel vendor check |
| `WAYPOINT_SEGMENT_SIZE` | 200 yards | Max distance per waypoint segment |

**Waypoint System:**
- Long journeys are split into ~200 yard segments
- Each segment uses `BotMovementManager::MoveTo()` with pointId for callback chaining
- `MovementInform()` callback triggers next segment immediately
- Path smoothing via `BotMovementManager::SmoothPath()` skips unnecessary waypoints
- Path validated before travel using `PathFinder::calculate()`

### Death Handling (GhostWalkingStrategy.h)

| Constant | Value | Purpose |
|----------|-------|---------|
| `DEATH_LOOP_COUNT` | 3 deaths | Triggers spirit healer |
| `DEATH_LOOP_WINDOW` | 600 sec | Time window for death loop |
| `CORPSE_RESURRECT_RANGE` | 5 yards | Distance to resurrect |

---

## Universal vs Strategy Behaviors

| Type | Behavior | When It Runs |
|------|----------|--------------|
| **Universal** | LootingBehavior | After combat ends, before other strategies |
| **Universal** | Combat response | Always responds to attackers |
| **Universal** | Buffing | Before grinding, maintains self-buffs |
| **Strategy** | GrindingStrategy | Default out-of-combat behavior |
| **Strategy** | VendoringStrategy | When bags full / gear broken |
| **Strategy** | TravelingStrategy | When area has no valid mobs |
| **Strategy** | GhostWalkingStrategy | When dead |

---

## State Transitions

### Combat Entry
```
Out of Combat → In Combat
├── Mob attacks bot (reactive)
├── GrindingStrategy finds and engages mob (proactive)
└── Bot has victim set (ranged pull in progress)
```

### Combat Exit
```
In Combat → Out of Combat
├── All attackers dead
├── Bot died
└── All threats evaded (rare)

On exit:
├── LootingBehavior::OnCombatEnded() enables looting mode
└── UpdateInCombatAI() calls AttackStop() when victim dead + no attackers
    └── This clears GetVictim() so bot exits combat branch properly
```

### Resting Entry/Exit
```
Not Resting → Resting
├── HP < 35% (after combat)
└── Mana < 45% (after combat)

Resting → Not Resting
├── HP ≥ 90% AND Mana ≥ 90%
└── Bot enters combat (forced stand)
```

### Travel Trigger
```
Grinding → Traveling
├── GrindingStrategy returns NO_TARGETS
├── 5 consecutive NO_TARGETS ticks
└── TravelingStrategy searches cached grind spots (loaded at startup)

Traveling → Grinding
├── Arrived at destination (within 30 yards)
├── Found mob while traveling (combat interrupt)
└── Stuck timeout (30 sec no progress)
```

---

## IBotStrategy Interface

All strategies implement this interface:

```cpp
class IBotStrategy
{
public:
    virtual ~IBotStrategy() = default;

    // Called every AI update tick when out of combat
    // Returns true if the strategy took an action
    virtual bool Update(Player* pBot, uint32 diff) = 0;

    // Called when bot enters combat
    virtual void OnEnterCombat(Player* pBot) = 0;

    // Called when bot leaves combat
    virtual void OnLeaveCombat(Player* pBot) = 0;

    // Strategy name for debugging
    virtual char const* GetName() const = 0;
};
```

---

## Startup Initialization

Before bots spawn, `PlayerBotMgr::Load()` builds static caches to avoid runtime DB queries:

```
PlayerBotMgr::Load()
├── LoadConfig()
├── PurgeAllRandomBots() (if RandomBot.Purge=1)
├── GenerateIfNeeded() (creates bots on first launch)
├── Load playerbot table into m_bots map
├── VendoringStrategy::BuildVendorCache()    ← Caches all vendor NPCs
├── TravelingStrategy::BuildGrindSpotCache() ← Caches all grind spots
└── AddRandomBot() (spawns MinBots initially)
```

| Cache | Data Source | Used By |
|-------|-------------|---------|
| Vendor Cache | creature_template + creature | VendoringStrategy::FindNearestVendor() |
| Grind Spot Cache | characters.grind_spots | TravelingStrategy::FindGrindSpot() |

---

## Debugging Tips

### Log Output
Bot initialization and state changes are logged:
```
[RandomBotAI] Bot Grommash initialized (Class: 1, Level: 5, Strategy: Grinding)
```

### PathFinder Debug Logging (Bot-Only)
PathFinder has debug logging filtered to only fire for player bots. Logs are prefixed with `[BOT]`:
```
[BOT] PathFinder::BuildPolyPath ENTER: BotName from (x,y,z) to (x,y,z)
[BOT] PathFinder::BuildPolyPath BotName: startPoly=XXX endPoly=XXX
[BOT] PathFinder::BuildPolyPath BotName: findPath result=0x0 polyLength=XX
[BOT] PathFinder::BuildPointPath BotName: result=0x0 pointCount=XX
```

**Key PathFinder Results:**
| Result Code | Meaning |
|-------------|---------|
| `0x40000000` | `DT_SUCCESS` - Path found |
| `0x40000010` | `DT_SUCCESS \| DT_BUFFER_TOO_SMALL` - Path found but truncated (INCOMPLETE) |
| `0x80000000` | `DT_FAILURE` - No path found |
| `startPoly=0` | Bot's current position has no navmesh (falling through floor or bad location) |

### Common Issues

| Symptom | Likely Cause |
|---------|--------------|
| Bot stands idle | No mobs in range, backoff active (waits up to 8s), or travel not triggering |
| Bot keeps sitting | Resting threshold not met (check HP/mana %) |
| Bot won't vendor | Bags not 100% full, gear not broken |
| Bot stuck traveling | BotMovementManager detects in 5 sec, tries micro-recovery then emergency teleport |
| Bot slow to find mobs | Backoff active from previous empty search (resets when mob found) |
| "Cannot reach" spam | Long path exceeding 256 waypoints (fixed), or navmesh hole |
| `startPoly=0` errors | Bot at invalid position (falling through floor or navmesh edge) |
| Bot falling through floor | Bad spawn location or navmesh gap - needs HS teleport recovery |

---

## PathFinder Integration

### How Travel Uses PathFinder

```
TravelingStrategy::StartTravel()
├── ValidatePath(destination)              // Check if path exists
│   └── PathFinder::calculate()
│       ├── BuildPolyPath() - Find polygon path
│       └── BuildPointPath() - Generate smooth waypoints
│           └── findSmoothPath() - Up to 256 waypoints
├── If PATHFIND_NOPATH → Abort travel
├── If PATHFIND_NORMAL or PATHFIND_INCOMPLETE → Proceed
└── GenerateWaypoints() - Split into 200-yard segments
```

### PathType Values
| Type | Value | Meaning |
|------|-------|---------|
| `PATHFIND_BLANK` | 0x00 | Path not built yet |
| `PATHFIND_NORMAL` | 0x01 | Complete valid path |
| `PATHFIND_SHORTCUT` | 0x02 | Direct line (ignores terrain) |
| `PATHFIND_INCOMPLETE` | 0x04 | Partial path (truncated or can't reach end) |
| `PATHFIND_NOPATH` | 0x08 | No valid path exists |

### Long Path Handling (Fixed 2026-01-25)
- Paths longer than 256 waypoints now return `PATHFIND_INCOMPLETE` instead of `PATHFIND_NOPATH`
- `ValidatePath()` accepts both `PATHFIND_NORMAL` and `PATHFIND_INCOMPLETE`
- Bots follow the truncated path and can recalculate when near the end

---

*Last Updated: 2026-01-31 (Added player pointer lifecycle section - use-after-free fix)*
