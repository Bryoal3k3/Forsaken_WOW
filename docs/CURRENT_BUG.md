# Current Bug Tracker

## Status: 3 Active Bugs

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

**Status**: ACTIVE (Medium Priority)

**Symptom**: Console error every time a Paladin bot casts Judgement:
```
ERROR: CastSpell: unknown spell id 0 by caster: Player Stor (Guid: 80).
```

**Root Cause**:
In vanilla WoW, Judgement **consumes** the active Seal. After Judgement fires, the Paladin has no Seal active. `PaladinCombat::UpdateOutOfCombat()` re-applies the Seal, but this only runs when out of combat. During combat, `UpdateCombat()` runs instead, and there's no Seal re-application after Judgement consumes it. The consumed Seal's spell pointer likely becomes invalid (spell ID 0), and subsequent code tries to reference it.

**Impact**:
- Console error spam (one per Judgement cast)
- Paladin loses Seal passive benefit (e.g., Seal of Righteousness extra holy damage) for remainder of fight
- DPS loss for Paladin bots

**Fix**:
Add Seal re-application inside `PaladinCombat::UpdateCombat()` after the Judgement cast, or check if Seal is active before casting Judgement-related follow-ups.

**Related Code**:
- `Combat/Classes/PaladinCombat.cpp` - Paladin combat handler
- `UpdateCombat()` line 33-37 - Judgement cast
- `UpdateOutOfCombat()` line 76-81 - Seal application (only runs out of combat)

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
