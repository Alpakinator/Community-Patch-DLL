<!--
A pull request that adds one extra to the 64-bit patch. Read the contribution guide for extras first.
One extra per pull request. Do not attach pictures of the game, scene bundles, saves or logs: describe them in words.
-->

## The extra

- **Key:** `<YourFlag>` (option id `<id>`, given in issue #<issue>)
- **Category:** bugfix / visual addition (experiments: the category of what they show)
- **Default:** 0 (a new extra is always merged with default 0)
- **Needs:** none / `<the key of the extra it needs>`
- **What the player sees:**
- **What it reads:** an existing feed kind (which) / a feed table (which) / a new render message (kind number and its
  values)

## The rules

Tick each box only when the check was run, and give its result next to it.

- [ ] **E1 Display only.** `tools/d10/statscmp.py` on the same save played with the flag on and off, same mode (both
      windowed or both without a window), at least 10 turns: `IDENTICAL`. Command and turns:
- [ ] **E1 for a DLL change** (only with a new render message): the DLL built with and without `VP64_RENDER_EVENTS`,
      `statscmp.py` and `savecmp.py` `IDENTICAL`; the 32-bit preprocessed source equal except for line numbers.
- [ ] **E2 Off is unchanged.** `sceneshot --dumped --compare --tolerance 1:1 --option <YourFlag>=0` on every bundle
      used: every frame equal. Bundles (by scene, not by file):
- [ ] **E3 One flag, one table entry, one document entry.** The row in `options/src/vp64_options.cpp`, the entry in
      `docs/OPTIONS.md`; `test_options` passed in both builds (x86 and x64).
- [ ] **E4 Default off.** The default is 0 and the key is not added to `kApprovedDefaults`.
- [ ] **E5 Independent.** The unit test runs the extra alone and with every other extra on; a needed extra that is
      off switches it off with a log line.
- [ ] **E6 Failure falls back.** The unit test takes the input away (at the start and during a session): one log
      line, nothing of the extra drawn, the frame goes on.
- [ ] **E7 Nothing hidden is shown.** What the extra shows, and where the game's own interface already shows it:
- [ ] **E8 Cost.** With the flag on and off: frame time (median and range, runs), seconds per turn (median and range,
      runs), peak memory; the machine. Why the numbers moved:
- [ ] **E9 Pictures.** On and off pictures of the scenes and cameras listed in the test, described here and kept for
      the review:
- [ ] **E10 No game files.** No file from the game, no picture of the game and no save, log or dump in this change
      (`git diff --stat <base>` read; the maintainer runs the content check). New art (if any), who made it and where
      it comes from:
- [ ] **E11 Messages go one way.** The extra reads only the feed; no call into the DLL, no DLL header.
- [ ] **E12 One extra.** This pull request holds one extra, its flag, its document entry, its tests and its evidence.

## Tests

- Unit test: command and last line:
- Under AddressSanitizer: command and last line:

## Licence

- [ ] I publish this contribution under GPL-3.0-or-later. Code taken from elsewhere names its project, path and
      version, and its licence is compatible.
