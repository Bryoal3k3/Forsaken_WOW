# RandomBot Decision Flow

This document describes how RandomBotAI coordinates strategies and makes decisions each update tick.

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
├── [RESTING?] ────────────────────────────────────────────┤
│   YES → BotCheats::HandleResting()                       │
│         ├── Sit and regen 5% HP/mana per 2 sec           │
│         └── Stand up when HP ≥90% AND mana ≥90%          │
│         RETURN                                           │
│                                                          │
├── [IN COMBAT or HAS VICTIM?] ────────────────────────────┤
│   YES → UpdateInCombatAI()                               │
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
    ├── GrindingStrategy::UpdateGrinding()
    │   ├── ENGAGED → Found mob, attacking → RETURN
    │   └── NO_TARGETS (5x consecutive) → TravelingStrategy
    │       └── Find new grind spot, travel there
    │           RETURN if busy
    │
    └── BotCombatMgr::UpdateOutOfCombat()
        └── Apply self-buffs
```

---

## Strategy Priority Order

| Priority | Strategy | Trigger Condition |
|----------|----------|-------------------|
| 1 | **GhostWalking** | Bot is dead |
| 2 | **Resting** | HP < 35% OR mana < 45% |
| 3 | **Combat** | In combat OR has attack victim |
| 4 | **Looting** | Recently killed mobs with loot nearby |
| 5 | **Vendoring** | Bags 100% full OR gear 0% durability |
| 6 | **Traveling** | No valid mobs for 5 consecutive ticks |
| 7 | **Grinding** | Default - find and kill mobs |

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
| `SEARCH_RANGE` | 150 yards | Radius to search for mobs |
| `LEVEL_RANGE` | ±2 levels | Valid mob level range |

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
| **Universal** | LootingBehavior | After combat ends, before strategies |
| **Universal** | Combat response | Always responds to attackers |
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

On exit: LootingBehavior::OnCombatEnded() queues nearby corpses
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
└── TravelingStrategy queries DB for new spot

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

## Debugging Tips

### Log Output
Bot initialization and state changes are logged:
```
[RandomBotAI] Bot Grommash initialized (Class: 1, Level: 5, Strategy: Grinding)
```

### Common Issues

| Symptom | Likely Cause |
|---------|--------------|
| Bot stands idle | No mobs in SEARCH_RANGE, travel not triggering |
| Bot keeps sitting | Resting threshold not met (check HP/mana %) |
| Bot won't vendor | Bags not 100% full, gear not broken |
| Bot stuck traveling | Navmesh issue, stuck timeout will reset |

---

*Last Updated: 2026-01-24*
