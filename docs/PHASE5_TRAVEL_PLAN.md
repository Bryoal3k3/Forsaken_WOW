# Phase 5: Travel System Implementation Plan (Revised)

## Overview

Implement a database-driven travel system that allows bots to find and travel to level-appropriate grinding spots. Includes pre-travel inventory/durability checks with proper anti-thrashing safeguards.

---

## Known Limitations (V1)

Document these upfront:

| Limitation | Description |
|------------|-------------|
| **Same-map only** | Bots cannot cross continents (no boats/zeppelins/flight paths) |
| **No dangerous path avoidance** | Bot may path through enemy faction territory |
| **Respawn timing** | Bot may travel to spot where mobs are temporarily dead |

---

## Strategy Coordination Protocol

**Key Principle**: Strategies don't "trigger" each other. Each strategy checks its own preconditions and yields (returns false) if not applicable. RandomBotAI checks strategies in priority order.

**Priority Order** (highest first):
1. `GhostWalkingStrategy` - Dead? Handle resurrection
2. `VendoringStrategy` - Bags full OR gear broken? Vendor
3. `GrindingStrategy` - Mobs available? Fight
4. `TravelingStrategy` - No mobs AND not recently arrived? Travel
5. Combat buffs - Fallback when all strategies yield

**Coordination Rules**:
- Strategies never call each other
- A strategy returns `true` = "I'm handling this tick, stop checking others"
- A strategy returns `false` = "Not my concern, try next strategy"
- TravelingStrategy yields if `ShouldVendorBeforeTravel()` is true (lets VendoringStrategy handle it naturally)

---

## Database Setup

### Table in `characters` Database

```sql
CREATE TABLE grind_spots (
    id INT PRIMARY KEY AUTO_INCREMENT,
    map_id INT NOT NULL,                    -- 0=Eastern Kingdoms, 1=Kalimdor
    zone_id INT NOT NULL DEFAULT 0,         -- For reference/debugging
    x FLOAT NOT NULL,
    y FLOAT NOT NULL,
    z FLOAT NOT NULL,
    min_level TINYINT NOT NULL,
    max_level TINYINT NOT NULL,
    faction TINYINT NOT NULL DEFAULT 0,     -- 0=both, 1=alliance, 2=horde
    radius FLOAT NOT NULL DEFAULT 100.0,    -- Roaming radius from center point
    priority TINYINT NOT NULL DEFAULT 50,   -- Higher = better spot (1-100)
    name VARCHAR(64)                        -- Debug name
);

CREATE INDEX idx_grind_level ON grind_spots(min_level, max_level);
CREATE INDEX idx_grind_faction ON grind_spots(faction);
CREATE INDEX idx_grind_map ON grind_spots(map_id);
```

### Populating Grind Spots (Auto-Generate + Manual Curation)

**Step 1: Auto-generate from mangos world database**

```sql
-- Simplified query (removed broken area_template subquery)
-- Zone names will be added manually during curation

SELECT
    c.map as map_id,
    ROUND(c.position_x / 100) * 100 as grid_x,
    ROUND(c.position_y / 100) * 100 as grid_y,
    ROUND(AVG(c.position_z), 1) as avg_z,
    MIN(ct.minlevel) as min_level,
    MAX(ct.maxlevel) as max_level,
    COUNT(*) as mob_count
FROM creature c
JOIN creature_template ct ON c.id = ct.entry
WHERE ct.rank = 0                    -- Non-elite only
  AND ct.minlevel > 0                -- Has a level
  AND ct.unit_flags & 2 = 0          -- Not non-attackable
  AND c.map IN (0, 1)                -- Eastern Kingdoms + Kalimdor only
GROUP BY c.map, grid_x, grid_y
HAVING mob_count >= 5
ORDER BY min_level, mob_count DESC;
```

**Step 2: Manual curation**
- Add zone names manually
- Remove dungeon/instance spots
- Remove spots in enemy faction territory
- Set faction tags and priority values
- Verify coordinates in-game

### Initial Test Data (Starting Zones)

