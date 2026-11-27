# The export package for the owner's read (ticket 4707, package E6, 2026-11-25)

Plan: `../07-architecture-decisions/architect-2026-11-17-upstream-export-plan.md` 4.5, 4.9 and 5 (E6); beta 5 plan
`docs/BETA5-PUBLIC-PLAN.md` (D257). One command makes, from one revision of this repository and one DLL branch of the
fork, everything the owner reads before the public source and the upstream pull request go out: a dry run of the
export into a throwaway folder, the file list, the two commits of the fork's `x64` branch made locally, both commit
messages, the pull request text and the public README. **Nothing is pushed, tagged, written to the ledger or sent**:
publication is ticket 0760, the MC's, on the owner's yes (D27, D95, D150).

Marks: `[C]` read in code or a run's output, `[L]` seen in a run, `[I]` inferred.

## 1. The command

```
export C5_EXPORT_AUTHOR="<name> <<id>+<login>@users.noreply.github.com>"   # never committed: the owner's identity
python -I -X utf8 tools/export/package.py --rev <REV> --dll-branch <DLL branch> --out C:/c5-64bit-work/<t>/package \
       [--fork-clone <own clone>] [--fetch] [--allow-findings] [--base Release-5.5] [--date <ISO>]
```

- `--rev`: the revision of this repository (the cut commit on Friday, C5).
- `--dll-branch`: the fork branch whose commits since `--base` become commit 1 (0762's branch). Looked up in the clone
  as given, then as `origin/<name>`; `--fetch` fetches it from the clone's `origin` first.
- `--out`: a new or empty folder outside this repository and outside the clones folder (setting `clones_dir`).
- `--fork-clone`: a local clone of your own. Without it the command clones the fork setting's clone
  (`tools/x64-dll/dll_source.py`, `deps/dll-source.ini` `fork`) into `OUT/fork` with `git clone --local --no-checkout`
  (hardlinked objects, seconds), sets `origin` to the `fork_url` setting and disables every push address
  (`no-push://disabled-by-package.py`). A `--fork-clone` that is, or lies inside, either long-lived clone (the `src`
  and `fork` settings) or lies under the clones folder is refused.
- The author and committer of both commits: `--author`, else the environment variable `C5_EXPORT_AUTHOR`; it must be
  a GitHub no-reply address (plan 4.4 point 6). It is never written in the repository (privacy rule of the brief;
  `tools/release/settings.py` refuses unknown keys in `deps/local-settings.ini`, so it is not a settings key either).
- `--date`: author and committer date of both commits; with the same inputs and date the two hashes repeat (tested).
- `--allow-findings`: package although the export check finds private content; `summary.txt` then starts with
  NOT AN EXPORT and `stage/64bit/NOT-AN-EXPORT.txt` is written (export.py's marker; not in the manifest, not in
  commit 2).

Exit codes: 0 done, 1 refused by a check (the export check's findings, the public-text check), 2 usage, target or git
error. Refusals before the export leave `--out` uncreated; no refusal leaves a branch in the clone.

## 2. What it does, in order [C: code and tests, section 5]

1. Arguments: `--out`, the author, `--date`, the fork clone (section 1).
2. The texts `tools/export/drafts/commit-1.txt`, `commit-2.txt`, `pr.md` (and `owner-read.md`) as committed at `--rev`
   (or `--drafts DIR`). The three public ones go through the **public-text check** before anything is written, and
   again after their `{{placeholders}}` are filled (`files`, `files_product`, `files_notes`, `base`; an unknown one
   is exit 2): numbers of four or more digits (ids, dates; write a count as `1,551`; the one allowed context is
   `_MSC_VER >= 1900`), `D<number>`, `lead-`/`impl-`/`oneoff-`/`dispatcher-`/`team-`, `MC`, the words ticket,
   tracker, epic, architect, coordinator, jira, lightbearer, the project's own names (`c5-64bit`, `c5h-`,
   `civ5-headless`), cherry-pick lines, and the export check's own rules with the private terms **and** the
   environment's user and machine names (user paths, e-mail addresses except the attribution address, personal names).
   `owner-read.md` is for the owner only and may carry ids.
3. `tools/export/export.py --dry-run` of `--rev` into `OUT/stage` (the upstream layout: files under
   `OUT/stage/64bit/`), with the private terms and the environment's user and machine names. export.py's refusal
   (exit 1) is this command's exit 1 unless `--allow-findings`. *Corrected 2026-11-26, ticket 0773: an
   environment-term finding now refuses like any other (export.py accepts `--no-env-terms` only with `--dry-run
   --allow-findings`, and marks such a copy NOT AN EXPORT); was: "`export.py --dry-run --no-env-terms` ... with the
   private terms".*
4. `OUT/files.tsv`: export.py's manifest byte for byte (`path, size, sha256` per file, then `manifest-sha256`).
   `OUT/files-by-group.txt`: per group (product, notes) the count and bytes, per top folder, then the paths.
   `OUT/export-log.txt`: export.py's stderr with the check's masked output. `OUT/findings.txt`: the full masked list
   of the export check's findings (`path:line: rule: match`, private terms and, since 0773, the environment's names), which `summary.txt`
   repeats by file, so the owner sees what the revision must still clear (lead-0763, #15432). `OUT/check-env-terms.txt`: `check.py
   --summary` **with** the environment's names; its finding count goes into `summary.txt`, and since ticket 0773
   (2026-11-26) a finding there refuses (exit 1) unless `--allow-findings` (a second guard behind step 3). Was: "does
   not refuse (the owner's first name in the notes is a pending question, thread 0763 #15343, ticket 0773)".
5. In the fork clone: `--base` (default `Release-5.5`, also tried as `refs/tags/` and `upstream/`) must be an ancestor
   of the DLL branch, which must have at least one commit over it; branch `x64-export-<rev7>` must not exist.
   - **Commit 1** = `git commit-tree <DLL branch>^{tree} -p <base>`: the DLL branch's commits since the base, their
     content unchanged, as one commit (0762's branch has three, 2026-11-25; if 0762 squashes them the tree is the
     same). Its own messages go to `OUT/dll-branch-log.txt`, not into the commit; the message is `commit-1.txt`.
     When the branch has one commit, `summary.txt` (line `message`) says whether `commit-1.txt` equals that commit's
     own message, so a later change of 0762's text is seen. `commit-1.txt` is, since 2026-11-25 17:20, the message of
     0762's `export-e5-dll-one` 1cea15013 verbatim (it already names the `64bit/` folder as the companion commit);
     the first draft, made from `export-e5-dll`'s three messages, is replaced (commit 7851b7e80 has it).
   - **Commit 2** = commit 1 with its top-level `64bit` entry replaced (or added) by a tree built in a temporary index
     (`OUT/package.index`, removed afterwards): every file of the manifest hashed from `OUT/stage/64bit/` with
     `git hash-object -w --no-filters` (no line-end or attribute conversion; the clone's `core.autocrlf` does not
     matter, tested with it on), each blob id compared with `--rev`'s blob id for the same path (any difference is
     exit 2), the file mode taken from `--rev` (100644 or 100755; a symbolic link is refused). Message `commit-2.txt`.
   - Author and committer: the no-reply identity; dates `--date` or now. Then `git branch x64-export-<rev7> <commit 2>`.
   - Checks, each printed in `summary.txt`: both commits' author and committer e-mail are the no-reply address (0762's
     commit carries the owner's personal e-mail as author, so commit 1 is made anew, never cherry-picked with its
     headers; lead-0763 #15511, added 17:45); commit 1's tree equals the DLL branch's tree; its parent is the base;
     commit 2 changes only `64bit/`; commit 2's `64bit/` holds exactly the manifest's file count.
6. `OUT/commit-1.txt`, `commit-2.txt`, `pr.md` (filled), `owner-read.md`, `summary.txt`; stdout `commit-1 <sha>`,
   `commit-2 <sha>`, `branch <name>`.

Why the commits are made by the package and not later: the installer built from C5 names the export commit in its GPL
notice (D261), so the two commits are made right after the cut and never rebased before 0760 pushes them (lead-0763,
the ticket's scope).

## 3. Runs

### 3.1 Smoke on the branch's own first commit (2026-11-25 16:59 EST, workstation) [L]

`--rev 7851b7e80` (main baa98d298 + this ticket's first commit), `--dll-branch export-e5-dll` (fork 58a8ce863, three
commits on Release-5.5 b0bce8c2f), own clone `C:/c5-64bit-work/4707/fork`, `--allow-findings`, a test author:
exit 0 in **87 s** (export.py about 10 s, the two checks with and without environment names about 8-10 s each, the
hashing of 1,551 files and the commits the rest). 1,551 files (product 1,340, notes 211), 40,786,503 bytes. Export
check without environment names: FAIL, 5 findings in 4 files (the known ones of `upstream-export.md` 14: the handle in
four origin comments, tickets 0769 and 0767), so NOT AN EXPORT. With the environment's names: 861 findings in 173
files (the owner's first name, mostly in the notes). All four commit checks OK.

### 3.2 First rehearsal: main bd4139f46 + this ticket, with `export-e5-dll-one` (2026-11-25 17:14 EST, workstation) [L]

`sh`-wrapped through `quietlock.py run`: `--rev 4bdf52d67` (main bd4139f46 merged into this branch; the drafts and the
new README are this branch's, so the rehearsal shows what main will hold after the merge), `--dll-branch
export-e5-dll-one --fetch`, no `--fork-clone` (the default clone of the fork setting into `OUT/fork`: origin set to
the `fork_url` setting, push disabled), `--allow-findings`, the owner's no-reply identity from the environment: exit 0
in **74 s** (76 s with the wrapper). 1,552 files (product 1,340, notes 212), 40,822,116 bytes, manifest sha256
`b769f51e...ce07f`. Export check without environment names: FAIL, **4 findings in 3 files** (the handle in the origin
comments `proxy/src/proxy_shell.cpp` lines 6 and 1176, `shell/composite/shell_composite.cpp` and `.ini` line 1: ticket
0769), so NOT AN EXPORT; `installer-notes.md`'s finding is gone since 0767's merge. With the environment's names: 860
findings in 173 files (the first name; ticket 0773). commit 1 `73d6f8b5c` (tree `67659eab4ebd` = 0762's commit's
tree, parent b0bce8c2f Release-5.5), commit 2 `015b43756`, branch `x64-export-4bdf52d`; 4/4 commit checks OK. The
default clone and the fetch are not timed apart (the whole run was shorter than the smoke's 87 s); `du` counts 4.7 GB
for `OUT/fork` because the objects are hardlinks of the fork setting's clone [I: not measured on disk].
This run had the three-message `commit-1.txt` (section 2 step 5); section 3.3 is the run handed to the owner.

### 3.3 The rehearsal for the owner's read (2026-11-25 17:31 EST, workstation) [L]

As 3.2 on `--rev 016c8abfe` (the same main plus `commit-1.txt` = 0762's message): exit 0 in **70 s** (71 s with the
wrapper). 1,552 files, 40,824,170 bytes, manifest sha256 `40c8e710...b1b2ea`; the same 4 findings in 3 files and 860
environment-name findings; commit 1 **`f487c967a`** (tree `67659eab4ebd`, parent b0bce8c2f), commit 2 **`97a0ee026`**,
branch `x64-export-016c8ab` in `C:/c5-64bit-work/4707/rehearsal-2/fork` (local, push disabled; not pushed); `message`
line: commit-1.txt is the DLL commit's own message; 4/4 commit checks OK. Copied without `fork/` (43 MB, 1,564 files)
to `%USERPROFILE%/Desktop/c5-64bit-patch-deliverables/beta5-package-rehearsal/`, posted on thread 4707 (#15528);
replaced there by 3.4's run.

This run's `pr.md` still had its two bracketed testing paragraphs; section 3.4's run replaced it in the deliverables
folder. The package is NOT AN EXPORT until 0769 clears the four origin comments. The hashes change with every run
(the date is part of the commit), so Friday's run on C5 is the one whose hashes go into the installer.

### 3.4 The run now in the deliverables folder (2026-11-25 17:48 EST, workstation) [L]

As 3.3 on `--rev 756cc2726` (adds `pr.md` filled from 0762's proofs, the e-mail header checks, owner-read points 5 and
7 after lead-0763's read, #15531): exit 0 in **51 s** (53 s with the wrapper). 1,552 files, 40,825,768 bytes, manifest
sha256 `892b5643...845318`; the same 4 findings in 3 files and 860 environment-name findings; commit 1 **`0840cb50d`**
(tree `67659eab4ebd`, parent b0bce8c2f), commit 2 **`c029a7a02`**, branch `x64-export-756cc27` in
`C:/c5-64bit-work/4707/rehearsal-3/fork`; 6/6 commit checks OK (both e-mail checks included). It replaced 3.3's copy in
`%USERPROFILE%/Desktop/c5-64bit-patch-deliverables/beta5-package-rehearsal/` (43 MB, 1,564 files). Evidence:
`tools/export/evidence/4707/acceptance.txt`. The four run times (87, 74, 70, 51 s) vary with the machine's load
[I: other teams' builds ran meanwhile; not a timing measurement].

## 4. Friday: the same command on C5 (0759's step)

```
export C5_EXPORT_AUTHOR="<the owner's name> <<id>+<login>@users.noreply.github.com>"
python -I -X utf8 tools/export/package.py --rev <C5> --dll-branch export-e5-dll-one --fetch \
       --out C:/c5-64bit-work/0759/package
```

`export-e5-dll-one` is 0762's one-commit branch on Release-5.5 (lead-0763, #15468, 2026-11-25; its sha is posted on
thread 0762); if 0762 hands back another name, that one. Run time on the workstation: section 3.

Then copy `C:/c5-64bit-work/0759/package` without its `fork/` folder to the deliverables folder for the owner, and
put the two hashes printed on stdout into the installer's GPL notice (D261): `tools/installer/build_installer.py
--source-commit <commit-2> --source-dll-commit <commit-1>` (0767's options, `tools/installer/README.md`; commit 2 is
the `x64` tip that holds both the `64bit/` folder and, through its parent, the DLL changes) [I: the mapping of the two
options to the two commits is read from that README, not run]. The branch
`x64-export-<C5 rev7>` stays in `C:/c5-64bit-work/0759/package/fork` until 0760 pushes it; it must not be remade
(a remake with another date gives other hashes). If the export check still finds content at C5, the command refuses
(exit 1); `--allow-findings` makes a package marked NOT AN EXPORT, which is not the one to publish.

## 5. Tests [C]

`PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -X utf8 -m unittest discover -s tools/export/tests -p "test_package.py"`
(through `perf/quietlock/quietlock.py run`; it builds temporary git repositories, no network). Covered: the package
made (manifest = the exported paths, product before notes, commit 1's parent and tree, commit 2 only under `64bit/`,
the base's stale `64bit/` file gone, every blob equal to the source's including a CRLF file and a binary file with
the clone's `core.autocrlf` on, identity, messages, filled texts, the environment-name finding counted, nothing
pushed, no tag, no ledger); the same hashes on two runs with `--date`; the default clone (made from the fork setting,
push refused, the long-lived stand-in's refs unchanged); `--fetch`. Refusals with their exit code and what is left:
`--out` not empty, inside the repository, under the clones folder; no author, a non-no-reply author; the long-lived
clones and a folder inside one; a clone under the clones folder; not a repository; a missing DLL branch, one off the
base, a missing base; the branch already there; export check findings (exit 1, no branch; with `--allow-findings`
NOT AN EXPORT and the marker not in commit 2); drafts with ids, internal names, a private term (masked in the
output); an unknown placeholder; a bad `--date`; a missing draft. The rule table of the public-text check; this
repository's drafts pass it; the README links only exported files, names no private file and carries the D260
licence line and upstream's issue address.

## 6. Not verified

- No package has been pushed or read by upstream (by design).
- The Friday run on C5 and 0762's final branch (section 4) has not run.
- The exported folder does not build on its own yet (ticket 4706, thread 0763 #15357): `exebuild/`, `tools/exebuild/`,
  `deps/dll.lock`, `proxy/model/` and the purity tools are not on the allow-list; listed in `owner-read.md` point 6.
- The PR text's x86 and testing sections are filled (2026-11-25 17:45, lead-0763 #15531) from 0762's proofs on
  Release-5.5 (`../03-dll-engine-boundary/upstream-export-e4-dll-commit.md` section 10 on branch impl-0762); its one
  bracketed line waits for C5's release take (the x64 release/LTO build, the release journeys).
