# Current Bug Tracker

## Status: 0 ACTIVE BUGS

All known bugs have been fixed!

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

*Last Updated: 2026-01-25 (All bugs fixed - movement sync bug resolved with waypoint segmentation)*
