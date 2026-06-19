---
name: dll-mechanic-implementation
description: "Implement or extend Civ V gameplay mechanics in GameCoreDLL C++ when SQL/XML is insufficient or absent. Use for AI logic changes, hardcoded rule changes, new mechanic hooks, and wiring C++ features to database options and localization. Keywords: CvGameCoreDLL_Expansion2, CustomMods.h, CustomModsGlobal.h, NewCustomModOptions.xml, CommunityOptions.sql, AI behavior, serialization risk."
argument-hint: "Provide mechanic behavior, target C++ classes/functions, and expected DB/text knobs."
user-invocable: true
disable-model-invocation: false
---

# DLL Mechanic Implementation

## When To Use
- Mechanic does not exist in repo or vanilla SQL/XML.
- Behavior is hardcoded and requires C++ updates.
- AI must be adjusted to understand new or changed mechanics.

## Procedure
1. Confirm DLL path is necessary.
- Verify SQL/XML absence first (or explicit hardcoded behavior).

2. Pick minimal owner surfaces.
- Identify core logic class and any AI class impacted.
- Avoid broad refactors unless requested.

3. Implement mechanic safely.
- Keep interfaces and save-sensitive structures stable when possible.
- Add guards/toggles if appropriate.

4. Wire DB and options.
- Add minimal required DB/text rows for visibility/tuning.
- If feature is toggleable, align C++ option checks with DB entries (CustomMods and related tables/files).

5. Validate impact.
- Check compile feasibility and obvious DB/schema consistency.
- Note multiplayer/desync and serialization risks when relevant.

## Constraints
- Never modify Sid Meier's Civilization V install files.
- Never modify UI_bc1 files.
- Keep changes scoped to the mechanic and directly affected AI logic.
