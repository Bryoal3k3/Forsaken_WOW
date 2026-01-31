# RandomBot AI Development Progress - Archive

This file contains archived session logs from the RandomBot AI development.
For current progress, see `docs/PROGRESS.md`.

---

## Session Log (Archived)

### 2026-01-28 - Auto-Generated Grind Spots (Phase 5.5) - TESTING

#### Overview
Replaced manual 23-entry `grind_spots` table with 2,684 auto-generated spots derived from creature spawn data. Bots now have full 1-60 coverage on both continents.

#### What Was Done

**1. Database Analysis & Generation**
- Queried `creature` + `creature_template` tables for grindable mobs
- Filter criteria:
  - `loot_id > 0` (has loot - gold OR items, includes beasts)
  - `rank = 0` (normal mob, not elite)
  - `type != 8` (not critter)
  - `flags_extra NOT IN (1024, 2)` (not guards or civilians)
  - `patch_min <= 10 AND patch_max >= 10` (1.12 patch only)
- Clustered spawns into 200-yard grid cells
- Used `STDDEV(level) < 5` to ensure level consistency per spot
- Bot level range per spot: mob_level - 3 to mob_level + 1

**2. Zone Validation System**
- Created `zone_levels` table (39 zones) from quest-creature mappings
- Cross-referenced spot coordinates with zone boundaries
- Excluded 21 dangerous spots (level mismatches at zone borders)

**3. Travel Randomization (Code Change)**
- Modified `TravelingStrategy::FindGrindSpot()` in `TravelingStrategy.cpp`
- Two-phase search:
  1. **Local spots** (within 800 yards) - randomly pick from valid options
  2. **Distant spots** (if no local) - weighted random favoring closer
- Small offset (+/-25 yards) prevents bots stacking on exact same point
- Prevents "train" behavior where all bots go to same spot

#### Database Tables

| Table | Location | Purpose |
|-------|----------|---------|
| `grind_spots` | characters DB | 2,684 grind locations (was 23) |
| `grind_spots_backup` | characters DB | Original manual spots backup |
| `zone_levels` | characters DB | 39 zone coordinate/level boundaries |

#### Statistics

| Metric | Value |
|--------|-------|
| Total spots | 2,684 |
| Eastern Kingdoms | 1,088 |
| Kalimdor | 1,596 |
| Level coverage | 1-60 |
| Dangerous spots excluded | 21 |

---

## Danger Zone System (Implemented - Disabled)

A reactive danger zone cache was implemented for threat avoidance during travel but is currently disabled pending the PathFinder bugs above being resolved.

**Files Created:**
- `DangerZoneCache.h/cpp` - Singleton spatial grid cache

**How It Works:**
1. Bot traveling gets attacked by mob 3+ levels higher
2. Bot reports danger location to `sDangerZoneCache`
3. Subsequent bots query cache when generating waypoints
4. Waypoints passing through danger zones are filtered out
5. Cache entries expire after 5 minutes

**To Enable:** Uncomment code in:
- `RandomBotAI.cpp:UpdateOutOfCombatAI()` - ReportDanger call
- `TravelingStrategy.cpp:GenerateWaypoints()` - FilterWaypointsForDanger call

---

### 2026-01-26 - BUG FIX: Bots Not Looting or Buffing (Bug #11)

#### Problem
Bots were "animalistic killing machines" - immediately targeting the next mob after a kill without looting corpses or casting self-buffs (Ice Armor, Demon Skin, Seal of Righteousness, etc.).

#### Root Causes (Two Issues)
1. **Buffing**: `UpdateOutOfCombat()` (buff logic) was called AFTER `GrindingStrategy::UpdateGrinding()`. Since grinding always found and engaged a target, the function returned before reaching the buff code.

2. **Looting**: `GetVictim()` returned the dead mob after combat ended. The check `if (inCombat || me->GetVictim())` kept bots in the combat branch, preventing the looting code from running.

