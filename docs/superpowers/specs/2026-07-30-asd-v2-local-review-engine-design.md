# ASD v2 Local Review Engine Design

Date: 2026-07-30
Status: Draft for user review

## Purpose

ASD v2 upgrades VoxelPilot's current local AI layer from broad heuristic discovery into a sharper, evidence-rich local review engine. The goal is to improve demo impact, region discovery accuracy, natural AI prompts, and readiness for future model-backed segmentation while keeping the current product fully local, explainable, and cautious.

The first implementation remains offline and heuristic. It should describe what it can measure, not claim anatomy, diagnosis, or object identity without user confirmation, imported metadata, or a future opt-in model provider.

## Core Direction

VoxelPilot's AI Assist should become a coherent local review workflow:

`Dataset Health -> Transfer Advisor -> ASD v2 Discovery -> Evidence-Rich Descriptions -> Guided Review -> Export Bundle`

The app should still expose individual tools, but the primary demo path should be a one-click Smart Review flow that runs the safe sequence and focuses the viewer on the most important discovered region.

Heuristic ASD labels may say:

- `high-density curved shell candidate`
- `mid-density compact region with stable boundary contrast`
- `low-density cavity/background pocket`

Heuristic ASD labels must not say:

- `tibia`
- `tumor`
- `artery`
- `defect`
- any anatomy, diagnosis, or domain-specific object identity

Those stronger names are allowed only when the region source is user-confirmed, imported metadata, or a future model provider with explicit provenance.

## Existing Project Fit

The design builds on the current VoxelPilot v2 architecture:

- `common/semantic_discovery.h` owns ASD region structures, discovery, scoring, relationships, correction matching, and guided-review helpers.
- `common/ai_prompt.h` owns local prompt parsing.
- `common/dataset_health.h` owns health and transfer-advisor helpers.
- `standalone_main.cu` wires AI Assist, ASD controls, Guided Review, callouts, hover text, and export actions into the ImGui app.
- `common/ai_review_bundle.h` writes ASD regions, relationships, label masks, and HTML review metadata.

The current discovery function accepts histogram inputs but uses a single sensitivity-derived threshold. ASD v2 should use the histogram and measured local evidence so descriptions become more specific because the data behind them is richer.

## ASD v2 Discovery

ASD v2 replaces single-threshold discovery with a bounded multi-band pass.

Discovery should derive candidate intensity bands from the active histogram:

- low-density foreground band;
- mid-density material band;
- high-density material band;
- upper-tail dense band.

Each band should be optional. Flat, empty, or background-only histograms may produce no useful bands and should return a clear status instead of weak generic regions.

For each viable band, ASD v2 should:

1. Choose thresholds from histogram percentiles rather than a fixed global threshold.
2. Seed only informative voxels, avoiding obvious background.
3. Grow connected components with intensity-band constraints.
4. Use local contrast or boundary evidence to avoid merging adjacent structures too eagerly.
5. Track voxel count, centroid, bounding box, intensity range, mean intensity, fill ratio, compactness, elongation, boundary strength, and normalized location.
6. Merge duplicate detections across nearby bands when regions overlap heavily or have nearly identical bounding boxes.
7. Rank candidates by importance, using size, contrast, boundary evidence, stability, and density-band usefulness.
8. Run bounded stability probes around nearby thresholds to see whether important regions persist.

The output should carry enough evidence for UI, hover, export, and future model normalization:

- `density_band`;
- `intensity_percentile_min` and `intensity_percentile_max`;
- `threshold_min` and `threshold_max`;
- `boundary_strength`;
- `stability_score`;
- `importance_score` and `importance_rank`;
- `shape_archetype`;
- `location_descriptor`;
- `evidence_summary`;
- merge/probe provenance when applicable.

## Shape And Naming

ASD v2 should preserve universal archetypes while adding more useful qualifiers:

- `cavity`;
- `shell`;
- `curved shell`;
- `rod`;
- `branching rod`;
- `plate`;
- `compact core`;
- `blob`;
- `cluster`;
- `diffuse field`;
- `unclassified`.

Names should be cautious but more descriptive than ASD-Lite:

