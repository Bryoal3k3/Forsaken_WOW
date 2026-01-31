# Current Bug Tracker

## Status: 2 Low-Priority Bugs Remaining (Bug #8, #12)

---

## Bug #16: Server Crash (Use-After-Free in BotMovementManager) - FIXED

**Status**: ✅ FIXED (2026-01-31)

**Symptom**: Random server crashes with ASAN heap-buffer-overflow errors:
```
AddressSanitizer: heap-buffer-overflow in BotMovementManager::IsMoving()
AddressSanitizer: heap-buffer-overflow in BotMovementManager::Chase()
```

**Root Cause**:
The bot AI persists across logout/login cycles (owned by `PlayerBotEntry`), but the Player object is destroyed on logout and recreated on login. The `BotMovementManager::m_bot` pointer was set once during initialization and never updated when the Player changed.

**Lifecycle Bug**:
1. Bot logs in first time → `BotMovementManager` created with `m_bot = Player A`
2. Bot logs out → Player A **destroyed**, but AI survives (owned by PlayerBotEntry)
3. Bot logs back in → NEW Player B created, `SetPlayer(B)` updates `me`
4. But `m_initialized = true`, so BotMovementManager is NOT recreated
5. `m_bot` still points to **freed** Player A → **CRASH**

**Fix (3 Parts)**:
1. **Added `SetBot()` method** to BotMovementManager - updates `m_bot` and `m_botGuid`
2. **Updated `OnPlayerLogin()`** in RandomBotAI - calls `SetBot(me)` to sync pointers
3. **Added `IsValid()` guards** to all movement entry points as safety net for threading races

**Files Modified**:
- `BotMovementManager.h` - Added `SetBot()`, `GetBot()` methods
- `BotMovementManager.cpp` - `SetBot()` impl, `IsValid()` guards on MoveTo/MoveNear/Chase/ChaseAtAngle/MoveAway
- `RandomBotAI.cpp` - `OnPlayerLogin()` now calls `m_movementMgr->SetBot(me)`

**Tested**: Server stable for 3+ hours with ASAN enabled, no crashes.

---

## Bug #15: Bots Stuck Forever on Unreachable Targets - FIXED

**Status**: ✅ FIXED (2026-01-30)

**Symptom**: Bots would get stuck forever trying to reach targets on mountains or steep terrain:
- Bot selected target with `endPoly=0` (no navmesh at target location)
- Motion type showed CHASE but bot wasn't moving
- Bot would slowly "shimmy" away from target (micro-recovery)
- Distance to target kept increasing (159 yards → 186 yards → ...)
- Infinite loop with `endPoly=0` error spam in console

**Root Cause**:
1. GrindingStrategy set victim via `Attack()` → `GetVictim()` returns target
2. RandomBotAI checks `if (inCombat || me->GetVictim())` → goes to `UpdateInCombatAI()`
3. But GrindingStrategy's timeout check was in `UpdateOutOfCombatAI()` - **never ran!**
4. Combat handlers kept trying MoveChase, failing, triggering micro-recovery
5. No give-up mechanism

**Fix - Complete GrindingStrategy Rewrite**:
1. New state machine: IDLE → APPROACHING → IN_COMBAT
2. Track own target via GUID (not just GetVictim)
3. 30 second approach timeout
4. Scan ALL mobs and pick random (not just nearest)
5. Validate path BEFORE committing to target
6. Added timeout check in `UpdateInCombatAI()` for stuck detection

**Files Modified**:
- `GrindingStrategy.h/cpp` - Complete rewrite with state machine
- `RandomBotAI.cpp` - Added timeout check in UpdateInCombatAI()

**Also Fixed**: GhostWalkingStrategy - ghosts now use direct MovePoint (no pathfinding needed, they walk through walls)

---

## Bug #11: Bots Not Looting or Buffing - FIXED

**Status**: ✅ FIXED (2026-01-26)

**Symptom**: After killing a mob, bots immediately target the next mob without:
- Looting the corpse
- Casting self-buffs (Ice Armor, Demon Skin, Seal of Righteousness, etc.)

