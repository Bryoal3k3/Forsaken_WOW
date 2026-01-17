# Current Bug Tracker

## Status: RESOLVED

---

## Problem Summary
Caster bots (Mage, Warlock, Priest) stand at spawn point targeting mobs but never cast, move, or turn.

## Symptoms
- Caster bots spawn and target nearby mobs
- They don't turn to face the target
- They don't cast any spells
- They don't move at all
- Melee classes work fine (they run in and attack)
- Hunters work fine (they move to range and shoot)

## Investigation

### Hypothesis
Casters are out of spell range. They call `Attack(target, false)` which sets the victim but doesn't initiate movement. Since they're at spawn (potentially 50-150 yards from mobs), all spell casts fail the range check in `CanTryToCastSpell()`.

### Code Flow
```
GrindingStrategy::Update()
    └─> FindGrindTarget() - finds mob at 50-150 yards
    └─> BotCombatMgr::Engage()
        └─> MageCombat::Engage()
            └─> Attack(pTarget, false) - sets victim, NO MOVEMENT
            └─> returns true (engaged)

RandomBotAI::UpdateAI()
    └─> me->GetVictim() exists, so enters combat AI
    └─> UpdateInCombatAI()
        └─> BotCombatMgr::UpdateCombat()
            └─> MageCombat::UpdateCombat()
                └─> CanTryToCastSpell(Frostbolt)
                    └─> dist (50-150) > maxRange (30) → returns FALSE
                └─> All spells fail range check, nothing happens
```

### Key Files
| File | Relevance |
|------|-----------|
| `Combat/Classes/MageCombat.cpp` | Mage Engage() missing MoveChase |
| `Combat/Classes/WarlockCombat.cpp` | Warlock Engage() missing MoveChase |
| `Combat/Classes/PriestCombat.cpp` | Priest Engage() missing MoveChase |
| `Combat/Classes/HunterCombat.cpp` | Working example - uses MoveChase(target, 25.0f) |
| `CombatBotBaseAI.cpp:2794` | Range check in CanTryToCastSpell() |

### Debug Logging Added
| File | Location | Tag | Purpose |
|------|----------|-----|---------|
| (none needed) | | | |

## Findings

**Comparison of Engage() implementations:**

| Class | Engage() Code | Result |
|-------|---------------|--------|
| Warrior (melee) | `Attack(true)` + `MoveChase()` | Works - runs to target |
| Hunter (ranged) | `Attack(false)` + `MoveChase(target, 25.0f)` | Works - moves to 25 yards |
| Mage (caster) | `Attack(false)` only | BROKEN - no movement |
| Warlock (caster) | `Attack(false)` only | BROKEN - no movement |
| Priest (caster) | `Attack(false)` only | BROKEN - no movement |

### Root Cause
**Location:** `MageCombat::Engage()`, `WarlockCombat::Engage()`, `PriestCombat::Engage()`
**Problem:** Casters call `Attack(pTarget, false)` but never move toward the target
**Why:** Without `MoveChase()`, casters stay at spawn point. `CanTryToCastSpell()` correctly returns false because they're out of spell range (30-35 yards for most spells, bot is 50-150 yards away).

## Fix

### Solution
Add `MoveChase(pTarget, 28.0f)` to caster Engage() methods, similar to Hunter. The 28-yard distance provides buffer for typical 30-35 yard spell ranges.

### Files Modified
- `src/game/PlayerBots/Combat/Classes/MageCombat.cpp`
- `src/game/PlayerBots/Combat/Classes/WarlockCombat.cpp`
- `src/game/PlayerBots/Combat/Classes/PriestCombat.cpp`

### Verification
1. Build and run server
2. Observe a Mage, Warlock, or Priest bot
3. Bot should now move toward target before casting
4. Spells should start casting once in range

## Notes
- Hunter already works because it uses `MoveChase(target, 25.0f)` in Engage()
- The original design intent was "casters stand and cast to pull" but this assumed they would already be in range
- In practice, bots spawn far from mobs in starting areas

---
*Started: 2025-01-16*
*Resolved: 2025-01-16*
