# RandomBot AI Code Audit Checklist

**Audit Date:** 2026-01-24
**Target Scale:** 100 - 3000 concurrent bots
**Status:** 0/12 issues resolved

---

## Critical Priority (Performance Blockers)

### [ ] 1. Database Query in Hot Path
**File:** `src/game/PlayerBots/Strategies/TravelingStrategy.cpp:199-231`
**Impact:** 300+ DB queries/second at scale

**Problem:**
```cpp
// Called every time a bot needs to travel - hits database
std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
    "SELECT x, y, z, name FROM grind_spots WHERE map_id = %u ..."));
```

**Fix Options:**
| Option | Description | Pros | Cons |
|--------|-------------|------|------|
| A | Cache grind spots at startup (like VendoringStrategy) | Fast lookups, zero runtime queries | Memory usage, stale if DB changes |
| B | Lazy cache with TTL | Fresh data, lower memory | First queries still hit DB |
| C | Pre-compute per-race/level at startup | Instant lookup | More complex initialization |

**Recommended:** Option A
**Assigned To:**
**PR Link:**
**Review Notes:**

---

### [ ] 2. Grid Search Every Tick
**File:** `src/game/PlayerBots/Strategies/GrindingStrategy.cpp:155-182`
**Impact:** 3000 grid searches/second at scale

**Problem:**
```cpp
// Called every 1000ms per bot - searches 150 yard radius
std::list<Creature*> creatures;
AllCreaturesInRange check(pBot, range);
MaNGOS::CreatureListSearcher<AllCreaturesInRange> searcher(creatures, check);
Cell::VisitGridObjects(pBot, searcher, range);  // EXPENSIVE
```

**Fix Options:**
| Option | Description | Pros | Cons |
|--------|-------------|------|------|
| A | Exponential backoff when no mobs found | Reduces idle searches | Slower reaction on respawn |
| B | Tiered radius (50 yards first, expand if empty) | Smaller searches most of time | More code complexity |
| C | Shared search cache for bots in same area | Major reduction | Coordination complexity |
| D | Increase interval to 2000ms when idle | Simple 50% reduction | Slower response |

**Recommended:** Option A + B combined
**Assigned To:**
**PR Link:**
**Review Notes:**

---

### [ ] 3. Dynamic Cast in Hot Path
**File:** `src/game/PlayerBots/Strategies/GrindingStrategy.cpp:68`
**Impact:** RTTI lookup every target acquisition

**Problem:**
```cpp
// dynamic_cast is slow - called every time a target is found
if (RandomBotAI* pAI = dynamic_cast<RandomBotAI*>(pBot->AI()))
{
    if (pAI->GetCombatMgr())
        pAI->GetCombatMgr()->Engage(pBot, pTarget);
}
```

**Fix Options:**
| Option | Description | Pros | Cons |
|--------|-------------|------|------|
| A | Store RandomBotAI* pointer at GrindingStrategy construction | Zero runtime cost | Requires constructor change |
| B | Use static_cast (type is guaranteed) | Fast | Less safe |
| C | Add virtual GetCombatMgr() to base AI class | Type-safe, no cast | Interface change |

**Recommended:** Option A
**Assigned To:**
**PR Link:**
**Review Notes:**

---

## High Priority (Performance)

### [ ] 4. List Allocation Every Search
**File:** `src/game/PlayerBots/Strategies/GrindingStrategy.cpp:158`
**Impact:** 3000+ heap allocations/second

**Problem:**
```cpp
// New list allocated every 1000ms per bot
std::list<Creature*> creatures;
```

**Fix Options:**
| Option | Description | Pros | Cons |
|--------|-------------|------|------|
| A | Make member variable, clear() between uses | Reuse allocation | Slight memory per bot |
| B | Use std::vector with reserve() | Better cache locality | Need capacity estimate |

**Recommended:** Option A
**Also Apply To:** `LootingBehavior.cpp:131` (same issue)
**Assigned To:**
**PR Link:**
**Review Notes:**

---

### [ ] 5. Debug PathFinder in Production
**File:** `src/game/PlayerBots/Strategies/VendoringStrategy.cpp:469-496`
**Impact:** Unnecessary path calculation every vendor trip

**Problem:**
```cpp
// Debug block creates PathFinder and calculates path just for logging
{
    PathFinder pathDebug(pBot);  // EXPENSIVE
    pathDebug.calculate(m_targetVendor.x, m_targetVendor.y, m_targetVendor.z, false);
    // ... 20 lines of debug logging
}
```

**Fix Options:**
| Option | Description |
|--------|-------------|
| A | Wrap in `#ifdef DEBUG` or `#ifndef NDEBUG` |
| B | Remove entirely (was for debugging tree collision issue, now fixed) |
| C | Gate behind runtime config flag |

**Recommended:** Option B (issue it was debugging is resolved)
**Assigned To:**
**PR Link:**
**Review Notes:**

---

## Medium Priority (Bugs / Safety)

### [ ] 6. Pre-Travel Vendor Threshold Mismatch
**File:** `src/game/PlayerBots/Strategies/TravelingStrategy.cpp:48-49`
**Impact:** Bots get stuck when bags >60% or durability <50%
**Tracked In:** `docs/CURRENT_BUG.md`

**Problem:**
```cpp
// TravelingStrategy yields at these thresholds:
if (VendoringStrategy::GetLowestDurabilityPercent(pBot) < 0.5f ||  // 50%
    VendoringStrategy::GetBagFullPercent(pBot) > 0.6f)              // 60%
    return false;  // Yields expecting VendoringStrategy to handle it

// BUT VendoringStrategy only triggers at:
return AreBagsFull(pBot) || IsGearBroken(pBot);  // 100% or 0%
```

