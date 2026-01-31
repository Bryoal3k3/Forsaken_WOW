# RandomBot Bug History (Archived)

This file contains documentation for bugs that have been fixed.
For active bugs, see `docs/CURRENT_BUG.md`.

---

## Bug #18: Training Learning Wrong Spells (Bots Teaching Mobs) - FIXED

**Status**: FIXED (2026-01-31)

**Symptom**: After training at class trainers, bots didn't use newly learned spells. Warlocks would spam-cast on mobs with a yellow "spell learned" visual effect appearing on the TARGET mob instead of dealing damage.

**Root Cause (Two Issues)**:

1. **Spell cache not refreshing**: Combat handlers use cached spell pointers (`m_spells.warlock.pImmolate`). After training, new spells exist in the player's spellbook but the cache wasn't updated.

2. **Learning "Teach" spells instead of actual spells**: The `npc_trainer_template` table contains teaching spell IDs, not actual combat spells:
   - Spell 1374 = "Teach Immolate" (has `SPELL_EFFECT_LEARN_SPELL`, triggers spell 348)
   - Spell 348 = actual Immolate

   Bots were learning spell 1374 and trying to CAST it on enemies - literally trying to teach wolves how to cast Immolate!

**Fix**:

1. **Refresh cache after training** - Added `SetAI()` method to TrainingStrategy. After learning spells, calls `ResetSpellData()` + `PopulateSpellData()`.

2. **Resolve teach spells** - In `GetLearnableSpells()`, check if spell has `SPELL_EFFECT_LEARN_SPELL` effect. If so, use the `EffectTriggerSpell` (the actual spell) instead.

**Files Modified**:
- `TrainingStrategy.h` - Added `CombatBotBaseAI* m_pAI` and `SetAI()` method
- `TrainingStrategy.cpp` - Added cache refresh after learning, teach spell resolution
- `RandomBotAI.cpp` - Added `m_trainingStrategy->SetAI(this)` call

**Result**: Bots now learn actual combat spells and use them immediately after training.

---

## Bug #17: Vendoring Not Selling Items - FIXED

**Status**: FIXED (2026-01-31)

**Symptom**: Bots would walk to a vendor when bags were full, arrive at the vendor, then do nothing. No items sold, no repairs made. The `.bot status` command always showed "GRINDING" even while walking to vendor.

**Root Cause (Two Issues)**:

1. **Status display broken**: `GetStatusInfo()` had no check for vendoring state - it always fell through to show "GRINDING" regardless of what the bot was actually doing.

2. **Vendor creature lookup failing**: `GetVendorCreature()` used a GUID-based lookup with a cached spawn GUID from server startup. The lookup `map->GetCreature(ObjectGuid(HIGHGUID_UNIT, entry, guid))` was silently failing. When `DoVendorBusiness()` returned false, the state machine continued to DONE anyway without selling anything.

**Fix**:
1. Added `IsActive()` method to VendoringStrategy
2. Added vendoring check to `GetStatusInfo()` - now shows "VendoringStrategy" when active
3. Replaced GUID lookup with `FindNearbyVendorCreature()` - searches for ANY friendly vendor within 30 yards using cell visitor pattern
4. Added MINIMAL-level logging so vendoring flow is visible in console

**Files Modified**:
- `RandomBotAI.cpp` - Added vendoring check to `GetStatusInfo()`
- `VendoringStrategy.h` - Added `IsActive()` method
- `VendoringStrategy.cpp` - Use `FindNearbyVendorCreature()` in AT_VENDOR state, added visible logging

**Result**: Bots now successfully sell items to vendors.

---

## Bug #16: Server Crash (Use-After-Free in BotMovementManager) - FIXED

**Status**: FIXED (2026-01-31)

**Symptom**: Random server crashes with ASAN heap-buffer-overflow errors:
```
AddressSanitizer: heap-buffer-overflow in BotMovementManager::IsMoving()
AddressSanitizer: heap-buffer-overflow in BotMovementManager::Chase()
```

