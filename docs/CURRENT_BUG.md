# Current Bug Tracker

## Status: 2 ACTIVE BUGS

---

## Active Bugs

### 1. Movement Sync Bug (Travel System)

**Severity**: Medium
**Component**: TravelingStrategy

**Symptoms**:
- Bots move at super speed during long-distance travel
- Bots disappear from player vision while traveling (still targetable)
- Movement not properly broadcast to nearby clients

**Observed When**: Bot travels long distance to a grind spot (e.g., Ironforge → Coldridge Valley)

**Suspected Cause**: Long-distance `MovePoint()` with `MOVE_PATHFINDING` flag may not broadcast movement packets correctly to observers, or speed calculation is incorrect for long paths.

**Potential Fixes**:
- Break long journeys into smaller waypoint segments (200-300 yards each)
- Investigate movement packet broadcasting for bot players
- Check if speed multiplier is being applied incorrectly

---

### 2. Pre-Travel Vendor Check Not Wired Up

**Severity**: Medium (causes bot to become stuck)
**Component**: TravelingStrategy / VendoringStrategy coordination

**Symptoms**:
- Bot with >60% full bags or <50% durability stands idle instead of vendoring before travel
- TravelingStrategy yields expecting VendoringStrategy to handle it
- VendoringStrategy only triggers at 100% full bags / 0% durability
- Bot just stands there doing nothing - completely stuck
- **Additional observation**: After killing the bot and it resurrecting (resurrection worked flawlessly), the bot still just stood there again - confirming the issue persists across death/resurrection

**Root Cause**: Threshold mismatch between strategies:
- TravelingStrategy yields at: bags >60% OR durability <50%
- VendoringStrategy triggers at: bags 100% full OR gear completely broken

**Current State**: `VendoringStrategy::ForceStart()` method added but not wired up to TravelingStrategy.

**Fix Required**: TravelingStrategy needs access to VendoringStrategy to call `ForceStart()` when pre-travel vendor conditions are met.

---

## Recently Fixed (2026-01-24)

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

*Last Updated: 2026-01-24 (Phase 5 Travel System bugs added)*