```sql
-- Coordinates should be verified against actual vMangos spawn data
-- Alliance
INSERT INTO grind_spots (map_id, zone_id, x, y, z, min_level, max_level, faction, radius, priority, name) VALUES
(0, 12, -8913, -136, 82, 1, 6, 1, 80.0, 70, 'Northshire Valley'),
(0, 12, -9464, 62, 56, 6, 12, 1, 120.0, 60, 'Elwynn Forest'),
(0, 1, -6240, 331, 383, 1, 6, 1, 80.0, 70, 'Coldridge Valley'),
(1, 141, 10311, 832, 1326, 1, 6, 1, 80.0, 70, 'Shadowglen');

-- Horde
INSERT INTO grind_spots (map_id, zone_id, x, y, z, min_level, max_level, faction, radius, priority, name) VALUES
(1, 14, -618, -4251, 38, 1, 6, 2, 80.0, 70, 'Valley of Trials'),
(1, 14, 296, -4706, 16, 6, 12, 2, 120.0, 60, 'Durotar'),
(1, 215, -2917, -257, 52, 1, 6, 2, 80.0, 70, 'Camp Narache'),
(0, 85, 1676, 1678, 121, 1, 6, 2, 80.0, 70, 'Deathknell'),
(0, 85, 2270, 323, 34, 6, 12, 2, 120.0, 60, 'Tirisfal Glades');
```

---

## Code Changes

### Constants (in appropriate header)

```cpp
namespace TravelConstants
{
    // Pre-travel vendor thresholds
    constexpr float DURABILITY_THRESHOLD = 0.5f;    // Vendor if any gear < 50%
    constexpr float BAG_FULL_THRESHOLD = 0.6f;      // Vendor if bags > 60% full

    // Anti-thrashing
    constexpr uint32 ARRIVAL_COOLDOWN_MS = 90000;   // 90 sec minimum stay at spot
    constexpr uint32 NO_MOBS_THRESHOLD = 5;         // Consecutive "no mobs" before travel

    // Movement
    constexpr float ARRIVAL_DISTANCE = 30.0f;       // Consider "arrived" within 30 yards
    constexpr uint32 STUCK_TIMEOUT_MS = 30000;      // 30 sec without progress = stuck
    constexpr float STUCK_MIN_DISTANCE = 5.0f;      // Must move 5 yards per check
}
```

### 1. Modify GrindingStrategy - Add Explicit Result Signaling

**File**: `src/game/PlayerBots/Strategies/GrindingStrategy.h`

```cpp
enum class GrindingResult
{
    ENGAGED,        // Found target, attacking
    NO_TARGETS,     // Searched area, no valid mobs found
    BUSY            // Doing something else (shouldn't happen in current design)
};

class GrindingStrategy : public IBotStrategy
{
public:
    // Change return type
    GrindingResult UpdateGrinding(Player* pBot, uint32 diff);

    // Keep IBotStrategy interface for compatibility
    bool Update(Player* pBot, uint32 diff) override;

    // Track consecutive failures
    uint32 GetNoMobsCount() const { return m_noMobsCount; }
    void ResetNoMobsCount() { m_noMobsCount = 0; }

private:
    uint32 m_noMobsCount = 0;
};
```

**File**: `src/game/PlayerBots/Strategies/GrindingStrategy.cpp`

```cpp
GrindingResult GrindingStrategy::UpdateGrinding(Player* pBot, uint32 diff)
{
    if (!pBot || !pBot->IsAlive() || pBot->IsInCombat())
        return GrindingResult::BUSY;

    if (pBot->GetVictim())
    {
        m_noMobsCount = 0;  // Reset on successful engagement
        return GrindingResult::ENGAGED;
    }

    Creature* pTarget = FindGrindTarget(pBot, SEARCH_RANGE);
    if (pTarget)
    {
        m_noMobsCount = 0;
        // ... engage target ...
        return GrindingResult::ENGAGED;
    }

    // No mobs found
    m_noMobsCount++;
    return GrindingResult::NO_TARGETS;
}

bool GrindingStrategy::Update(Player* pBot, uint32 diff)
{
    return UpdateGrinding(pBot, diff) == GrindingResult::ENGAGED;
}
```

### 2. New TravelingStrategy (Note: -ing for consistency)

**File**: `src/game/PlayerBots/Strategies/TravelingStrategy.h`

