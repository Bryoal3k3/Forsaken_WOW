# Current Bug Tracker

## Status: 3 ACTIVE BUGS (1 New, 2 Low Priority)

---

## Bug #7: Casters Targeting Mobs Without Line of Sight - NEW

**Status**: INVESTIGATING

**Symptom**: Caster bots (observed with Undead in Tirisfal) target mobs inside buildings/barns. They stand outside facing the target, in range, but can't cast because a wall blocks Line of Sight. They stand idle trying to cast repeatedly.

**Screenshot**: Undead bot "Daner" targeting "Wretched Zombie" inside a barn - bot is outside, facing target, in range, but wall blocks LoS.

**Key Details**:
- Bot passes all `IsValidGrindTarget()` checks (distance, level, faction, reachability)
- Bot is in casting range (< 30 yards)
- Bot faces target correctly
- But wall/building obstructs Line of Sight
- Spells fail silently due to LoS check in spell system

**Potential Fixes**:

1. **Add LoS check to `IsValidGrindTarget()`** (Target Selection)
   - Use `IsWithinLOSInMap()` to verify clear sightline
   - Rejects mobs behind walls before engagement
   - Cost: One LoS check per potential target

2. **Add LoS check to combat/engagement** (Combat Layer)
   - If no LoS to current target, clear target and find new one
   - Handles case where mob runs behind cover during combat
   - More reactive but doesn't prevent initial bad selection

3. **Both** (Defense in depth)
   - Check at selection AND during combat
   - Most robust but higher performance cost

**Related Code**:
- `GrindingStrategy.cpp:IsValidGrindTarget()` - Target validation
- `BotCombatMgr.cpp` - Combat updates
- `Unit::IsWithinLOSInMap()` - LoS check function

---

## Bug #5: BuildPointPath FAILED Spam for Short Paths - LOW PRIORITY

**Status**: INVESTIGATING (Low Priority - cosmetic log spam only)

**Symptom**: Console spam when bots try to path to very close targets:
```
ERROR: [BOT] PathFinder::BuildPointPath Jamxi: FAILED result=0x40000000 pointCount=1 polyLength=1 - returning NOPATH
```

**Key Details**:
- `result=0x40000000` = `DT_SUCCESS` (NOT a failure!)
- `pointCount=1, polyLength=1` = trivially short path (bot and target in same/adjacent polygon)
- Code incorrectly treats this as NOPATH

**Root Cause**:
In `PathFinder.cpp` line 525:
```cpp
if (pointCount < 2 || dtStatusFailed(dtResult))
```

When `pointCount=1` (trivially short path), this triggers even though `dtResult=DT_SUCCESS`.

**Impact**: Log spam only - nearby valid mobs are still engaged correctly due to the reachability check handling this case.

**Proposed Fix**: Change condition to only fail when `dtStatusFailed(dtResult)` is true, not when path is just short.

---

## Bug #2: Invalid StartPoly During Normal Grinding - LOW PRIORITY

**Status**: INVESTIGATING (Low Priority - handled by recovery system)

**Symptom**: Bots getting `startPoly=0` errors while grinding normally (NOT falling).

**Error Log**:
```
ERROR: [BOT] PathFinder::BuildPolyPath: Invalid poly - startPoly=0 endPoly=8689 for Burarka from (-772.6,-4249.8,57.7) to (-792.0,-4284.0,56.5)
```

**Key Details**:
- `startPoly=0` but `endPoly=8689` means destination valid, current position is not
- Z values increasing (walking UP a slope, not falling)
- Location: Durotar area (-772, -4249)
- Possibly navmesh edge case at polygon boundaries

**Note**: Recovery teleport handles this case - bot teleports after 15 seconds if truly stuck.

---

## Future Enhancements (Not Bugs)

### Console Timestamps
- User requested timestamps in console output
- Check `mangosd.conf` for logging options or modify Log.cpp

### Road/Path Following for Travel
- **Status**: Deferred
- **Issue**: Bots travel in straight lines, may path through high-level mob areas
- **Solution**: Create `travel_routes` table with hand-placed road waypoints

### Race Distribution Configuration
- **Status**: Deferred
- **Issue**: Class-first selection leads to uneven race distribution
- **Solution**: Add config options for race/class probability weights

---

## Recently Fixed (2026-01-26)

### Bug #6: Stuck Protection Too Aggressive - FIXED

**Issue**: Recovery teleport was firing when bots were resting (drinking) or fighting near walls/obstacles.

**Error Logs**:
```
ERROR: [BOT] PathFinder::BuildPolyPath: Invalid poly - startPoly=851 endPoly=0 for Carvuey
[RandomBotAI] Carvuey stuck at invalid position for 15 ticks, teleporting to hearthstone
```