**Root Causes**:
1. **Buffing**: `UpdateOutOfCombat()` was called AFTER grinding, so never reached when targets were plentiful
2. **Looting**: `GetVictim()` returned dead mob, keeping bot in combat branch and skipping looting
3. **Flow**: Bot never properly exited combat state after kill

**Fixes Applied**:
1. Moved `m_combatMgr->UpdateOutOfCombat(me)` BEFORE grinding in `UpdateOutOfCombatAI()`
2. Added `AttackStop()` call when victim dies and no new attacker found
3. This allows proper exit from combat branch → looting runs → buffs checked → then find target

**Files Modified**:
- `RandomBotAI.cpp` - Reordered buff call, added AttackStop() for dead victims

**Tested 2026-01-26**: Bots now loot corpses (gold + items) and maintain self-buffs.

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

## Bug #13: Ranged Bots Freezing/Stuck - FIXED

**Status**: ✅ FIXED (2026-01-29)

**Symptom**: Ranged bots (Mage, Warlock, Priest, Hunter) would frequently freeze in place after selecting a target. They had a victim set, Motion type was CHASE, but they weren't moving or casting.

**Root Cause**: `MoveChase(target, 28.0f)` calculated a position 28 yards FROM the target. This often resulted in `PATHFIND_INCOMPLETE` paths. The ChaseMovementGenerator would then stop moving if the bot had Line of Sight to the target (assuming "if you can see it, you don't need to move"). But the bot was still 47+ yards away, out of casting range.

**Fix**: Remove all offset values from MoveChase calls for ranged classes. Chase directly to the target and let `HandleRangedMovement()` stop the bot at casting range (30 yards).

**Files Modified**:
- `Combat/CombatHelpers.h` - Removed 28.0f offset from EngageCaster(), HandleRangedMovement(), HandleCasterFallback()
- `Combat/Classes/HunterCombat.cpp` - Removed 25.0f offset from Engage()

**Also Fixed (Same Session)**:
- Added `HandleCasterFallback()` - wand then melee when all caster spells fail
- Fixed Hunter Auto Shot check - removed IsMoving() blocker
- Fixed Priest heal-lock - removed early return after heal attempt
- Added `.bot status` debug command for diagnosing stuck bots

---

## Bug #12: Hunter Auto Shot Cooldown Spam

**Status**: ACTIVE (Low Priority)

**Symptom**: Console spam when hunters are in combat:
```
ERROR: Player::AddCooldown> Spell(75) try to add and already existing cooldown?
```

**Key Details**:
- Spell 75 = Auto Shot
- Error fires repeatedly during hunter combat
- Bot is trying to cast Auto Shot when it's already active/on cooldown
- Previous "fix" (checking `CURRENT_AUTOREPEAT_SPELL`) did not resolve the issue

**Impact**:
- Console log spam (cosmetic)
- Possible minor performance impact from repeated failed casts

**Related Code**:
- `HunterCombat.cpp` - Hunter combat handler
- `Player::AddCooldown()` - Cooldown management
- `CURRENT_AUTOREPEAT_SPELL` - Auto-repeat spell slot

**Investigation Needed**:
- Check how Auto Shot is being triggered in `HunterCombat::UpdateCombat()`
- Verify the `CURRENT_AUTOREPEAT_SPELL` check is working correctly
- Consider checking `HasSpellCooldown()` before attempting cast

---

---

## Bug #2: Bots Walking Onto Steep Slopes - FIXED

**Status**: ✅ FIXED (2026-01-26)

**Symptom**: Bots getting `startPoly=0` errors while grinding - they walked onto steep slopes (no navmesh) trying to reach mobs.

**Root Cause**: `ChaseMovementGenerator` and `FollowMovementGenerator` did not exclude steep slopes from pathfinding. When a bot chased a mob on the other side of a hill, the path went OVER the hill instead of around it, leading the bot onto terrain with no navmesh.

**Fix**: Added `ExcludeSteepSlopes()` for bots in `TargetedMovementGenerator.cpp`:
```cpp
// Bots should not path through steep slopes (players can't walk on them)
if (owner.IsPlayer() && ((Player const*)&owner)->IsBot())
    path.ExcludeSteepSlopes();
```