- `dense blob-like structure` becomes `high-density compact core candidate`.
- `dense shell-like structure` becomes `high-density curved shell candidate`.
- `mid-density cluster-like structure` becomes `mid-density branching cluster candidate`.
- `low-density candidate region` becomes `low-density cavity/background pocket`.

The naming system should distinguish descriptor specificity from semantic identity. It can be specific about measured density, shape, boundary, location, and stability. It cannot invent anatomy or diagnosis.

## Evidence-Rich Descriptions

Each region should get a compact evidence profile and a generated explanation.

The evidence profile should answer:

- what was detected;
- where it is in normalized volume space;
- which histogram band produced it;
- why the component was grouped;
- how stable it was across nearby probes;
- how it relates to other discovered regions;
- what source produced the label.

Example description:

`ASD-v2 grouped this as a high-density curved shell candidate in the upper-right volume quadrant. It came from the 84-97% intensity band, has thin-shell geometry, and stayed stable across nearby sensitivity probes. Ranked #2 by boundary contrast and size. Source: local heuristic, not anatomy or diagnosis.`

Description generation should follow source-specific naming rules:

- `heuristic`: cautious density/shape/location descriptor only;
- `user_confirmed`: user-provided label may be used directly;
- `imported_metadata`: imported label may be used with source shown;
- `model`: model-suggested label may be used only with model name, confidence, and suggested wording.

## Smart Review

AI Assist should add a primary Smart Review action. Smart Review should run the best safe local sequence:

1. Update Dataset Health.
2. Preview or apply transfer advice when visibility is poor and the existing safety rules allow it.
3. Run ASD v2 discovery.
4. Select the highest-importance reviewable region.
5. Enable Guided Review affordances, selected-region glow, and focused callouts.
6. Prepare export-ready region metadata.

Smart Review should respect the existing Performance Safety Governor. If a dataset is flat, too large, memory-pressured, or has no useful foreground, Smart Review should stop at the last reliable step and explain what happened.

The status line should narrate the result in demo-friendly language:

`Smart Review found 5 candidate regions. Region #1 is a high-density curved shell candidate with 82% stability. Guided Review is focused on it now.`

## Natural AI Prompts

AI Assist prompts should map to local actions and review workflows, not just transfer settings.

New prompt intents should include:

- `find important structures`: run Smart Review;
- `discover regions`: run ASD v2 only;
- `explain region 2`: select region 2 and show its evidence-rich explanation;
- `show the densest shell`: select the best high-density shell or curved-shell candidate;
- `compare top regions`: summarize the top candidates by density, shape, stability, and location;
- `make labels more specific`: regenerate or display evidence-rich descriptions;
- `hide weak regions`: suppress low-confidence or low-stability candidates from callouts;
- `review next region`: advance Guided Review;
- `export AI bundle`: export the current review bundle.

Prompt handling should remain explicit and safe. It should not silently make diagnostic claims, overwrite user corrections, or run expensive work after Performance Safety blocks it. If a prompt asks for unsupported anatomy or diagnosis, VoxelPilot should explain that only local descriptive review is available unless a user label, imported label, or future model provider supplies that identity.

## Future Model Bridge

ASD v2 should introduce a clean provider boundary without activating model inference yet.

The internal pipeline should be:

`Segmentation Provider -> Region Candidates -> Provenance Normalization -> Description Generator -> UI/Export`

The first provider is `ASD-v2 local heuristic`.

Future optional providers may include:

- local ONNX segmentation model;
- MONAI-style model;
- slice or volume prompt model;
- imported segmentation mask;
- user-confirmed correction memory.

Every region should carry normalized provenance:

- `source`;
- `provider_name`;
- `provider_version` when available;
- `confidence`;
- `evidence_summary`;
- `name_policy`.

The model bridge is intentionally architectural in this phase. It should prevent future model output from being mixed invisibly with heuristic ASD output.

## Data Flow