**Root Cause**: The stuck detection was checking if a path to a point 5 yards ahead was valid. If the bot was facing a wall (while resting or in melee combat), `endPoly=0` caused false positives even though `startPoly` was valid (bot's position was fine).

**Fix**: Changed detection from PathFinder-based to Map::GetHeight()-based:
- Now checks if terrain exists at bot's current XY position
- Only triggers if `GetHeight()` returns `INVALID_HEIGHT` OR bot is 50+ units below terrain
- No longer falsely triggers when bot is simply facing an obstacle

**Files Modified**:
- `RandomBotAI.cpp` - Replaced PathFinder check with Map::GetHeight() check, removed PathFinder.h and cmath includes

**Tested 2026-01-26**: Bots no longer teleport while resting or fighting near walls.

---

### Bug #3 & #4: Bots Walking Into Terrain / Targeting Unreachable Mobs - FIXED

**Issue**: Bots would walk up steep slopes and get stuck inside rocks/terrain trying to reach mobs on unreachable terrain.

**Root Cause Analysis**:
1. `GrindingStrategy::IsValidGrindTarget()` didn't validate path reachability
2. Initial fix checked for `PATHFIND_NOPATH` but didn't catch all cases
3. When `endPoly=0` and terrain is "swimmable", PathFinder returns `PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH` instead of `PATHFIND_NOPATH`

**Fix**: Updated reachability check to reject BOTH `PATHFIND_NOPATH` AND `PATHFIND_NOT_USING_PATH`:
```cpp
PathType type = path.getPathType();
if ((type & PATHFIND_NOPATH) || (type & PATHFIND_NOT_USING_PATH))
    return false;
```

**Why**: `PATHFIND_NOT_USING_PATH` means "go straight, no real navmesh path" - bots should NOT engage mobs that require straight-line movement through terrain.

**Files Modified**:
- `GrindingStrategy.cpp` - Added PathFinder include and comprehensive reachability check

**Tested 2026-01-26**: Bots now skip mobs on steep slopes and unreachable terrain.

---

### Bug #1: Bot Falling Through Floor - FIXED

**Issue**: Bots fell through floor causing infinite `startPoly=0` pathfinding error spam.

**Fix Implemented** (4 parts - defense in depth):

1. **Z validation in `GenerateWaypoints()`** - Try `GetHeight()` with MAX_HEIGHT, then with interpolated Z as reference. Skip waypoint entirely if both fail.

2. **Recovery teleport in `RandomBotAI`** - Track invalid position. After 15 ticks (~15 seconds), teleport bot to hearthstone location.

3. **Z correction on grind spot cache load** - `BuildGrindSpotCache()` now calls `Map::GetHeight()` to correct Z values.

4. **Rate-limited error logging** - `startPoly=0` errors now logged once per 10 seconds per bot.

**Files Modified**:
- `TravelingStrategy.cpp` - Z validation in waypoints, Z correction on cache load
- `RandomBotAI.h` - Added `m_invalidPosCount`, `INVALID_POS_THRESHOLD` (15 ticks)
- `RandomBotAI.cpp` - Recovery teleport detection
- `PathFinder.cpp` - Rate-limited error logging

**Commit**: `af528e7c4` - "fix: Bot falling through floor - Z validation + recovery teleport"

---

## Recently Fixed (2026-01-25)

### PathFinder Long Path Bug - FIXED
(See PROGRESS.md for details)

### Movement Sync Bug (Travel System) - FIXED
(See PROGRESS.md for details)

### Combat Facing Bug - FIXED
(See PROGRESS.md for details)

---

## Session Notes (2026-01-26)

### What Was Done
1. Investigated and fixed Bug #1 (bot falling through floor) - 4-part fix
2. Investigated and fixed Bug #3 & #4 (unreachable mobs) - reachability check with PATHFIND_NOT_USING_PATH
3. Investigated and fixed Bug #6 (aggressive stuck protection) - changed to Map::GetHeight() based detection
4. Identified Bug #5 (BuildPointPath spam) - low priority, cosmetic only
5. Identified Bug #7 (LoS issue with mobs inside buildings) - casters stuck trying to cast through walls

### Bugs Fixed This Session
- Bug #1: Bot falling through floor ✅
- Bug #3 & #4: Unreachable mobs ✅
- Bug #6: Aggressive stuck protection ✅

### Remaining
- Bug #7: LoS issue - casters targeting mobs inside buildings (NEW)
- Bug #2: Invalid startPoly edge cases (Low Priority - handled by recovery)
- Bug #5: BuildPointPath log spam (Low Priority - cosmetic only)

---

## How to Use This File

When a new bug is discovered:
1. Change status to "INVESTIGATING" or "FIX IN PROGRESS"
2. Document symptoms, investigation steps, root cause
3. Track fix implementation and testing
4. Move to "Recently Fixed" when verified

---

*Last Updated: 2026-01-26 (Bug #1, #3/#4, #6 fixed; Bug #7 identified)*
