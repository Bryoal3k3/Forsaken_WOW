# Current Bug Tracker

## Status: NO ACTIVE BUGS

All known issues have been resolved. System is stable.

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

*Last Updated: 2025-01-17*
