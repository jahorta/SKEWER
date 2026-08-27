# SKEWER Implementation Roadmap

Status: ordinary Dreamcast import, viewing, editing, patch persistence, and verified export implemented
Last reviewed: 2026-08-27

## Delivery strategy

SKEWER should be developed as vertical slices that leave the application inspectable and testable at each stage. The first complete path should use one ordinary field before Area 99 specialization is introduced. Each stage updates these living documents when implementation evidence changes the model.

Compiled-language changes require an elevated solution build with the configured MSVC v145 toolchain. Focused tests are useful during development, but milestone acceptance includes the complete SKEWER solution build.

## Implemented first slice (2026-08-27)

The initial executable combines the non-editing portions of Stages 0 through 2 into one usable inspection path:

- the `SkewerCore`, `SkewerQt`, and `SkewerTests` x64 Debug/Release solution structure;
- Qt 6.10.3 Widgets shell with an embedded Qt Quick 3D viewport and deployed portable runtime;
- unique FIELD discovery, ECT-derived field enumeration, disabled missing pairs, and deferred Area 99;
- directory-wide AKLZ ECT rejection plus strict Dreamcast validation of the selected ECT/MLD pair;
- SPICE-backed ordinary ECT and MLD import, eight-table validation, and surfaced diagnostics;
- transformed GRND/GOBJ scene extraction, selector coloring, resource visibility, stable distinct key styles, and exact CPU BVH selection;
- left-side selection/metadata inspection, right-side read-only ECT inspection, and explicit jump-to-table navigation;
- executable-local persistence for the most recent root, active field, camera, table, visibility, expert display, and selection;
- deterministic unit coverage and optional real-corpus integration tests for all 25 ordinary Dreamcast ECT/MLD pairs currently present in the reference FIELD corpus, totaling 25,692 selectable scene triangles while preserving the eight-table ECT contract. The corpus covers mixed GRND/GOBJ, GRND-only, and GOBJ-only fields; A099A remains deferred.

This slice deliberately does not create semantic patch files or mutable working copies. Stage 3 begins the editing/persistence contract. Full directory-wide MLD platform probing also remains deferred: current discovery can identify compressed GameCube ECT datasets immediately, while an MLD is authoritatively classified when its pair is selected.

## Implemented ordinary authoring slice (2026-08-27)

The second slice completes the basic non-ALX ordinary-field editing path across Stages 3, 4, and 6:

- one mutable `FieldDocument` working model for selector overlays and full-width ECT edits, with transaction-level undo/redo and warning-only suspicious ECT totals/ranges;
- selector assignment for `0` through `8` over one or many selected GRND/GOBJ triangles, live recoloring, and strict rejection of selector `9`;
- editable stage, overall encounter rate, encounter IDs, and weights for eight fixed 32-row tables;
- deterministic per-field semantic JSON patches governed by the checked-in version 1 schema, atomically checkpointed after edits, on field changes, and at orderly shutdown;
- patch restore with already-applied classification, current-source conflict preservation, and explicit rebasing for uniquely resolved keys;
- hidden fresh-source export preflight, SPICE MLD patch planning and ECT serialization, candidate reparsing, selected-batch staging/rollback, changed-only output, overwrite confirmation, and SHA-256 export receipts;
- workspace transition choices to export-and-archive, archive without export, explicitly discard, or cancel;
- focused editing/schema/store tests plus combined MLD/ECT import-edit-export-reload validation across all 25 ordinary reference pairs. A separate deterministic-random A111C selector-only test patches one triangle, reloads the emitted MLD, resolves the same GRND/GOBJ semantic key, and verifies the requested selector.

ALX enrichment, Area 99, GameCube, lasso/brush selection, and general packed metadata editing remain deferred.

## Stage 0: solution bootstrap

Create the buildable application skeleton without implementing file-format logic.

Scope:

- `SKEWER.sln` with x64 Debug and Release configurations.
- `SkewerCore`, `SkewerQt`, and `SkewerTests` projects.
- SPICE projects grouped under a third-party solution folder.
- Shared SKEWER-only compiler and Qt property sheets.
- Qt Widgets main window containing an embedded placeholder Qt Quick 3D scene.
- QML/resource deployment and `windeployqt` setup.
- Portable executable-local working-directory bootstrap, startup writeability probe, versioned FIELD-workspace manifest, and versioned per-field patch schema. An unwritable executable location blocks editing with instructions to move the executable; there is no AppData fallback.
- Deterministic JSON patch read/write and schema validation for sparse semantic MLD and ECT edits.
- Checked-in JSON Schema for the versioned human-readable field-patch contract.
- Repository ignores for Visual Studio, build, Qt-generated, and deployment output.

