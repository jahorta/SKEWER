# SKEWER Project Goals and Background

Status: initial project charter and research baseline  
Last reviewed: 2026-08-27

## Project goal

SKEWER (Skies Keyed Encounters: Weighting, Editing, and Regions) will be a GUI editing environment for random encounters in *Skies of Arcadia*. Its central purpose is to let an editor understand and change the relationship between traversable field geometry and the encounter tables used at each location, without requiring manual binary editing.

The intended workflow is:

1. Select a field and load its relevant MLD and ECT assets.
2. View the field's decoded GRND and GOBJ collision geometry in a 3D viewport.
3. Select one or more collision triangles and edit their authored encounter selector without damaging the other packed triangle metadata.
4. Inspect and edit the encounter tables reachable from those triangles, including the battle stage, overall encounter rate, and ordered encounter rows and weights.
5. Use selected ALX-exported tables to show useful formation, enemy, and event context alongside otherwise numeric encounter IDs.
6. Export edited ECT files and produce MLD outputs by applying validated, fixed-size triangle-metadata patches to the source files.

The normal editing experience should expose semantic concepts such as encounter regions, tables, formations, and weights. Raw packed values, source offsets, platform byte order, and compression are provenance or expert-detail concerns rather than the primary editing model.

## Evidence baseline

This document is grounded in two current sources:

- `D:\SoAInvestigate`, especially the encounter formula catalog and the July 2026 dungeon/overworld collision-selector studies.
- `C:\Users\jahor\source\repos\jahorta\SPICE`, especially `SpiceEct`, `SpiceMLD`, and the typed `SpiceTrade` ALX interchange models.

The research describes the game behavior; the SPICE code establishes which file structures and edit/export operations are already represented in reusable code. Planning statements below distinguish confirmed behavior from product intent and open work.

SPICE is intended to be a pinned Git submodule dependency of SKEWER. SKEWER should consume its public semantic models, parsers, writers, and patch-planning APIs rather than copy their implementation. The submodule has not yet been added to this new repository; adding and pinning it belongs to the implementation/bootstrap plan, not to this documentation-only step.

## Confirmed game-data model

### Field collision geometry and triangle metadata

MLD files contain field resources referenced by their index entries. GRND blocks represent collision/walk-surface geometry, while GOBJ blocks can also contribute collision geometry through object-node stream meshes. Encounter regions can be carried by either resource type; the Catacombs evidence includes a GOBJ encounter strip as well as GRND regions.

SPICE's canonical GRND and GOBJ triangle models retain three raw 16-bit metadata words per face. The encounter selector is not a separate two-byte field with no other meaning. Research identifies the decimal tens digit of the low 15 bits of the face's third metadata word as the authored encounter selector. The word's high bit participates in stream winding, and other decimal digits encode independent collision, surface, or force behavior.

An encounter edit must therefore replace only the encounter-selector digit and preserve every other bit and decimal digit. SPICE already implements this rule for in-place Dreamcast patches and retains the exact source-word offsets needed to validate each change.

### Ordinary dungeon encounter selection

For the validated Catacombs assets (`a106a.mld` and `a106c.mld`), the authored selector digit directly becomes the active dungeon encounter table ID:

- selector `0` disables normal encounter-distance accumulation;
- selectors `1` through `7` select the correspondingly numbered encounter tables in the validated map;
- the same selector encoding occurs in both GRND and GOBJ geometry;
- an MLD ground-entry TBLID selects a registered collision resource and must not be confused with the per-triangle encounter table selector.

The runtime collision query finds the current triangle, decodes its packed metadata, and publishes the decoded encounter selector for the encounter system. The current evidence strongly supports this convention, but the research recommends spot-checking another dungeon before claiming universal coverage for every field in the game.

### Area 99 / overworld selection

Area 99 is intentionally not modeled as a direct triangle-to-table mapping. Its triangle selector is a local lane, normally `1` through `8`. The `fldEfcontrol` data combines that lane with:

- a coarse 7-by-6 X/Z world grid;
- one of three altitude bands;
- a scenario/progression page.

The lookup result contains both an encounter-zone choice and a table ID. The zone chooses the active ECT/ENP pair, while the table ID selects within that zone. A lane of zero, an invalid lane, or a lookup result of zero disables the normal Area 99 encounter roll.

This produces irregular boundaries from collision triangles while allowing the same local lane to resolve differently across world-grid cells, altitudes, or scenario pages. The editor must visualize both the authored lane and its context-resolved zone/table result. It must not globally label an Area 99 lane as one fixed encounter table.

