# Current Bug Tracker

## Status: NEXT SESSION

---

## Problem Summary
Ranged classes (casters/hunters) try to kite at level 1 but mobs are slightly faster than players, causing them to constantly run away instead of fighting.

## Symptoms
- Ranged bots engage a mob then start running backwards
- Mobs are ~slightly faster than player run speed
- Bots never stop to cast, just keep running
- Eventually get caught and die or run forever

## Investigation

### Hypothesis
The ranged combat strategy likely has logic to maintain distance from melee mobs. At low levels without speed boosts, this creates an unwinnable kiting situation.

### Code Flow
```
MageCombat::UpdateCombat() / HunterCombat::UpdateCombat()
    └─> Check if mob is too close?
    └─> Try to create distance?
    └─> Never stops to cast because always "too close"
```

### Key Files
| File | Relevance |
|------|-----------|
| `Combat/Classes/MageCombat.cpp` | Mage combat logic |
| `Combat/Classes/WarlockCombat.cpp` | Warlock combat logic |
| `Combat/Classes/PriestCombat.cpp` | Priest combat logic |
| `Combat/Classes/HunterCombat.cpp` | Hunter combat logic |
| `CombatBotBaseAI.cpp` | Base combat utilities |

### Debug Logging Added
| File | Location | Tag | Purpose |
|------|----------|-----|---------|
| | | | |

## Findings
<!-- To be filled in next session -->

### Root Cause
**Location:** TBD
**Problem:** TBD
**Why:** TBD

## Fix

### Solution
Possible approaches:
1. Remove kiting logic entirely at low levels
2. Stand and fight when no escape abilities available
3. Only kite when snare/root is available (Frost Nova, Concussive Shot, etc.)

### Files Modified
-

### Verification
1. Build and run server
2. Observe low-level ranged bots in combat
3. Verify they stand and cast instead of endlessly running

## Notes
- This is a combat strategy issue, not a bug per se
- At higher levels with snares/roots, kiting may be viable
- Consider level-based or ability-based combat strategy selection

---
*Started: 2025-01-17*
*Resolved:*