Acceptance:

- Elevated Debug solution build succeeds with MSVC v145.
- The GUI launches and displays the embedded 3D viewport.
- `SkewerTests` launches through the solution test workflow.
- The SPICE submodule remains clean and pinned.

## Stage 1: game-data root discovery and document loading

Implement the first real user workflow: choose an extracted game-data root, locate its unique `FIELD` directory, and select an ECT-derived field entry.

Scope:

- Case-insensitive lookup for exactly one directory named `FIELD`, counting the selected root itself and searching descendants recursively.
- Refusal with candidate-path diagnostics when zero or multiple FIELD directories are found.
- Directory-wide platform probing and whole-directory rejection with `GameCube is not yet supported.` when any ECT or MLD asset is identified as GameCube.
- Direct-child, case-insensitive ECT enumeration within the unique FIELD directory.
- Field picker showing every unambiguous ECT stem, with entries lacking a matching MLD visible but disabled.
- Deferred `a099a` entry visible but disabled even when its MLD pair exists.
- Ambiguous folded-stem and unmatched-file diagnostics.
- Asynchronous SPICE parsing of the selected pair.
- Dreamcast-only platform validation.
- Immutable MLD baseline and ECT baseline/working-copy creation.
- Field-level diagnostics and viewable-versus-writable capability state.
- Recent game-data root persistence.
- Restoration of the active FIELD workspace and all of its per-field patch documents, without requiring source hash or timestamp equality merely to load patches for inspection.
- Restoration of active table, camera, visibility, and selection state, but not undo history.

Acceptance:

- Discovery tests cover zero/one/multiple FIELD directories, mixed-case directory and file extensions, unmatched files, duplicate folded stems, empty directories, and stable sorting.
- An ECT without an MLD appears grayed out and cannot be opened; an MLD without an ECT creates no field-list entry.
- Selecting a valid ordinary field parses both files and exposes resource/table counts.
- Detection of any GameCube ECT or MLD asset rejects the whole FIELD directory rather than installing a partial catalog.
- An ECT or MLD whose platform cannot be determined rejects the whole FIELD directory as corrupted.
- Switching fields cannot leak selections or edits from the prior document.
- Switching fields checkpoints the current patch and preserves every other field patch in the same FIELD workspace without requiring export.
- Parse failure leaves the previous valid document intact or closes it coherently; partial state is never installed.

## Stage 2: MLD scene and exact triangle selection

Render encounter-bearing geometry and make GRND/GOBJ selection exact.

Scope:

- Render-neutral geometry extraction from the parsed MLD.
- Batched Qt Quick 3D geometry for GRND resources and GOBJ nodes.
- Camera orbit, pan, zoom, framing, and reset.
- Resource/node visibility tree on the left.
- CPU ray/triangle intersection with a BVH or equivalent acceleration structure.
- Distinct `GrndTriangleKey` and `GobjTriangleKey` results.
- Selection overlay and left-side selected-triangle inspector.
- Single-click replacement, Ctrl-click toggle, and Shift-click additive selection.
- Deferred box/lasso and brush-painting interactions.
- All decoded GRND/GOBJ blocks visible by default, with every triangle colored by its active working selector including selector `0`.
- Selector-only inspector by default and opt-in expert metadata/provenance display.

Acceptance:

- Unit tests prove exact picking on overlapping, back-facing, transformed, and nearest-hit triangles.
- GRND picks never produce GOBJ keys and GOBJ picks always include a node index.
- Hiding a resource removes it from both rendering and picking.
- A selected face remains identified correctly when colors or display modes change.
- Representative fields remain interactively usable without creating one QML object per triangle.

Context extension:

- Build supported object geometry from SPICE's Sa3D Blender IR, never the obsolete world model.
- Accept only exact normalized `wall`, `walluv`, and `doorwall` entry names.
- Render bind-pose geometry in a separate untextured, non-editable layer with type-level visibility controls.
- Preserve GRND/GOBJ picking and patch identities; context geometry is presentation-only.
- Frame the combined encounter and context bounds.

