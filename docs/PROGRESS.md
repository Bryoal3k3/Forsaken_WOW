# RandomBot AI Development Progress

## Project Status: Phase 6 COMPLETE (Movement Manager)

| Phase | Status | Description |
|-------|--------|-------------|
| Phase 0 | ✅ Complete | Auto-generate bot accounts and characters |
| Phase 1 | ✅ Complete | Combat AI - bots fight when attacked |
| Phase 2 | ✅ Complete | Grinding - find mobs, kill, loot, rest |
| Phase 3 | ✅ Complete | Death handling and respawn |
| Phase 4 | ✅ Complete | Vendoring - sell items, repair gear |
| Phase 4.5 | ✅ Complete | Combat system refactor - class-appropriate engagement |
| Phase 5 | ✅ Complete | Travel system - find and travel to grind spots |
| Phase 5.5 | ✅ Complete | Auto-generated grind spots + local randomization |
| Phase 6 | ✅ Complete | Centralized BotMovementManager |

### Phase 5 Features Implemented
- **Out-leveled area detection**: Bot recognizes when no mobs within +/-2 levels exist (5 consecutive ticks)
- **Zone transition**: Bot queries DB for level/faction-appropriate grind spots, travels there
- **Stuck detection**: 30-second timeout resets travel if bot gets stuck on terrain
- **Combat interrupt**: Bot fights if attacked while traveling, then resumes
- **Anti-thrashing**: 90-second arrival cooldown prevents constant spot-hopping
- **Post-resurrection reset**: Travel state resets after death/resurrection

### Phase 5 Status
All Phase 5 bugs have been fixed:
- ✅ Movement sync bug (waypoint segmentation + terrain flags)
- ✅ Pre-travel vendor check (ForceStart wired up)
- ✅ Combat facing bug (SetFacingToObject added)
- ✅ PathFinder long path bug (truncated paths return INCOMPLETE not NOPATH)

