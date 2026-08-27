# SKEWER Project Goals and Background

Status: initial project charter and research baseline  
Last reviewed: 2026-08-27

## Project goal

SKEWER (Skies Keyed Encounters: Weighting, Editing, and Regions) will be a GUI editing environment for random encounters in *Skies of Arcadia*. Its central purpose is to let an editor understand and change the relationship between traversable field geometry and the encounter tables used at each location, without requiring manual binary editing.

The intended workflow is:

1. Select an extracted game-data root, let SKEWER locate its single `FIELD` directory, and choose one of the fields enumerated from its ECT files.
2. View the field's decoded GRND and GOBJ collision geometry in a 3D viewport.
3. Select one or more collision triangles and edit their authored encounter selector without damaging the other packed triangle metadata.
4. Inspect and edit the encounter tables reachable from those triangles, including the battle stage, overall encounter rate, and ordered encounter rows and weights.
5. Use `enemyencounter.csv` and `enemy.csv` to show useful formation and enemy context alongside otherwise numeric encounter IDs.
6. Checkpoint each changed field as one auditable semantic patch file containing its MLD-selector and ECT-table edits.
7. Select one or more changed fields for export; before publication, SKEWER invisibly preflights their current patches through the complete validation and output-generation pipeline.
8. Atomically export the selected changed ECT/MLD outputs to a user-selected directory only when every selected field passes preflight.

The normal editing experience should expose semantic concepts such as encounter regions, tables, formations, and weights. Raw packed values, source offsets, platform byte order, and compression are provenance or expert-detail concerns rather than the primary editing model.

## Evidence baseline

This document is grounded in two current sources:

- `D:\SoAInvestigate`, especially the encounter formula catalog and the July 2026 dungeon/overworld collision-selector studies.
- `C:\Users\jahor\source\repos\jahorta\SPICE`, especially `SpiceEct`, `SpiceMLD`, and the typed `SpiceTrade` ALX interchange models.

`D:\SoANotes\Mapping\Report - Overworld encounters.pdf` is retained as coarse historical mapping context. It distinguishes encounter zones from the eight tables within each zone and records 13 used overworld zones, but current `D:\SoAInvestigate` function and data-flow evidence is authoritative for concrete runtime distinctions.

The research describes the game behavior; the SPICE code establishes which file structures and edit/export operations are already represented in reusable code. Planning statements below distinguish confirmed behavior from product intent and open work.

SPICE is a pinned Git submodule dependency at the repository root. SKEWER should consume its public semantic models, parsers, writers, and patch-planning APIs rather than copy their implementation.

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

Initial SKEWER authoring uses the complete ordinary format range: `0` means no encounters and `1` through `8` select the eight physical ECT tables in order. The Catacombs evidence happens to exercise only table IDs `1` through `7`; it does not redefine the eighth physical table as selector `0`.

### Area 99 / overworld selection

Area 99 is retained as important background and a future target, but it is deferred from the initial SKEWER implementation. It coordinates multiple MLD assets for world X/Z buckets in addition to the contextual lookup and indexed ECT/ALX structure described below. The first supported fields are ordinary locations outside Area 99.

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

The current physical patcher is not yet a general MLD writer. Initial SKEWER support is Dreamcast-only. GameCube/AKLZ input, triangle patching, arbitrary metadata editing, and topology changes are outside the initial product boundary even where an individual SPICE parser can decode the format.

The selected dataset is treated as one platform. If discovery identifies any GameCube ECT or MLD file in the located `FIELD` directory, SKEWER rejects the entire directory with `GameCube is not yet supported.` rather than mixing capabilities or exposing a partially usable list.

### ALX-derived enrichment

`SpiceTrade` has typed ALX 5.0.0 interchange for exactly:

- `enemy.csv`;
- `enemyencounter.csv`;
- `enemyevent.csv`.

SKEWER's initial ALX scope is narrower than the complete typed SPICE surface: it consumes `enemyencounter.csv` and `enemy.csv`, but not `enemyevent.csv`. The encounter table provides field-grouped formation records whose entry IDs correspond to the encounter IDs stored in ECT rows. Each formation has eight enemy-reference slots. SPICE confirms that each slot already retains both an enemy ID and a localized reference name.