## Stage 3: selector editing and undo

Add semantic MLD editing without writing files yet.

Scope:

- Selector edit overlay keyed by the GRND/GOBJ variant.
- Single-selection and multi-selection assignment using `0` for no encounters and `1` through `8` for the corresponding table.
- Baseline, working, and modified-value display in the inspector.
- `Mixed` inspector state for heterogeneous selections; assigning `0` through `8` updates the full selection as one undoable transaction.
- Undo/redo transactions.
- Modified-triangle coloring and resource/tree dirty indicators.
- Translation of overlays into SPICE patch requests as a preview operation.
- Atomic update of the selected field's patch document after semantic edit transactions, when switching fields, and again during orderly shutdown.

Acceptance:

- Setting a selector back to baseline removes the overlay entry.
- One bulk assignment is one undoable command.
- Patch-plan tests show that only the selector digit changes and all other packed metadata remains intact.
- Invalid selectors, stale keys, unsupported source formats, and missing provenance produce diagnostics without corrupting document state.

## Stage 4: ECT editor and encounter resolution

Connect geometry selectors to editable ECT tables.

Scope:

- Right-side encounter table navigation and editor.
- Validation that the ordinary ECT contains exactly eight physical tables.
- Editing of battle stage, overall encounter rate, and all 32 ordered rows.
- No insertion or removal of tables or encounter rows.
- Ordinary selector-to-table resolution.
- Explicit jump-to-table button enabled only for a nonempty triangle selection with one shared selector in `1` through `8`; selector `0` and mixed selections disable it.
- Numeric IDs and raw weights always visible.
- Friendly percentage display only when its assumptions hold.
- ECT validation for table bounds, malformed weight totals, and unresolved IDs.
- Full 16-bit value entry with warning-only gameplay-range and total diagnostics; no clamping or normalization.
- Initial warnings for row-weight totals other than 100, individual weights over 100, and nonzero-weight encounter IDs unresolved by the selected optional ALX data.
- ECT undo/redo integrated with the document edit history.
- ECT edits serialized into the same per-field semantic patch document as MLD selector edits.

Acceptance:

- A homogeneous nonzero triangle selection can navigate to its table on demand without replacing the triangle selection; geometry selection does not switch tables automatically.
- Row ordering and authored weights are preserved exactly.
- Editing and undoing every ECT field restores semantic equality with the baseline.
- Flat ECT serialization and reparsing reproduce the intended working model.
- The UI labels the eight physical slots as tables `1` through `8` and presents selector `0` separately as no encounters.

## Stage 5: ALX formation and enemy enrichment (implemented)

Add read-only context from `enemyencounter.csv` and `enemy.csv`.

Implementation status: SKEWER loads the two typed SpiceTrade tables asynchronously, remembers the optional directory in portable workspace schema version 3, associates ordinary Dreamcast fields through `<FIELD-STEM>_EP.BIN`, and displays the selected ECT row in a separate read-only formation dock. Detailed discrepancies are reported only in the diagnostics dock. Enemy combat-stat inspection remains deferred.

Scope:

- ALX dataset-directory selection and global persistence.
- Validation that both `enemy.csv` and `enemyencounter.csv` are present.
- Non-blocking fallback when the remembered ALX directory or either CSV is missing or invalid.
- Typed import through SpiceTrade for the two admitted tables only.
- Exact-filter formation index and field-filter association.
- ECT encounter-ID to formation-entry join.
- Eight-slot formation display on the ECT side of the window.
- Enemy-reference name display and enemy-ID join to the unique `filter == "*"` enemy record.
- Missing/duplicate wildcard-record and name-disagreement diagnostics.
- User responsibility for choosing the matching regional ALX dataset, with disagreements reported as non-blocking warnings.

Acceptance:

- Ordinary Dreamcast corpus fields group formation rows by their `_EP.BIN` filter.
- An ECT encounter ID selects the matching formation within the active field group rather than a same-ID row from another field.
- Empty enemy slots are not treated as real enemies.
- Non-wildcard enemy variants remain represented by the imported ALX model but do not compete with the canonical wildcard row in the initial join.
- The enemy ID, not either localized name, determines the join.
- Missing or unusable ALX data does not block native MLD/ECT editing.