#### Solution (Two Parts)
1. **Moved buff check BEFORE grinding** in `UpdateOutOfCombatAI()` - buffs are now applied before looking for the next target

2. **Added `AttackStop()` call** in `UpdateInCombatAI()` when victim is dead and no new attacker found - this clears `GetVictim()` so the bot properly exits the combat branch

#### Files Modified
- `RandomBotAI.cpp` - Reordered buff call, added AttackStop() for dead victims

---

### 2026-01-26 - BUG FIX: Bots Not Entering Caves/Buildings (Bug #9)

#### Problem
Bots would target mobs inside caves and buildings but stand outside instead of pathing through the entrance to fight them. Both melee and ranged classes affected.

#### Root Cause (Two Issues)
1. **Target Selection**: Bug #7's LoS check prevented ranged classes from targeting mobs inside structures entirely - a "half-ass fix" that avoided the problem instead of solving it
2. **Movement Interruption**: When `MoveChase` was interrupted by `ChaseMovementGenerator` edge cases (INCOMPLETE path + LoS = StopMoving), nothing restarted the chase

#### Solution (Two Parts)
1. **Removed LoS check from `IsValidGrindTarget()`** - Let bots target mobs in caves/buildings
2. **Added movement persistence in combat handlers**:
   - `HandleRangedMovement()`: If at casting range but NO line of sight -> `MoveChase()` closer (through the door/entrance)
   - `HandleMeleeMovement()`: If not in melee range and not moving -> re-issue `MoveChase()`

#### Files Modified
- `GrindingStrategy.cpp` - Removed LoS check from target validation
- `CombatHelpers.h` - Added LoS check to `HandleRangedMovement()`, new `HandleMeleeMovement()` helper
- `WarriorCombat.cpp`, `RogueCombat.cpp`, `PaladinCombat.cpp`, `ShamanCombat.cpp`, `DruidCombat.cpp` - Added `HandleMeleeMovement()` call

---

### 2026-01-26 - BUG FIX: Bots Walking Onto Steep Slopes (Bug #2)

#### Problem
Bots were walking onto steep slopes (terrain with no navmesh) and getting `startPoly=0` pathfinding errors. They would try to chase mobs by going OVER hills instead of around them, ending up stuck on slopes.

#### Root Cause
`ChaseMovementGenerator` and `FollowMovementGenerator` in `TargetedMovementGenerator.cpp` did not call `ExcludeSteepSlopes()` for player bots. When calculating chase paths, steep terrain was included, leading bots onto no-navmesh areas.

#### Solution
Added steep slope exclusion specifically for bots (not NPCs - they can walk on slopes):
```cpp
// Bots should not path through steep slopes (players can't walk on them)
if (owner.IsPlayer() && ((Player const*)&owner)->IsBot())
    path.ExcludeSteepSlopes();
```

#### Files Modified
- `TargetedMovementGenerator.cpp` - Added `ExcludeSteepSlopes()` for bots in both ChaseMovementGenerator and FollowMovementGenerator

---

### 2026-01-26 - BUG FIXES (Falling Through Floor, Unreachable Mobs, Stuck Protection)

#### Bug #1: Bot Falling Through Floor - FIXED
- **Problem**: Bots fell through floor causing infinite `startPoly=0` pathfinding error spam
- **Fix**: 4-part defense in depth:
  1. Z validation in `GenerateWaypoints()` - skip invalid waypoints
  2. Recovery teleport after 15 ticks of invalid position
  3. Z correction on grind spot cache load
  4. Rate-limited error logging (10 sec per bot)
- **Files Modified**: `TravelingStrategy.cpp`, `RandomBotAI.h/cpp`, `PathFinder.cpp`
- **Commit**: `af528e7c4`

