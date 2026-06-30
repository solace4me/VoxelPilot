# Phase 2 Design: Adaptive Semantic Discovery for 3D Volumes

Date: 2026-06-29
Status: Draft for user review
Branch: `codex/v2-cinematic-ai`

## Purpose

Phase 2 upgrades VoxelPilot v2 from a cinematic volume viewer into a cautious, explainable exploration assistant for unknown 3D volumes. The feature is called **Adaptive Semantic Discovery (ASD)**.

ASD automatically discovers meaningful candidate regions in an imported volume, assigns cautious semantic descriptions, connects those regions to hover pop-ups and cinematic highlighting, and preserves provenance so the app never pretends to know more than it does.

The goal is not to hardcode anatomy or depend on a single static dataset. The user can import medical CT, MRI-like data, industrial CT, microscopy, synthetic volumes, or unknown raw datasets. ASD should still produce useful region guidance without requiring trained model inference in the first implementation.

## Research Gap Target

VoxelPilot is targeting the gap between **visual rendering** and **interpretable volume understanding**. Many systems can render a 3D volume, but arbitrary imported volumes remain difficult to interpret when no segmentation mask, labels, ontology, or trained domain model is available.

ASD addresses this by combining unsupervised region discovery, cautious semantic guessing, hover-based explanations, correction provenance, and exportable metadata. This creates a bridge between pure visualization and future model-backed recognition.

## Core Principle

Every discovered label must expose its source and uncertainty.

VoxelPilot may say:

- `AI-suggested high-density connected region`
- `Likely dense shell structure`
- `Possible bone-like material, if this is calibrated CT`
- `Elongated tubular region`

VoxelPilot must not say:

- `Tibia`
- `Posterior tibial artery`
- `Tumor`
- `Aortic valve`

unless that name comes from a user correction, imported metadata, or a future validated model plugin with confidence/provenance.

## Algorithm Overview

### Step 1: Smart Region Detection

ASD-Lite starts with lightweight local analysis:

1. Use the existing histogram to identify candidate density bands.
2. Use gradient magnitude to avoid merging across strong boundaries.
3. Seed connected components from high-information voxels.
4. Grow regions inside intensity and boundary constraints.
5. Limit region count and voxel budgets so laptop responsiveness stays safe.

Initial implementation should run on the CPU using bounded sampling and flood-fill scaffolding already in `common/ai_features.h`. Later versions can move selected passes to CUDA.

### Step 2: Region Features

For each discovered region, compute compact descriptors:

- voxel count;
- bounding box;
- centroid;
- intensity min/max/mean;
- density class: low, mid, high;
- elongation estimate from bounding-box aspect ratio;
- compactness estimate;
- boundary strength estimate;
- confidence score;
- **region stability score**: how consistently the region appears across a small set of nearby sensitivity settings;
- **region archetype**: universal shape class such as `cavity`, `shell`, `rod`, `blob`, `plate`, or `cluster`;
- **importance score**: a ranking value derived from size, density contrast, boundary strength, and stability.

These descriptors must be cheap enough to compute locally and small enough to export as JSON. Stability must be bounded to at most three sensitivity probes in ASD-Lite, and large volumes may use sampled or seed-level stability instead of full repeated flood fills.

### Step 3: Cautious Semantic Guessing

ASD maps descriptors and archetypes to cautious region names.

Universal archetypes are intentionally non-medical and non-industrial:

- `cavity`
- `shell`
- `rod`
- `blob`
- `plate`
- `cluster`
- `unclassified`

Examples:

- High density + shell archetype + large component: `dense shell-like structure`
- High density + rod archetype: `dense elongated rod-like structure`
- Mid density + blob archetype: `soft-density blob-like region`
- Low density + cavity archetype: `low-density cavity/background region`
- Unknown profile: `unclassified connected region`

Domain-aware naming is allowed only when domain confidence exists. For raw `uint8` without calibration, use relative density language. For calibrated CT in the future, HU-aware rules may add `bone-like`, `air-like`, or `soft-tissue-like`, but still with caution.

### Step 4: Region Relationships

ASD should compute a lightweight relationship graph for discovered regions. Relationships are limited to the bounded region count, so this is at most a small top-N comparison rather than a full-volume graph.

Relationship descriptors:

- centroid distance;
- bounding-box adjacency;
- approximate containment;
- no overlap for final labels because each voxel belongs to one exported region mask label.

Examples of cautious relationship text:

- `Region 1 is adjacent to Region 2.`
- `Region 3 is spatially contained within the bounding box of Region 4.`
- `Region 5 is isolated from other high-importance regions.`

### Step 5: Region Importance Ranking

ASD should rank discovered regions so the interface can guide the viewer immediately. Importance combines:

