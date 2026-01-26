# Current Bug Tracker

## Status: 3 ACTIVE BUGS

---

## Bug #3 & #4: Bots Walking Into Terrain / Targeting Unreachable Mobs - FIX IN PROGRESS

**Status**: FIX IN PROGRESS - Partial fix implemented, needs refinement

**Symptom**:
- Bots walk up steep slopes and get stuck inside rocks/terrain
- Bots select mobs on unreachable terrain (steep slopes, caves, ridges)
- After recovery teleport, bots immediately walk into terrain again trying to reach mobs

**Error Logs**:
```
ERROR: [BOT] PathFinder::BuildPolyPath: Invalid poly - startPoly=8408 endPoly=0 for Nekkazim from (-697.7,-4175.2,41.5) to (-694.1,-4171.8,41.5)
ERROR: [BOT] PathFinder::BuildPolyPath: Invalid poly - startPoly=2147493047 endPoly=0 for Jamxi from (-660.0,-4255.9,44.6) to (-664.4,-4258.4,44.6)
```

**Key Details**:
- `endPoly=0` means mob's position has no valid navmesh (steep slope)
- Recovery teleport works, but bot immediately tries to reach another unreachable mob
- Screenshot shows bot (Jamxi) with only feet visible through rock after walking into terrain

### Investigation Completed (2026-01-26)

**Root Cause Analysis:**

1. **`GrindingStrategy::IsValidGrindTarget()` didn't validate path reachability** - PARTIAL FIX APPLIED
   - Added PathFinder check: `if (path.getPathType() & PATHFIND_NOPATH) return false;`
   - File: `GrindingStrategy.cpp` lines 179-185

2. **BUT the fix doesn't catch all cases!** - NEEDS ADDITIONAL FIX

   In `PathFinder.cpp` lines 254-259, when `endPoly=0`:
   ```cpp
   if ((endPoly == INVALID_POLYREF && m_sourceUnit->GetTerrain()->IsSwimmable(endPos.x, endPos.y, endPos.z)))
       m_type = m_sourceUnit->CanSwim() ? PathType(PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH) : PATHFIND_NOPATH;
   ```

   - If terrain near the invalid poly is "swimmable", path type is `PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH`
   - Players can swim, so `CanSwim()` returns true
   - Our check `if (path.getPathType() & PATHFIND_NOPATH)` **doesn't catch this case!**

### Proposed Final Fix

Change the reachability check in `GrindingStrategy.cpp` from:
```cpp
// Current (broken):
if (path.getPathType() & PATHFIND_NOPATH)
    return false;
```

To:
```cpp
// Fixed - also reject PATHFIND_NOT_USING_PATH:
PathType type = path.getPathType();
if ((type & PATHFIND_NOPATH) || (type & PATHFIND_NOT_USING_PATH))
    return false;
```

**Why**: `PATHFIND_NOT_USING_PATH` means "we couldn't find a real navmesh path, just go straight" - bots should NOT engage mobs that require straight-line movement through terrain.

### Files Modified So Far
- `GrindingStrategy.cpp` - Added PathFinder include and reachability check (partial fix)

### Files Needing Modification
- `GrindingStrategy.cpp` - Update check to also reject `PATHFIND_NOT_USING_PATH`

---

## Bug #5: BuildPointPath FAILED Spam for Short Paths - NEW

**Status**: INVESTIGATING

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

**Impact**: May cause nearby valid mobs to be incorrectly rejected by our reachability check.

**Proposed Fix**: Change condition to only fail when `dtStatusFailed(dtResult)` is true, not when path is just short.

---

## Bug #2: Invalid StartPoly During Normal Grinding - INVESTIGATING

**Status**: INVESTIGATING (Low Priority)

**Symptom**: Bots getting `startPoly=0` errors while grinding normally (NOT falling).

**Error Log**:
```
ERROR: [BOT] PathFinder::BuildPolyPath: Invalid poly - startPoly=0 endPoly=8689 for Burarka from (-772.6,-4249.8,57.7) to (-792.0,-4284.0,56.5)
```

**Key Details**:
- `startPoly=0` but `endPoly=8689` means destination valid, current position is not
- Z values increasing (walking UP a slope, not falling)
- Location: Durotar area (-772, -4249)
- Possibly navmesh edge case

**Note**: Recovery teleport handles this case - bot teleports after 15 seconds.

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

### Bug #1: Bot Falling Through Floor - VERIFIED FIXED

**Issue**: Bots fell through floor causing infinite `startPoly=0` pathfinding error spam.

**Fix Implemented** (4 parts - defense in depth):

1. **Z validation in `GenerateWaypoints()`** - Try `GetHeight()` with MAX_HEIGHT, then with interpolated Z as reference. Skip waypoint entirely if both fail.

2. **Recovery teleport in `RandomBotAI`** - Track consecutive `PATHFIND_NOPATH` results. After 15 ticks (~15 seconds), teleport bot to hearthstone location.

3. **Z correction on grind spot cache load** - `BuildGrindSpotCache()` now calls `Map::GetHeight()` to correct Z values.

4. **Rate-limited error logging** - `startPoly=0` errors now logged once per 10 seconds per bot.

**Files Modified**:
- `TravelingStrategy.cpp` - Z validation in waypoints, Z correction on cache load
- `RandomBotAI.h` - Added `m_invalidPosCount`, `INVALID_POS_THRESHOLD` (15 ticks)
- `RandomBotAI.cpp` - Recovery teleport detection + PathFinder include
- `PathFinder.cpp` - Rate-limited error logging

**Tested 2026-01-26**: Recovery teleport confirmed working. Bots at invalid positions teleport to hearthstone after 15 seconds. Multiple bots observed teleporting correctly:
- Gadzua, Jamxi, Mukgash, Muirra, Muirru all teleported successfully

**Commit**: `af528e7c4` - "fix: Bot falling through floor - Z validation + recovery teleport"

---

## Recently Fixed (2026-01-25)

### PathFinder Long Path Bug - FIXED
(See previous entries for details)

### Movement Sync Bug (Travel System) - FIXED
(See previous entries for details)

### Combat Facing Bug - FIXED
(See previous entries for details)

---

## Session Notes (2026-01-26)

### What Was Done
1. Investigated Bug #1 thoroughly - found root cause (missing Z validation)
2. Implemented 4-part fix for Bug #1 (prevention + recovery)
3. Committed and pushed fix: `af528e7c4`
4. Investigated Bug #3 & #4 - found `MoveChase()` doesn't use `MOVE_EXCLUDE_STEEP_SLOPES`
5. Added partial fix: PathFinder reachability check in `IsValidGrindTarget()`
6. Discovered the check isn't catching all cases due to `PATHFIND_NOT_USING_PATH`
7. Identified Bug #5: BuildPointPath spam for short paths

### What Needs To Be Done Next
1. Update `GrindingStrategy.cpp` reachability check to also reject `PATHFIND_NOT_USING_PATH`
2. Optionally fix Bug #5 (BuildPointPath short path handling)
3. Test the complete fix
4. Commit when verified

### Uncommitted Changes
- `GrindingStrategy.cpp` - Partial reachability fix (needs update)

---

## How to Use This File

When a new bug is discovered:
1. Change status to "INVESTIGATING" or "FIX IN PROGRESS"
2. Document symptoms, investigation steps, root cause
3. Track fix implementation and testing
4. Move to "Recently Fixed" when verified

---

*Last Updated: 2026-01-26 (Bug #1 verified fixed, Bug #3/#4 partial fix, Bug #5 identified)*
