# RandomBot AI Development Progress

## Project Status: Phase 7 COMPLETE (Training System)

| Phase | Status | Description |
|-------|--------|-------------|
| Phase 0 | Complete | Auto-generate bot accounts and characters |
| Phase 1 | Complete | Combat AI - bots fight when attacked |
| Phase 2 | Complete | Grinding - find mobs, kill, loot, rest |
| Phase 3 | Complete | Death handling and respawn |
| Phase 4 | Complete | Vendoring - sell items, repair gear |
| Phase 4.5 | Complete | Combat system refactor - class-appropriate engagement |
| Phase 5 | Complete | Travel system - find and travel to grind spots |
| Phase 5.5 | Complete | Auto-generated grind spots + local randomization |
| Phase 6 | Complete | Centralized BotMovementManager |
| Phase 7 | Complete | Automatic spell training at class trainers |

### Active Bugs
- Bug #12: Hunter Auto Shot cooldown spam (Low Priority - cosmetic)

### For older session logs, see `docs/archive/PROGRESS_ARCHIVE.md`

---

## 2026-03-14 - cMangos PlayerBots Comparison Report

### What Happened
Created a comprehensive comparison report between our RandomBot AI and cMangos's PlayerBots system. No code was written - this was a research/analysis session.

### Deliverable
- `docs/CMANGOS_COMPARISON_REPORT.md` - Full feature-by-feature comparison, architecture analysis, scalability assessment, and roadmap recommendations. Reference material based on two cMangos docs located at `/home/bryoal/Desktop/PlayerBots Fork/`.

---

## 2026-02-09 - Questing System Brainstorm Review & Revision

### What Happened
Full review and revision of `docs/Quest_Implementation/BRAINSTORM.md`. No code was written - this was a planning session.

### Key Changes to Brainstorm
| Change | Description |
|--------|-------------|
| Quest chains | Added `PrevQuestId`/`NextQuestId` awareness, breadcrumb quest handling |
| Strategy integration | Reframed as weighted task system (external, swaps `m_strategy`) instead of mutually exclusive coin flip |
| Class/Race filtering | Added `RequiredClasses`/`RequiredRaces` checks, `Player::CanTakeQuest()` as final gate |
| Quest discovery | Replaced vague "quest hub" with indexed NPC cache + cluster concept (scales to 3000 bots) |
| Gameobject quest givers | Cache now includes both creature AND gameobject sources for quest givers and turn-ins |
| Opportunistic discovery | Bots detect nearby quest givers while doing other activities (questing mode only) |
| Item-started quests | Loot hook in LootingBehavior for `item_template.StartQuest` items (questing mode only) |
| Quest log management | 20 quest limit, grey/stale quest abandonment |
| Cross-map filtering | Skip quests with objectives on another continent |
| Multi-objective quests | Up to 4 kill targets + 6 item requirements per quest, including mixed types |
| Kill sub-tasks | QuestingStrategy composes GrindingStrategy internally with creature filter |
| Exploration quests | Corrected: areatrigger system is separate from `ReqCreatureOrGOId` (negative = gameobject, NOT areatrigger) |
| Group/Elite quests | Deferred to future grouping system |
| Reward selection | Simple class-based heuristic now, spec-aware system built as infrastructure for later |

