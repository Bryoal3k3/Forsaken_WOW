# Phase 5: Travel System - Detailed Technical Report

## Executive Summary

The Travel System enables bots to autonomously find and travel to level-appropriate grinding spots when their current location has no valid mobs. It uses a database-driven approach with anti-thrashing safeguards to prevent erratic behavior.

---

## System Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                            RandomBotAI                                       │
│                         (Main Brain/Coordinator)                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│   ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐       │
│   │   Ghost     │  │  Vendoring  │  │  Grinding   │  │  Traveling  │       │
│   │  Walking    │  │  Strategy   │  │  Strategy   │  │  Strategy   │       │
│   │  Strategy   │  │             │  │             │  │   [NEW]     │       │
│   └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘       │
│          │                │                │                │               │
│          ▼                ▼                ▼                ▼               │
│   ┌─────────────────────────────────────────────────────────────────┐      │
│   │                    Strategy Priority Chain                       │      │
│   │  1. Dead? → GhostWalking                                        │      │
│   │  2. Bags full/Gear broken? → Vendoring                          │      │
│   │  3. Mobs nearby? → Grinding                                     │      │
│   │  4. No mobs (5+ ticks)? → Traveling                             │      │
│   │  5. Fallback → Out-of-combat buffs                              │      │
│   └─────────────────────────────────────────────────────────────────┘      │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                         characters.grind_spots                               │
│                           (Database Table)                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│  id | map_id | x | y | z | min_level | max_level | faction | priority | name│
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Strategy Coordination Protocol

### Key Principle
Strategies **never call each other**. Each strategy checks its own preconditions and **yields** (returns `false`) if not applicable. RandomBotAI checks strategies in priority order.

### Priority Order Flow

```
                    ┌──────────────────┐
                    │  UpdateAI Tick   │
                    │   (every 1 sec)  │
                    └────────┬─────────┘
                             │
                             ▼
                    ┌──────────────────┐
                    │   Bot Alive?     │
                    └────────┬─────────┘
                             │
              ┌──────────────┴──────────────┐
              │ NO                          │ YES
              ▼                             ▼
     ┌────────────────┐           ┌──────────────────┐
     │ GhostWalking   │           │  In Combat?      │
     │   Strategy     │           └────────┬─────────┘
     │ (handle death) │                    │
     └────────────────┘         ┌──────────┴──────────┐
                                │ YES                 │ NO
                                ▼                     ▼
                       ┌────────────────┐   ┌──────────────────┐
                       │ UpdateInCombat │   │ UpdateOutOfCombat│
                       │      AI        │   │       AI         │
                       └────────────────┘   └────────┬─────────┘
                                                     │
                                                     ▼
                                          ┌──────────────────────┐
                                          │ Priority 1: Vendoring│
                                          │ NeedsToVendor()?     │
                                          └────────┬─────────────┘
                                                   │
                                    ┌──────────────┴──────────────┐
                                    │ YES                         │ NO
                                    ▼                             ▼
                           ┌────────────────┐          ┌──────────────────────┐
                           │   Vendoring    │          │ Priority 2: Grinding │
                           │   Strategy     │          │ UpdateGrinding()     │
                           │   (return)     │          └────────┬─────────────┘
                           └────────────────┘                   │
                                                                ▼
                                                     ┌─────────────────────┐
                                                     │   GrindingResult?   │
                                                     └────────┬────────────┘
                                                              │
                              ┌────────────────┬──────────────┴──────────────┐
                              │                │                             │
                              ▼                ▼                             ▼
                         ENGAGED          NO_TARGETS                       BUSY
                              │                │                             │
                              ▼                ▼                             ▼
                    ┌─────────────────┐ ┌─────────────────┐        ┌─────────────────┐
                    │ Reset travel    │ │ noMobsCount++   │        │ Do nothing      │
                    │ state, fighting │ │ Check threshold │        │ (in combat/etc) │
                    └─────────────────┘ └────────┬────────┘        └─────────────────┘
                                                 │
                                                 ▼
                                      ┌─────────────────────┐
                                      │ noMobsCount >= 5?   │
                                      └────────┬────────────┘
                                               │
                                ┌──────────────┴──────────────┐
                                │ NO                          │ YES
                                ▼                             ▼
                       ┌────────────────┐          ┌──────────────────────┐
                       │ Out-of-combat  │          │ Priority 3: Traveling│
                       │    buffs       │          │ SignalNoMobs()       │
                       └────────────────┘          │ Update()             │
                                                   └──────────────────────┘
```