**Root Cause**:
The bot AI persists across logout/login cycles (owned by `PlayerBotEntry`), but the Player object is destroyed on logout and recreated on login. The `BotMovementManager::m_bot` pointer was set once during initialization and never updated when the Player changed.

**Lifecycle Bug**:
1. Bot logs in first time -> `BotMovementManager` created with `m_bot = Player A`
2. Bot logs out -> Player A **destroyed**, but AI survives (owned by PlayerBotEntry)
3. Bot logs back in -> NEW Player B created, `SetPlayer(B)` updates `me`
4. But `m_initialized = true`, so BotMovementManager is NOT recreated
5. `m_bot` still points to **freed** Player A -> **CRASH**

**Fix (3 Parts)**:
1. **Added `SetBot()` method** to BotMovementManager - updates `m_bot` and `m_botGuid`
2. **Updated `OnPlayerLogin()`** in RandomBotAI - calls `SetBot(me)` to sync pointers
3. **Added `IsValid()` guards** to all movement entry points as safety net

**Files Modified**:
- `BotMovementManager.h` - Added `SetBot()`, `GetBot()` methods
- `BotMovementManager.cpp` - `SetBot()` impl, `IsValid()` guards
- `RandomBotAI.cpp` - `OnPlayerLogin()` now calls `m_movementMgr->SetBot(me)`

---

## Bug #15: Bots Stuck Forever on Unreachable Targets - FIXED

**Status**: FIXED (2026-01-30)

**Symptom**: Bots would get stuck forever trying to reach targets on mountains or steep terrain.

**Root Cause**:
1. GrindingStrategy set victim via `Attack()` -> `GetVictim()` returns target
2. RandomBotAI checks `if (inCombat || me->GetVictim())` -> goes to `UpdateInCombatAI()`
3. But GrindingStrategy's timeout check was in `UpdateOutOfCombatAI()` - **never ran!**
4. Combat handlers kept trying MoveChase, failing, triggering micro-recovery
5. No give-up mechanism

**Fix - Complete GrindingStrategy Rewrite**:
1. New state machine: IDLE -> APPROACHING -> IN_COMBAT
2. Track own target via GUID (not just GetVictim)
3. 30 second approach timeout
4. Scan ALL mobs and pick random (not just nearest)
5. Validate path BEFORE committing to target

**Files Modified**:
- `GrindingStrategy.h/cpp` - Complete rewrite with state machine
- `RandomBotAI.cpp` - Added timeout check in UpdateInCombatAI()

---

## Bug #13: Ranged Bots Freezing/Stuck - FIXED

**Status**: FIXED (2026-01-29)

**Symptom**: Ranged bots (Mage, Warlock, Priest, Hunter) would frequently freeze in place after selecting a target.

**Root Cause**: `MoveChase(target, 28.0f)` calculated a position 28 yards FROM the target. This often resulted in `PATHFIND_INCOMPLETE` paths. The ChaseMovementGenerator would then stop moving if the bot had Line of Sight to the target.

**Fix**: Remove all offset values from MoveChase calls for ranged classes.

**Files Modified**:
- `Combat/CombatHelpers.h` - Removed 28.0f offset
- `Combat/Classes/HunterCombat.cpp` - Removed 25.0f offset

---

## Bug #11: Bots Not Looting or Buffing - FIXED

**Status**: FIXED (2026-01-26)

**Symptom**: After killing a mob, bots immediately target the next mob without looting or buffing.

**Root Causes**:
1. **Buffing**: `UpdateOutOfCombat()` was called AFTER grinding
2. **Looting**: `GetVictim()` returned dead mob, keeping bot in combat branch

**Fixes**:
1. Moved `m_combatMgr->UpdateOutOfCombat(me)` BEFORE grinding
2. Added `AttackStop()` call when victim dies