### Bug Fixes in Doc
- Fixed `GAMEOBJECT_TYPE_QUESTGIVER` from type 3 to correct type 2
- Fixed incorrect claim that `ReqCreatureOrGOId` negative values are areatrigger IDs (they're gameobject entries)

### Next Steps
1. Update `docs/Quest_Implementation/IMPLEMENTATION_PLAN.md` to match revised brainstorm
2. Begin coding Phase 0A: Object Interaction System

---

## 2026-02-01 - Resting System Overhaul

### Changes
Improved resting to use spell-based regeneration with proper buffs and animations (like cMangos bots).

### Before vs After

| Aspect | Before | After |
|--------|--------|-------|
| HP threshold | 35% | **50%** (triggers rest after resurrection) |
| Mana threshold | 45% | **40%** (less frequent drinking) |
| Regen method | Cheat (manual ModifyHealth/ModifyPower) | **Spell-based** (cast food/drink spells) |
| Food spell | 1131 (flat 358 HP/tick) | **24005** (2% max HP/tick) |
| Drink spell | 1137 (flat 489 mana/tick) | **24355** (2% max mana/tick) |
| Scaling | Fixed amounts | **Percentage-based** (scales with level) |
| Buff icon | None | **Shows food/drink buff** |
| Animation | Just sits | **Holds bread/cup** |

### Why These Changes
- **50% HP threshold**: Players resurrect at corpse with 50% HP/mana - now bots rest immediately after resurrection instead of jumping into combat at half health
- **40% mana threshold**: Prevents casters from drinking constantly during normal grinding
- **Percentage-based regen**: A level 60 bot with 5000 HP regens 100 HP/tick, while a level 10 bot with 500 HP regens 10 HP/tick - scales naturally with level

### Files Modified
- `BotCheats.h` - Changed spell IDs, removed unused tick timer constants
- `BotCheats.cpp` - Rewrote to cast food/drink spells instead of manual regen
- `RandomBotAI.h` - Removed `m_restingTickTimer` member
- `RandomBotAI.cpp` - Updated HandleResting call signature

### Note
Food animation (eating motion) doesn't play because `SPELL_AURA_OBS_MOD_HEALTH` (aura 20) doesn't trigger `EMOTE_ONESHOT_EAT` in the core - only `SPELL_AURA_MOD_REGEN` (aura 84) does. Drink animation works. This is a cosmetic limitation.

---

## 2026-01-31 - Phase 7: TrainingStrategy (Automatic Spell Learning)

### Feature
Bots now automatically travel to class trainers and learn new spells when they reach even levels (2, 4, 6, 8, etc.).

### Implementation

**State Machine:**
```
IDLE -> Check level -> FINDING_TRAINER -> TRAVELING -> AT_TRAINER -> Learn spells -> DONE
                              |                |
                       No trainer found    Combat interrupt -> Resume after
```

**Key Features:**
| Feature | Description |
|---------|-------------|
| Level-based triggers | Training at even levels (2, 4, 6...) |
| Trainer cache | Built at startup from `creature_template` (trainer_type=0, trainer_class>0) |
| Faction-aware | Finds trainers matching bot's faction |
| Same-map only | Won't cross continents for training |
| Highest priority | Runs before vendoring/grinding |
| Combat handling | Resumes travel after combat interruption |
| 5 minute timeout | Prevents infinite stuck on long distances |
| Free spells | Testing mode - no gold cost |

### Files Created
- `Strategies/TrainingStrategy.h` - Strategy header with state machine
- `Strategies/TrainingStrategy.cpp` - Full implementation (~534 LOC)

### Files Modified
- `RandomBotAI.h/cpp` - Added training check, `NeedsTraining()`, `HasTrainedThisLevel()`
- `PlayerBotMgr.cpp` - Initialize trainer cache on startup
- `CMakeLists.txt` - Added TrainingStrategy files

### Result
Bots automatically seek out trainers and learn new spells as they level up.

---

## 2026-01-31 - BUG FIX: Vendoring Not Selling Items

### Problem
Bots would walk to a vendor when bags were full, but then do nothing - no items sold, no repairs made. The `.bot status` command always showed "GRINDING" even while walking to vendor.

### Root Cause (Two Issues)

**Issue 1: Status display broken**
`GetStatusInfo()` had no check for vendoring state - it always fell through to show "GRINDING".

**Issue 2: Vendor creature lookup failing**
`GetVendorCreature()` used a GUID-based lookup (`map->GetCreature(ObjectGuid(...))`) with a cached spawn GUID from server startup. This lookup was silently failing, so `DoVendorBusiness()` returned false but the state machine continued to DONE anyway.

### Solution

| Fix | Description |
|-----|-------------|
| Added `IsActive()` to VendoringStrategy | Returns true when vendoring is in progress |
| Added vendoring check to `GetStatusInfo()` | Now shows "VendoringStrategy" when active |
| Replaced GUID lookup with nearby search | `FindNearbyVendorCreature()` searches for ANY friendly vendor within 30 yards |
| Added MINIMAL-level logging | All vendoring states now log visibly to console |

### Files Modified
- `RandomBotAI.cpp` - Added vendoring check to `GetStatusInfo()`
- `VendoringStrategy.h` - Added `IsActive()` method
- `VendoringStrategy.cpp` - Use `FindNearbyVendorCreature()`, added visible logging

### Result
Bots now successfully sell items to vendors. Console shows full vendoring flow:
```
[VendoringStrategy] Bot Tallbi needs to vendor (bags full: yes, free slots: 0)
[VendoringStrategy] Bot Tallbi walking to vendor at (-6104.5, 384.0, 395.6)
[VendoringStrategy] Bot Tallbi arrived at vendor location
[VendoringStrategy] Bot Tallbi selling to Grundel Harkin
[VendoringStrategy] Bot Tallbi sold 15 items for 496 copper
```

---

## 2026-01-31 - CRITICAL BUG FIX: Server Crash (Use-After-Free)

### Problem
Server was crashing randomly with ASAN heap-buffer-overflow errors in `BotMovementManager` methods. Crashes occurred during normal bot operation (grinding, combat, movement).

### Root Cause
The bot AI (owned by `PlayerBotEntry`) persists across logout/login cycles, but the Player object is destroyed and recreated. When a bot reconnects:
1. `SetPlayer(newPlayer)` updates `me` in the AI
2. But `BotMovementManager::m_bot` still pointed to the OLD (freed) Player
3. Any access to `m_bot` triggered use-after-free -> crash

### Solution (3 Parts)

| Part | Change |
|------|--------|
| 1. `SetBot()` method | New method to update `m_bot` and `m_botGuid` when player changes |
| 2. `OnPlayerLogin()` | Now calls `SetBot(me)` to sync pointers on reconnect |
| 3. `IsValid()` guards | Added to all movement entry points as threading safety net |

### Files Modified
- `BotMovementManager.h` - Added `SetBot()`, `GetBot()` declarations
- `BotMovementManager.cpp` - `SetBot()` implementation, `IsValid()` guards on 6 methods
- `RandomBotAI.cpp` - `OnPlayerLogin()` calls `m_movementMgr->SetBot(me)`

### Result
Server stable for 3+ hours with ASAN enabled. No crashes during normal bot operation.

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
IDLE -> Scan & pick random target -> APPROACHING -> IN_COMBAT -> IDLE
                                        |
                                 TIMEOUT (30s) -> Clear target -> IDLE
```

**Key Changes:**
| Old Behavior | New Behavior |
|--------------|--------------|
| Grab nearest mob | Scan ALL mobs, pick random |
| Trust `GetVictim()` blindly | Track own target via GUID + state |
| No approach timeout | 30 second timeout to reach target |
| Path check during scan only | Path MUST be valid before committing |

### Files Modified
- `GrindingStrategy.h/cpp` - Complete rewrite with state machine
- `RandomBotAI.cpp` - Added timeout check in `UpdateInCombatAI()` for stuck detection
- `GhostWalkingStrategy.cpp` - Fixed ghost corpse walk (direct MovePoint, no pathfinding)

---

## 2026-01-30 - BotMovementManager Implementation (Phase 6 COMPLETE)

Centralized all RandomBot movement into a single `BotMovementManager` class.

### Key Features
| Feature | Purpose |
|---------|---------|
| Priority system | IDLE < WANDER < NORMAL < COMBAT < FORCED |
| Duplicate detection | Prevents MoveChase spam |
| CC validation | Won't issue moves while stunned/rooted |
| Multi-Z height search | Caves/buildings |
| Path smoothing | Skip unnecessary waypoints |
| 5-second stuck detection | Micro-recovery before emergency teleport |

### Files Created
- `BotMovementManager.h/cpp` - Full implementation (~400 LOC)

### Files Modified
- `RandomBotAI.h/cpp` - Initialize manager, call Update(), wire to strategies
- All 9 class combat handlers - Use movement manager
- All strategies - Use movement manager

---

## 2026-01-29 - CRITICAL BUG FIX: Ranged Bots Freezing (Bug #13)

### Problem
Ranged bots (Mage, Warlock, Priest, Hunter) would frequently freeze in place after selecting a target. They had a victim set, Motion type was CHASE, but they weren't moving or casting.

### Root Cause
`MoveChase(target, 28.0f)` offset caused pathfinding issues. ChaseMovementGenerator sees: incomplete path + has LoS -> STOPS MOVING (assumes "if you can see it, you don't need to move").

### Solution
Remove offset from all MoveChase calls for ranged classes. Chase directly to target and let `HandleRangedMovement()` stop the bot at casting range.

### Files Modified
- `Combat/CombatHelpers.h` - Removed 28.0f offset from `EngageCaster()`, `HandleRangedMovement()`, `HandleCasterFallback()`
- `Combat/Classes/HunterCombat.cpp` - Removed 25.0f offset from `Engage()`

### Also Added: `.bot status` Debug Command
GM command to diagnose stuck bots - shows bot state, target, movement, strategy.

---

## Current Bot Capabilities

**What bots CAN do:**
- Auto-generate on first server launch
- Fight with class-appropriate combat rotations
- Maintain self-buffs between fights
- Loot corpses after combat
- Rest when low HP/mana (spell-based regen with buff icons, scales with level)
- Handle death (ghost walk, resurrect)
- Vendor when bags full or gear broken
- Travel to new grind spots when area is depleted
- Enter caves and buildings to reach mobs
- React to being attacked (switch targets)
- Learn new spells from class trainers at even levels

**Known Limitations:**
- Same-map travel only (no boats/zeppelins/flight paths)
- May path through dangerous areas

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

*Last Updated: 2026-02-01 (Resting System Overhaul)*