```cpp
#pragma once
#include "IBotStrategy.h"

class TravelingStrategy : public IBotStrategy
{
public:
    TravelingStrategy();

    bool Update(Player* pBot, uint32 diff) override;
    void OnEnterCombat(Player* pBot) override;
    void OnLeaveCombat(Player* pBot) override;
    char const* GetName() const override { return "TravelingStrategy"; }

    // Called by RandomBotAI when grinding reports NO_TARGETS
    void SignalNoMobs() { m_noMobsSignaled = true; }

    // Called when bot arrives at destination or finds mobs
    void ResetArrivalCooldown();

private:
    enum class TravelState
    {
        IDLE,           // Not traveling, checking if needed
        FINDING_SPOT,   // Query DB for destination
        WALKING,        // Moving to destination
        ARRIVED         // At destination, on cooldown
    };

    TravelState m_state = TravelState::IDLE;
    bool m_noMobsSignaled = false;

    // Destination
    float m_targetX = 0, m_targetY = 0, m_targetZ = 0;
    std::string m_targetName;

    // Anti-thrashing
    uint32 m_arrivalTime = 0;           // When we arrived at current spot
    uint32 m_noMobsConsecutive = 0;     // Counter from GrindingStrategy

    // Stuck detection
    float m_lastX = 0, m_lastY = 0;
    uint32 m_lastProgressTime = 0;

    // Helpers
    bool FindGrindSpot(Player* pBot);
    bool IsAtDestination(Player* pBot) const;
    bool ShouldTravel(Player* pBot) const;
    static uint8 GetBotFaction(Player* pBot);
};
```

**File**: `src/game/PlayerBots/Strategies/TravelingStrategy.cpp`