- voxel count;
- density contrast;
- boundary strength;
- stability score;
- optional penalty for rejected/user-hidden regions.

The top three non-rejected regions may be highlighted in the legend and optionally emphasized in the render overlay. This makes ASD feel intelligent without using fabricated labels.

### Step 6: Description Generation

Each region gets a short explanation assembled from local rules:

- what was detected;
- why it was grouped;
- what descriptors support the guess;
- what the source is;
- how confident ASD is.

Example:

`AI-suggested dense elongated structure. This region was grouped by connected high-intensity voxels with strong boundary contrast. Confidence 72%. Source: ASD-Lite heuristic.`

### Step 7: Hover Pop-Ups

When the user hovers over the 3D render or slice views:

1. CUDA/CPU ray picking identifies the visible voxel.
2. VoxelPilot checks the ASD region mask.
3. The pop-up shows region name, confidence, source, intensity, voxel coordinates, and description.
4. If Cinematic Mode is enabled, the selected/hovered region can receive stronger visual emphasis.
5. If region relationships exist, the pop-up may include one concise relationship note.

### Step 8: Cinematic Emphasis Modes

ASD should integrate with Cinematic Lite using cheap visual emphasis modes:

- region color tint via the existing label overlay;
- stronger hover glow using existing cinematic/rim controls;
- selected-region rim emphasis;
- optional top-three region tinting.

True soft shadows or secondary shadow-ray lighting are deferred because they can increase GPU cost. The first implementation should reuse the existing single-pass renderer and label overlay.

### Step 9: Continuous Refinement

User feedback is optional but valuable:

- rename region;
- confirm AI suggestion;
- reject region;
- assign domain/category;
- export corrections.

Corrections should be stored locally in a small JSON knowledge file. Future imports may reuse correction vocabulary only when descriptor/domain similarity is high enough. The app must not blindly apply a correction from one dataset to unrelated data.

## User Experience

Add an `Adaptive Semantic Discovery` section to AI Assist:

- `Discover Regions` button;
- `Max Regions` slider;
- `Sensitivity` slider;
- `Min Region Size` input;
- `Region Legend` list;
- selected region controls: rename, confirm, reject, color;
- `Top Regions` list ranked by importance;
- archetype and stability badges;
- lightweight relationship notes;
- cinematic emphasis toggle for hovered/selected/top regions;
- export semantic regions as mask + JSON.

Hover pop-ups should become more informative after discovery. Before discovery, hover remains density/material heuristic only.

## Architecture

### Data Types

Create a focused semantic discovery structure in `common/ai_features.h` or a new `common/semantic_discovery.h` if the file becomes too large.

Suggested types:

```c
typedef enum {
    VOXELPILOT_SEMANTIC_SOURCE_HEURISTIC = 1,
    VOXELPILOT_SEMANTIC_SOURCE_USER_CONFIRMED = 2,
    VOXELPILOT_SEMANTIC_SOURCE_IMPORTED = 3,
    VOXELPILOT_SEMANTIC_SOURCE_MODEL = 4
} VoxelPilotSemanticSource;

typedef struct {
    unsigned char label_id;
    char name[64];
    char description[256];
    char category[64];
    VoxelPilotSemanticSource source;
    float confidence;
    float stability_score;
    float importance_score;
    char archetype[32];
    int importance_rank;
    size_t voxel_count;
    float centroid[3];
    int bbox_min[3];
    int bbox_max[3];
    float min_intensity;
    float max_intensity;
    float mean_intensity;
    float elongation;
    float compactness;
    float boundary_strength;
    float color[3];
    int user_confirmed;
    int rejected;
} VoxelPilotSemanticRegion;

typedef enum {
    VOXELPILOT_REGION_RELATION_ADJACENT = 1,
    VOXELPILOT_REGION_RELATION_CONTAINS = 2,
    VOXELPILOT_REGION_RELATION_INSIDE = 3,
    VOXELPILOT_REGION_RELATION_ISOLATED = 4
} VoxelPilotRegionRelationType;

typedef struct {
    unsigned char region_a;
    unsigned char region_b;
    VoxelPilotRegionRelationType type;
    float centroid_distance;
    float confidence;
    char description[160];
} VoxelPilotRegionRelation;
```

### Discovery Function

Expose one simple function first:

```c
int voxelpilot_discover_semantic_regions(
    const float *volume_data,
    int width,
    int height,
    int depth,
    const float *histogram,
    int histogram_bins,
    int max_regions,
    float sensitivity,
    size_t min_region_voxels,
    unsigned char *label_mask,
    VoxelPilotSemanticRegion *regions,
    int *out_region_count);
```

This function should be deterministic and bounded. It should return zero when input is invalid or the volume is too large for the configured budget.

