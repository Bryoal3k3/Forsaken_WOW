# Current Bug Tracker

## Status: 1 Low-Priority Bug Remaining

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

## How to Use This File

When a new bug is discovered:
1. Add it here with status "INVESTIGATING" or "FIX IN PROGRESS"
2. Document symptoms, investigation steps, root cause
3. Track fix implementation and testing
4. Move to `docs/archive/BUG_HISTORY.md` when verified fixed

---

*Last Updated: 2026-01-31*