`a099a.mld` and `A099A.ECT` are related parts of this workflow but are different formats and should not be conflated. The MLD supplies collision geometry and contextual lookup data; the ECT is the special indexed container holding the overworld encounter zones and their tables.

### Encounter check timing and probability

The game invokes the encounter step-counter path once in the active field-update body. Ground and ship movement update a movement-distance accumulator, but the step-counter path uses that accumulator only as a zero/nonzero movement latch; it does not wait for a fixed distance threshold and does not scale the probability by the distance moved. When all other encounter gates pass and the accumulator is nonzero, the game clears it, increments the current table's step count exactly once, and performs one encounter-probability roll. Continuous eligible movement can therefore permit one roll per active field update, while standing still permits none. Static evidence establishes the active-update cadence but does not by itself prove that every such update corresponds one-to-one with a displayed video frame.

The probability calculation uses at least:

- the accumulated step count;
- the active table's overall encounter rate;
- a map modifier;
- one RNG value.

Changing the active encounter table can reset encounter progression state. SKEWER should therefore avoid presenting the overall encounter rate as a simple independent “percent per frame.” A later simulation or preview feature must reproduce the nonzero-movement latch, the other eligibility gates, and the step-count formula rather than substitute a normalized per-frame approximation.

### ECT table contents and row selection

SPICE confirms that every encounter table contains:

- a 16-bit battle stage;
- a 16-bit overall encounter rate;
- exactly 32 ordered encounter rows;
- a 16-bit encounter ID and 16-bit row weight in each row.

After an encounter succeeds, the game scans the ordered rows using a remaining-weight rejection process. It is not implemented as one normalized weighted draw. When all eligible weights are valid and total 100, the marginal probabilities match the familiar `weight / 100` interpretation. Invalid totals or ineligible rows require exact scan emulation and must not be silently normalized by the editor.

SKEWER may show a friendly percentage interpretation when its assumptions hold, but raw weights, ordering, eligibility limitations, and validation warnings remain authoritative.

## Confirmed SPICE reuse surfaces

### ECT semantic editing and export

`SpiceEct` already provides the platform-neutral semantic model SKEWER needs:

- ordinary ECT files are `EctFlatContent`, a sequence of encounter tables;
- exact basename `A099A.ECT` uses `EctOverworldContent`;
- each indexed Area 99 entry owns a title and exactly eight encounter tables;
- each ordinary serialized table is `0x84` bytes;
- Dreamcast inputs/outputs are raw little-endian;
- GameCube inputs/outputs use big-endian decoded data and conventional AKLZ wrapping.

The observed US Area 99 file has 135 indexed entries and 1,080 tables, including 95 `dam*` entries. Those `dam*` records use the same table structure and must remain editable rather than being skipped as opaque data.

### MLD parsing, display data, and patching

`SpiceMLD` already provides:

- MLD parsing with platform/endian detection and AKLZ input handling;
- decoded GRND and GOBJ collision meshes;
- per-face raw metadata;
- transforms and source provenance needed by a 3D viewer;
- Area 99 context-aware encounter visualization in the existing Blender research importer;
- atomic fixed-size patches for the encounter-selector digit in uncompressed little-endian Dreamcast MLD files.

The patch planner verifies the source platform, compression state, resource identity, triangle and node indices, retained semantic hash, expected original bytes, non-overlap, and absence of conflicting edits before applying any write. SKEWER should preserve this plan-then-apply contract and present failures rather than weakening those checks.

The current physical patcher is not yet a general MLD writer. GameCube/AKLZ triangle patching, arbitrary metadata editing, and topology changes are outside its supported boundary.

### ALX-derived enrichment

`SpiceTrade` has typed ALX 5.0.0 interchange for exactly:

- `enemy.csv`;
- `enemyencounter.csv`;
- `enemyevent.csv`.

These tables can enrich numeric encounter records with formation, enemy, placement, initiative, reward/stat, and related display context. They are reference/editing data with locale-specific schemas, ordering, duplicate IDs, and display strings that must be preserved. SKEWER should consume this narrow typed surface rather than depend on a generic collection of untyped ALX CSV rows.

Native SPICE ownership remains authoritative for ECT and MLD data. ALX-derived data fills semantic gaps; it does not replace the native format models.

## Product scope

### Required capabilities