**Fix Options:**
| Option | Description |
|--------|-------------|
| A | Wire up VendoringStrategy::ForceStart() from TravelingStrategy |
| B | Lower VendoringStrategy thresholds to match |
| C | Add configurable thresholds to both strategies |

**Recommended:** Option A
**Assigned To:**
**PR Link:**
**Review Notes:**

---

### [ ] 7. Vendor Cache Thread Safety
**File:** `src/game/PlayerBots/Strategies/VendoringStrategy.cpp`
**Impact:** Potential race condition during startup

**Problem:**
```cpp
static std::vector<VendorLocation> s_vendorCache;
static std::mutex s_cacheMutex;  // Only used in BuildVendorCache()

// FindNearestVendor reads WITHOUT lock:
for (VendorLocation const& loc : s_vendorCache)  // RACE if cache building
```

**Fix Options:**
| Option | Description |
|--------|-------------|
| A | Verify cache is fully built before any bot spawns (may already be true) |
| B | Add read lock to FindNearestVendor() |
| C | Use std::shared_mutex for read-write locking |

**Recommended:** Option A (verify current ordering in PlayerBotMgr::Load)
**Assigned To:**
**PR Link:**
**Review Notes:**

---

## Low Priority (Code Quality)

### [ ] 8. Dead Code: GetNextFreeCharacterGuid()
**File:** `src/game/PlayerBots/RandomBotGenerator.cpp:313-324`
**Impact:** None (cleanup only)

**Problem:**
```cpp
// This method exists but is NEVER called
uint32 RandomBotGenerator::GetNextFreeCharacterGuid()
{
    std::unique_ptr<QueryResult> result = CharacterDatabase.PQuery(
        "SELECT MAX(guid) FROM characters");
    // ...
}
```

**Fix:** Delete method and declaration in header
**Assigned To:**
**PR Link:**
**Review Notes:**

---

### [ ] 9. Code Duplication: Caster Engage()
**Files:**
- `src/game/PlayerBots/Combat/Classes/MageCombat.cpp:21-39`
- `src/game/PlayerBots/Combat/Classes/PriestCombat.cpp:21-39`
- `src/game/PlayerBots/Combat/Classes/WarlockCombat.cpp:21-39`

**Problem:** Identical 19-line Engage() method in 3 files.

**Fix Options:**
| Option | Description |
|--------|-------------|
| A | Create CasterCombatBase class |
| B | Create free function `bool EngageCaster(Player*, Unit*, CombatBotBaseAI*)` |
| C | Leave as-is (acceptable duplication for clarity) |

**Recommended:** Option B (simple helper function)
**Assigned To:**
**PR Link:**
**Review Notes:**

---

### [ ] 10. Code Duplication: Caster Movement Logic
**Files:**
- `MageCombat.cpp:47-59`
- `PriestCombat.cpp:47-59`
- `WarlockCombat.cpp:47-59`
- `HunterCombat.cpp:47-59`

**Problem:** Identical snare-check/stop-movement block in 4 files:
```cpp
bool targetIsSnared = pVictim->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED) ||
                      pVictim->HasAuraType(SPELL_AURA_MOD_ROOT);
float dist = pBot->GetDistance(pVictim);
bool inCastRange = dist <= 30.0f;
if (inCastRange && !targetIsSnared && pBot->IsMoving())
{
    pBot->StopMoving();
    pBot->GetMotionMaster()->Clear();
}
```

**Fix Options:**
| Option | Description |
|--------|-------------|
| A | Extract to helper: `void HandleRangedMovement(Player*, Unit*)` |
| B | Create RangedCombatBase class |

**Recommended:** Option A
**Assigned To:**
**PR Link:**
**Review Notes:**

---

### [ ] 11. Code Duplication: Melee Engage()
**Files:**
- `WarriorCombat.cpp:21-34`
- `RogueCombat.cpp:21-34`
- (Likely also Paladin, Shaman, Druid)

**Problem:** Near-identical Engage() for melee classes.

**Fix:** Create helper `bool EngageMelee(Player*, Unit*)` if addressing #9
**Assigned To:**
**PR Link:**
**Review Notes:**

---

### [ ] 12. Memory Leak: m_generatedNames Never Cleared
**File:** `src/game/PlayerBots/RandomBotGenerator.cpp`
**Impact:** Minor - only grows on purge/regenerate cycles

**Problem:**
```cpp
std::vector<std::string> m_generatedNames;  // Names added but never removed

// In GenerateUniqueBotName():
m_generatedNames.push_back(name);  // Grows forever
```

**Fix:** Clear `m_generatedNames` at start of `GenerateRandomBots()` or after generation completes
**Assigned To:**
**PR Link:**
**Review Notes:**

---

## Completion Tracking

| Priority | Total | Done | Remaining |
|----------|-------|------|-----------|
| Critical | 3 | 0 | 3 |
| High | 2 | 0 | 2 |
| Medium | 2 | 0 | 2 |
| Low | 5 | 0 | 5 |
| **Total** | **12** | **0** | **12** |

---

## Review Process

1. Developer claims issue by adding name to "Assigned To"
2. Create branch: `fix/audit-{issue-number}-{short-description}`
3. Implement fix following recommended option (or propose alternative)
4. Create PR, link in "PR Link" field
5. Get review from at least one other dev
6. Add review notes/decisions to "Review Notes"
7. After merge, mark checkbox `[x]` and update tracking table

---

*Last Updated: 2026-01-24*
