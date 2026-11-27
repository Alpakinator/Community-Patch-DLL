# Tests of the renderer inside server.exe

Run every suite at once with `run_all.py` (ticket 0482): it builds each test program through its own builder and runs
the suites in a fixed order, each through `perf/quietlock/quietlock.py run`, with `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1`,
one summary line per suite, exit 1 if any suite fails or skips a test:

    python -I server/tests/render/run_all.py --out C:/c5-64bit-work/<ticket>/rt-all --who <ticket> --exe <CivilizationV_DX11.exe> --game <install> [--asan]

`--list` names the suites and what each builds and runs; `--only SUITE` runs a few; `--no-build` reuses the builds in
`--out`; `--main DIR` (a plain run on main) adds the A/B suite `unchanged-vs-main`; a file `<out>/STOP` ends the run
before its next step. Every option, the environment each suite gets and the ASan variant are in the script's docstring.
The scene-bundle programs run with `scenebundle/build_scenebundle_tests.py --run-only` (sceneshot_test's timeout 1500 s).
`offline-equals-live` replays the scene library (`C5_SCENE_LIBRARY`) and needs the bundles' adapter (the workstation).
A full plain run took 41 min on 2026-11-17 (builds 13 min); its result is in
`research/06-tooling-and-workflows/implementation-effectiveness.md` section 12.

A builder that compiles `server/src/render/*.cpp` or anything that includes `srv_render_scene.h` needs `models/include`
on its include list and links `c5r_models.lib` (from the c5render build, `src/units/`) after `c5render.lib`: since ticket
0300, `srv_render_scene.h` includes `c5r/pathmodels/pathmodels.h`, which includes `c5models/anim.h` (2026-11-18, ticket
0536: the overlay-scene, shadow-scene and forest builders fixed, as fow's and build_render_tests.py before them; the
results are in `research/06-tooling-and-workflows/offline-scene-harness.md` section 29).