#### Bug #3 & #4: Bots Walking Into Terrain / Targeting Unreachable Mobs - FIXED
- **Problem**: Bots walked up steep slopes, got stuck in rocks trying to reach mobs on unreachable terrain
- **Root Cause**: `IsValidGrindTarget()` didn't validate path reachability. Initial NOPATH check missed cases where PathFinder returned `PATHFIND_NOT_USING_PATH` instead
- **Fix**: Added comprehensive reachability check that rejects BOTH `PATHFIND_NOPATH` AND `PATHFIND_NOT_USING_PATH`
- **Files Modified**: `GrindingStrategy.cpp`

#### Bug #6: Stuck Protection Too Aggressive - FIXED
- **Problem**: Recovery teleport fired when bots were resting or fighting near walls/obstacles
- **Root Cause**: Check was pathing to point 5 yards ahead - failed if facing a wall even though bot's position was valid
- **Fix**: Changed from PathFinder-based to Map::GetHeight()-based detection. Only triggers if terrain is actually invalid at bot's position
- **Files Modified**: `RandomBotAI.cpp` (removed PathFinder.h and cmath includes)

---

### 2026-01-25 - PATHFINDER LONG PATH FIX

#### Problem
Long-distance travel paths (e.g., Kharanos -> Coldridge Valley, ~1000 yards) were failing with `PATHFIND_NOPATH` even though the path existed. Debug logging revealed:
```
BuildPointPath: FAILED result=0x80000000 pointCount=256 polyLength=79
```

#### Root Cause
Bug in vMangos `PathFinder.cpp:findSmoothPath()`. When generating smooth waypoints from a poly path, if the 256-point buffer filled up, it returned `DT_FAILURE` instead of `DT_SUCCESS | DT_BUFFER_TOO_SMALL`.

The original code comment said "this is most likely a loop" - assuming full buffer = infinite loop. But for long paths (79 polygons), 256 waypoints is legitimately not enough.

#### Solution

**Part 1: Fix findSmoothPath() return value**
```cpp
// OLD (wrong):
return nsmoothPath < MAX_POINT_PATH_LENGTH ? DT_SUCCESS : DT_FAILURE;

// NEW (correct):
return nsmoothPath < MAX_POINT_PATH_LENGTH ? DT_SUCCESS : (DT_SUCCESS | DT_BUFFER_TOO_SMALL);
```

**Part 2: Handle truncation in BuildPointPath()**
```cpp
// Mark truncated paths as INCOMPLETE, not NOPATH
if (dtResult & DT_BUFFER_TOO_SMALL)
    m_type = PATHFIND_INCOMPLETE;
```

#### Files Modified
- `src/game/Maps/PathFinder.cpp` - Fixed findSmoothPath return value and BuildPointPath handling

---

### 2026-01-25 - Movement Sync Bug Fixed (TravelingStrategy Overhaul)
- **Issue**: Bots moved at super speed, disappeared, floated through air during long-distance travel
- **Root Cause**: Single `MovePoint()` for entire journey failed when navmesh couldn't find path, falling back to direct 2-point line ignoring terrain
- **Fix**: Complete TravelingStrategy overhaul:
  1. Added `MOVE_EXCLUDE_STEEP_SLOPES` flag for terrain handling
  2. Added `ValidatePath()` - PathFinder check before travel (aborts if unreachable)
  3. Added `GenerateWaypoints()` - breaks journey into ~200 yard segments
  4. Added `MoveToCurrentWaypoint()` - issues MovePoint with proper flags
  5. Added `OnWaypointReached()` - chains waypoints via MovementInform callback
  6. Added `MovementInform()` override in RandomBotAI to notify TravelingStrategy
  7. Immediate waypoint transitions (call MoveToCurrentWaypoint directly, no Update tick delay)
- **Files Modified**:
  - `TravelingStrategy.h` - PathFinder include, waypoint members, 4 new methods
  - `TravelingStrategy.cpp` - Path validation, waypoint generation, terrain flags, ~100 new lines
  - `RandomBotAI.h` - MovementInform override declaration
  - `RandomBotAI.cpp` - MovementInform implementation