1. Volume load builds normalized volume data and histogram.
2. Dataset Health checks whether discovery is meaningful.
3. Transfer Advisor improves visibility when needed.
4. ASD v2 builds density bands from histogram percentiles.
5. Region discovery creates candidate masks and evidence descriptors.
6. Duplicate candidates are merged and ranked.
7. Correction memory is applied only when descriptors match clearly.
8. Description generator produces source-aware labels and explanations.
9. UI uses regions for AI Assist, hover text, callouts, and Guided Review.
10. Export bundle writes regions, relationships, evidence, provenance, label mask, snapshot, and HTML summary.

## Error Handling

ASD v2 should explain failures and partial success clearly:

- Flat volume: discovery cannot produce meaningful regions because all samples are nearly constant.
- Background-dominant volume: use Transfer Advisor before interpreting ASD results.
- Weak oversegmentation: too many unstable candidates were filtered; lower sensitivity or reduce minimum voxels.
- Performance pressure: Smart Review or export was blocked by Performance Safety.
- No histogram: discovery requires a loaded volume with histogram metadata.
- Unsupported prompt: local AI can describe regions but cannot assert anatomy, diagnosis, or object identity without provenance.

## Performance Guardrails

ASD v2 must preserve VoxelPilot's local NVIDIA laptop positioning.

Guardrails:

- keep hard max region count bounded by existing ASD limits;
- cap density bands and stability probes;
- avoid new CUDA kernels in this phase;
- keep flood-fill queues bounded;
- refuse or sample very large volumes when memory pressure is high;
- reuse label-mask rendering and callout paths;
- use Performance Safety before Smart Review, ASD discovery, and full bundle export.

## Testing

Unit and regression tests should cover:

- histogram-band threshold selection;
- low-density, mid-density, high-density, and upper-tail discovery;
- invalid, flat, tiny, and background-dominant datasets;
- duplicate region merge behavior;
- archetype classification and richer names;
- location descriptor generation;
- evidence summary generation;
- stability scoring;
- correction-memory matching after ASD v2 descriptors change;
- prompt intents for Smart Review, explanation, densest-shell selection, top-region comparison, weak-region hiding, guided-review navigation, and bundle export;
- export JSON includes evidence and provenance fields;
- heuristic-only output does not contain anatomy, diagnosis, or unsupported domain identity.

Manual verification should cover:

- Bonsai semantic demo produces less generic top-region labels.
- Guided Review focuses the highest-ranked reviewable region.
- Hover and callouts show evidence-rich descriptions.
- AI review bundle contains richer region JSON and a linked report.
- Existing render regression and volume import tests still pass.

## Acceptance Criteria

- Smart Review exists as a primary AI Assist workflow.
- ASD v2 uses histogram-derived density bands rather than a single global threshold.
- Discovered regions include richer evidence fields.
- Labels are more specific but remain non-diagnostic and source-aware.
- Hover text, Guided Review, callouts, and export bundle use the improved descriptions.
- Natural prompts can trigger Smart Review and region explanation workflows.
- Future model-backed labels have a defined provider/provenance boundary, but no model inference is required in this phase.
- Performance Safety continues to block risky ASD and export actions.
- Tests prove local heuristic output does not invent anatomy or diagnosis.

## Non-Goals

- No cloud AI.
- No required model download.
- No anatomy or diagnosis from heuristic ASD.
- No CUDA renderer rewrite.
- No out-of-core streaming.
- No full medical segmentation workflow.
- No automatic trust transfer from one dataset's user correction to unrelated data.

## Risks And Mitigations

- Risk: richer labels sound too authoritative. Mitigation: enforce source-aware naming and heuristic-only forbidden terms tests.
- Risk: multi-band discovery creates too many candidates. Mitigation: merge duplicates, rank by stability, and hide weak regions by default.
- Risk: stability probes increase runtime. Mitigation: cap probes and reuse bounded candidate descriptors instead of full expensive rediscovery on large volumes.
- Risk: correction memory mismatches after descriptor changes. Mitigation: update matching to use explicit evidence fields and show correction provenance.
- Risk: Smart Review changes too much silently. Mitigation: keep actions local, explain each outcome, and respect existing Performance Safety.

## Implementation Scope

This design is a single focused upgrade: ASD v2 local-first review intelligence. It includes discovery, descriptions, prompt orchestration, export metadata, and a future provider boundary. It does not implement a real model provider yet.

