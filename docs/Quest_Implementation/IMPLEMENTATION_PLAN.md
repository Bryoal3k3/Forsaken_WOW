# Questing System Implementation Plan

**Status**: Phases R1-Q6 Complete + GO Item Looting
**Created**: 2026-03-14
**Last Updated**: 2026-03-15

---

## Completed Phases

| # | Phase | Commit | Description |
|---|-------|--------|-------------|
| 1 | R1 | 1f19dc9 | Activity tier system refactor (IBotActivity + GrindingActivity) |
| 2 | Q1 | 259d287 | Quest caches (givers, turn-ins, item drops) |
| 3 | Q2 | 4cef88b | QuestingActivity skeleton (accept/turn-in loop) |
| 4 | Q3 | 227f226 | Kill quest objective completion |
| 5 | Q4 | 6b73365 | Collect quest objectives (mob drops + closest spawn fix) |
| 6 | Q5 | 381ec8d | Object interaction + GO quests + source items + bounce fix |
| 7 | Q6 | ba76bc6 | Exploration quest support |
| 8 | -- | bb25b7a | GO item looting + mob relocation + stuck fixes |

## Known Issues

- **GO spawn cycling**: Bot keeps going to same despawned GO spawn. Needs relocate-after-timeout like mob areas.
- **Quest giver bouncing**: Mostly fixed, but some edge cases remain (Zinxy, Rokoli, Niss cycling at entry 713). These bots have non-actionable quests with no kill/collect/GO/explore objectives.
- **Grumdu stuck**: Bot repeatedly stuck at invalid position, teleports home, gets stuck again. Separate from questing — likely a map/navmesh issue.

## Remaining Phases

| # | Phase | Description | Priority |
|---|-------|-------------|----------|
| 1 | Q7 | Item usage quests (use items on targets/self/location) | Medium |
| 2 | Q8 | Quest log management (soft timeout 15min, abandon grey/stale) | High |
| 3 | Q9 | Multi-quest overlap optimization | Low |
| 4 | Q10 | Opportunistic features (item-start quests, nearby pickup) | Low |
| 5 | Q11 | Reward selection (class-based stat heuristic) | Low |
| 6 | Q12 | Weighted activity system (configurable quest/grind ratio) | Medium |
| 7 | -- | GO spawn relocation (same pattern as mob area timeout) | Medium |
| 8 | -- | Gameobject item quests from non-chest types (goobers, etc.) | Low |

---

*Last Updated: 2026-03-15*
