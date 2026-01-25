# Current Bug Tracker

## Status: 2 ACTIVE BUGS

---

## Bug #1: Bot Falling Through Floor - NEEDS FIX

**Status**: INVESTIGATING

**Symptom**: Bot "Yenya" fell through the floor and caused infinite pathfinding error spam.

**Error Log**:
```
ERROR: [BOT] PathFinder::BuildPolyPath: Invalid poly - startPoly=0 endPoly=3125 for Yenya from (-343.8,-4237.0,62.9) to (-381.5,-4183.2,55.8)
(repeated infinitely)
```

**Key Details**:
- `startPoly=0` means bot's current position has no navmesh polygon (below ground)
- Bot was visually falling through the floor when observed
- Location: Durotar area (-343.8, -4237.0, 62.9)
- Causes infinite spam because bot keeps trying to pathfind every tick

**Proposed Fix**:
1. Detect falling through floor (Z dropping rapidly or no valid startPoly for X consecutive ticks)
2. Teleport bot to hearthstone location to recover
3. Rate-limit the error log to prevent spam

---

## Bug #2: Invalid StartPoly During Normal Grinding - INVESTIGATING

**Status**: INVESTIGATING

**Symptom**: Bots "Burarka" and "Wua" getting `startPoly=0` errors while grinding normally (NOT falling).

**Error Log**:
```
ERROR: [BOT] PathFinder::BuildPolyPath: Invalid poly - startPoly=0 endPoly=8689 for Burarka from (-772.6,-4249.8,57.7) to (-792.0,-4284.0,56.5)
ERROR: [BOT] PathFinder::BuildPolyPath: Invalid poly - startPoly=0 endPoly=8689 for Wua from (-772.6,-4249.8,57.7) to (-792.0,-4284.0,56.5)
```

**Key Details**:
- `startPoly=0` but `endPoly=8689` means destination is valid, current position is not
- Z values increasing: 57.7 → 58.3 → 58.8 → 59.3 (walking UP a slope, not falling)
- Bots grinding normally, not visually stuck
- Location: Durotar area (-772, -4249)
- Possibly on edge of navmesh coverage or slope without navmesh

**Investigation Needed**:
- Check if this area has proper navmesh coverage
- Determine if bots are on terrain edge/slope
- May need to add tolerance or recovery logic

---

---

## Future Enhancements (Not Bugs)

### Road/Path Following for Travel
- **Status**: Deferred
- **Issue**: Bots travel in straight lines, may path through high-level mob areas
- **Investigated**: cMangos playerbots uses teleportation, no road waypoint data exists
- **Solution**: Create `travel_routes` table with hand-placed road waypoints (future work)

### Race Distribution Configuration
- **Status**: Deferred
- **Issue**: Class-first selection leads to uneven race distribution (fewer gnomes/trolls)
- **Solution**: Add config options for race/class probability weights (future enhancement)

---

## Recently Fixed (2026-01-25)

### PathFinder Long Path Bug - FIXED

**Issue**: Long paths (Kharanos → Coldridge Valley, ~1000 yards) failed with `PATHFIND_NOPATH` (PathType 8) even though the path existed. Multiple bots trying to validate the same long path would all fail.

**Root Cause**: Bug in vMangos `PathFinder.cpp:findSmoothPath()`. When the smooth path filled the 256-point buffer, it returned `DT_FAILURE` instead of `DT_SUCCESS | DT_BUFFER_TOO_SMALL`. The original comment said "this is most likely a loop" - assuming buffer full = infinite loop. But for long paths, 256 waypoints is legitimately not enough.

**Debug Evidence**:
```
BuildPointPath: FAILED result=0x80000000 pointCount=256 polyLength=79
```
- `findPath()` SUCCEEDED (79 polygons found)
- `findSmoothPath()` filled buffer with 256 valid waypoints
- Then returned `DT_FAILURE` (0x80000000) instead of success with truncation flag

**Fix** (2 parts):

1. **findSmoothPath() return value** - Return proper truncation status:
   ```cpp
   // OLD: return nsmoothPath < MAX_POINT_PATH_LENGTH ? DT_SUCCESS : DT_FAILURE;
   // NEW:
   return nsmoothPath < MAX_POINT_PATH_LENGTH ? DT_SUCCESS : (DT_SUCCESS | DT_BUFFER_TOO_SMALL);
   ```

2. **BuildPointPath() handling** - Mark truncated paths as INCOMPLETE:
   ```cpp
   if (dtResult & DT_BUFFER_TOO_SMALL)
       m_type = PATHFIND_INCOMPLETE;
   ```

**Why Safe**:
- `PATHFIND_INCOMPLETE` already used throughout codebase for partial paths
- `TargetedMovementGenerator` already handles incomplete paths
- Semantically correct: truncated path IS incomplete, not "no path"
- Better than before: units get 256 valid waypoints instead of wall-ignoring shortcut

**Files Modified**:
- `src/game/Maps/PathFinder.cpp` - Fixed findSmoothPath return + BuildPointPath handling

