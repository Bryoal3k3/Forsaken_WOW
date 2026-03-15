# Current Bug Tracker

## Status: 3 Active Bugs, 3 Recently Fixed

For fixed bug history, see `docs/archive/BUG_HISTORY.md`

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

## Bug #19: Paladin Judgement Casts Spell ID 0

**Status**: PARTIALLY FIXED (Medium Priority)

**Symptom**: Console error when Paladin bot Judgements without a Seal:
```
ERROR: CastSpell: unknown spell id 0 by caster: Player Hulfain (Guid: 20)
```

**Root Cause**:
Judgement consumes the active Seal. The engine looks up the Seal's "judgement effect" spell — if no Seal is active, it gets spell ID 0.

**Fix Applied (2026-03-15)**:
- Added Seal re-apply at top of `UpdateCombat()` when Seal is missing
- Added `HasAura(Seal)` guard on Judgement block — won't cast Judgement unless Seal is active

**Current Status**:
Fix reduced bouncing but errors still fire (188 in latest run). The Seal guard may be bypassed when Judgement and Seal are on the same GCD tick, or the vanilla Judgement spell internally checks/consumes the Seal in a way that races with our HasAura check. Needs deeper investigation into the spell chain mechanics.

**Related Code**:
- `Combat/Classes/PaladinCombat.cpp` - Seal re-apply + Judgement guard

---

## Bug #20: Server Auto-Restart from Honor System

**Status**: RESOLVED (Config Fix)

**Symptom**: Server shuts down after ~15 minutes of runtime with no crash or error. Last console output shows normal bot activity then `Shutting down world...`

**Root Cause**:
`AutoHonorRestart = 1` in `mangosd.conf` causes the Honor system (`HonorMgr.cpp:589`) to call `ShutdownServ(900, ...)` on startup when honor rank recalculation is needed. This sets a 15-minute countdown timer that gracefully shuts down the server when it expires. It's not a crash -- it's an intentional scheduled restart.

**Fix Applied**:
Set `AutoHonorRestart = 0` in `mangosd.conf`. Server now runs indefinitely.

**Investigation Notes**:
- ASAN showed only shutdown-time leaks, no crash reports (because it wasn't a crash)
- Anticrash handler (set to 28) was initially suspected of swallowing SIGSEGV, temporarily set to 0
- GDB confirmed no signals were raised -- `World::StopNow` breakpoint was never hit
- Shutdown went through `ShutdownServ` timer path which sets `m_stopEvent = true` directly in `World::Update()`
- `MaxCoreStuckTime = 0` (disabled), no OOM in dmesg

---

## Bug #21: _SaveInventory Null Item Pointer

**Status**: ACTIVE (Low Priority — cosmetic)

**Symptom**: Periodic error during character save:
```
ERROR: _SaveInventory - Null item pointer found in item update queue of Player Tak (Guid: 27).
```

**Impact**: Log spam only. No crash, no data loss. The save function skips null entries and continues.

**Investigation**:
- 407 errors across 30+ bots in a 51-minute run
- Initially suspected vendoring (`RemoveItem` not cleaning update queue). Fixed sell pattern to match real `HandleSellItemOpcode` exactly (`RemoveFromUpdateQueueOf` + `AddItemToBuyBackSlot`). Error count did not decrease.
- Likely caused by other item operations: `AutoStoreLoot`, `RewardQuest`, or item creation/destruction elsewhere. The null appears when an item is freed/invalidated between being queued and `_SaveInventory` running.

**Related Code**:
- `Player.cpp:16861` — core code, logs and continues
- `VendoringStrategy.cpp` — sell pattern already corrected

---

## Bug #22: Quest Giver Bouncing — RESOLVED

**Status**: RESOLVED (2026-03-15)

**Was**: Bots with empty quest logs repeatedly visited the same quest giver (entry 713 had 1,151 trips in 35 min). Bot accepted 0 quests, went back to CHECKING_QUEST_LOG, found same giver, looped every 1-3 seconds.

**Fix**: Exhausted giver tracking in QuestingActivity. After 0 quests accepted, giver entry is marked exhausted and skipped in future searches. 5-minute cooldown, clears on level-up. `FindNearestQuestGiver` accepts skip set.

**Result**: 1,151 → 28 trips (97% reduction).

---

## Bug #23: GO Item Loot Infinite Loop — RESOLVED

**Status**: RESOLVED (2026-03-15)

**Was**: Bots stuck in infinite loop interacting with Cactus Apple (328 interactions, zero items). `LootObject` used wrong loot type and never marked items as looted, so GO stayed at GO_ACTIVATED. Bot kept finding the despawned GO and retrying.

**Fix**: Changed `LOOT_SKINNING` → `LOOT_CORPSE`. Force `unlootedCount=0` after AutoStoreLoot. Added `CanInteractWith` gate before loot attempt — if GO not interactable, immediately relocate to different spawn.

**Result**: 328 spam interactions → 0. Proper relocations working, quests completing.

---

## Bug #24: Vendor Repair Infinite Loop — RESOLVED

**Status**: RESOLVED (2026-03-15)

**Was**: Bot Garencan triggered "needs to vendor (gear broken: yes)" 46 times but never repaired. `DurabilityRepairAll(true)` silently failed when bot couldn't afford it. Gear stayed broken → vendoring re-triggered → loop.

**Fix**: `DurabilityRepairAll(false, 0.0f)` — free repairs for bots (consistent with free spell training). Removed `IsArmorer()` gate.

**Result**: 46 loop triggers → 7 triggers with 54 successful repairs, no loops.

---

## Questing System Bugs

See `docs/Quest_Implementation/KNOWN_ISSUES.md` for comprehensive tracking of all questing-related bugs, limitations, and untested features.

---

## Future Enhancements (Not Bugs)

### Console Timestamps
- **Status**: RESOLVED
- Set `LogTime = 1` in `mangosd.conf`

### Road/Path Following for Travel
- **Status**: Deferred
- **Issue**: Bots travel in straight lines, may path through high-level mob areas
- **Solution**: Create `travel_routes` table with hand-placed road waypoints

### Race Distribution Configuration
- **Status**: Deferred
- **Issue**: Class-first selection leads to uneven race distribution
- **Solution**: Add config options for race/class probability weights

---

## How to Use This File

When a new bug is discovered:
1. Add it here with status "INVESTIGATING" or "FIX IN PROGRESS"
2. Document symptoms, investigation steps, root cause
3. Track fix implementation and testing
4. Move to `docs/archive/BUG_HISTORY.md` when verified fixed

---

*Last Updated: 2026-03-15*