The enemy ID is the source of truth for joining a formation slot to `enemy.csv`. Initial SKEWER joins use the canonical `enemy.csv` rows whose filter is exactly `*`; those rows are expected to provide one record per enemy ID. The formation slot's embedded name remains the immediate display label and an independent consistency check against the joined wildcard enemy record. Missing or duplicate wildcard records and name disagreements are diagnostics rather than reasons to substitute a different ID.

These tables are initially reference data rather than SKEWER export targets. Their locale-specific schemas, ordering, duplicate IDs, filters, and display strings must be preserved. SKEWER should consume this narrow typed surface rather than depend on a generic collection of untyped ALX CSV rows.

Native SPICE ownership remains authoritative for ECT and MLD data. ALX-derived data fills semantic gaps; it does not replace the native format models.

## Product scope

### Required capabilities

- Let the user select an extracted game-data root and locate a directory named `FIELD` case-insensitively, counting the selected directory itself and searching descendants recursively.
- Refuse to open the root if no `FIELD` directory or more than one `FIELD` directory is found; multiple candidates must be reported to the user.
- Enumerate the field list from direct-child ECT files in the unique `FIELD` directory, using case-insensitive extension and stem comparison while retaining the actual paths.
- Keep an ECT-derived field visible but disabled when no paired MLD with the same case-insensitive stem exists. MLD files without an ECT do not create list entries.
- Keep `a099a` visible but disabled with an Area 99 support-deferred explanation.
- Reject the entire FIELD directory as corrupted if any ECT or MLD is malformed enough that its platform cannot be determined.
- Resolve and load the matching MLD/ECT pair and optional ALX context for the selected field.
- Render decoded GRND and GOBJ triangles in a navigable 3D viewport.
- Display all decoded GRND and GOBJ blocks by default and color every triangle by its active working selector, including the no-encounter selector `0`.
- Render exact `wall`, `walluv`, and `doorwall` entries from their referenced Ninja object resources as a separate non-editable context layer. Use authored bind-pose transforms only; do not evaluate motions, textures, or materials.
- Provide separate visibility groups for editable encounter surfaces and field context, with context controls for Wall, WallUV, and Doorwall. Combined scene bounds drive initial framing.
- Support single-click selection, Ctrl-click toggle, Shift-click additive selection, visibility controls, and coloring by authored selector. Box/lasso selection and brush painting are deferred.
- For ordinary fields, show the directly selected encounter table.
- Provide an explicit jump-to-table button when the current triangle selection has one shared nonzero encounter selector; disable it for an empty, mixed-selector, or no-encounter selection.
- Show only the encounter selector in the triangle inspector by default. Expose raw metadata words, decoded non-encounter properties, resource addresses, triangle/node indices, and provenance only when an expert-metadata option is enabled.
- Show `Mixed` for a selection containing multiple selectors; choosing a selector applies it to the entire selection as one undoable operation.
- Restrict authored selector edits to `0` for no encounters and decimal digits `1` through `8` for the eight encounter tables, while preserving every other packed triangle semantic.
- Require exactly eight encounter tables for an initially supported ordinary field.
- Edit each table's fixed 32 ordered encounter rows; table creation/removal and row creation/removal are outside the initial editor.
- Permit the full representable 16-bit values for ECT fields in the first iteration. Explain and warn about suspicious ranges or row-weight totals without blocking the edit, changing row order, or normalizing data.
- Let the user select an ALX 5.0.0 data directory containing both `enemyencounter.csv` and `enemy.csv`, and remember that directory globally.
- Treat selection of the correct regional ALX dataset as the user's responsibility; warn about missing joins, inconsistent names, or other disagreements rather than blocking native editing.
- Group ordinary Dreamcast `enemyencounter.csv` rows by the baked-in `<FIELD-STEM>_EP.BIN` convention and join their entry IDs to ECT encounter IDs. Lowercase `.enp` filters are the distinct GameCube convention and are not used for the initial Dreamcast join.
- Treat Area 99 suffixes `01` through `13` as encounter-zone identities, not encounter-table identities; each zone still contains eight tables.
- Resolve each enemy-reference ID against the `enemy.csv` row whose filter is exactly `*`, retaining the reference name already carried by `enemyencounter.csv` for display and consistency diagnostics.
- Preview every intended file output and validation result before writing.
- Maintain one executable-local workspace for the located FIELD dataset and one semantic patch file per changed field stem.
- Keep UI/session state separate from per-field patch content.
- Select one or more changed fields for export, with a dedicated command to select all patches in the active workspace. Invisibly preflight their current saved patches through source parsing, semantic resolution, SPICE MLD patch planning, ECT serialization, and output reparsing before any publication.
- Export canonical ECT output for the chosen target platform.
- Apply verified, fixed-size MLD patches in place to a copied/output asset, preserving unrelated bytes.
- Let the user choose an export directory and write only files whose working content differs from the source, retaining their original basenames.
- Present one confirmation listing every changed destination file that already exists before overwriting any of them.
- Persist selector and ECT changes for every changed field in the active FIELD workspace. Checkpoint the current field's patch after semantic edits, on field switches, and during orderly program close, then restore the workspace on the next run without modifying source assets.
- Require the executable directory to be writable. If it is not, warn the user that SKEWER is portable and must be moved to a writable location before editing can continue.
- Before replacing the active FIELD workspace with a different game-data root, account for all retained field patches through an explicit export/archive/discard/cancel workflow.