```cpp
#include "TravelingStrategy.h"
#include "VendoringStrategy.h"
#include "Database/DatabaseEnv.h"

using namespace TravelConstants;

bool TravelingStrategy::Update(Player* pBot, uint32 diff)
{
    if (!pBot || !pBot->IsAlive())
        return false;

    switch (m_state)
    {
        case TravelState::IDLE:
        {
            if (!ShouldTravel(pBot))
                return false;

            // Check if we should vendor first - if so, YIELD entirely
            // Let VendoringStrategy handle it on next tick
            if (VendoringStrategy::GetLowestDurabilityPercent(pBot) < DURABILITY_THRESHOLD ||
                VendoringStrategy::GetBagFullPercent(pBot) > BAG_FULL_THRESHOLD)
            {
                sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
                    "TravelingStrategy: %s needs vendor before travel, yielding",
                    pBot->GetName());
                return false;  // Yield - vendoring will trigger naturally
            }

            m_state = TravelState::FINDING_SPOT;
            // Fall through
        }

        case TravelState::FINDING_SPOT:
        {
            if (!FindGrindSpot(pBot))
            {
                sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
                    "TravelingStrategy: %s no grind spot found for level %u",
                    pBot->GetName(), pBot->GetLevel());
                m_state = TravelState::IDLE;
                return false;
            }

            sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
                "TravelingStrategy: %s traveling to %s",
                pBot->GetName(), m_targetName.c_str());

            m_lastX = pBot->GetPositionX();
            m_lastY = pBot->GetPositionY();
            m_lastProgressTime = WorldTimer::getMSTime();

            pBot->GetMotionMaster()->MovePoint(
                0, m_targetX, m_targetY, m_targetZ,
                MOVE_PATHFINDING | MOVE_RUN_MODE);

            m_state = TravelState::WALKING;
            return true;
        }

        case TravelState::WALKING:
        {
            if (IsAtDestination(pBot))
            {
                sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
                    "TravelingStrategy: %s arrived at %s",
                    pBot->GetName(), m_targetName.c_str());

                m_arrivalTime = WorldTimer::getMSTime();
                m_state = TravelState::ARRIVED;
                m_noMobsSignaled = false;
                return false;  // Let grinding take over
            }

            // Stuck detection
            float dx = pBot->GetPositionX() - m_lastX;
            float dy = pBot->GetPositionY() - m_lastY;
            float distMoved = sqrt(dx*dx + dy*dy);

            if (distMoved >= STUCK_MIN_DISTANCE)
            {
                m_lastX = pBot->GetPositionX();
                m_lastY = pBot->GetPositionY();
                m_lastProgressTime = WorldTimer::getMSTime();
            }
            else if (WorldTimer::getMSTimeDiff(m_lastProgressTime, WorldTimer::getMSTime()) > STUCK_TIMEOUT_MS)
            {
                sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
                    "TravelingStrategy: %s stuck, resetting",
                    pBot->GetName());
                m_state = TravelState::IDLE;
                return false;
            }

            return true;  // Still walking
        }

        case TravelState::ARRIVED:
        {
            // Cooldown period - stay here and let grinding work
            if (WorldTimer::getMSTimeDiff(m_arrivalTime, WorldTimer::getMSTime()) < ARRIVAL_COOLDOWN_MS)
            {
                return false;  // Yield to grinding
            }

            // Cooldown expired, back to idle
            m_state = TravelState::IDLE;
            return false;
        }
    }

    return false;
}

bool TravelingStrategy::ShouldTravel(Player* pBot) const
{
    // Don't travel if we recently arrived somewhere
    if (m_state == TravelState::ARRIVED)
        return false;

    // Don't travel if grinding hasn't signaled NO_TARGETS enough times
    if (!m_noMobsSignaled)
        return false;

    // Get consecutive no-mobs count from RandomBotAI/GrindingStrategy
    // Only travel after threshold consecutive failures
    // (This will be wired up in RandomBotAI)

    return true;
}

uint8 TravelingStrategy::GetBotFaction(Player* pBot)
{
    // Map race to faction: 1=Alliance, 2=Horde
    switch (pBot->GetRace())
    {
        case RACE_HUMAN:
        case RACE_DWARF:
        case RACE_NIGHTELF:
        case RACE_GNOME:
            return 1;  // Alliance
        case RACE_ORC:
        case RACE_UNDEAD:
        case RACE_TAUREN:
        case RACE_TROLL:
            return 2;  // Horde
        default:
            return 0;  // Unknown
    }
}

bool TravelingStrategy::FindGrindSpot(Player* pBot)
{
    uint32 level = pBot->GetLevel();
    uint32 mapId = pBot->GetMapId();
    uint8 faction = GetBotFaction(pBot);

    // Query with bounding box pre-filter for efficiency (5000 yard box)
    float px = pBot->GetPositionX();
    float py = pBot->GetPositionY();

    auto result = CharacterDatabase.PQuery(
        "SELECT x, y, z, name FROM grind_spots "
        "WHERE map_id = %u "
        "AND min_level <= %u AND max_level >= %u "
        "AND (faction = 0 OR faction = %u) "
        "AND x BETWEEN %f AND %f "
        "AND y BETWEEN %f AND %f "
        "ORDER BY priority DESC, POW(x - %f, 2) + POW(y - %f, 2) ASC "
        "LIMIT 1",
        mapId, level, level, faction,
        px - 5000.0f, px + 5000.0f,
        py - 5000.0f, py + 5000.0f,
        px, py);

    if (result)
    {
        Field* fields = result->Fetch();
        m_targetX = fields[0].GetFloat();
        m_targetY = fields[1].GetFloat();
        m_targetZ = fields[2].GetFloat();
        m_targetName = fields[3].GetCppString();
        return true;
    }

    // No spot in bounding box, try without distance limit
    // Priority first, then distance as tiebreaker for equal-priority spots
    result = CharacterDatabase.PQuery(
        "SELECT x, y, z, name FROM grind_spots "
        "WHERE map_id = %u "
        "AND min_level <= %u AND max_level >= %u "
        "AND (faction = 0 OR faction = %u) "
        "ORDER BY priority DESC, POW(x - %f, 2) + POW(y - %f, 2) ASC LIMIT 1",
        mapId, level, level, faction, px, py);

    if (result)
    {
        Field* fields = result->Fetch();
        m_targetX = fields[0].GetFloat();
        m_targetY = fields[1].GetFloat();
        m_targetZ = fields[2].GetFloat();
        m_targetName = fields[3].GetCppString();
        return true;
    }

    return false;
}

bool TravelingStrategy::IsAtDestination(Player* pBot) const
{
    float dx = pBot->GetPositionX() - m_targetX;
    float dy = pBot->GetPositionY() - m_targetY;
    return (dx*dx + dy*dy) < (ARRIVAL_DISTANCE * ARRIVAL_DISTANCE);
}

void TravelingStrategy::OnEnterCombat(Player* pBot)
{
    if (m_state == TravelState::WALKING)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
            "TravelingStrategy: %s entered combat while traveling, pausing",
            pBot->GetName());
        // Don't reset - just pause. Combat handler takes over.
    }
}

void TravelingStrategy::OnLeaveCombat(Player* pBot)
{
    // Resume walking if we were interrupted
    if (m_state == TravelState::WALKING)
    {
        pBot->GetMotionMaster()->MovePoint(
            0, m_targetX, m_targetY, m_targetZ,
            MOVE_PATHFINDING | MOVE_RUN_MODE);
    }
}

void TravelingStrategy::ResetArrivalCooldown()
{
    m_arrivalTime = 0;
    m_noMobsSignaled = false;
    if (m_state == TravelState::ARRIVED)
        m_state = TravelState::IDLE;
}
```