### Current Active Bugs (See docs/CURRENT_BUG.md)
- ✅ All major bugs fixed (2026-01-26)
- ✅ Bug #9: Bots now enter caves/buildings to fight mobs (2026-01-26)
- ✅ Bug #2: Bots no longer walk onto steep slopes (2026-01-26)
- ✅ Bug #5: BuildPointPath log spam fixed (2026-01-26)
- ✅ Bug #11: Bots now properly loot and buff (2026-01-26)
- ✅ Bug #13: Ranged bots no longer freeze/stuck (2026-01-29)
- ✅ Bug #15: Bots stuck on unreachable targets forever (2026-01-30)
- 🟡 Low priority: Combat reactivity - bot ignores attackers while moving (Bug #8)
- 🟡 Low priority: Hunter Auto Shot cooldown spam (Bug #12)

---

## 2026-01-31 - CRITICAL BUG FIX: Server Crash (Use-After-Free)

### Problem
Server was crashing randomly with ASAN heap-buffer-overflow errors in `BotMovementManager` methods. Crashes occurred during normal bot operation (grinding, combat, movement).

### Root Cause Analysis
The bot AI (owned by `PlayerBotEntry`) persists across logout/login cycles, but the Player object is destroyed and recreated. When a bot reconnects:
1. `SetPlayer(newPlayer)` updates `me` in the AI
2. But `BotMovementManager::m_bot` still pointed to the OLD (freed) Player
3. Any access to `m_bot` triggered use-after-free → crash

This was discovered using Address Sanitizer (ASAN) which was added in a previous commit.

### Solution (3 Parts)

| Part | Change |
|------|--------|
| 1. `SetBot()` method | New method to update `m_bot` and `m_botGuid` when player changes |
| 2. `OnPlayerLogin()` | Now calls `SetBot(me)` to sync pointers on reconnect |
| 3. `IsValid()` guards | Added to all movement entry points as threading safety net |

### Files Modified

| File | Changes |
|------|---------|
| `BotMovementManager.h` | Added `SetBot()`, `GetBot()` declarations |
| `BotMovementManager.cpp` | `SetBot()` implementation, `IsValid()` guards on 6 methods |
| `RandomBotAI.cpp` | `OnPlayerLogin()` calls `m_movementMgr->SetBot(me)` |

### IsValid() Implementation
```cpp
bool BotMovementManager::IsValid() const
{
    if (!m_bot || m_botGuid.IsEmpty())
        return false;
    Player* pPlayer = ObjectAccessor::FindPlayer(m_botGuid);
    return pPlayer && pPlayer == m_bot && pPlayer->IsInWorld();
}
```

### Result
- Server stable for 3+ hours with ASAN enabled
- No crashes during normal bot operation
- Fix addresses both logout/login pointer staleness AND threading race conditions

---

## 2026-01-30 - GrindingStrategy Refactor + GhostWalking Fix

### Problem
Bots would get stuck forever on unreachable targets:
1. Bot selected target on terrain with no navmesh (mountain, steep slope)
2. `GetVictim()` was set, so bot went to combat path in RandomBotAI
3. GrindingStrategy's timeout check was in out-of-combat path - never ran!
4. Bot kept trying to MoveChase, failing, triggering micro-recovery (shimmy)
5. Infinite loop - bot drifted further and further from target

### Solution: Complete GrindingStrategy Rewrite

**New State Machine:**
```
IDLE → Scan & pick random target → APPROACHING → IN_COMBAT → IDLE
                                        |
                                 TIMEOUT (30s) → Clear target → IDLE
```

**Key Changes:**
| Old Behavior | New Behavior |
|--------------|--------------|
| Grab nearest mob | Scan ALL mobs, pick random |
| Trust `GetVictim()` blindly | Track own target via GUID + state |
| No approach timeout | 30 second timeout to reach target |
| Path check during scan only | Path MUST be valid before committing |

**New Constants:**
| Constant | Value | Purpose |
|----------|-------|---------|
| `SEARCH_RANGE` | 75 yards | Scan radius for mobs |
| `APPROACH_TIMEOUT_MS` | 30 seconds | Give up if can't reach target |
| `LEVEL_RANGE` | 2 | Attack same level or up to 2 below (no higher) |

### Files Modified

| File | Changes |
|------|---------|
| `GrindingStrategy.h` | New state machine (`GrindState` enum), timeout tracking |
| `GrindingStrategy.cpp` | Complete rewrite - scan all, pick random, timeout check |
| `RandomBotAI.cpp` | Added timeout check in `UpdateInCombatAI()` for stuck detection |
| `GhostWalkingStrategy.cpp` | Fixed ghost corpse walk - use direct MovePoint (ghosts walk through walls) |

### GhostWalkingStrategy Fix
Ghosts were standing at graveyard instead of walking to corpse. Root cause: `BotMovementManager::MoveTo()` has path validation that failed for ghost paths.

**Fix:** Ghosts now use direct `MovePoint()` without pathfinding - they can walk through walls anyway.

### Result
- Bots spread out more naturally (random target selection)
- No more infinite stuck loops on mountains
- 30 second timeout ensures bots always recover
- Ghost corpse walking works again

---

## 2026-01-30 - BotMovementManager Implementation (Phase 6 COMPLETE)

### Overview
Centralized all RandomBot movement into a single `BotMovementManager` class to fix:
- Hill hugging (path smoothing)
- Bot stacking (destination randomization)
- Movement spam/jitter (duplicate detection)
- Slow stuck detection (5 sec vs 30 sec)
- Terrain issues (multi-Z search)
- CC checks (won't move while stunned/rooted)

### Architecture
```
RandomBotAI
├── BotMovementManager (NEW - single point of control)
│   ├── Priority system (IDLE < WANDER < NORMAL < COMBAT < FORCED)
│   ├── Duplicate detection (prevents MoveChase spam)
│   ├── CC validation (won't issue moves while stunned/rooted)
│   ├── Multi-Z height search (caves/buildings)
│   ├── Path smoothing (skip unnecessary waypoints)
│   └── 5-second stuck detection with micro-recovery
│   ↓
└── MotionMaster (engine calls)
```

### Key Features
| Feature | Purpose |
|---------|---------|
| `MoveTo()` | Point movement with Z validation |
| `Chase()` | Combat movement with duplicate detection |
| `MoveNear()` | 8-angle search to prevent stacking |
| `MoveAway()` | Flee mechanism (8-angle search away from threat) |
| `SmoothPath()` | Skip waypoints when LoS exists |
| `SearchBestZ()` | Try 5 heights (±8, ±16, ±24, ±32, ±40 yards) |
| `Update()` | Stuck detection every 1 sec, micro-recovery at 2 checks |

### Files Created
| File | Purpose |
|------|---------|
| `BotMovementManager.h` | Header with enums, structs, class declaration |
| `BotMovementManager.cpp` | Full implementation (~400 LOC) |

### Files Modified
| File | Changes |
|------|---------|
| `RandomBotAI.h` | Added manager member and accessor |
| `RandomBotAI.cpp` | Initialize manager, call Update(), wire to strategies |
| `CMakeLists.txt` | Added new source files |
| `Combat/IClassCombat.h` | Added SetMovementManager() to interface |
| `Combat/BotCombatMgr.h/cpp` | Pass manager to class handlers |
| `Combat/CombatHelpers.h` | All functions accept optional BotMovementManager* |
| `Combat/Classes/*.cpp` | All 9 class handlers use movement manager |
| `Strategies/GrindingStrategy.h/cpp` | Uses movement manager for fallback chase |
| `Strategies/TravelingStrategy.h/cpp` | Uses MoveTo() + path smoothing |
| `Strategies/VendoringStrategy.h/cpp` | Uses MoveTo() for vendor navigation |
| `Strategies/GhostWalkingStrategy.h/cpp` | Uses MoveTo() for corpse walk |
| `Strategies/LootingBehavior.h/cpp` | Uses MoveTo() for loot approach |

### Integration Pattern
Each strategy/handler receives movement manager via `SetMovementManager()` during initialization:
```cpp
// In RandomBotAI one-time init:
m_movementMgr = std::make_unique<BotMovementManager>(me);
if (m_combatMgr)
    m_combatMgr->SetMovementManager(m_movementMgr.get());
if (m_strategy)
    m_strategy->SetMovementManager(m_movementMgr.get());
// ... etc for all strategies
```

### Build Errors Fixed During Implementation
| Error | Fix |
|-------|-----|
| Vector3 redefinition | Use `G3D::Vector3` instead of custom struct |
| GetPhaseMask() doesn't exist | Remove phase mask from LoS calls (vanilla has no phasing) |
| frand redefinition | Remove static frand (already in Util.h) |

### Status
✅ Compiles and builds successfully
✅ All strategies migrated to use movement manager
✅ All 9 class combat handlers migrated

---

## 2026-01-29 - CRITICAL BUG FIX: Ranged Bots Freezing (Bug #13)

### Problem
Ranged bots (Mage, Warlock, Priest, Hunter) would frequently freeze in place after selecting a target. They had a victim set, Motion type was CHASE, but they weren't moving or casting. Attacking their target would "wake them up".

### Root Cause (Deep in ChaseMovementGenerator)
`MoveChase(target, 28.0f)` was used to chase to 28 yards offset. The ChaseMovementGenerator:
1. Calculates position 28 yards FROM target
2. PathFinder tries to path to that calculated offset position
3. Path often comes back as `PATHFIND_INCOMPLETE` (common for longer/awkward paths)
4. Bot has Line of Sight to target
5. Code at `TargetedMovementGenerator.cpp:217-231` sees: incomplete path + has LoS → **STOPS MOVING**

The assumption was "if you can see the target, you don't need to move" - but that's wrong for ranged classes at 47 yards!

### Solution
Remove offset from all MoveChase calls for ranged classes. Chase directly to target and let `HandleRangedMovement()` stop the bot at casting range.

### Files Modified
| File | Change |
|------|--------|
| `Combat/CombatHelpers.h` | `EngageCaster()`: `MoveChase(target, 28.0f)` → `MoveChase(target)` |
| `Combat/CombatHelpers.h` | `HandleRangedMovement()`: `MoveChase(victim, 28.0f)` → `MoveChase(victim)` |
| `Combat/CombatHelpers.h` | `HandleCasterFallback()`: `MoveChase(victim, 28.0f)` → `MoveChase(victim)` |
| `Combat/Classes/HunterCombat.cpp` | `Engage()`: `MoveChase(target, 25.0f)` → `MoveChase(target)` |

### Additional Fixes (Same Session)
| Fix | Description |
|-----|-------------|
| Caster fallback | Added `HandleCasterFallback()` - wand then melee when all spells fail |
| Hunter Auto Shot | Removed `IsMoving()` check that blocked Auto Shot initiation |
| HandleRangedMovement | Check motion type, not just `IsMoving()` |
| Priest heal-lock | Removed early return that blocked damage spells after heal attempt |

### New Debug Command: `.bot status`
Added GM command to diagnose stuck bots:
```
=== Bot Status: Graveleth ===
Level 2 Warlock | HP: 78/78 (100%)
Mana: 133/133 (100%)
Position: (1737.9, 1433.9, 114.0) Map 0
[State] Action: GRINDING | Strategy: Grinding
Moving: NO | Casting: NO
[Victim] Scarlet Convert (100% HP) | Dist: 47.1 | LoS: YES | InCombat: NO
[Motion] Type: CHASE
```

**Files Created/Modified for Debug Command:**
- `Chat.h` - Added `HandleBotStatusCommand` declaration
- `Chat.cpp` - Added "status" to `botCommandTable`
- `RandomBotAI.h` - Added `BotAction` enum, `BotStatusInfo` struct, `GetStatusInfo()`
- `RandomBotAI.cpp` - Implemented `GetStatusInfo()`
- `PlayerBotMgr.cpp` - Implemented `HandleBotStatusCommand()`

### Result
10+ minutes of testing across all caster bots on server - zero stuck bots. The freeze bug is fixed.

---

## 2026-01-28 - Auto-Generated Grind Spots (Phase 5.5) - TESTING

### Overview
Replaced manual 23-entry `grind_spots` table with 2,684 auto-generated spots derived from creature spawn data. Bots now have full 1-60 coverage on both continents.

### What Was Done

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
- Small offset (±25 yards) prevents bots stacking on exact same point
- Prevents "train" behavior where all bots go to same spot

### Database Tables

| Table | Location | Purpose |
|-------|----------|---------|
| `grind_spots` | characters DB | 2,684 grind locations (was 23) |
| `grind_spots_backup` | characters DB | Original manual spots backup |
| `zone_levels` | characters DB | 39 zone coordinate/level boundaries |

### Files Modified
- `TravelingStrategy.cpp` - Local randomization + weighted distant selection

### Files Created
- `docs/grind_spots_report.tsv` - Full spot list with mob names/levels

### Statistics

| Metric | Value |
|--------|-------|
| Total spots | 2,684 |
| Eastern Kingdoms | 1,088 |
| Kalimdor | 1,596 |
| Level coverage | 1-60 |
| Dangerous spots excluded | 21 |

### Status: 🟡 TESTING
Awaiting verification that:
- Bots properly spread out within zones (no train behavior)
- Level progression works across all levels
- No bots sent to dangerous mismatched zones

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

## 2026-01-26 - BUG FIX: Bots Not Looting or Buffing (Bug #11)

### Problem
Bots were "animalistic killing machines" - immediately targeting the next mob after a kill without looting corpses or casting self-buffs (Ice Armor, Demon Skin, Seal of Righteousness, etc.).

### Root Causes (Two Issues)
1. **Buffing**: `UpdateOutOfCombat()` (buff logic) was called AFTER `GrindingStrategy::UpdateGrinding()`. Since grinding always found and engaged a target, the function returned before reaching the buff code.

2. **Looting**: `GetVictim()` returned the dead mob after combat ended. The check `if (inCombat || me->GetVictim())` kept bots in the combat branch, preventing the looting code from running.

### Solution (Two Parts)
1. **Moved buff check BEFORE grinding** in `UpdateOutOfCombatAI()` - buffs are now applied before looking for the next target

2. **Added `AttackStop()` call** in `UpdateInCombatAI()` when victim is dead and no new attacker found - this clears `GetVictim()` so the bot properly exits the combat branch

### Files Modified
- `RandomBotAI.cpp` - Reordered buff call, added AttackStop() for dead victims

### New Flow
```
Kill mob → AttackStop() clears victim → Exit combat branch →
Looting runs → Buffs checked → Then find next target
```

### Result
Bots now properly loot corpses (gold + items) and maintain self-buffs between fights.

---

## 2026-01-26 - BUG FIX: Bots Not Entering Caves/Buildings (Bug #9)

### Problem
Bots would target mobs inside caves and buildings but stand outside instead of pathing through the entrance to fight them. Both melee and ranged classes affected.

### Root Cause (Two Issues)
1. **Target Selection**: Bug #7's LoS check prevented ranged classes from targeting mobs inside structures entirely - a "half-ass fix" that avoided the problem instead of solving it
2. **Movement Interruption**: When `MoveChase` was interrupted by `ChaseMovementGenerator` edge cases (INCOMPLETE path + LoS = StopMoving), nothing restarted the chase

### Solution (Two Parts)
1. **Removed LoS check from `IsValidGrindTarget()`** - Let bots target mobs in caves/buildings
2. **Added movement persistence in combat handlers**:
   - `HandleRangedMovement()`: If at casting range but NO line of sight → `MoveChase()` closer (through the door/entrance)
   - `HandleMeleeMovement()`: If not in melee range and not moving → re-issue `MoveChase()`

### Files Modified
- `GrindingStrategy.cpp` - Removed LoS check from target validation
- `CombatHelpers.h` - Added LoS check to `HandleRangedMovement()`, new `HandleMeleeMovement()` helper
- `WarriorCombat.cpp`, `RogueCombat.cpp`, `PaladinCombat.cpp`, `ShamanCombat.cpp`, `DruidCombat.cpp` - Added `HandleMeleeMovement()` call

### Result
Bots now properly path through cave entrances and building doors to reach and kill mobs inside.

---

## 2026-01-26 - BUG FIX: Bots Walking Onto Steep Slopes (Bug #2)

### Problem
Bots were walking onto steep slopes (terrain with no navmesh) and getting `startPoly=0` pathfinding errors. They would try to chase mobs by going OVER hills instead of around them, ending up stuck on slopes.

### Root Cause
`ChaseMovementGenerator` and `FollowMovementGenerator` in `TargetedMovementGenerator.cpp` did not call `ExcludeSteepSlopes()` for player bots. When calculating chase paths, steep terrain was included, leading bots onto no-navmesh areas.

### Solution
Added steep slope exclusion specifically for bots (not NPCs - they can walk on slopes):
```cpp
// Bots should not path through steep slopes (players can't walk on them)
if (owner.IsPlayer() && ((Player const*)&owner)->IsBot())
    path.ExcludeSteepSlopes();
```

### Files Modified
- `TargetedMovementGenerator.cpp` - Added `ExcludeSteepSlopes()` for bots in both ChaseMovementGenerator and FollowMovementGenerator
- `PathFinder.cpp` - Enhanced logging for remaining edge cases (Map, Zone, Area, dist, moving, inCombat)

### Result
Bots now path around steep hills like real players would. A bot in Durotar was observed running all the way around the orc starter zone to reach a scorpid on a hill, rather than trying to climb over. Mobs/NPCs still climb hills normally (unchanged).

---

## 2026-01-26 - BUG FIXES (Falling Through Floor, Unreachable Mobs, Stuck Protection)

### Bug #1: Bot Falling Through Floor - FIXED
- **Problem**: Bots fell through floor causing infinite `startPoly=0` pathfinding error spam
- **Fix**: 4-part defense in depth:
  1. Z validation in `GenerateWaypoints()` - skip invalid waypoints
  2. Recovery teleport after 15 ticks of invalid position
  3. Z correction on grind spot cache load
  4. Rate-limited error logging (10 sec per bot)
- **Files Modified**: `TravelingStrategy.cpp`, `RandomBotAI.h/cpp`, `PathFinder.cpp`
- **Commit**: `af528e7c4`

### Bug #3 & #4: Bots Walking Into Terrain / Targeting Unreachable Mobs - FIXED
- **Problem**: Bots walked up steep slopes, got stuck in rocks trying to reach mobs on unreachable terrain
- **Root Cause**: `IsValidGrindTarget()` didn't validate path reachability. Initial NOPATH check missed cases where PathFinder returned `PATHFIND_NOT_USING_PATH` instead
- **Fix**: Added comprehensive reachability check that rejects BOTH `PATHFIND_NOPATH` AND `PATHFIND_NOT_USING_PATH`
- **Files Modified**: `GrindingStrategy.cpp`

### Bug #6: Stuck Protection Too Aggressive - FIXED
- **Problem**: Recovery teleport fired when bots were resting or fighting near walls/obstacles
- **Root Cause**: Check was pathing to point 5 yards ahead - failed if facing a wall even though bot's position was valid
- **Fix**: Changed from PathFinder-based to Map::GetHeight()-based detection. Only triggers if terrain is actually invalid at bot's position
- **Files Modified**: `RandomBotAI.cpp` (removed PathFinder.h and cmath includes)

### Bug #7: Casters Targeting Mobs Without Line of Sight - FIXED
- **Problem**: Caster/ranged bots (Mage, Priest, Warlock, Hunter) would target mobs inside buildings, move into casting range outside, but couldn't cast due to wall blocking LoS. They looped forever.
- **Root Cause**: `IsValidGrindTarget()` checked reachability but not Line of Sight. Ranged classes stop at distance - if LoS blocked there, stuck.
- **Fix**: Added LoS check for ranged classes only:
  - Created `IsRangedClass()` helper (Mage, Priest, Warlock, Hunter)
  - Added `IsWithinLOSInMap()` check after reachability check
  - Melee classes unaffected - they path directly to target
- **Performance**: ~5000 LoS checks/sec at 3000 bots (only ranged classes pay cost)
- **Files Modified**: `GrindingStrategy.h`, `GrindingStrategy.cpp`

### Summary
All major bugs now fixed. Only low-priority issues remaining:
- Bug #2: Invalid startPoly edge cases (handled by recovery system)
- Bug #5: BuildPointPath log spam (cosmetic only)
- Bug #8: Combat reactivity (identified - bot ignores attackers while moving)

---

## 2026-01-25 - PATHFINDER LONG PATH FIX

### Problem
Long-distance travel paths (e.g., Kharanos → Coldridge Valley, ~1000 yards) were failing with `PATHFIND_NOPATH` even though the path existed. Debug logging revealed:
```
BuildPointPath: FAILED result=0x80000000 pointCount=256 polyLength=79
```

### Root Cause
Bug in vMangos `PathFinder.cpp:findSmoothPath()`. When generating smooth waypoints from a poly path, if the 256-point buffer filled up, it returned `DT_FAILURE` instead of `DT_SUCCESS | DT_BUFFER_TOO_SMALL`.

The original code comment said "this is most likely a loop" - assuming full buffer = infinite loop. But for long paths (79 polygons), 256 waypoints is legitimately not enough.

### Solution

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

### Why This Is Safe
- `PATHFIND_INCOMPLETE` is already used throughout vMangos for partial paths
- `TargetedMovementGenerator` already handles incomplete paths properly
- Semantically correct: a truncated path IS an incomplete path
- Better behavior: units get 256 valid waypoints instead of a terrain-ignoring shortcut

### Files Modified
- `src/game/Maps/PathFinder.cpp` - Fixed findSmoothPath return value and BuildPointPath handling

### Debug Logging
Added bot-only PathFinder logging (filtered by `IsPlayerBot()` helper function) for future debugging. Logs are prefixed with `[BOT]`.

---

## 2025-01-11 - COMBAT SYSTEM REFACTOR

### Problem Solved
All bots (including casters) were initiating combat with melee auto-attack. Mages would run up and swing at mobs before casting. Hunters would run into melee instead of using Auto Shot.

### Solution: Modular Combat Handlers
Created a new combat architecture with per-class handlers:

| Class Type | Engagement Behavior |
|------------|---------------------|
| **Melee** (Warrior, Rogue, Paladin, Shaman, Druid) | `Attack()` + `MoveChase()` - run in and melee |
| **Caster** (Mage, Priest, Warlock) | `Attack(false)` + `MoveChase()` + `HandleRangedMovement()` |
| **Hunter** | `Attack(false)` + `MoveChase()` + Auto Shot + `HandleRangedMovement()` |

*Note: Original implementation used MoveChase offsets (28.0f/25.0f) which were later removed by Bug #13 fix.*

### New Architecture

```
src/game/PlayerBots/
├── Combat/
│   ├── IClassCombat.h              ← Interface for class handlers
│   ├── BotCombatMgr.h/cpp          ← Combat coordinator
│   │
│   └── Classes/
│       ├── WarriorCombat.h/cpp     ← Melee engagement
│       ├── PaladinCombat.h/cpp     ← Melee engagement
│       ├── HunterCombat.h/cpp      ← Ranged, Auto Shot at 25 yards
│       ├── MageCombat.h/cpp        ← Caster, spell pull
│       ├── PriestCombat.h/cpp      ← Caster, spell pull
│       ├── WarlockCombat.h/cpp     ← Caster, spell pull
│       ├── RogueCombat.h/cpp       ← Melee engagement
│       ├── ShamanCombat.h/cpp      ← Melee engagement
│       └── DruidCombat.h/cpp       ← Melee engagement
```

### How It Works

1. **RandomBotAI** creates a `BotCombatMgr` on initialization
2. **BotCombatMgr** creates the appropriate class handler based on `GetClass()`
3. **GrindingStrategy** calls `GetCombatMgr()->Engage()` instead of generic `Attack()`
4. Each class handler implements `IClassCombat` interface:
   - `Engage()` - How to initiate combat (class-specific)
   - `UpdateCombat()` - Combat rotation
   - `UpdateOutOfCombat()` - Buffs, pet management

### Files Created (21 new files)

| File | Purpose |
|------|---------|
| `Combat/IClassCombat.h` | Interface for class handlers |
| `Combat/BotCombatMgr.h` | Combat coordinator header |
| `Combat/BotCombatMgr.cpp` | Combat coordinator implementation |
| `Combat/Classes/*Combat.h/cpp` | 9 class handlers (18 files) |

### Files Modified

| File | Changes |
|------|---------|
| `RandomBotAI.h` | Added `BotCombatMgr` member and accessor |
| `RandomBotAI.cpp` | Initialize combat manager, delegate to handlers |
| `GrindingStrategy.cpp` | Use `GetCombatMgr()->Engage()` for engagement |
| `CMakeLists.txt` | Added new source files and include directories |

---

## 2025-01-11 - CRITICAL BUG FIXES

### Issues Fixed

**1. Horde Bots Not Attacking (Orc, Tauren, Troll)**
- **Problem**: Horde bots (Orc, Tauren, Troll) would spawn but stand idle, never attacking mobs. Alliance bots and Night Elf worked fine.
- **Root Cause**: The mob search range (50 yards) was too small. Horde starting areas (Valley of Trials, Red Cloud Mesa) have hostile mobs spawned further from the spawn point than Alliance areas. The search only found friendly NPCs (trainers, quest givers).
- **Fix**: Increased `SEARCH_RANGE` from 50.0f to 150.0f in `GrindingStrategy.h`.

**2. GUID Conflict Bug**
- **Problem**: When user created characters, they would overwrite bot characters because both used GUID 1, 2, 3, etc.
- **Root Cause**: `RandomBotGenerator::GetNextFreeCharacterGuid()` queried DB directly but didn't update `ObjectMgr.m_CharGuids` counter. When player created character, `GeneratePlayerLowGuid()` returned conflicting GUID.
- **Fix**: Changed `RandomBotGenerator::GenerateRandomBots()` to use `sObjectMgr.GeneratePlayerLowGuid()` instead of manual GUID calculation. This keeps the GUID counter in sync.

**2. Account ID Corruption Bug**
- **Problem**: Bot characters' account IDs in DB would change from real values (1-6) to fake session IDs (10000+) after server restart.
- **Root Cause**: Bots use generated session account IDs (10000+) to allow multiple bots from same account to be online. But `Player::SaveToDB()` saved the session account ID instead of the real DB account ID.
- **Fix**: Modified `Player::SaveToDB()` to detect bots (`IsBot()`) and query the DB for the real account ID before saving. Only uses the DB value if it's valid (< 10000).

**3. Vendor Cache Timing**
- **Problem**: Vendor cache was built lazily on first use, causing delay message after bots started logging in.
- **Fix**: Moved `VendoringStrategy::BuildVendorCache()` call to `PlayerBotMgr::Load()`, before bots start. Added progress bar using `BarGoLink`.

### New Features

**RandomBot.Purge Config Option**
- Set `RandomBot.Purge = 1` in mangosd.conf
- On server startup, completely removes all RandomBot data:
  - Deletes all characters using `Player::DeleteFromDB()` (handles all related tables)
  - Deletes from `playerbot` table
  - Deletes RNDBOT accounts from realmd
- Logs reminder to set back to 0 after purge

### Files Modified

| File | Changes |
|------|---------|
| `GrindingStrategy.h` | Increased `SEARCH_RANGE` from 50.0f to 150.0f |
| `PlayerBotMgr.h` | Added `m_confPurgeRandomBots` member |
| `PlayerBotMgr.cpp` | Added purge config loading, purge call in Load(), vendor cache call, fixed bot loading to use real account IDs |
| `RandomBotGenerator.h` | Added `PurgeAllRandomBots()` declaration |
| `RandomBotGenerator.cpp` | Added `PurgeAllRandomBots()` implementation with progress bar, changed `GenerateRandomBots()` to use `sObjectMgr.GeneratePlayerLowGuid()` |
| `Player.cpp` | Modified `SaveToDB()` to preserve real account ID for bots |
| `VendoringStrategy.h` | Made `BuildVendorCache()` public |
| `VendoringStrategy.cpp` | Added progress bar to `BuildVendorCache()` |

### Technical Details

**Bot Session Account IDs (Why 10000+)**
- Multiple bot characters share real accounts (RNDBOT001 has 9 chars)
- Can't have multiple WorldSessions on same account
- Solution: Generate fake session account IDs (10000+) for each bot
- This allows all 50 bots to be online simultaneously
- The REAL account ID is preserved in the database via SaveToDB fix

**SaveToDB Fix Logic**
```cpp
if (IsBot())
{
    // Query DB for real account ID
    result = CharacterDatabase.PQuery("SELECT account FROM characters WHERE guid = %u", GetGUIDLow());
    if (result)
    {
        uint32 dbAccountId = result->Fetch()[0].GetUInt32();
        // Only use if valid (not a corrupted session ID)
        if (dbAccountId > 0 && dbAccountId < 10000)
            saveAccountId = dbAccountId;
    }
}
```

---

## Current Bot Capabilities

**What bots CAN do:**
1. Auto-generate on first server launch (accounts + characters)
2. Spawn in the world without GUID conflicts
3. Be whispered by players (properly registered in ObjectAccessor)
4. **Maintain self-buffs** (Ice Armor, Demon Skin, Seal of Righteousness, etc.) - checked before each fight
5. Fight back when attacked (class-appropriate combat rotations)
6. **Engage targets appropriately by class:**
   - Melee classes (Warrior, Rogue, Paladin, Shaman, Druid) charge in
   - Casters (Mage, Priest, Warlock) move into range (28 yards) and cast
   - Hunters use Auto Shot at 25 yard range, melee fallback when mobs close in
7. Autonomously find and attack mobs (including neutral/yellow mobs)
8. **Enter caves and buildings** to fight mobs inside (path through entrances)
9. Skip mobs already tapped by other players/bots
10. Loot corpses after combat (gold + items)
11. Rest when low HP/mana (sit + cheat regen, no consumables needed)
12. Handle death - release spirit, ghost walk to corpse, resurrect
13. Death loop detection - use spirit healer if dying too often
14. Vendor when bags full or gear broken - walk to nearest vendor
15. Sell all items and repair gear at vendor
16. Persist correctly across server restarts (account IDs preserved)
17. **Travel to new grind spots** when current area has no mobs (Phase 5)
18. Handle getting stuck while traveling (30-sec timeout reset)
19. Resume travel after combat interruption

**Known Limitations:**
- Same-map travel only (no boats/zeppelins/flight paths)
- May path through dangerous areas (no road following / threat avoidance)
- Race distribution skewed toward races with more class options (future config enhancement)

---

## Configuration

| Setting | Default | Purpose |
|---------|---------|---------|
| `RandomBot.Enable` | 0 | Enable RandomBot system |
| `RandomBot.Purge` | 0 | Purge all bots on startup (set back to 0 after!) |
| `RandomBot.MaxBots` | 50 | Bots to generate |
| `RandomBot.MinBots` | 3 | Minimum bots online |
| `RandomBot.Refresh` | 60000 | Add/remove interval (ms) |

---

## Architecture

```
src/game/PlayerBots/
├── RandomBotAI.h/cpp           ← Bot AI (main loop, coordinates all systems)
├── RandomBotGenerator.h/cpp    ← Auto-generation + purge on first launch
├── PlayerBotMgr.h/cpp          ← Bot lifecycle management
├── BotCheats.h/cpp             ← Cheat utilities (resting)
│
├── Combat/                     ← Combat system (class-specific handlers)
│   ├── IClassCombat.h          ← Interface for class handlers
│   ├── BotCombatMgr.h/cpp      ← Combat coordinator
│   └── Classes/                ← Per-class combat handlers
│       ├── WarriorCombat.h/cpp
│       ├── PaladinCombat.h/cpp
│       ├── HunterCombat.h/cpp
│       ├── MageCombat.h/cpp
│       ├── PriestCombat.h/cpp
│       ├── WarlockCombat.h/cpp
│       ├── RogueCombat.h/cpp
│       ├── ShamanCombat.h/cpp
│       └── DruidCombat.h/cpp
│
└── Strategies/                 ← High-level behavior
    ├── IBotStrategy.h          ← Strategy interface
    ├── GrindingStrategy.h/cpp  ← Find mob → kill
    ├── LootingBehavior.h/cpp   ← Loot corpses after combat
    ├── GhostWalkingStrategy.h/cpp ← Death handling
    ├── VendoringStrategy.h/cpp ← Sell items, repair gear
    └── TravelingStrategy.h/cpp ← Find and travel to grind spots
```

### Layer Responsibilities

| Layer | Responsibility |
|-------|---------------|
| **RandomBotAI** | Main brain - coordinates strategies, combat manager, behaviors |
| **BotCombatMgr** | Combat coordinator - creates/delegates to class handlers |
| **IClassCombat** | Class-specific combat - engagement, rotations, buffs |
| **Strategies** | High-level goals - what to do next (find mob, rest, sell) |

---

## Database Tables

| Table | Purpose |
|-------|---------|
| `realmd.account` | Bot accounts (RNDBOT001, RNDBOT002...) |
| `characters.characters` | Bot character data |
| `characters.playerbot` | Links char_guid to AI type ('RandomBotAI') |

---

## Build & Test

```bash
# Build
cd ~/Desktop/Forsaken_WOW/core/build && make -j$(nproc) && make install

# Run (two terminals)
cd ~/Desktop/Forsaken_WOW/run/bin && ./realmd   # Terminal 1
cd ~/Desktop/Forsaken_WOW/run/bin && ./mangosd  # Terminal 2
```

---

## Testing Commands

```sql
-- Check bot accounts
SELECT id, username FROM realmd.account WHERE username LIKE 'RNDBOT%';

-- Check bot characters and their accounts
SELECT guid, account, name, level FROM characters.characters
WHERE account IN (SELECT id FROM realmd.account WHERE username LIKE 'RNDBOT%') LIMIT 10;

-- Verify account IDs are correct (should be 1-6, NOT 10000+)
SELECT guid, account, name FROM characters.characters WHERE account >= 10000;
```

---

## Session Log

### 2026-01-25 - Movement Sync Bug Fixed (TravelingStrategy Overhaul)
- **Issue**: Bots moved at super speed, disappeared, floated through air during long-distance travel
- **Root Cause**: Single `MovePoint()` for entire journey failed when navmesh couldn't find path, falling back to direct 2-point line ignoring terrain
- **Investigation**:
  - Analyzed BattleBotAI waypoint system (uses segmented waypoints + `MOVE_EXCLUDE_STEEP_SLOPES`)
  - Checked taxi path data - only air paths, not usable for ground travel
  - Researched cMangos playerbots - they use teleportation, no road waypoint data
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
- **Impact**: Fixes all terrain/pathing issues for long-distance travel

### 2026-01-25 - Combat Facing Bug Fixed
- **Issue**: Bots got stuck when target was behind them (hunter couldn't engage wolf behind it)
- **Root Cause**: `Engage()` and `UpdateCombat()` didn't ensure bot faced target. `MoveChase()` doesn't handle facing when already in range.
- **Fix**: Added facing logic in two places:
  1. `CombatHelpers.h` - `SetFacingToObject(pTarget)` before `Attack()` in both `EngageMelee()` and `EngageCaster()`
  2. `BotCombatMgr.cpp` - Facing check in `UpdateCombat()` using BattleBotAI pattern (`HasInArc()` + `SetInFront()` + `SendMovementPacket()`)
- **Files Modified**: `Combat/CombatHelpers.h`, `Combat/BotCombatMgr.cpp`
- **Tested**: Bot now "snaps" to face target before attacking
- **Impact**: +9 lines, fixes stuck combat for all classes

### 2026-01-24 - Code Audit Issue #8 Fixed
- **Issue**: Dead code - `GetNextFreeCharacterGuid()` method never called
- **Fix**: Removed method from RandomBotGenerator.cpp and declaration from .h
- **Context**: Method was obsoleted when bot generation switched to `sObjectMgr.GeneratePlayerLowGuid()`
- **Files Modified**: `RandomBotGenerator.cpp`, `RandomBotGenerator.h`
- **Impact**: 13 lines of dead code removed

### 2026-01-24 - CODE AUDIT #1 COMPLETE (12/12 issues)
- **Full report:** See `docs/Audit_1.md` for detailed breakdown
- **Issues Fixed:**
  - #1: Cache grind spots at startup (zero runtime DB queries)
  - #2: Tiered grid search + exponential backoff (~44% reduction)
  - #3: Remove dynamic_cast from hot path (store pointer directly)
  - #4: Eliminate list allocation in search (single-pass checker)
  - #5: Remove debug PathFinder from production
  - #6: Wire up pre-travel vendor trigger (fixes stuck bot bug)
  - #7: Document cache thread safety ordering
  - #8: Remove dead GetNextFreeCharacterGuid() method
  - #9-11: Create CombatHelpers.h (consolidate ~150 lines duplicate code)
  - #12: Clear m_generatedNames between generation cycles
- **Files Created:** `Combat/CombatHelpers.h`
- **Files Deleted:** `CODE_AUDIT_CHECKLIST.md`, `PHASE5_TRAVEL_PLAN.md`, `PHASE5_TRAVEL_SYSTEM_REPORT.md`
- **Net Impact:** -47 lines of code, major performance improvements at scale

### 2026-01-24 - Code Audit Issue #7 Verified
- **Issue**: Vendor/grind spot cache thread safety (potential race condition)
- **Analysis**: Verified ordering in PlayerBotMgr::Load() is already correct
  - Step 5.5: Caches built (VendoringStrategy, TravelingStrategy)
  - Step 6: Bots spawn (AFTER caches are ready)
  - Both caches also have fallback checks if somehow accessed before startup
- **Fix**: Added explicit comment documenting the ordering requirement
- **Files Modified**: `PlayerBotMgr.cpp`
- **Impact**: Documents thread safety guarantee for future maintainers

### 2026-01-24 - Code Audit Issue #6 Fixed
- **Issue**: Pre-travel vendor threshold mismatch (bots stuck when bags >60% or durability <50%)
- **Fix**: Wired up VendoringStrategy::ForceStart() from TravelingStrategy
  - Added `SetVendoringStrategy()` setter and `m_pVendoringStrategy` member to TravelingStrategy
  - RandomBotAI constructor now wires up the cross-strategy reference
  - TravelingStrategy calls `ForceStart()` when yielding for pre-travel vendor check
- **Files Modified**:
  - `TravelingStrategy.h` - Added setter and member
  - `TravelingStrategy.cpp` - Call ForceStart() before yielding
  - `RandomBotAI.cpp` - Wire up vendoring strategy in constructor
- **Tested**: Bot with full bags correctly vendors before traveling to new grind spot
- **Impact**: Fixes bug where bots would stand idle at intermediate bag/durability thresholds

### 2026-01-24 - Code Audit Issue #5 Fixed
- **Issue**: Debug PathFinder in production (unnecessary path calculation every vendor trip)
- **Fix**: Removed 28-line debug block and unused `PathFinder.h` include
- **Context**: Debug code was added to investigate tree collision issue, which is now fixed
- **Files Modified**: `VendoringStrategy.cpp`
- **Impact**: Eliminates duplicate path calculation on every vendor trip

### 2026-01-24 - Code Audit Issue #1 Fixed
- **Issue**: Database query in TravelingStrategy hot path (300+ queries/sec at scale)
- **Fix**: Cache grind spots at startup, search in-memory at runtime
  - Added `GrindSpotData` struct to hold cached spot data
  - Added static cache `s_grindSpotCache` with mutex (same pattern as VendoringStrategy)
  - `BuildGrindSpotCache()` loads all spots from DB once at startup with progress bar
  - `FindGrindSpot()` now searches in-memory array instead of executing SQL queries
  - Called from `PlayerBotMgr::Load()` alongside vendor cache build
- **Files Modified**:
  - `TravelingStrategy.h` - Added GrindSpotData struct, static cache members, BuildGrindSpotCache()
  - `TravelingStrategy.cpp` - Implemented cache build, replaced DB queries with in-memory search
  - `PlayerBotMgr.cpp` - Added include and cache build call at startup
- **Data Fix**: Corrected grind_spots level ranges (starter zones 1-5, not 1-6)
- **Impact**: Zero runtime DB queries for travel destination selection

### 2026-01-24 - Code Audit Issue #2 Fixed
- **Issue**: Grid search every tick (3000 searches/sec at scale)
- **Fix**: Tiered radius search + exponential backoff
  - Tier 1: 50 yards (fast local search)
  - Tier 2: 150 yards (only if tier 1 empty)
  - Backoff: 1s → 2s → 4s → 8s when no mobs found
- **Files Modified**:
  - `GrindingStrategy.h` - Added `m_skipTicks`, `m_backoffLevel`, tiered range constants
  - `GrindingStrategy.cpp` - Added cooldown check, tiered search, backoff logic
- **Commit**: 0de100df3
- **Impact**: ~44% reduction in searches at scale, faster close-range detection

### 2026-01-24 - Code Audit Issue #4 Fixed
- **Issue**: List allocation every search in GrindingStrategy (heap allocations)
- **Fix**: Eliminated list entirely - replaced with single-pass `CreatureLastSearcher` using stateful `NearestGrindTarget` checker
- **Files Modified**:
  - `GrindingStrategy.h` - Made `IsValidGrindTarget()` public for checker access
  - `GrindingStrategy.cpp` - Replaced `AllCreaturesInRange` + list + loop with `NearestGrindTarget` + `CreatureLastSearcher`
- **Commit**: 0de100df3
- **Impact**: Zero container allocations, eliminates second iteration pass
- **Code reduction**: 27 lines → 17 lines for checker + FindGrindTarget

### 2026-01-24 - Code Audit Issue #3 Fixed
- **Issue**: dynamic_cast in GrindingStrategy hot path (RTTI overhead)
- **Fix**: Store BotCombatMgr* directly via setter, called during bot initialization
- **Files Modified**:
  - `GrindingStrategy.h` - Added SetCombatMgr() setter and m_pCombatMgr member
  - `GrindingStrategy.cpp` - Replaced dynamic_cast block with direct pointer check
  - `RandomBotAI.cpp` - Call SetCombatMgr() during initialization
- **Commit**: d8aa6de1c
- **Impact**: Eliminates ~3000 RTTI lookups/sec at scale

### 2026-01-24 - Code Audit Complete
- **Created**: `docs/CODE_AUDIT_CHECKLIST.md` - 12 issues identified for scale optimization
- **Critical Issues (3)**:
  1. DB query in TravelingStrategy hot path
  2. Grid search every tick in GrindingStrategy
  3. dynamic_cast in hot path
- **High Priority (2)**: List allocations, debug PathFinder in production
- **Medium Priority (2)**: Pre-travel vendor bug, thread safety
- **Low Priority (5)**: Dead code, code duplication, minor memory leak
- **Purpose**: Checklist for tackling issues across multiple sessions with dev reviews

### 2026-01-24 - Documentation Update
- **Created**: `docs/BOT_DECISION_FLOW.md` - comprehensive state machine documentation
  - Update loop flowchart
  - Strategy priority order
  - All constants and thresholds
  - State transition diagrams
  - Debugging tips
- **Updated**: `CLAUDE.md`
  - Added TravelingStrategy to architecture
  - Added grind_spots table to database tables
  - Added RandomBot.Purge to configuration
  - Added reference to BOT_DECISION_FLOW.md in documentation protocol
  - Updated last modified date

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
- **Working Features**:
  - Travel detection (5 consecutive "no mobs" ticks)
  - DB query for level/faction-appropriate spots
  - Stuck detection (30-sec timeout)
  - Combat interrupt (fight → resume travel)
  - Anti-thrashing (90-sec arrival cooldown)
- **Known Bugs** (documented in CURRENT_BUG.md):
  1. Movement sync bug - super speed / disappearing during long travel
  2. Pre-travel vendor check not wired up - bot stands idle with >60% bags

### 2026-01-24 - Tree Collision FIXED
- **Problem**: Bots walked through trees that players cannot walk through
- **Investigation** (2026-01-23):
  1. Confirmed navmesh paths went through trees (`.mmap path` showed straight line)
  2. Found trees had `nBoundingTriangles = 0` in M2 files - no collision mesh
  3. Tested `ignoreM2Model = false` in Map.h - didn't help (LoS ≠ pathfinding)
  4. Checked M2 `collision_box` and `collision_sphere_radius` fields - all zeros
  5. Ran full vmap extraction with debug output - no hidden collision data found
- **Root Cause**: **Elysium client had corrupted/modified M2 data** - trees were missing collision info that exists in vanilla 1.12
- **Fix**: Re-extracted vmaps and mmaps from a **clean vanilla 1.12 WoW client**
- **Result**: Bots now properly pathfind around trees!
- **Lesson Learned**: Always extract from clean, unmodified client files. Private server clients may have corrupted data.

### 2025-01-22 - Enable Navmesh Pathfinding for Bot Movement
- **Problem**: Bots were walking through terrain (falling through hills/mounds) when using MovePoint for vendoring, ghost walking, and looting.
- **Root Cause**: `MovePoint()` was called without the `MOVE_PATHFINDING` flag, causing it to create direct 2-point paths that ignore the navmesh.
- **Fix**: Added `MOVE_PATHFINDING | MOVE_RUN_MODE` flags to all MovePoint calls in bot strategies.
- **Debug Logging**: Added PathFinder debug output to VendoringStrategy to verify navmesh is being used (shows path type and waypoint count).
- **Files Modified**:
  - `src/game/PlayerBots/Strategies/VendoringStrategy.cpp` - Pathfinding + debug logging
  - `src/game/PlayerBots/Strategies/GhostWalkingStrategy.cpp` - Pathfinding flag
  - `src/game/PlayerBots/Strategies/LootingBehavior.cpp` - Pathfinding flag
- **Result**: Bots now use navmesh pathfinding (confirmed 35 waypoints in test). Terrain collision works. Tree/doodad collision is a separate navmesh data issue to investigate later.

### 2025-01-17 - Hunter Melee Fallback Fix ✅ VERIFIED
- **Problem**: Level 1 hunters would stand idle when mobs reached melee range - no attacks at all.
- **Root Cause**: Hunters engaged with `Attack(pTarget, false)` which disables melee auto-attack. The melee fallback code tried Wing Clip, Mongoose Bite, Raptor Strike - but level 1 hunters don't have these abilities yet. Code then returned early, doing nothing.
- **Fix**: Added `Attack(pVictim, true)` and `MoveChase(pVictim)` at the start of the melee block. Now hunters will melee auto-attack when mobs close in, using special abilities when available at higher levels.
- **Files Modified**:
  - `src/game/PlayerBots/Combat/Classes/HunterCombat.cpp` - Enable melee auto-attack in deadzone
- **Status**: ✅ Tested and working

### 2025-01-17 - Ranged Kiting Bug Fix ✅ VERIFIED
- **Problem**: Low-level ranged bots (Mage, Hunter, Warlock, Priest) would endlessly run backwards instead of fighting. Mobs are faster than players at level 1.
- **Root Cause**: `MoveChase(target, 28.0f)` creates continuous movement that tries to maintain 28 yards. When mob chases, bot keeps repositioning backward forever.
- **Initial Fix**: Added snare/root check - bots only kite if target has `SPELL_AURA_MOD_DECREASE_SPEED` or `SPELL_AURA_MOD_ROOT`. Otherwise they stand and fight.
- **Bug in Initial Fix**: The snare check stopped ALL movement including initial approach. Bots would target mobs but never walk toward them.
- **Final Fix**: Added distance check - only stop movement if bot is within 30 yards (casting range). This allows approach movement to complete before stopping.
- **Hunter-specific**: Added melee ability checks (Wing Clip, Mongoose Bite, Raptor Strike) for deadzone combat.
- **Files Modified**:
  - `src/game/PlayerBots/Combat/Classes/MageCombat.cpp` - Snare check + range check
  - `src/game/PlayerBots/Combat/Classes/HunterCombat.cpp` - Snare check + range check + melee abilities
  - `src/game/PlayerBots/Combat/Classes/WarlockCombat.cpp` - Snare check + range check
  - `src/game/PlayerBots/Combat/Classes/PriestCombat.cpp` - Snare check + range check
- **Status**: ✅ Tested and working

### 2025-01-16 - Bot Combat Bug Fix (Mobs Not Aggroing + Starting Gear)
- **Problem**: Bots couldn't be targeted by mobs (cinematic pending) and spawned naked (no starting gear).
- **Root Cause**: New characters with `played_time_total == 0` trigger intro cinematic, making them untargetable. Initial fix (setting played_time=1) broke `AddStartingItems()` which also checks for played_time=0.
- **Fix**: Added `!pCurrChar->IsBot()` check to cinematic trigger in CharacterHandler.cpp. Bots keep played_time=0 (get starting gear) but skip cinematic.
- **Files Modified**:
  - `src/game/Handlers/CharacterHandler.cpp` - Added IsBot() check to cinematic condition
  - `src/game/PlayerBots/RandomBotAI.cpp` - Backup CinematicEnd() call (safety fallback)
- **Result**: Bots spawn with starting gear and are properly targetable by mobs.

### 2025-01-16 - Caster Bot Fix (Not Moving or Casting)
- **Problem**: Caster bots (Mage, Warlock, Priest) stood at spawn targeting mobs but never moved, turned, or cast spells.
- **Root Cause**: Caster `Engage()` methods called `Attack(target, false)` but never moved toward the target. Mobs were found up to 150 yards away, but spell range is only 30 yards. `CanTryToCastSpell()` correctly rejected all casts due to range check.
- **Fix**: Added `MoveChase(pTarget, 28.0f)` to caster Engage() methods, matching Hunter's approach.
- **Files Modified**:
  - `src/game/PlayerBots/Combat/Classes/MageCombat.cpp`
  - `src/game/PlayerBots/Combat/Classes/WarlockCombat.cpp`
  - `src/game/PlayerBots/Combat/Classes/PriestCombat.cpp`
- **Result**: Casters now move into range and cast spells correctly.

### 2025-01-13 - Race-Specific Name Generation
- **Feature**: Bot names now match their race/gender using Warcraft-style syllable patterns
- **Implementation**: Added syllable-based name generator to `RandomBotGenerator`
- **Name Patterns by Race**:
  | Race | Style | Examples |
  |------|-------|----------|
  | Orc | Guttural, harsh | Grom'kar, Drak'thar |
  | Troll | Tribal, apostrophes | Zul'jan, Vol'kali |
  | Tauren | Nature, soft | Cairne, Mulgara |
  | Undead | Dark, twisted | Sylvanas, Drakul |
  | Human | Medieval Western | Anduin, Lothar |
  | Dwarf | Nordic, sturdy | Magni, Brann |
  | Gnome | Whimsical, mechanical | Mekkatorque, Fizzi |
  | Night Elf | Flowing, melodic | Tyrande, Malfurion |
- **Validation Rules**: 3-12 characters, no triple consecutive letters, blacklist for famous names
- **Files Modified**:
  - `src/game/PlayerBots/RandomBotGenerator.h` - Added `RaceNameData` struct, new methods
  - `src/game/PlayerBots/RandomBotGenerator.cpp` - Full syllable data for 8 races, name generation logic
- **Result**: Bots now have immersive, lore-appropriate names

### 2025-01-13 - Fix Generated Player Names Not Title Case
- **Problem**: Generated bot names were all lowercase (e.g., "norosu" instead of "Norosu")
- **Root Cause**: `GeneratePlayerName()` in ObjectMgr.cpp built names from lowercase vowels/consonants without capitalizing
- **Fix**: Added `name[0] = std::toupper(name[0]);` before returning the generated name
- **Files Modified**: `src/game/ObjectMgr.cpp`
- **Result**: All generated player names now have proper title case

### 2025-01-12 - Purge GUID Counter Fix
- **Problem**: After purging bots with `RandomBot.Purge`, newly generated bots would get unnecessarily high GUIDs (e.g., 51+ instead of 1+)
- **Root Cause**: `ObjectMgr::m_CharGuids` counter is loaded from DB at startup via `SetHighestGuids()`. When purge deletes bots, the counter is NOT reset, so new bots continue from the old max GUID.
- **Fix**: Added `ObjectMgr::ReloadCharacterGuids()` method that reloads the GUID counter from the database. Called at end of `PurgeAllRandomBots()`.
- **Files Modified**:
  - `src/game/ObjectMgr.h` - Added `ReloadCharacterGuids()` declaration
  - `src/game/ObjectMgr.cpp` - Implemented `ReloadCharacterGuids()`
  - `src/game/PlayerBots/RandomBotGenerator.cpp` - Call `ReloadCharacterGuids()` after purge
- **Result**: After purge, new bots get GUIDs starting from the correct value (respecting any remaining player characters)

### 2025-01-12 - Bot Registration Fix (Whispers Now Work)
- **Problem**: Bots couldn't be whispered, and ObjectAccessor lookups by name failed
- **Root Cause**: `ObjectAccessor::AddObject()` stored names without normalization (e.g., "norosu"), but `FindPlayerByName()` and `FindMasterPlayer()` normalized search terms to title case (e.g., "Norosu")
- **Fix**: Modified `ObjectAccessor::AddObject()` and `RemoveObject()` to normalize names before adding/removing from name maps
- **Files Modified**: `src/game/ObjectAccessor.cpp`
- **Result**: Bots are now whisperable, all ObjectAccessor lookups work correctly

### 2025-01-11 - Combat System Refactor (Phase 4.5)
- Refactored combat engagement to be class-appropriate
- Created modular combat handler architecture (BotCombatMgr + IClassCombat)
- 21 new files: interface, manager, 9 class handlers
- Casters now stand and cast to pull (no melee run-up)
- Hunters use Auto Shot at 25 yard range
- Melee classes charge in as expected

### 2025-01-11 - Horde Bot Fix
- Fixed Orc, Tauren, Troll bots standing idle - increased mob search range from 50 to 150 yards
- Horde starting areas have mobs further from spawn point than Alliance areas

### 2025-01-11 - Critical Bug Fixes
- Fixed GUID conflict: bots now use `sObjectMgr.GeneratePlayerLowGuid()`
- Fixed account ID corruption: `SaveToDB()` preserves real account ID for bots
- Added `RandomBot.Purge` config option for clean bot removal
- Moved vendor cache build to startup with progress bar
- **TESTED**: Server restarts preserve correct account IDs, no GUID conflicts

### 2025-01-11 - Bug Fix: Bots not showing in /who list
- Fixed `world_phase_mask = 0` issue in RandomBotGenerator

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

*Last Updated: 2026-01-31*
*Current State: Phase 6 complete. Use-after-free crash fixed (BotMovementManager pointer sync on reconnect).*