### Safety and fidelity requirements

- Source assets are never overwritten implicitly. “Patch in place” describes fixed-offset patching within the chosen output file, not unannounced modification of the user's only source copy.
- Per-field patches store semantic expected and replacement values separately from source files. They may be loaded for audit even when source values have changed. A uniquely resolved mismatch may be explicitly accepted into the current patch state before export; unresolved or ambiguous identities remain hard failures.
- Export has no separate dry-run action or expected-value approval step. It invisibly validates the selected current patch states and publishes nothing if any selected field fails.
- Successful export retains the semantic patches unchanged and records source/output hashes, destination, warnings, already-applied entries, and result in a separate receipt.
- Resource addresses in version 1 patches use canonical lowercase `0x`-prefixed eight-digit hexadecimal strings.
- Reverting the last edit removes the empty patch automatically.
- Unchanged source bytes and unrelated packed metadata must remain byte-identical.
- Every editable triangle must retain a stable provenance identity from viewport selection through export. GRND and GOBJ use distinct key types because GOBJ identity additionally requires an object-node index.
- Unsupported or ambiguous resources remain visible with diagnostics and are not guessed into an editable state.
- Area-specific selector validity belongs in SKEWER's semantic validation layer, not in the low-level SPICE patch mechanism.
- Derived colors, percentages, and ALX display names are presentation data; raw semantic values and source provenance remain authoritative.

## Initial non-goals

- Replacing SPICE's ECT or MLD parsers with SKEWER-specific binary readers.
- Editing collision topology, vertices, transforms, materials, textures, or unrelated packed surface properties.
- Animating or texturing contextual wall and door geometry.
- Treating encounter checks as a simple per-frame percentage.
- Treating ECT row weights as always-normalized percentages.
- Treating an Area 99 triangle lane as a globally fixed encounter table.
- General ALX CSV support beyond `enemy.csv` and `enemyencounter.csv`.
- Editing or exporting ALX CSV content in the initial SKEWER workflow.
- GameCube field loading or output in the initial SKEWER workflow.
- Area 99 field loading, visualization, editing, or export in the initial SKEWER workflow.
- Editing ENP or other adjacent encounter data until a concrete SKEWER workflow requires it and ownership is defined.

## Acceptance statement

The first complete SKEWER workflow succeeds when a user can load the `a106a` Dreamcast acceptance field, select visible GRND/GOBJ faces, understand the encounter mapping at those faces, change selectors between no-encounter and tables 1 through 8, edit its eight fixed-size ECT tables, review validation and optional ALX-derived context, and export only changed outputs that reparse to the intended semantic values while preserving every unrelated source byte.

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