---

## GrindingResult Enum - Explicit Signaling

### Before (Old Design)
```cpp
// Update() returned bool - ambiguous
bool Update(Player* pBot, uint32 diff);
// true = "doing something" (but what?)
// false = "not doing anything" (why?)
```

### After (New Design)
```cpp
enum class GrindingResult
{
    ENGAGED,        // Found target, attacking
    NO_TARGETS,     // Searched area, no valid mobs found
    BUSY            // Doing something else (in combat, etc.)
};

GrindingResult UpdateGrinding(Player* pBot, uint32 diff);
```

### Why This Matters

```
┌─────────────────────────────────────────────────────────────────────┐
│                     GrindingResult Flow                              │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│   UpdateGrinding() called                                           │
│          │                                                          │
│          ▼                                                          │
│   ┌─────────────────┐                                               │
│   │ Bot alive and   │──NO──▶ return BUSY                            │
│   │ not in combat?  │                                               │
│   └────────┬────────┘                                               │
│            │ YES                                                    │
│            ▼                                                        │
│   ┌─────────────────┐                                               │
│   │ Already has     │──YES──▶ m_noMobsCount = 0                     │
│   │ victim?         │         return ENGAGED                        │
│   └────────┬────────┘                                               │
│            │ NO                                                     │
│            ▼                                                        │
│   ┌─────────────────┐                                               │
│   │ FindGrindTarget │                                               │
│   │ (150 yard range)│                                               │
│   └────────┬────────┘                                               │
│            │                                                        │
│     ┌──────┴──────┐                                                 │
│     │             │                                                 │
│     ▼             ▼                                                 │
│  FOUND         NOT FOUND                                            │
│     │             │                                                 │
│     ▼             ▼                                                 │
│  Engage()     m_noMobsCount++                                       │
│  m_noMobsCount=0  return NO_TARGETS                                 │
│  return ENGAGED                                                     │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## TravelingStrategy State Machine

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    TravelingStrategy State Machine                           │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│                              ┌─────────┐                                    │
│                              │  IDLE   │◀─────────────────────────┐        │
│                              └────┬────┘                          │        │
│                                   │                               │        │
│                                   │ ShouldTravel() == true        │        │
│                                   │ AND no vendor needed          │        │
│                                   ▼                               │        │
│                         ┌─────────────────┐                       │        │
│                         │  FINDING_SPOT   │                       │        │
│                         └────────┬────────┘                       │        │
│                                  │                                │        │
│                    ┌─────────────┴─────────────┐                  │        │
│                    │                           │                  │        │
│                    ▼                           ▼                  │        │
│            Spot Found              No Spot Found                  │        │
│                    │                           │                  │        │
│                    ▼                           │                  │        │
│           ┌────────────────┐                   │                  │        │
│           │    WALKING     │                   │                  │        │
│           │                │                   │                  │        │
│           │ MovePoint()    │                   │                  │        │
│           │ with pathfind  │                   │                  │        │
│           └───────┬────────┘                   │                  │        │
│                   │                            │                  │        │
│     ┌─────────────┼─────────────┐              │                  │        │
│     │             │             │              │                  │        │
│     ▼             ▼             ▼              │                  │        │
│  Arrived     Still Moving    Stuck             │                  │        │
│  (< 30 yd)   (progress)    (30 sec timeout)    │                  │        │
│     │             │             │              │                  │        │
│     │             │             └──────────────┴──────────────────┘        │
│     │             │                                                        │
│     │             └────────────────────────────┐                           │
│     │                                          │                           │
│     ▼                                          │                           │
│  ┌─────────────┐                               │                           │
│  │   ARRIVED   │                               │                           │
│  │             │                               │                           │
│  │  90 sec     │                               │                           │
│  │  cooldown   │                               │                           │
│  └──────┬──────┘                               │                           │
│         │                                      │                           │
│         │ Cooldown expired                     │                           │
│         │ OR ResetArrivalCooldown() called     │                           │
│         │                                      │                           │
│         └──────────────────────────────────────┘                           │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### State Descriptions

| State | Description | Exit Conditions |
|-------|-------------|-----------------|
| `IDLE` | Not traveling, waiting for signal | `ShouldTravel()` returns true |
| `FINDING_SPOT` | Querying database for destination | Spot found → WALKING, No spot → IDLE |
| `WALKING` | Moving toward destination | Arrived → ARRIVED, Stuck → IDLE |
| `ARRIVED` | At destination, on cooldown | 90 sec elapsed → IDLE |

---

## Anti-Thrashing System

### Problem: Travel Thrashing
Without safeguards, bots could:
1. Arrive at spot
2. All mobs dead (respawn timer)
3. Immediately travel to another spot
4. Repeat endlessly

### Solution: Multi-Layer Protection

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        Anti-Thrashing Safeguards                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  Layer 1: Consecutive Failure Threshold                                     │
│  ═══════════════════════════════════════                                    │
│                                                                              │
│    NO_MOBS_THRESHOLD = 5                                                    │
│                                                                              │
│    ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐                                      │
│    │ T1 │→│ T2 │→│ T3 │→│ T4 │→│ T5 │→ NOW can travel                      │
│    └────┘ └────┘ └────┘ └────┘ └────┘                                      │
│      ↑      ↑      ↑      ↑      ↑                                         │
│    No mob No mob No mob No mob No mob                                       │
│                                                                              │
│    If ANY tick finds a mob → counter resets to 0                            │
│                                                                              │
│                                                                              │
│  Layer 2: Arrival Cooldown                                                  │
│  ═════════════════════════                                                  │
│                                                                              │
│    ARRIVAL_COOLDOWN_MS = 90000 (90 seconds)                                 │
│                                                                              │
│    ┌─────────────────────────────────────────────────────────┐              │
│    │                                                         │              │
│    │  ARRIVED ──────────────────────────────────────▶ IDLE   │              │
│    │           ◀── 90 seconds must pass ──▶                  │              │
│    │                                                         │              │
│    │  Bot MUST stay at spot for 90 sec minimum               │              │
│    │  Even if no mobs found during this time                 │              │
│    │                                                         │              │
│    └─────────────────────────────────────────────────────────┘              │
│                                                                              │
│                                                                              │
│  Layer 3: Found Mob Reset                                                   │
│  ════════════════════════                                                   │
│                                                                              │
│    If grinding finds a target:                                              │
│    • m_noMobsCount = 0                                                      │
│    • TravelingStrategy.ResetArrivalCooldown()                               │
│                                                                              │
│    This means: "Current spot is good, stay here"                            │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Pre-Travel Vendor Check

### Why Check Before Traveling?
If bot needs repairs or bag space, traveling first would waste time. Better to vendor, then travel.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                      Pre-Travel Vendor Check Flow                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│   TravelingStrategy.Update() called                                         │
│          │                                                                   │
│          ▼                                                                   │
│   ┌─────────────────────────┐                                               │
│   │ ShouldTravel() == true? │                                               │
│   └────────────┬────────────┘                                               │
│                │ YES                                                        │
│                ▼                                                            │
│   ┌─────────────────────────────────────────────────────┐                   │
│   │           Pre-Travel Checks                          │                   │
│   ├─────────────────────────────────────────────────────┤                   │
│   │                                                      │                   │
│   │  GetLowestDurabilityPercent() < 0.5f (50%)?         │                   │
│   │                    OR                                │                   │
│   │  GetBagFullPercent() > 0.6f (60%)?                  │                   │
│   │                                                      │                   │
│   └────────────────────────┬────────────────────────────┘                   │
│                            │                                                │
│              ┌─────────────┴─────────────┐                                  │
│              │ YES                       │ NO                               │
│              ▼                           ▼                                  │
│   ┌──────────────────────┐    ┌──────────────────────┐                     │
│   │ Return false (YIELD) │    │ Proceed to travel    │                     │
│   │                      │    │ (FINDING_SPOT state) │                     │
│   │ Next tick:           │    └──────────────────────┘                     │
│   │ VendoringStrategy    │                                                  │
│   │ will handle it       │                                                  │
│   └──────────────────────┘                                                  │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Threshold Methods Added to VendoringStrategy

```cpp
// Returns 0.0 (empty) to 1.0 (full)
static float GetBagFullPercent(Player* pBot);