- **Tested**: Bot ran smoothly from Kharanos to Coldridge Valley without stopping or terrain issues

### 2026-01-25 - Combat Facing Bug Fixed
- **Issue**: Bots got stuck when target was behind them (hunter couldn't engage wolf behind it)
- **Root Cause**: `Engage()` and `UpdateCombat()` didn't ensure bot faced target. `MoveChase()` doesn't handle facing when already in range.
- **Fix**: Added facing logic in two places:
  1. `CombatHelpers.h` - `SetFacingToObject(pTarget)` before `Attack()` in both `EngageMelee()` and `EngageCaster()`
  2. `BotCombatMgr.cpp` - Facing check in `UpdateCombat()` using BattleBotAI pattern (`HasInArc()` + `SetInFront()` + `SendMovementPacket()`)
- **Files Modified**: `Combat/CombatHelpers.h`, `Combat/BotCombatMgr.cpp`

### 2026-01-24 - Code Audit Issue #8 Fixed
- **Issue**: Dead code - `GetNextFreeCharacterGuid()` method never called
- **Fix**: Removed method from RandomBotGenerator.cpp and declaration from .h
- **Context**: Method was obsoleted when bot generation switched to `sObjectMgr.GeneratePlayerLowGuid()`
- **Files Modified**: `RandomBotGenerator.cpp`, `RandomBotGenerator.h`

### 2026-01-24 - CODE AUDIT #1 COMPLETE (12/12 issues)
- **Full report:** See `docs/archive/audits/Audit_1.md` for detailed breakdown

### 2026-01-24 - Phase 5 COMPLETE (Travel System)
- **Feature**: Bots now travel to level-appropriate grind spots when current area has no mobs
- **Database**: Created `characters.grind_spots` table with starting zone data
- **New Files**:
  - `sql/custom/grind_spots.sql` - Table schema + test data
  - `Strategies/TravelingStrategy.h/cpp` - State machine for travel
- **Modified Files**:
  - `GrindingStrategy.h/cpp` - Added `GrindingResult` enum for explicit signaling
  - `VendoringStrategy.h/cpp` - Added percentage-based threshold methods
  - `RandomBotAI.h/cpp` - Integrated TravelingStrategy
  - `GhostWalkingStrategy.cpp` - Reset travel state on resurrection

### 2026-01-24 - Tree Collision FIXED
- **Problem**: Bots walked through trees that players cannot walk through
- **Root Cause**: **Elysium client had corrupted/modified M2 data** - trees were missing collision info that exists in vanilla 1.12
- **Fix**: Re-extracted vmaps and mmaps from a **clean vanilla 1.12 WoW client**
- **Result**: Bots now properly pathfind around trees!

### 2025-01-22 - Enable Navmesh Pathfinding for Bot Movement
- **Problem**: Bots were walking through terrain (falling through hills/mounds) when using MovePoint for vendoring, ghost walking, and looting.
- **Root Cause**: `MovePoint()` was called without the `MOVE_PATHFINDING` flag, causing it to create direct 2-point paths that ignore the navmesh.
- **Fix**: Added `MOVE_PATHFINDING | MOVE_RUN_MODE` flags to all MovePoint calls in bot strategies.
- **Files Modified**:
  - `src/game/PlayerBots/Strategies/VendoringStrategy.cpp` - Pathfinding + debug logging
  - `src/game/PlayerBots/Strategies/GhostWalkingStrategy.cpp` - Pathfinding flag
  - `src/game/PlayerBots/Strategies/LootingBehavior.cpp` - Pathfinding flag

### 2025-01-17 - Hunter Melee Fallback Fix
- **Problem**: Level 1 hunters would stand idle when mobs reached melee range - no attacks at all.
- **Fix**: Added `Attack(pVictim, true)` and `MoveChase(pVictim)` at the start of the melee block.
- **Files Modified**: `src/game/PlayerBots/Combat/Classes/HunterCombat.cpp`

### 2025-01-17 - Ranged Kiting Bug Fix
- **Problem**: Low-level ranged bots (Mage, Hunter, Warlock, Priest) would endlessly run backwards instead of fighting.
- **Fix**: Added snare/root check - bots only kite if target has snare/root. Added distance check - only stop movement if bot is within 30 yards.
- **Files Modified**: `MageCombat.cpp`, `HunterCombat.cpp`, `WarlockCombat.cpp`, `PriestCombat.cpp`

### 2025-01-16 - Bot Combat Bug Fix (Mobs Not Aggroing + Starting Gear)
- **Problem**: Bots couldn't be targeted by mobs (cinematic pending) and spawned naked.
- **Fix**: Added `!pCurrChar->IsBot()` check to cinematic trigger in CharacterHandler.cpp.
- **Files Modified**: `CharacterHandler.cpp`, `RandomBotAI.cpp`

### 2025-01-16 - Caster Bot Fix (Not Moving or Casting)
- **Problem**: Caster bots stood at spawn targeting mobs but never moved, turned, or cast spells.
- **Fix**: Added `MoveChase(pTarget, 28.0f)` to caster Engage() methods.
- **Files Modified**: `MageCombat.cpp`, `WarlockCombat.cpp`, `PriestCombat.cpp`

### 2025-01-13 - Race-Specific Name Generation
- **Feature**: Bot names now match their race/gender using Warcraft-style syllable patterns
- **Files Modified**: `RandomBotGenerator.h`, `RandomBotGenerator.cpp`

### 2025-01-13 - Fix Generated Player Names Not Title Case
- **Fix**: Added `name[0] = std::toupper(name[0]);` before returning the generated name
- **Files Modified**: `src/game/ObjectMgr.cpp`

### 2025-01-12 - Purge GUID Counter Fix
- **Problem**: After purging bots, newly generated bots would get unnecessarily high GUIDs
- **Fix**: Added `ObjectMgr::ReloadCharacterGuids()` method called after purge.
- **Files Modified**: `ObjectMgr.h`, `ObjectMgr.cpp`, `RandomBotGenerator.cpp`

### 2025-01-12 - Bot Registration Fix (Whispers Now Work)
- **Problem**: Bots couldn't be whispered, and ObjectAccessor lookups by name failed
- **Fix**: Modified `ObjectAccessor::AddObject()` and `RemoveObject()` to normalize names
- **Files Modified**: `src/game/ObjectAccessor.cpp`

### 2025-01-11 - Combat System Refactor (Phase 4.5)
- Refactored combat engagement to be class-appropriate
- Created modular combat handler architecture (BotCombatMgr + IClassCombat)
- 21 new files: interface, manager, 9 class handlers

### 2025-01-11 - Horde Bot Fix
- Fixed Orc, Tauren, Troll bots standing idle - increased mob search range from 50 to 150 yards

### 2025-01-11 - Critical Bug Fixes
- Fixed GUID conflict: bots now use `sObjectMgr.GeneratePlayerLowGuid()`
- Fixed account ID corruption: `SaveToDB()` preserves real account ID for bots
- Added `RandomBot.Purge` config option for clean bot removal
- Moved vendor cache build to startup with progress bar

### 2025-01-10 - Phase 4 COMPLETE (Vendoring)
- Created VendoringStrategy with state machine
- Vendor cache, faction-aware vendor finding
- Bot walks to vendor, sells all items, repairs gear

### 2025-01-10 - Phase 3 COMPLETE (Death Handling)
- Ghost walking, resurrection, death loop detection

### 2025-01-10 - Phase 2 COMPLETE (Grinding + Resting)
- Neutral mob targeting, looting, resting with BotCheats

### 2025-01-09 - Phase 1 COMPLETE (Combat AI)
- All 9 class rotations implemented

---

*Archived: 2026-01-31*