### 3. Modify VendoringStrategy - Add Threshold Methods

**File**: `src/game/PlayerBots/Strategies/VendoringStrategy.h`

Add:
```cpp
// Percentage-based checks for pre-travel decisions
static float GetBagFullPercent(Player* pBot);
static float GetLowestDurabilityPercent(Player* pBot);
```

**File**: `src/game/PlayerBots/Strategies/VendoringStrategy.cpp`

```cpp
float VendoringStrategy::GetBagFullPercent(Player* pBot)
{
    uint32 totalSlots = 16;  // Backpack
    uint32 usedSlots = 0;

    // Count backpack
    for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        if (pBot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            usedSlots++;
    }

    // Count bags
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        if (Bag* pBag = dynamic_cast<Bag*>(pBot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag)))
        {
            uint32 bagSize = pBag->GetBagSize();
            totalSlots += bagSize;
            for (uint32 slot = 0; slot < bagSize; ++slot)
            {
                if (pBag->GetItemByPos(slot))
                    usedSlots++;
            }
        }
    }

    return totalSlots > 0 ? (float)usedSlots / (float)totalSlots : 0.0f;
}

float VendoringStrategy::GetLowestDurabilityPercent(Player* pBot)
{
    float lowestPercent = 1.0f;

    for (int i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
    {
        Item* pItem = pBot->GetItemByPos(INVENTORY_SLOT_BAG_0, i);
        if (pItem && pItem->GetMaxDurability() > 0)
        {
            float percent = (float)pItem->GetDurability() / (float)pItem->GetMaxDurability();
            if (percent < lowestPercent)
                lowestPercent = percent;
        }
    }

    return lowestPercent;
}
```

### 4. Modify RandomBotAI - Integration

**File**: `src/game/PlayerBots/RandomBotAI.h`

```cpp
#include "Strategies/TravelingStrategy.h"

class RandomBotAI : public CombatBotBaseAI
{
    // ... existing ...
    std::unique_ptr<TravelingStrategy> m_travelingStrategy;

public:
    TravelingStrategy* GetTravelingStrategy() { return m_travelingStrategy.get(); }
};
```

**File**: `src/game/PlayerBots/RandomBotAI.cpp`

```cpp
// In constructor
m_travelingStrategy = std::make_unique<TravelingStrategy>();

void RandomBotAI::UpdateOutOfCombatAI()
{
    // Priority 1: Vendoring (bags full OR gear broken)
    if (m_vendoringStrategy && m_vendoringStrategy->Update(me, RB_UPDATE_INTERVAL))
        return;

    // Priority 2: Grinding
    GrindingResult grindResult = m_strategy->UpdateGrinding(me, 0);
    if (grindResult == GrindingResult::ENGAGED)
    {
        // Found and attacking a target
        m_travelingStrategy->ResetArrivalCooldown();  // We found mobs, reset travel state
        return;
    }

    // Priority 3: Travel (only if grinding found NO_TARGETS)
    if (grindResult == GrindingResult::NO_TARGETS)
    {
        // Check if we've had enough consecutive failures
        if (m_strategy->GetNoMobsCount() >= TravelConstants::NO_MOBS_THRESHOLD)
        {
            m_travelingStrategy->SignalNoMobs();
            if (m_travelingStrategy->Update(me, RB_UPDATE_INTERVAL))
                return;
        }
    }

    // Fallback: Out of combat buffs
    m_combatMgr->UpdateOutOfCombat(me);
}
```

### 5. Modify GhostWalkingStrategy - Spirit Healer Handling

**File**: `src/game/PlayerBots/Strategies/GhostWalkingStrategy.cpp`

