# Current Bug Tracker

## Status: 1 ACTIVE BUG

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

*Last Updated: 2026-01-24 (Pre-travel vendor bug fixed)*