- Select a field from an explicit project/workspace definition rather than from unrelated loose files.
- Resolve and load all MLD, ECT, and optional ALX context required by that field.
- Render decoded GRND and GOBJ triangles in a navigable 3D viewport.
- Support picking, multi-selection, visibility controls, and coloring by authored selector.
- For ordinary fields, show the directly selected encounter table.
- For Area 99, show authored lane plus position-, altitude-, and scenario-resolved zone/table results.
- Edit selectors without changing the other packed triangle semantics.
- Edit the complete ECT semantic model, including indexed Area 99 entries and `dam*` entries.
- Explain and validate row weights without silently changing their order or normalizing malformed data.
- Show optional ALX-derived names and formation/enemy details while keeping numeric IDs visible.
- Preview every intended file output and validation result before writing.
- Export canonical ECT output for the chosen target platform.
- Apply verified, fixed-size MLD patches in place to a copied/output asset, preserving unrelated bytes.

### Safety and fidelity requirements

- Source assets are never overwritten implicitly. “Patch in place” describes fixed-offset patching within the chosen output file, not unannounced modification of the user's only source copy.
- Unchanged source bytes and unrelated packed metadata must remain byte-identical.
- Every editable triangle must retain a stable provenance identity from viewport selection through export.
- Unsupported or ambiguous resources remain visible with diagnostics and are not guessed into an editable state.
- Area-specific selector validity belongs in SKEWER's semantic validation layer, not in the low-level SPICE patch mechanism.
- Derived colors, percentages, resolved Area 99 maps, and ALX display names are presentation data; raw semantic values and source provenance remain authoritative.

## Initial non-goals

- Replacing SPICE's ECT or MLD parsers with SKEWER-specific binary readers.
- Editing collision topology, vertices, transforms, materials, textures, or unrelated packed surface properties.
- Treating encounter checks as a simple per-frame percentage.
- Treating ECT row weights as always-normalized percentages.
- Treating an Area 99 triangle lane as a globally fixed encounter table.
- General ALX CSV support beyond the explicitly typed tables needed for encounter context.
- Editing ENP or other adjacent encounter data until a concrete SKEWER workflow requires it and ownership is defined.

## Open planning questions

These are not blockers for the project charter, but later planning documents must resolve them:

1. What manifest defines a selectable field and the exact relationship among its MLD, ECT, optional ENP, region/platform, and ALX dataset?
2. Is the first writable milestone Dreamcast-only, matching the current SPICE in-place MLD patch support, or must GameCube triangle output be implemented immediately?
3. Which selector values are valid for each ordinary field, and what evidence/validation source owns that policy?
4. How should the editor identify and switch Area 99 scenario pages and altitude views without overstating inferred game-state labels?
5. Which ALX joins are display-only, and which enriched records—if any—will SKEWER eventually allow users to edit and export?
6. How will modified files be named, grouped, and packaged so that a field export is complete and reproducible?

## Acceptance statement

The first complete SKEWER workflow succeeds when a user can load a supported field, select visible GRND/GOBJ faces, understand the encounter mapping at those faces, change their selectors, edit the reachable ECT tables, review validation and ALX-derived context, and export outputs that reparse to the intended semantic values while preserving every unrelated source byte.

## Research and implementation references

Primary SoAInvestigate references:

- `D:\SoAInvestigate\Analyses\formula_catalog.md` (`ENC-001` through `ENC-006`).
- `D:\SoAInvestigate\Analyses\20260715_1113_dungeon_encounter_grnd\20260715_1143_dungeon_encounter_grnd_summary.txt`.
- `D:\SoAInvestigate\Analyses\20260715_1051_encounter_grnd_correlation\20260715_1101_encounter_grnd_correlation_summary.txt`.
- `D:\SoAInvestigate\Analyses\overworld_rng_investigation.txt`.

Primary SPICE references:

- `Docs/EctFileLayout.md`.
- `SpiceEct/EctModel.h`, `EctParser.h/.cpp`, and `EctFileWriter.h/.cpp`.
- `Docs/MldFileLayout.md` and `Docs/MldFileProgress.md`.
- `SpiceMLD/Model/MldGroundModel.h`.
- `SpiceMLD/Patching/DreamcastTrianglePatcher.h/.cpp`.
- `SpiceMLD/blender/README.md` and its Area 99 triangle-metadata display implementation.
- `SpiceTrade/AlxTypedModel.h`, `AlxTypedCodec.h/.cpp`, and `AlxTypedWorkspace.h/.cpp`.