**Files Modified**:
- `TargetedMovementGenerator.cpp` - Added steep slope exclusion for bots in Chase and Follow generators
- `PathFinder.cpp` - Enhanced logging (Map, Zone, Area, dist, moving, inCombat) for remaining edge cases

**Result**: Bots now path around steep hills like real players would. Mobs still climb hills normally (unchanged).

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

### Bug #10: Casters Not Moving Into Range - FIXED

**Status**: ✅ FIXED (2026-01-26)

**Symptom**: Caster bots (Mage, Priest, Warlock) would target mobs but stand at 35+ yards without casting. They were stuck out of spell range (30 yards for most spells) and never moved closer.

**Root Cause**: `HandleRangedMovement()` in `CombatHelpers.h` only handled two cases:
1. In range but no LoS → move closer
2. In range with LoS → stop moving

It did NOT handle the case where the bot was OUT of cast range and needed to move closer.

**Fix**: Added a third case to `HandleRangedMovement()` to move closer when out of cast range.

*Note: The original fix used `MoveChase(pVictim, 28.0f)` with an offset, but this was later changed to `MoveChase(pVictim)` (no offset) by Bug #13 fix, which discovered that offsets caused pathfinding issues.*

**Files Modified**:
- `CombatHelpers.h` - Added out-of-range movement handling

**Also Fixed This Session**:
- Hunter Auto Shot spam (Spell 75 cooldown error) - Added check for `CURRENT_AUTOREPEAT_SPELL` before casting (did not fully resolve)

---

### Bug #5: BuildPointPath FAILED Spam for Short Paths - FIXED

**Status**: ✅ FIXED (2026-01-26)

**Symptom**: Console spam when bots tried to path to very close targets:
```
ERROR: [BOT] PathFinder::BuildPointPath Jamxi: FAILED result=0x40000000 pointCount=1 polyLength=1 - returning NOPATH
```

**Root Cause**: In `PathFinder.cpp`, the condition `if (pointCount < 2 || dtStatusFailed(dtResult))` treated trivially short paths (bot already at target) as failures, even when `dtResult=DT_SUCCESS`.

**Fix**: Separated the two conditions - only return NOPATH when `dtStatusFailed(dtResult)` is true. When `pointCount < 2` but dtResult succeeded, build a shortcut path and keep the existing PATHFIND_NORMAL type.

**Files Modified**:
- `PathFinder.cpp` - Split condition into two separate checks

---

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
8. **Fixed Bug #2 (steep slope pathfinding)** - Added ExcludeSteepSlopes() for bots in ChaseMovementGenerator
9. **Fixed Bug #5 (BuildPointPath spam)** - Split pointCount < 2 check from dtStatusFailed check
10. **Fixed Bug #10 (casters not moving into range)** - Added out-of-range handling to HandleRangedMovement
11. **Attempted Hunter Auto Shot fix** - Check CURRENT_AUTOREPEAT_SPELL before casting (did not resolve)

### Bugs Fixed This Session
- Bug #1: Bot falling through floor ✅
- Bug #2: Bots walking onto steep slopes ✅
- Bug #3 & #4: Unreachable mobs ✅
- Bug #5: BuildPointPath log spam ✅
- Bug #6: Aggressive stuck protection ✅
- Bug #7: Casters targeting mobs without LoS ✅ (superseded by #9)
- Bug #9: Bots not entering caves/buildings ✅
- Bug #10: Casters not moving into range ✅

### Remaining
- Bug #8: Combat reactivity - bot ignores attackers (Low Priority)
- Bug #12: Hunter Auto Shot cooldown spam (Low Priority)

---

## How to Use This File

When a new bug is discovered:
1. Change status to "INVESTIGATING" or "FIX IN PROGRESS"
2. Document symptoms, investigation steps, root cause
3. Track fix implementation and testing
4. Move to "Recently Fixed" when verified

---

*Last Updated: 2026-01-31 (Added Bug #16 use-after-free fix)*
