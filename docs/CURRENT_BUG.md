# Current Bug Tracker

## Status: 2 ACTIVE BUGS (Low Priority)

---

## Bug #8: Combat Reactivity - Bot Ignores Attackers While Moving

**Status**: IDENTIFIED (Low Priority)

**Symptom**: When a bot is moving toward a selected target and gets attacked by a different mob, the bot continues walking toward its original target instead of fighting back against the attacker.

**Key Details**:
- Bot targets Mob A, starts walking toward it
- Mob B attacks bot from behind/side
- Bot enters combat (`IsInCombat() = true`)
- `GetVictim()` is still Mob A
- Bot keeps walking toward Mob A while Mob B beats on them

**Impact**:
- Low-level: Minor issue in starting zones with mostly yellow mobs
- High-level: Serious issue - bot could die walking through mob packs

**Potential Fixes**:
1. In `UpdateInCombatAI()`, check if bot is being attacked by something other than current target
2. If attacker is closer or current target isn't attacking back, switch targets
3. Consider threat/aggro list for smarter target selection

**Related Code**:
- `RandomBotAI.cpp:UpdateInCombatAI()` - Combat update loop
- `Player::GetVictim()` - Current attack target
- `Unit::GetAttackers()` - List of units attacking this unit

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

### Bug #9: Bots Not Entering Caves/Buildings to Fight Mobs - FIXED

**Issue**: Bots would target mobs inside caves/buildings but stand outside instead of pathing through the entrance to kill them. Both melee and ranged classes were affected.

**Root Cause (Two Problems)**:
1. **Target selection**: Bug #7's LoS check prevented ranged classes from targeting mobs inside structures entirely
2. **Movement**: When bots did target a mob inside, `MoveChase` would get interrupted by edge cases in `ChaseMovementGenerator` (INCOMPLETE path + LoS = StopMoving), and nothing restarted the chase

**Fix (Two Parts)**:
1. **Removed LoS check from target selection** - Bots should target mobs in caves/buildings and path through entrances. The LoS check was a "half-ass fix" that prevented targeting instead of enabling proper movement.

2. **Added movement persistence in combat update**:
   - `HandleRangedMovement()`: If at range but NO LoS → `MoveChase()` closer (through the door)
   - `HandleMeleeMovement()`: If not in melee range and not moving → re-issue `MoveChase()`

**Files Modified**:
- `GrindingStrategy.cpp` - Removed LoS check from `IsValidGrindTarget()`
- `CombatHelpers.h` - Added LoS check to `HandleRangedMovement()`, added new `HandleMeleeMovement()` helper
- `WarriorCombat.cpp` - Added `HandleMeleeMovement()` call
- `RogueCombat.cpp` - Added `HandleMeleeMovement()` call
- `PaladinCombat.cpp` - Added `HandleMeleeMovement()` call
- `ShamanCombat.cpp` - Added `HandleMeleeMovement()` call
- `DruidCombat.cpp` - Added `HandleMeleeMovement()` call

**Tested 2026-01-26**: Bots now properly enter caves and buildings to kill mobs inside.

---

### Bug #7: Casters Targeting Mobs Without Line of Sight - SUPERSEDED BY BUG #9

**Original Issue**: Caster/ranged bots would target mobs inside buildings, move into casting range outside, but couldn't cast due to LoS block.

**Original Fix**: Added LoS check in `IsValidGrindTarget()` for ranged classes to skip mobs they couldn't see.

**Problem**: This prevented bots from EVER grinding inside caves/buildings - they'd skip all mobs inside.

**Better Fix (Bug #9)**: Instead of skipping the target, make bots MOVE CLOSER when they can't see. The LoS check was removed from target selection and moved to the movement handler.

---

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
5. Fixed Bug #7 (LoS issue with mobs inside buildings) - initial LoS check for ranged classes
6. Identified Bug #8 (combat reactivity) - bot ignores attackers while moving to target
7. **Fixed Bug #9 (cave/building targeting)** - Supersedes Bug #7 with proper movement-based solution

### Bugs Fixed This Session
- Bug #1: Bot falling through floor ✅
- Bug #3 & #4: Unreachable mobs ✅
- Bug #6: Aggressive stuck protection ✅
- Bug #7: Casters targeting mobs without LoS ✅ (superseded by #9)
- Bug #9: Bots not entering caves/buildings ✅

### Remaining
- Bug #8: Combat reactivity - bot ignores attackers (Low Priority)
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

*Last Updated: 2026-01-26 (Bug #9 fixed - bots now enter caves/buildings to fight)*