**Debug Logging**: Bot-only PathFinder logging left in place (filtered by `IsPlayerBot()`) for future debugging.

**Tested**: Bots now successfully validate and travel long paths (Kharanos → Coldridge Valley).

---

### Movement Sync Bug (Travel System) - FIXED

**Issue**: Bots moved at super speed, disappeared during long-distance travel (IF → Coldridge).

**Root Cause**: Single `MovePoint()` for entire journey. When navmesh couldn't find a valid path for long distances, it fell back to direct 2-point path ignoring terrain. Also missing `MOVE_EXCLUDE_STEEP_SLOPES` flag.

**Fix**: Complete TravelingStrategy overhaul:
1. Added `MOVE_EXCLUDE_STEEP_SLOPES` flag for terrain handling
2. Added path validation before travel (PathFinder check - aborts if unreachable)
3. Added waypoint segmentation (~200 yard segments)
4. Added `MovementInform()` handler in RandomBotAI for waypoint chaining
5. Immediate waypoint transitions (no 5-second pause between segments)

**Files Modified**:
- `TravelingStrategy.h` - PathFinder include, waypoint members, new methods
- `TravelingStrategy.cpp` - Path validation, waypoint generation, terrain flags
- `RandomBotAI.h` - MovementInform override
- `RandomBotAI.cpp` - MovementInform implementation

**Tested**: Bot ran smoothly from Kharanos to Coldridge Valley starter zone without stopping or floating.

---

### Combat Facing Bug - FIXED

**Issue**: Bot gets stuck when target is behind it. Hunter observed unable to engage wolf behind it.

**Root Cause**: Neither `Engage()` nor `UpdateCombat()` ensured bot was facing target. `MoveChase()` handles movement but not facing when already in range.

**Fix**: Added facing logic in two places:
1. `CombatHelpers.h` - Added `SetFacingToObject(pTarget)` to `EngageMelee()` and `EngageCaster()` before `Attack()` call
2. `BotCombatMgr.cpp` - Added facing check in `UpdateCombat()` using BattleBotAI pattern: check `HasInArc()`, then `SetInFront()` + `SendMovementPacket()`

**Files Modified**:
- `Combat/CombatHelpers.h` - Added facing on engagement
- `Combat/BotCombatMgr.cpp` - Added mid-combat facing check

**Tested**: Bot now "snaps" to face target before attacking. Hunter Auto Shot works correctly when target is behind.

---

## Recently Fixed (2026-01-24)

### Pre-Travel Vendor Check - FIXED

**Issue**: Bot with >60% full bags or <50% durability stood idle instead of vendoring before travel.

**Root Cause**: Threshold mismatch - TravelingStrategy yielded at 60%/50%, but VendoringStrategy only triggered at 100%/0%.

**Fix**: Wired up `VendoringStrategy::ForceStart()` from TravelingStrategy. Added `SetVendoringStrategy()` setter, called in RandomBotAI constructor. Now TravelingStrategy calls `ForceStart()` when yielding for pre-travel vendor check.

**Tested**: Bot with full bags correctly vendors before traveling to new grind spot.

---

## Recently Fixed (2026-01-24 - Earlier)

### Tree/Doodad Collision - FIXED

**Issue**: Bots walked straight through large collidable trees while players and creatures collided properly.

**Root Cause**: Corrupted/modified client data from Elysium client. The Elysium client's M2 models had missing or zeroed collision data (`nBoundingTriangles = 0`).

**Solution**: Re-extracted vmaps and mmaps from a **clean vanilla 1.12 WoW client** instead of the Elysium client.

**Investigation Summary** (2026-01-23):
- Verified bot code uses navmesh correctly (`MOVE_PATHFINDING` flag)
- `.mmap path` command confirmed paths went through trees
- Checked vmap extractor code - found it skips M2s with `nBoundingTriangles = 0`
- Initially concluded trees had no collision data (incorrect - Elysium client was the problem)
- Tested `ignoreM2Model = false` - didn't help (LoS doesn't affect pathfinding anyway)
- **Final fix**: Fresh extraction from clean 1.12 client produced proper collision data

**Lesson Learned**: Always extract from clean, unmodified client files. Private server clients may have corrupted or modified data.

---

## Recently Fixed (2025-01-17)

### Hunter Melee Fallback
- **Issue**: Level 1 hunters stood idle when mobs reached melee range
- **Fix**: Added `Attack(pVictim, true)` to enable melee auto-attack in deadzone
- **Commit**: `d5ed30613`

### Ranged Kiting Loop
- **Issue**: Ranged bots ran backwards forever instead of fighting
- **Fix**: Added distance check - only stop movement when in casting range (30 yards)
- **Commit**: `99b8850ad`

---

## How to Use This File

When a new bug is discovered:
1. Change status to "INVESTIGATING" or "FIX IN PROGRESS"
2. Document symptoms, investigation steps, root cause
3. Track fix implementation and testing
4. Move to "Recently Fixed" when verified

---

*Last Updated: 2026-01-25 (2 new bugs: falling through floor + invalid startPoly during grinding)*