// Returns 0.0 (broken) to 1.0 (full durability)
static float GetLowestDurabilityPercent(Player* pBot);
```

---

## Database Schema: grind_spots

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        characters.grind_spots                                │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌─────────┬─────────┬─────────────────────────────────────────────────┐   │
│  │ Column  │  Type   │ Description                                      │   │
│  ├─────────┼─────────┼─────────────────────────────────────────────────┤   │
│  │ id      │ INT PK  │ Auto-increment primary key                       │   │
│  │ map_id  │ INT     │ 0 = Eastern Kingdoms, 1 = Kalimdor              │   │
│  │ zone_id │ INT     │ Zone ID (for debugging/reference)               │   │
│  │ x       │ FLOAT   │ X coordinate of grind spot center               │   │
│  │ y       │ FLOAT   │ Y coordinate of grind spot center               │   │
│  │ z       │ FLOAT   │ Z coordinate of grind spot center               │   │
│  │min_level│ TINYINT │ Minimum level for this spot                     │   │
│  │max_level│ TINYINT │ Maximum level for this spot                     │   │
│  │ faction │ TINYINT │ 0 = both, 1 = Alliance, 2 = Horde               │   │
│  │ radius  │ FLOAT   │ Roaming radius (unused in V1)                   │   │
│  │ priority│ TINYINT │ 1-100, higher = preferred spot                  │   │
│  │ name    │VARCHAR64│ Human-readable spot name                        │   │
│  └─────────┴─────────┴─────────────────────────────────────────────────┘   │
│                                                                              │
│  Indexes:                                                                   │
│  • idx_grind_level (min_level, max_level) - Fast level-based lookups       │
│  • idx_grind_faction (faction) - Filter by faction                         │
│  • idx_grind_map (map_id) - Same-map queries                               │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Query Logic

```sql
-- Step 1: Try nearby spots first (5000 yard bounding box)
SELECT x, y, z, name FROM grind_spots
WHERE map_id = ?                           -- Same map
AND min_level <= ? AND max_level >= ?      -- Level appropriate
AND (faction = 0 OR faction = ?)           -- Faction compatible
AND x BETWEEN ? AND ?                      -- Bounding box
AND y BETWEEN ? AND ?
ORDER BY priority DESC,                    -- Best spots first
         distance ASC                      -- Then closest