Relationships can be computed by a second bounded helper after discovery:

```c
int voxelpilot_compute_region_relationships(
    const VoxelPilotSemanticRegion *regions,
    int region_count,
    VoxelPilotRegionRelation *relations,
    int max_relations,
    int *out_relation_count);
```

### Renderer Integration

Reuse the existing label-mask overlay. ASD writes label IDs into the same mask pathway, so CUDA rendering does not need a new mask system.

Cinematic highlighting can use the existing overlay color/alpha first. Later, a selected-region uniform can add stronger glow.

### UI Integration

The AI Assist panel becomes the control surface for semantic discovery. The existing manual annotation section remains, but shifts from “manual-only labels” to “semantic region correction and export.”

### Persistence And Export

Export should include:

- raw label mask;
- JSON metadata for each region;
- source/provenance;
- confidence;
- region descriptors;
- archetype, stability score, and importance rank;
- region relationship notes;
- user confirmation/rejection state.

Local knowledge base should be added after core ASD works. It can be a later subtask if Phase 2 becomes too large.

## Performance Guardrails

ASD must not damage VoxelPilot’s laptop-based selling point.

Initial limits:

- default max regions: 8;
- hard max regions: 16;
- default min region size: 128 voxels;
- stability probes capped at three sensitivity levels;
- relationship graph capped to discovered regions only, with hard max 16 regions;
- bounded flood-fill queue;
- skip or sample large volumes when needed;
- no neural model inference in ASD-Lite;
- no secondary shadow-ray pass for Phase 2 cinematic emphasis;
- CUDA renderer remains responsible for rendering.

CPU handles discovery orchestration and descriptors. GPU handles raymarching, rendering, existing label overlay, and future acceleration.

## Acceptance Criteria

- User can click `Discover Regions` after loading an arbitrary raw volume.
- App creates AI-suggested semantic regions without user-provided labels.
- App avoids hardcoded anatomy names for unknown data.
- Hover pop-ups show region name, description, confidence, source, intensity, and voxel coordinates.
- Region legend lists discovered regions with colors, confidence, archetype, stability, and importance rank.
- User can rename/confirm/reject a discovered region.
- Export writes mask + richer JSON metadata, including archetypes, stability scores, importance scores, and relationship notes.
- Default render baseline still passes.
- Cinematic Lite remains optional and compatible with discovered region overlays.
- Discovery has bounded runtime and region count.
- Top three important regions can be visually emphasized without changing the default renderer path.

## Non-Goals

- No full medical diagnosis.
- No guaranteed anatomy recognition.
- No cloud inference.
- No mandatory user labeling.
- No model downloads in this phase.
- No out-of-core streaming implementation in this phase.
- No heavy shadow-ray cinematic rendering in this phase.
- No semantic claims without provenance.

## Risks And Mitigations

- Risk: users overtrust heuristic labels. Mitigation: use cautious names, confidence, and source badges.
- Risk: arbitrary volumes produce poor regions. Mitigation: expose sensitivity, min size, and reject controls.
- Risk: discovery hangs on large volumes. Mitigation: bounded queues, max region count, capped stability probes, top-N relationships, and sampling guardrails.
- Risk: file grows too large. Mitigation: move ASD helpers into `common/semantic_discovery.h` if implementation expands beyond simple helper functions.
- Risk: old manual annotations conflict with ASD labels. Mitigation: represent both as semantic regions with different source values.

## Implementation Phases

### Phase 2A: ASD-Lite Core

- Add semantic region data structure.
- Add deterministic density-band connected-component discovery.
- Generate archetypes, cautious names, descriptions, stability scores, and importance scores.
- Write unit tests around discovery, naming, and invalid input.

### Phase 2B: UI And Hover

- Add `Discover Regions` controls.
- Display semantic legend with top-three importance ranking.
- Upgrade hover pop-ups to use semantic metadata, stability, archetype, and concise relationship notes.
- Reuse label overlay for discovered regions and add cheap cinematic emphasis modes for hovered/selected/top regions.

### Phase 2C: Corrections And Export

- Add rename/confirm/reject controls.
- Export richer JSON metadata, including relationships and ranking.
- Optionally create local correction file after core export is stable.

### Phase 2D: Future Model Bridge

- Define plugin interface for future ONNX/MONAI/SAM-style models.
- Keep model-backed labels separate from ASD heuristic labels by source and confidence.

## Self-Review

- Scope is focused on ASD-Lite plus UI/hover integration.
- No hardcoded anatomy names are required.
- Arbitrary imported datasets are supported through cautious descriptor-based names.
- Performance limits are explicit, including capped stability probes and top-N relationships.
- The design remains aligned with VoxelPilot v2’s local NVIDIA laptop positioning.
