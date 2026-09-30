# Plan: split standalone_main.cu into modules

## Why

`standalone_main.cu` is ~5,900 lines and 150 top-level functions in one
translation unit. It works, but it's the main obstacle to easy
navigation, code review, and eventually parallel work on the codebase.
This is Tier 3c of the improvement roadmap - the highest-risk item in
Tier 3, because Claude has no local compiler and cannot verify a step
compiles before asking for a build check. This plan exists so the split
happens as a sequence of small, independently-verifiable steps instead
of one large rewrite.

## What the file currently looks like

- 150 top-level `static` functions, only 9 comment banners in the whole
  file, and one long unlabeled stretch from roughly line 2440 to 5357
  (~2,900 lines) covering nearly all of the ImGui panel drawing, ribbon,
  ASD orchestration, and export logic.
- One struct, `StandaloneApp` (lines 84-299), passed by pointer into
  almost every function. This is the shared state everything hangs off.
- Every file-scope `static` that isn't a function is a `const` table
  (`k_resolution_presets`, `k_ribbon_tab_labels`, etc.) or a shader
  source string - confirmed by audit, there is no mutable global state
  outside `StandaloneApp`. That's good: modules won't need to fight over
  hidden shared globals, only over `StandaloneApp` itself.
- Cross-references are handled today by one forward-declaration block
  (starting ~line 431) instead of headers. Splitting into files means
  replacing that block with real declarations in a shared header.
- Only two lines in this entire file touch the CUDA Runtime API directly
  (`cudaGetDeviceCount` at line 477, a `cudaError_t` check at line 3810)
  and there is no kernel code (`__global__`/`__device__`/`<<<>>>`) at
  all. Nearly everything in this file is ordinary host C++ that doesn't
  need `nvcc` specifically to compile.
- Precedent already exists for mixing `cl.exe`-compiled objects into the
  final `nvcc`-linked binary: `common/volume_import.cpp` and
  `third_party/glew-2.2.0/src/glew.c` are both compiled with plain
  `cl.exe` today (see `build_windows.bat`) and linked successfully
  alongside the `nvcc`-produced objects. This means extracted files can
  become `.cpp` files compiled with `cl.exe`, matching that existing,
  working pattern, rather than staying `.cu`/`nvcc`.

## Shared header

A new header (proposed name `standalone_app.h`) holds:

- The constants and `#define`s currently at lines 50-83
- The `StandaloneApp` struct itself (84-299)
- The `k_*` constant table declarations other files need to reference
- Declarations for any function called from a file other than the one
  that defines it (this replaces today's single forward-declaration
  block)

Every extracted file includes this header.

## Proposed extraction order

One module per step, each build-verified before starting the next.
Ordered from least coupled / smallest blast radius to most coupled
(main loop last):

1. `export_bundles.cpp` - AI review bundle + insight report generation
   (~lines 3913-4117). Reads app state, writes files, no ImGui calls.
   Smallest blast radius - good first step to prove the process works.
2. `annotation_and_semantic_memory.cpp` - label mask, annotation
   regions, ASD correction-memory save/load (~lines 1377-1801, plus
   `semantic_relationship_note` / `semantic_total_voxels` /
   `refresh_semantic_annotation_overlays`). Logic-heavy, minimal ImGui.
3. `workspace_presets.cpp` - workspace preset save/load and CLI parsing
   helpers (~lines 1119-1215, 2127-2237).
4. `performance_safety_glue.cpp` - performance governor integration:
   `refresh_performance_governor`, adaptive quality, streaming estimate,
   `performance_allows_optional_action`, `draw_performance_safety_block`
   (~lines 3732-4235). Already has a clean boundary via
   `common/performance_governor.h`.
5. `ui_ai_assist_panel.cpp` - `draw_ai_assist_panel`,
   `draw_metadata_panel`, related color/layout helpers (~lines
   4145-4542).
6. `ui_controls_panel.cpp` - `draw_controls_panel`, browse/load volume,
   keyboard shortcuts (~lines 2848-2971).
7. `ui_ribbon_and_layout.cpp` - dockspace, ribbon, layout persistence
   (~lines 2454-2847).
8. `ui_demo_and_onboarding.cpp` - walkthrough, splash, status bar, demo
   banner, cinematic callouts (~lines 4719-5332).
9. `gl_viewport_and_render.cpp` - GL resource setup, shaders,
   `render_frame_*` (~lines 505-2453, minus whatever's extracted above).
10. `standalone_main.cpp` (renamed, trimmed) - `main()`, `draw_gui()`,
    GLFW callbacks, and whatever's left after the above.

Per step: move the functions and their needed includes into the new
file, add matching declarations to `standalone_app.h` for anything
called cross-file, update `build_windows.bat` to compile the new
`.cpp` with `cl.exe` (same flags as the existing `volume_import.cpp`
step) and add its `.obj` to the final `nvcc` link line, then ask for a
`build_windows.ps1` run before starting the next step. No change is
needed to `run_all_tests_windows.bat` or the GitHub Actions workflow
for this - they call the existing build scripts, not the file list
directly.

## Risks

- No local compiler: every step depends on a real build check reporting
  back the exact error if something doesn't compile clean first try.
  Expect more back-and-forth than earlier UI changes in this project.
- This spans many sessions - it's a large effort done one file at a
  time, not a single sitting.
- ImGui itself keeps state only within a frame and within matching
  Begin/End pairs, which stay inside each function being moved, so
  splitting draw_* functions across files shouldn't change ImGui
  behavior - low risk, but worth stating since it's the kind of thing
  that's easy to get subtly wrong.

## Recommendation

Start with step 1 (`export_bundles.cpp`) as a low-risk proof that the
whole process - extract, header declarations, build script update,
build check - works end to end, before committing to the rest of the
sequence.