LIMIT 1;

-- Step 2: If no nearby spots, search entire map
SELECT x, y, z, name FROM grind_spots
WHERE map_id = ?
AND min_level <= ? AND max_level >= ?
AND (faction = 0 OR faction = ?)
ORDER BY priority DESC, distance ASC
LIMIT 1;
```

---

## Stuck Detection System

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         Stuck Detection                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  Constants:                                                                  │
│  • STUCK_TIMEOUT_MS = 30000 (30 seconds)                                    │
│  • STUCK_MIN_DISTANCE = 5.0f (must move 5 yards)                            │
│                                                                              │
│  ┌───────────────────────────────────────────────────────────────────┐      │
│  │                     WALKING State Loop                             │      │
│  ├───────────────────────────────────────────────────────────────────┤      │
│  │                                                                    │      │
│  │  Every Update() tick:                                             │      │
│  │                                                                    │      │
│  │  1. Calculate distance moved since last check:                    │      │
│  │     dx = currentX - m_lastX                                       │      │
│  │     dy = currentY - m_lastY                                       │      │
│  │     distMoved = sqrt(dx² + dy²)                                   │      │
│  │                                                                    │      │
│  │  2. If distMoved >= 5.0:                                          │      │
│  │     • Update m_lastX, m_lastY                                     │      │
│  │     • Reset m_lastProgressTime = now                              │      │
│  │     → Bot is making progress, continue                            │      │
│  │                                                                    │      │
│  │  3. If distMoved < 5.0 AND (now - m_lastProgressTime) > 30 sec:  │      │
│  │     → Bot is STUCK                                                │      │
│  │     → Reset to IDLE state                                         │      │
│  │     → Clear m_noMobsSignaled                                      │      │
│  │     → Log warning                                                 │      │
│  │                                                                    │      │
│  └───────────────────────────────────────────────────────────────────┘      │
│                                                                              │
│  Timeline Example:                                                          │
│                                                                              │
│  T=0sec   T=5sec   T=10sec  T=15sec  T=20sec  T=25sec  T=30sec             │
│    │        │        │        │        │        │        │                  │
│    ▼        ▼        ▼        ▼        ▼        ▼        ▼                  │
│  Start   Moved    Moved    STUCK!   STUCK!   STUCK!   TIMEOUT!             │
│  walking  8yd      3yd     (no move) (no move)(no move) → IDLE             │
│    │        │        │                                                      │
│    └────────┴────────┴── Progress timer resets each time bot moves 5+ yd   │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Combat Interrupt Handling

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    Combat Interrupt During Travel                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  Scenario: Bot is traveling and gets attacked by a mob                      │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  WALKING State                                                       │    │
│  │       │                                                              │    │
│  │       │ ◀── Mob attacks bot                                         │    │
│  │       │                                                              │    │
│  │       ▼                                                              │    │
│  │  OnEnterCombat() called                                             │    │
│  │       │                                                              │    │
│  │       │ • State remains WALKING (paused, not reset)                 │    │
│  │       │ • Log "entered combat while traveling, pausing"             │    │
│  │       │                                                              │    │
│  │       ▼                                                              │    │
│  │  Combat system takes over                                           │    │
│  │  (BotCombatMgr handles fight)                                       │    │
│  │       │                                                              │    │
│  │       │ ◀── Combat ends (mob killed)                                │    │
│  │       │                                                              │    │
│  │       ▼                                                              │    │
│  │  OnLeaveCombat() called                                             │    │
│  │       │                                                              │    │
│  │       │ • If state is still WALKING:                                │    │
│  │       │   Resume MovePoint() to destination                         │    │
│  │       │                                                              │    │
│  │       ▼                                                              │    │
│  │  Continue traveling to destination                                  │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│  Key Point: Travel state is PAUSED, not RESET                               │
│  Bot will continue to same destination after combat                         │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Ghost Walking Integration

### Resurrection Sickness Handling

```
┌─────────────────────────────────────────────────────────────────────────────┐
│              Spirit Healer Resurrection + Travel State                       │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  When bot uses spirit healer (death loop detected):                         │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  GhostWalkingStrategy.OnDeath()                                     │    │
│  │       │                                                              │    │
│  │       │ IsInDeathLoop() == true (3+ deaths in 10 min)               │    │
│  │       │                                                              │    │
│  │       ▼                                                              │    │
│  │  ResurrectPlayer(0.5f, true)  ← applies Resurrection Sickness       │    │
│  │       │                                                              │    │
│  │       ▼                                                              │    │
│  │  Check for aura 15007 (Resurrection Sickness)                       │    │
│  │       │                                                              │    │
│  │       │ If present:                                                 │    │
│  │       │ • Log "has resurrection sickness, will wait"                │    │
│  │       │ • Bot will rest/wait (sickness debuffs stats heavily)       │    │
│  │       │ • Sickness expires naturally (10 min for high level)        │    │
│  │       │                                                              │    │
│  │       ▼                                                              │    │
│  │  Reset travel state via GetTravelingStrategy()->ResetArrivalCooldown()│   │
│  │       │                                                              │    │
│  │       │ This ensures:                                               │    │
│  │       │ • Bot doesn't immediately travel away from graveyard        │    │
│  │       │ • Can evaluate if graveyard area has mobs                   │    │
│  │       │ • Fresh start for travel decision making                    │    │
│  │                                                                      │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│  Same reset happens for normal corpse resurrection                          │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## File Changes Summary