```cpp
// After spirit healer resurrection
void GhostWalkingStrategy::HandleSpiritHealerRes(Player* pBot)
{
    // Check for resurrection sickness
    if (pBot->HasAura(15007))  // Resurrection Sickness spell ID
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
            "GhostWalkingStrategy: %s has res sickness, will wait before grinding",
            pBot->GetName());
        // Don't signal travel - let bot rest/wait
        // The res sickness will expire and normal grinding will resume
        return;
    }

    // No res sickness - check if current location has mobs first
    // If grinding finds targets, no travel needed
    // This happens naturally through the normal update flow

    // Just reset travel state so it can evaluate fresh
    if (RandomBotAI* pAI = dynamic_cast<RandomBotAI*>(pBot->AI()))
    {
        if (pAI->GetTravelingStrategy())
            pAI->GetTravelingStrategy()->ResetArrivalCooldown();
    }
}
```

---

## Files Summary

| Action | File |
|--------|------|
| **CREATE** | `src/game/PlayerBots/Strategies/TravelingStrategy.h` |
| **CREATE** | `src/game/PlayerBots/Strategies/TravelingStrategy.cpp` |
| **MODIFY** | `src/game/PlayerBots/Strategies/GrindingStrategy.h` - Add GrindingResult enum, counter |
| **MODIFY** | `src/game/PlayerBots/Strategies/GrindingStrategy.cpp` - Implement UpdateGrinding() |
| **MODIFY** | `src/game/PlayerBots/Strategies/VendoringStrategy.h` - Add percentage methods |
| **MODIFY** | `src/game/PlayerBots/Strategies/VendoringStrategy.cpp` - Implement percentage methods |
| **MODIFY** | `src/game/PlayerBots/Strategies/GhostWalkingStrategy.cpp` - Res sickness handling |
| **MODIFY** | `src/game/PlayerBots/RandomBotAI.h` - Add m_travelingStrategy |
| **MODIFY** | `src/game/PlayerBots/RandomBotAI.cpp` - Integration with GrindingResult |
| **MODIFY** | `src/game/PlayerBots/CMakeLists.txt` - Add TravelingStrategy.cpp |
| **CREATE** | SQL script for characters.grind_spots table |

---

## Implementation Order

### Step 1: Database Setup
1. Create `grind_spots` table in `characters` database
2. Insert test data for starting zones
3. Verify queries work: `SELECT * FROM grind_spots WHERE min_level <= 5 AND max_level >= 5 AND faction IN (0, 1);`

### Step 2: GrindingStrategy Changes
1. Add `GrindingResult` enum
2. Add `m_noMobsCount` member
3. Implement `UpdateGrinding()` returning explicit result
4. Keep `Update()` for IBotStrategy compatibility

### Step 3: VendoringStrategy Threshold Methods
1. Add `GetBagFullPercent()`
2. Add `GetLowestDurabilityPercent()`
3. Test with debug logging

### Step 4: TravelingStrategy Core
1. Create header with state machine
2. Implement `Update()` with all states
3. Implement `FindGrindSpot()` DB query
4. Implement `GetBotFaction()` race mapping
5. Add stuck detection and logging

### Step 5: Integration
1. Add `m_travelingStrategy` to RandomBotAI
2. Update `UpdateOutOfCombatAI()` with GrindingResult handling
3. Wire up anti-thrash logic (NO_MOBS_THRESHOLD check)

### Step 6: GhostWalkingStrategy Update
1. Add res sickness check (aura 15007)
2. Reset travel state after spirit healer res

### Step 7: Full Data Generation
1. Run auto-generation query
2. Manual curation pass
3. Import curated data

---

## Verification

### Build
```bash
cd ~/Desktop/Forsaken_WOW/core/build && make -j$(nproc) && make install
```

### Test Scenarios

| Scenario | Expected Behavior |
|----------|-------------------|
| Bot out-levels area | After 5+ "no mobs" ticks, travels to new spot |
| Bot at good spot, mobs respawning | Stays put (cooldown), doesn't thrash |
| Bot needs vendor before travel | TravelingStrategy yields, VendoringStrategy handles |
| Bot uses spirit healer | Checks for res sickness, waits if present |
| Bot arrives at destination | 90 sec cooldown before considering travel again |
| Bot gets stuck while traveling | Resets after 30 sec, tries again |

### Debug Logging
All state transitions logged with `LOG_LVL_DEBUG`:
- "TravelingStrategy: %s traveling to %s"
- "TravelingStrategy: %s arrived at %s"
- "TravelingStrategy: %s needs vendor before travel, yielding"
- "TravelingStrategy: %s stuck, resetting"