## Stage 6: hidden preflight and verified field export

Complete the ordinary-field authoring loop.

Scope:

- Field-patch multi-selection and an audit view listing every semantic MLD and ECT edit, including expected and replacement values, plus a dedicated command to select all patches in the active workspace.
- No separate dry-run command or validation approval workflow.
- Hidden export preflight covering schema validation, current source resolution/parsing, expected-value comparison, semantic application, SPICE MLD patch planning, ECT serialization, and candidate-output reparsing.
- Explicit edit-state rebase for expected-value mismatches whose semantic identities still resolve uniquely; unresolved or ambiguous identities remain blocking.
- Export preview listing copied, patched, and serialized files from the successfully validated preflight plan.
- Output-directory selection distinct from the source FIELD directory.
- Changed-only output using each dirty source asset's original basename directly in the selected directory.
- ECT serialization through SpiceEct.
- Copy of the source MLD followed by planned selector patches on the output copy.
- Staging, validation, reparsing, and atomic selected-batch publication through the preflight plan.
- No output for unchanged source assets and clear handling of destination name collisions.
- One aggregate confirmation listing every changed destination file that will be overwritten.
- Separate successful-export receipt containing patch identities, source/output hashes, destination, timestamp, warnings, already-applied entries, and publication result; semantic patch files remain unchanged.
- Single-active-workspace transition flow: export-and-archive, archive without export, confirmed discard, or cancel.

**Decision:** use an ordinary uncompressed Dreamcast field for the first writable milestone. Initial SKEWER support does not load or export GameCube fields.

Acceptance:

- A valid patch can be read, written deterministically, and round-tripped without semantic loss.
- Hidden preflight writes no source, output, or patch content and identifies every file and semantic edit that would change.
- An unresolved triangle/table key or expected-value conflict is reported against its exact patch entry and is never silently redirected.
- A current value already equal to the requested replacement is classified as already applied and does not require user intervention.
- If any selected field fails preflight, no selected field output is published.
- Source assets remain byte-identical.
- Selector-only edits emit only the MLD, ECT-only edits emit only the ECT, and combined edits emit both.
- Exported ECT reparses to the working semantic model.
- Exported MLD reparses with every requested selector and unchanged unrelated metadata.
- Patch expected-byte validation catches a changed or mismatched source file.
- Any staging or validation failure leaves no partially published field output.
- A no-op document emits no files.

## Cross-cutting validation

Every milestone should maintain:

- Debug solution build success with the required elevated MSBuild executable.
- Focused unit tests for changed core behavior.
- `git diff --check` cleanliness.
- No unintended SPICE submodule changes.
- No checked-in `.vs`, build, deployment, generated corpus, or local user-setting files.
- Diagnostics that identify the field, asset, resource/table, and stable semantic key involved.
- Edit-time, field-switch, and orderly-close persistence of every changed field in the active FIELD workspace as one semantic patch document per field.
- Fresh SPICE patch planning and ECT serialization against current parsed source during hidden export preflight, even when a patch was authored against an older baseline.
- Publication consumes the validated preflight plan instead of interpreting the patch through a separate path.

GUI testing should concentrate on model/controller behavior and a small launch smoke test initially. Exact picking, discovery, joins, edit transactions, and export rules belong in deterministic core tests rather than screenshot-driven tests.

## Deferred work

- Collision topology, transform, material, and texture editing.
- General packed triangle-metadata editing beyond the encounter selector.
- ALX editing or export.
- `enemyevent.csv` integration.
- ENP binary editing.
- GameCube or compressed MLD selector export until SPICE has an equally strict patch/write path.
- Area 99 loading, visualization, editing, and export. Future support must coordinate its multiple world-bucket MLD files, contextual lane lookup, 13 encounter zones, eight tables per zone, indexed ECT content including `dam*`, scenario pages, altitude bands, and zone-specific ALX groups.
- A normalized encounter-frequency simulator until the runtime gating and step-count model is implemented faithfully.

## Milestone decisions

- Use `a106a` as the initial ordinary Dreamcast vertical-slice and acceptance fixture because it has validated GRND and GOBJ encounter geometry.
- Keep box/lasso and brush selector authoring in a later usability milestone unless early testing demonstrates they are necessary.