### New Files Created

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  sql/custom/grind_spots.sql                                                  │
│  ├── CREATE TABLE grind_spots                                               │
│  ├── CREATE INDEX (3 indexes)                                               │
│  └── INSERT test data (Alliance + Horde starting zones)                     │
│                                                                              │
│  src/game/PlayerBots/Strategies/TravelingStrategy.h                         │
│  ├── TravelConstants namespace (thresholds, timeouts)                       │
│  ├── TravelState enum (IDLE, FINDING_SPOT, WALKING, ARRIVED)               │
│  └── TravelingStrategy class declaration                                    │
│                                                                              │
│  src/game/PlayerBots/Strategies/TravelingStrategy.cpp                       │
│  ├── State machine implementation                                           │
│  ├── Database query logic (FindGrindSpot)                                   │
│  ├── Stuck detection                                                        │
│  └── Combat interrupt handling                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Modified Files

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  GrindingStrategy.h                                                          │
│  ├── + GrindingResult enum                                                  │
│  ├── + m_noMobsCount member                                                 │
│  ├── + UpdateGrinding() method                                              │
│  └── + GetNoMobsCount(), ResetNoMobsCount()                                 │
│                                                                              │
│  GrindingStrategy.cpp                                                        │
│  ├── + UpdateGrinding() implementation                                      │
│  └── ~ Update() now calls UpdateGrinding()                                  │
│                                                                              │
│  VendoringStrategy.h                                                         │
│  ├── + GetBagFullPercent() declaration                                      │
│  └── + GetLowestDurabilityPercent() declaration                             │
│                                                                              │
│  VendoringStrategy.cpp                                                       │
│  ├── + GetBagFullPercent() implementation                                   │
│  └── + GetLowestDurabilityPercent() implementation                          │
│                                                                              │
│  RandomBotAI.h                                                               │
│  ├── + #include "Strategies/GrindingStrategy.h"                             │
│  ├── + TravelingStrategy forward declaration                                │
│  ├── + m_travelingStrategy member                                           │
│  ├── + GetTravelingStrategy() accessor                                      │
│  └── + GetGrindingStrategy() accessor                                       │
│                                                                              │
│  RandomBotAI.cpp                                                             │
│  ├── + #include "Strategies/TravelingStrategy.h"                            │
│  ├── + Initialize m_travelingStrategy in constructor                        │
│  └── ~ UpdateOutOfCombatAI() - integrated travel system                     │
│                                                                              │
│  GhostWalkingStrategy.cpp                                                    │
│  ├── + #include "TravelingStrategy.h"                                       │
│  ├── + #include "RandomBotAI.h"                                             │
│  ├── + Resurrection sickness check (aura 15007)                             │
│  └── + Travel state reset on resurrection                                   │
│                                                                              │
│  CMakeLists.txt                                                              │
│  ├── + TravelingStrategy.cpp                                                │
│  └── + TravelingStrategy.h                                                  │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Constants Reference

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         TravelConstants Namespace                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  Pre-Travel Vendor Thresholds:                                              │
│  ─────────────────────────────                                              │
│  DURABILITY_THRESHOLD     = 0.5f     // Vendor if any gear < 50%            │
│  BAG_FULL_THRESHOLD       = 0.6f     // Vendor if bags > 60% full           │
│                                                                              │
│  Anti-Thrashing:                                                            │
│  ───────────────                                                            │
│  ARRIVAL_COOLDOWN_MS      = 90000    // 90 sec minimum stay at spot         │
│  NO_MOBS_THRESHOLD        = 5        // 5 consecutive "no mobs" to travel   │
│                                                                              │
│  Movement:                                                                  │
│  ─────────                                                                  │
│  ARRIVAL_DISTANCE         = 30.0f    // Within 30 yards = "arrived"         │
│  STUCK_TIMEOUT_MS         = 30000    // 30 sec no progress = stuck          │
│  STUCK_MIN_DISTANCE       = 5.0f     // Must move 5+ yards = progress       │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Known Limitations (V1)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                            V1 Limitations                                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  1. SAME-MAP ONLY                                                           │
│     ─────────────                                                           │
│     Bots cannot travel between continents.                                  │
│     No boat/zeppelin/flight path support.                                   │
│     Query filters by map_id to ensure valid paths.                          │
│                                                                              │
│  2. NO DANGEROUS PATH AVOIDANCE                                             │
│     ────────────────────────────                                            │
│     Bot may path through enemy faction territory.                           │
│     May aggro high-level mobs en route.                                     │
│     Pathfinding is collision-based, not threat-based.                       │
│                                                                              │
│  3. RESPAWN TIMING                                                          │
│     ──────────────                                                          │
│     Bot may travel to spot where mobs are temporarily dead.                 │
│     90-second arrival cooldown mitigates this somewhat.                     │
│     Bot will wait and mobs should respawn.                                  │
│                                                                              │
│  4. NO SPOT RATING/FEEDBACK                                                 │
│     ────────────────────────                                                │
│     Spots are static in database.                                           │
│     No learning from experience.                                            │
│     Priority is manually assigned.                                          │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Testing Checklist

| Scenario | Expected Behavior | Verification |
|----------|-------------------|--------------|
| Bot out-levels area | After 5+ "no mobs" ticks, travels to new spot | Check logs for travel message |
| Bot at good spot, mobs respawning | Stays put (cooldown), doesn't thrash | No travel logs during cooldown |
| Bot needs vendor before travel | TravelingStrategy yields, VendoringStrategy handles | Vendoring logs before travel |
| Bot uses spirit healer | Checks for res sickness, travel state reset | Res sickness log message |
| Bot arrives at destination | 90 sec cooldown before considering travel again | ARRIVED state in logs |
| Bot gets stuck while traveling | Resets after 30 sec, tries again | "stuck, resetting" log |
| Bot attacked while traveling | Pauses, fights, resumes same destination | Combat then resume logs |

---

*Generated: Phase 5 Travel System Implementation Report*
*Part of the vMangos RandomBot AI Project*