**Files Modified**:
- `RandomBotAI.cpp`

---

## Bug #9: Bots Not Entering Caves/Buildings to Fight Mobs - FIXED

**Status**: FIXED (2026-01-26)

**Symptom**: Bots would target mobs inside caves/buildings but stand outside.

**Root Cause**: Movement interrupted by edge cases in `ChaseMovementGenerator`.

**Fix**:
1. Removed LoS check from target selection
2. Added movement persistence in combat handlers

**Files Modified**:
- `GrindingStrategy.cpp`
- `CombatHelpers.h`
- All 5 melee class combat files

---

## Bug #8: Combat Reactivity - Bot Ignores Attackers While Moving - FIXED

**Status**: FIXED (2026-01-30)

**Symptom**: Bot continued walking toward original target when attacked by different mob.

**Fix**: Added combat reactivity in `UpdateInCombatAI()` - iterates through attackers and switches target if needed.

**Files Modified**:
- `RandomBotAI.cpp`
- `GrindingStrategy.h/cpp` - Added `SetTarget()` method

---

## Bug #6: Stuck Protection Too Aggressive - FIXED

**Status**: FIXED (2026-01-26)

**Symptom**: Recovery teleport fired when bots were resting or fighting near walls.

**Fix**: Changed from PathFinder-based to Map::GetHeight()-based detection.

**Files Modified**:
- `RandomBotAI.cpp`

---

## Bug #5: BuildPointPath FAILED Spam for Short Paths - FIXED

**Status**: FIXED (2026-01-26)

**Symptom**: Console spam when bots tried to path to very close targets.

**Fix**: Separated pointCount < 2 check from dtStatusFailed check.

**Files Modified**:
- `PathFinder.cpp`

---

## Bug #3 & #4: Bots Walking Into Terrain / Targeting Unreachable Mobs - FIXED

**Status**: FIXED (2026-01-26)

**Symptom**: Bots walked up steep slopes, got stuck in rocks.

**Fix**: Added comprehensive reachability check that rejects `PATHFIND_NOPATH` AND `PATHFIND_NOT_USING_PATH`.

**Files Modified**:
- `GrindingStrategy.cpp`

---

## Bug #2: Bots Walking Onto Steep Slopes - FIXED

**Status**: FIXED (2026-01-26)

**Symptom**: Bots got `startPoly=0` errors while grinding on steep slopes.

**Fix**: Added `ExcludeSteepSlopes()` for bots in `TargetedMovementGenerator.cpp`.

**Files Modified**:
- `TargetedMovementGenerator.cpp`
- `PathFinder.cpp`

---

## Bug #1: Bot Falling Through Floor - FIXED

**Status**: FIXED (2026-01-26)

**Symptom**: Bots fell through floor causing infinite `startPoly=0` error spam.

**Fix (4 parts)**:
1. Z validation in `GenerateWaypoints()`
2. Recovery teleport after 15 ticks
3. Z correction on grind spot cache load
4. Rate-limited error logging

**Files Modified**:
- `TravelingStrategy.cpp`
- `RandomBotAI.h/cpp`
- `PathFinder.cpp`

---

## Bug #10: Casters Not Moving Into Range - FIXED

**Status**: FIXED (2026-01-26)

**Symptom**: Caster bots would target mobs but stand at 35+ yards without casting.

**Fix**: Added out-of-range handling to `HandleRangedMovement()`.

**Files Modified**:
- `CombatHelpers.h`

---

## Bug #7: Casters Targeting Mobs Without Line of Sight - SUPERSEDED BY BUG #9

**Original Fix**: Added LoS check in `IsValidGrindTarget()` for ranged classes.

**Problem**: This prevented bots from EVER grinding inside caves/buildings.

**Better Fix (Bug #9)**: Move closer when can't see target, instead of skipping target.

---

*Last Updated: 2026-01-31*
