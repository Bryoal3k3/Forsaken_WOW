# RandomBot AI Development Progress

## Project Status: Phase 6 COMPLETE (Movement Manager)

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

### Active Bugs
- Bug #12: Hunter Auto Shot cooldown spam (Low Priority - cosmetic)

### For older session logs, see `docs/archive/PROGRESS_ARCHIVE.md`

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
- Rest when low HP/mana
- Handle death (ghost walk, resurrect)
- Vendor when bags full or gear broken
- Travel to new grind spots when area is depleted
- Enter caves and buildings to reach mobs
- React to being attacked (switch targets)

**Known Limitations:**
- Same-map travel only (no boats/zeppelins/flight paths)
- May path through dangerous areas
- Vendoring walks to vendor but transaction broken

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

*Last Updated: 2026-01-31*
