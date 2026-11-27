# Upstream: CPK one-row queries left open block a mod-change rebuild (draft for the owner, nothing sent)

Written 2026-11-14 for queue ticket 0184 (account B). Rules: D95 (nothing sent upstream carries internal bug or
decision ids, project names or cherry-pick lines); nothing is sent without the owner's yes.

## Recommendation: no report is needed

Upstream has already fixed this. LoneGazebo/Community-Patch-DLL commit `14d16fa3d` "Step CPK database queries to the end
(#13467)" (JohnsterID, 2026-11-10, after Release-5.4.8; on upstream `master` at `16596561f` of 2026-11-11 in the local
fork clone) changes `CPK.Misc.CustomModOptionEnabled` from `return query(name)() ~= nil` to a loop that steps the query
to its end, and does the same for six other one-row reads (`CPK.Util.Color` GetColorByType, VP
`PlotMouseoverInclude` description getters, VP `AssignStartingPlots` ASP:IsModActive's modpack fallback, the VP EUI
compatibility `PopulateUniques` trait query, the eight `GameInfo.Defines{...}()` reads of `InfoTooltipInclude`, and
`VPUI_core`'s `BALANCE_CORE_JFD` read). Its commit message describes the same mechanism this project found
independently (proxy-design.md section 23): the statement stays on its row while the Lua state lives, and a rebuild
then fails with "database table is locked" and ends without the mods' tables and columns.

The fix reaches players with the first VP release after 5.4.8. Until then 5.4.8 players can meet the end described
below; nothing needs to be sent for that either, as upstream knows the cause.

## Optional: a short confirmation on the pull request (only if the owner wants one)

Upstream's commit message says the include-time statements "were closed before every rebuild tested" and names a save
load with a different DLC list as the trigger it reproduced. A comment with a second, independent trigger and the
SQLite detail may help the reviewers. Text, D95-clean:

> Confirmed independently on 5.4.8 (CP v151, release DLL, CP + VP + EUI only). After a save is loaded and the player
> exits to the main menu, the main menu holds eight `SELECT NULL FROM CustomModOptions WHERE Name = ? AND Value = 1
> LIMIT 1` statements (VDBE pc 17, one per front-end Lua state that included CPK) and the `GetColorByType` statement of
> the game's load screen (`... FROM Colors ... LIMIT 1`, pc 32) left stepped on the game database connection. Changing
> the mod set from there makes the deactivation fail `DETACH Localization` ("database Localization is locked": SQLite
> keeps a btree in a read transaction while other statements of the connection are active, `btreeEndTransaction`).
> When the Colors statement is still alive at the next activation, every `DROP TABLE` of VP's work tables fails with
> "database table is locked" (`OP_Destroy`) and the database ends without VP's columns (836 tables, about 31,200 rows
> instead of 832 tables, 43,682 rows); the DLL then stops at the first missing column. We also saw the same end once
> right after the Mods menu (Back on the single-player mods screen, then NEXT), with "no such column: ConsecutiveEras"
> for `Building_ThemingBonuses`. With the files of this pull request in place the same sequence leaves no statement
> stepped at any point and both rebuilds go through.

Every sentence above is backed by proxy-design.md sections 23.3, 23.6 and 23.7 (2026-11-14). One of the observed ends
(836 tables) was on a build that runs the DLL out of process; the stock-DLL end with the dialog is 0174's workstation
run. If the owner wants the comment, they may prefer to drop the out-of-process detail and keep only the stock observations.

## What the claims rest on

- The eight stepped statements, their SQL, pc 17 and the shared connection: full dumps scanned for SQLite 3.7.17 `Vdbe`
  objects (proxy-design.md 23.3, 23.3a; `shell/evidence/0174/excerpts/`), and live scans in 0184 (23.7).
- The SQLite source lines: the 3.7.17 amalgamation the project's own database build uses (23.3).
- The dialog text: `shell/evidence/0174/excerpts/s1-dialog.txt` (stock route, plain release build of the DLL).
