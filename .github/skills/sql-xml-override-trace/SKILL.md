---
name: sql-xml-override-trace
description: "Trace Civ V SQL/XML database ownership and override chain. Use when balancing values, finding where a row is defined or overridden, resolving load-order conflicts, or when a feature seems missing in Community Patch, Vox Populi, EUI compatibility, or vanilla Civ V defaults. Keywords: modinfo, OnModActivated, UpdateDatabase, Dependencies, Type key, table row, override, load order."
argument-hint: "Provide mechanic name, table/Type keys, and whether EUI variant is in scope."
user-invocable: true
disable-model-invocation: false
---

# SQL XML Override Trace

## When To Use
- A value exists but you need the final effective source.
- SQL/XML row is hard to find across (1), (2), and (3a).
- A feature appears missing and you must decide between vanilla default vs new DLL work.

## Procedure
1. Confirm install variant and scope.
- Check if EUI variant is relevant before touching (3a).

2. Read minimal load-order anchors first.
- (1) modinfo: Dependencies and OnModActivated.
- (2) modinfo: Dependencies and OnModActivated.
- (3a) modinfo only for EUI tasks.

3. Trace by stable keys.
- Search by Type, ID, or table key in (1) and (2) first.
- Use targeted reads around matches, not whole files.

4. Resolve missing-feature cases.
- If not found in repo SQL/XML, check vanilla Civ V SQL/XML as read-only reference.
- If not found there either, route to DLL implementation workflow.

5. Return a compact decision.
- Report ownership layer, final override location, and next edit file(s).

## Constraints
- Never modify Sid Meier's Civilization V install files.
- Never modify UI_bc1 files.
- Keep reads small and targeted.
