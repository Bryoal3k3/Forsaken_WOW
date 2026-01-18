# Current Bug Tracker

## Status: ✅ FIXED AND VERIFIED

---

## Problem Summary
Ranged classes (casters/hunters) try to kite at level 1 but mobs are slightly faster than players, causing them to constantly run away instead of fighting.

## Symptoms
- Ranged bots engage a mob then start running backwards
- Mobs are ~slightly faster than player run speed
- Bots never stop to cast, just keep running
- Eventually get caught and die or run forever

## Investigation

### Root Cause
**Location:** `Engage()` methods in ranged combat handlers
**Problem:** `MoveChase(pTarget, 28.0f)` creates a continuous movement generator that tries to maintain 28 yards distance. When mob chases the bot, the generator keeps repositioning the bot backward.
**Why:** At low levels, players have no speed boosts and mobs are slightly faster, creating an unwinnable kiting loop.

### Code Flow
```
MageCombat::Engage() / HunterCombat::Engage()
    └─> MoveChase(target, 28.0f)  // Maintain 28 yard distance
        └─> Movement generator continuously repositions
            └─> Mob chases, bot backs up, repeat forever
```

### Key Discovery
- `pVictim->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED)` - checks if target is snared
- `pVictim->HasAuraType(SPELL_AURA_MOD_ROOT)` - checks if target is rooted
- PartyBotAI already uses this pattern but doesn't check before kiting

---

## Fix

### Solution Implemented
**Approach:** Only kite when target is snared/rooted. Otherwise, stand and fight.

Added snare check to all ranged combat handlers:
```cpp
bool targetIsSnared = pVictim->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED) ||
                      pVictim->HasAuraType(SPELL_AURA_MOD_ROOT);

if (!targetIsSnared && pBot->IsMoving())
{
    pBot->StopMoving();
    pBot->GetMotionMaster()->Clear();
}
```

**Hunter-specific:** Added melee fallback for deadzone (< 8 yards where ranged attacks don't work):
- Wing Clip (applies snare - allows backing off after)
- Mongoose Bite
- Raptor Strike

### Files Modified
| File | Changes |
|------|---------|
| `Combat/Classes/MageCombat.cpp` | Added snare check before kiting |
| `Combat/Classes/HunterCombat.cpp` | Added snare check + melee attacks for deadzone |
| `Combat/Classes/WarlockCombat.cpp` | Added snare check before kiting |
| `Combat/Classes/PriestCombat.cpp` | Added snare check before kiting |

### Verification
1. Build and run server
2. Observe low-level ranged bots in combat
3. Verify they stand and cast instead of endlessly running
4. Verify hunters use melee attacks when mobs are in deadzone
5. Verify bots DO kite after applying snares (Frost Nova, Wing Clip, etc.)

---

## Notes
- Snare-applying spells: Frost Nova (Mage), Frostbolt chill (Mage), Wing Clip (Hunter), Concussive Shot (Hunter)
- Priests/Warlocks have limited snares at low levels - they will mostly stand and fight
- At higher levels with more CC options, kiting becomes viable automatically

---
*Started: 2025-01-17*
*Fix Implemented: 2025-01-17*
*Tested:* ✅ VERIFIED 2025-01-17
