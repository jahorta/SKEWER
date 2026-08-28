# SKEWER Solution and Application Architecture

Status: active architecture baseline
Last reviewed: 2026-08-28

## Architectural goals

SKEWER should keep binary-format ownership in SPICE, editing semantics in a Qt-free application core, and presentation in a thin Qt desktop application. The architecture must make exact triangle identity, undoable edits, validation, and byte-preserving export easier to test than they would be if those responsibilities lived in the main window.

## Solution organization

**Decision:** begin with three SKEWER projects and the relevant SPICE projects in a solution folder.

```text
SKEWER.sln
|
+- build/
|  +- Skewer.Common.props
|  `- Skewer.Qt.props
|
+- src/
|  +- SkewerCore/          C++20 static library; no Qt dependency
|  `- SkewerQt/            Qt desktop executable and 3D viewport
|
+- tests/
|  `- SkewerTests/         Core, picking, join, and export tests
|
+- planning/
`- SPICE/                  Pinned Git submodule
```

The dependency direction is:

```text
SkewerQt ---------+
                  v
SkewerTests --> SkewerCore --> SpiceMLD
                            --> SpiceEct
                            --> SpiceSCT
                            `-> SpiceTrade
```

`SkewerCore` references the public SPICE libraries. `SkewerQt` references `SkewerCore` and Qt. UI code must not bypass `SkewerCore` to edit SPICE models directly.

**Decision:** do not create a separate Qt 3D library initially. The viewer remains a strongly separated component under `SkewerQt/Viewer`, but splitting its QML, resources, and generated Qt code into another static library is deferred until there is a second consumer or a clear build-time benefit.

## SkewerCore responsibilities

Suggested areas within the core library are:

- `Document`: loaded field state, baselines, working copies, dirty state, and semantic edit operations.
- `Workspace`: one executable-local workspace for the located FIELD dataset, including a session manifest and independently auditable per-field patch documents.
- `PatchSchema`: versioned parsing, validation, deterministic serialization, and migration of semantic field patches.
- `FieldDiscovery`: recursive unique-`FIELD` lookup, directory-wide Dreamcast platform validation, ECT-derived field enumeration, pair validation, and disabled-entry diagnostics.
- `Geometry`: render-neutral triangle data, bounds, and exact picking acceleration.
- `Encounter`: ordinary selector interpretation, fixed ECT access, and optional ALX joins. Area 99 coordination is deferred.
- `Validation`: actionable diagnostics with stable source identities.
- `ExportPreflight`: hidden no-write resolution of selected current field patches, including expected-value checks, SPICE MLD planning, ECT serialization, and reparsing.
- `Export`: atomic publication of the fully validated preflight plan.

The central `FieldDocument` should own or reference:

- the selected `FieldAssetPair`;
- the immutable parsed MLD baseline and source bytes;
- the parsed ECT baseline and editable working copy;
- a triangle-selector edit overlay keyed by the distinct GRND/GOBJ key types;
- optional `enemyencounter` and `enemy` indexes;
- diagnostics, dirty state, and edit history inputs.

**Decision:** the parsed MLD baseline remains immutable. Selector changes live in a SKEWER overlay. At export, the overlay is translated into `spice::mld::patching::DreamcastTriangleSelectorEdit` values and submitted to SPICE's plan-then-apply API. This preserves the original model and provenance required by the patch planner.

The ECT model is already a semantic editable representation. SKEWER may keep a baseline and working `spice::ect::EctFile`, compare them for dirty state, and serialize the working copy through `EctFileWriter`.

## Qt application composition

**Decision:** use a Qt Widgets application shell with a Qt Quick 3D scene embedded through `QQuickWidget`, following the proven composition in SAVOR's `SavorQt3D` while keeping SKEWER's editing model separate.

The main window layout is:

```text
+----------------------+---------------------------+-------------------------+
| MLD side             |                           | ECT side                |
|                      |                           |                         |
| Field/resource tree  |     3D MLD viewport       | Encounter table list    |
| GRND/GOBJ visibility |                           | Table/row editor        |
| Triangle inspector   |                           | Separate ALX formation  |
+----------------------+---------------------------+-------------------------+
| Diagnostics, validation, dirty state, and export preview                  |
+----------------------------------------------------------------------------+
```

The selected-triangle inspector belongs on the left with the MLD resource tree. Encounter tables and the separate ALX formation dock belong on the right. The ALX dock follows the selected ECT row but remains visibly distinct from editable native ECT content. This makes the boundary between geometry/selector authoring, ECT authoring, and read-only enrichment visible in the interface.

QML owns camera manipulation, scene presentation, and pointer gesture capture. C++ owns field state, scene conversion, exact selection, edits, and view models. `MainWindow` coordinates these components but should not become their implementation.

### Qt shell and feature-widget ownership

**Decision:** `MainWindow` is a thin application shell. It owns menus, application-level actions, status-bar presentation, modal workflow dialogs, `QDockWidget` creation and placement, stable dock `objectName` values, window-level layout persistence, and the high-level signal connections between feature widgets and controllers. It must not remain the implementation site for the controls and presentation logic inside each dock.

Dock contents are ordinary feature-level `QWidget` classes rather than `QDockWidget` subclasses. `MainWindow` retains the dock wrappers because allowed areas, splitting, tabification, toggle actions, and `QMainWindow::saveState`/`restoreState` are shell concerns. Each feature widget owns its child controls, local presentation state, and programmatic-update suppression.

Feature widgets expose semantic setters, getters, and signals. They do not expose internal `QComboBox`, `QTreeWidget`, `QTableWidget`, or editor pointers to `MainWindow`. User edits are emitted as intent, such as a field selection, visibility change, selector assignment, or keyed ECT value request. A controller or coordinating layer validates and applies that intent to `FieldDocument`, then supplies the resulting presentation state back to the widgets. UI classes must not bypass `SkewerCore` to mutate SPICE models directly.

The feature widgets are:

- `FieldSceneWidget`: field selection, GRND/GOBJ and field-context resource visibility, context opacity, selected-resource notification, and patch-conflict rebase affordance.
- `TriangleInspectorWidget`: selected-triangle summary, selector choice, explicit jump-to-table and apply requests, and optional expert metadata.
- `GroundMetadataWidget`: read-only presentation for the selected GRND resource's ground metadata.
- `EncounterEditorWidget`: the eight-table selector, table header fields, ordered encounter rows, modified-value presentation, row selection, and keyed ECT edit requests.
- `FormationInspectorWidget`: read-only ALX load state and the formation/enemy view for the selected ECT row.
- `DiagnosticsWidget`: diagnostic formatting, severity presentation, and bounded diagnostic history.
- `ViewportWidget`: `QQuickWidget` and QML loading, render/selection mesh properties, camera properties, context opacity, and QML load diagnostics.

The coordinating components are:

- `FieldSessionController`: asynchronous field discovery/loading and ALX loading, catalog and enrichment state, `FieldDocument` lifetime, semantic edits, undo/redo, and read-only session lookups used by the feature widgets.
- `ViewportController`: camera-facing commands, ray requests, exact picking, visibility, selection, `SceneAdapter` ownership, and the QObject boundary presented directly to QML. `MainWindow` is not the QML backend.
- `WorkspaceController`: checkpoint scheduling, UI/session-state restoration, field-patch lifecycle, conflict rebasing, and workspace archive/discard operations. Export remains an application command while delegating its preflight and publication work to the existing core services.
- `SceneAdapter`: conversion of core geometry and display attributes into render batches.

Dedicated `QAbstractItemModel` implementations such as `ResourceTreeModel`, `EncounterTableModel`, `FormationModel`, and `DiagnosticsModel` remain available when sorting, filtering, richer roles, reuse, or model-level testing justifies them. They are not a prerequisite for the first decomposition. Initially, the existing item widgets may remain as private implementation details behind the feature-widget APIs.

A single `MainWindow`-wide population guard must not coordinate otherwise independent controls. Each feature widget should suppress only its own programmatic updates, normally with `QSignalBlocker` or a local scoped guard, so refreshing one dock cannot silently suppress events in another.

The Qt source grouping is:

```text
src/SkewerQt/
|  MainWindow.h/.cpp
|
+- Widgets/
|  +- FieldSceneWidget.h/.cpp
|  +- TriangleInspectorWidget.h/.cpp
|  +- GroundMetadataWidget.h/.cpp
|  +- EncounterEditorWidget.h/.cpp
|  +- FormationInspectorWidget.h/.cpp
|  `- DiagnosticsWidget.h/.cpp
|
+- Viewport/
|  +- ViewportWidget.h/.cpp
|  +- ViewportController.h/.cpp
|  +- SceneAdapter.h/.cpp
|  `- SelectorGeometry.h/.cpp
|
`- Session/
   +- FieldSessionController.h/.cpp
   +- WorkspaceController.h/.cpp
   `- WorkspaceStateStore.h/.cpp
```

This organization is implemented. `MainWindow` retains window-level UI orchestration while domain/session ownership, patch lifecycle, and QML integration remain behind the controller boundaries above.

The initial viewport selection contract is single click to replace the selection, Ctrl-click to toggle one triangle, and Shift-click to add one triangle. Box/lasso selection and brush painting are deferred. Selection is stable document state keyed by `TriangleKey`, not a transient QML highlight list.

MLD-to-ECT navigation is explicit rather than automatic. The triangle inspector exposes a jump-to-table button only when every selected triangle has the same selector in `1` through `8`. The button is disabled for an empty selection, mixed selectors, or selector `0`. Pressing it navigates the ECT side to the corresponding table without replacing the triangle selection. Selecting an ECT table does not implicitly replace or mass-select geometry in the initial interaction model.

Long-running parsing and scene conversion should use `QtConcurrent` and `QFutureWatcher`. A completed document/scene is installed on the UI thread as one coherent state; background work must not mutate live Qt models.

## Rendering and selection

**Evidence:** Qt Quick 3D's public pick result does not expose a primitive/triangle index. The installed Qt documentation also states that picking custom `QQuick3DGeometry` uses its bounding volume, which is not accurate enough for per-triangle metadata editing.

**Decision:** render with Qt Quick 3D but perform exact picking in C++:

1. QML supplies the pointer position.
2. The viewport maps near and far points into scene space and produces a ray.
3. A core picker tests the ray against a BVH or equivalent acceleration structure.
4. The result is a `TriangleKey` variant containing either a `GrndTriangleKey` or `GobjTriangleKey`.
5. The viewport renders a separate highlight overlay for the selected faces.

Geometry should be batched by GRND resource or GOBJ node rather than represented by one QML `Model` per face. Expanded per-face vertices may be used where independent selector colors are needed. Render batches must retain a mapping back to stable core keys; render-array indices are never document identities.

The initial raw view shows all decoded GRND and GOBJ blocks. Every triangle is colored by its active working selector, including selector `0`; selector-zero geometry uses its own subdued palette entry rather than being hidden. Resource visibility controls can hide blocks explicitly. A runtime-state preset changes this raw visibility only after the user selects one.

The viewport has two independent render layers. **Encounter Surfaces** contains the selector-colored, pickable GRND/GOBJ batches and retains per-resource controls. **Field Context** contains only exact normalized `wall`, `walluv`, and `doorwall` entries, grouped by type. Context geometry is projected from SPICE's Sa3D Blender IR through entry and object-node transforms, including weighted bind-pose placement. The obsolete SPICE world model is not used.

Context objects are untextured, unanimated, and non-editable. Their source material sidedness is preserved: authored double-sided triangle sets disable culling while the remaining triangle sets use backface culling. Motion bindings and animation frame zero are ignored so authored node transforms remain the bind pose. Context batches do not receive `TriangleKey` values and are omitted from CPU picking and patch/export state. Both layers are visible by default, context opacity is adjustable, and their combined bounds drive `Frame All`.

### Event-ground groups and state presets

Scene Layers groups event-ground resources by their owning MLD entry and displays the entry's signed `tblId`. Within a group, each `groundAddresses` ordinal is one mutually exclusive variant labeled by zero-based ordinal and decoded kind, GRND or GOBJ. A selected GRND variant enables its GRND batch; a selected GOBJ variant enables all rendered node batches for that GOBJ address as one logical resource. Mixed lists may therefore switch between GRND and GOBJ variants.

Ordinary GOBJ references from `objectAddresses` remain ordinary scene resources outside opcode-114 grouping. If the same physical GOBJ also appears in `groundAddresses`, the two reference roles remain distinct and are not merged into one visibility rule. Separate MLD entries also remain independently controllable when they share a resource address.

Named SCT presets are a separate control from the resource-tree hierarchy because one variant may participate in multiple script states. Applying a preset atomically assigns every resolved event-ground group either `Disabled` or `Variant N`. Preset evaluation starts with the loader default of ordinal `0`, then applies opcode-114 mutations in section control-flow order; a group not mentioned by a mutation retains its previous or default state. A `-1` operand disables the resolved group.

SKEWER initially retains the raw all-resources view until the user explicitly selects a preset. Missing optional SCT data leaves that raw browsing mode available without presets. Duplicate `tblId` targets, invalid ordinals, ambiguous SCT pairing, and unsupported control flow produce diagnostics rather than guessed visibility.

`SpiceSCT` owns SCT parsing. SKEWER owns deterministic field-to-SCT pairing, opcode-114 interpretation, resolution to MLD entry groups, control-flow evaluation, diagnostics, and presentation. Preset selection and resulting visibility are UI/session state: they do not mutate MLD or SCT data, create triangle-selector edits, enter field patch documents, or alter export output.

Initial display modes should include:

- resource kind and resource/node ownership;
- authored selector;
- modified selector;
- resolved ordinary encounter table;
- invalid, unresolved, or unsupported data.

## Editing and undo

Core edits should be semantic operations such as:

- set one or more triangle selectors to `0` for no encounters or `1` through `8` for the corresponding encounter table;
- set an ECT table's stage or overall rate;
- set an ordered encounter row's ID or weight;
- restore a selection or table to its baseline value.

ECT editing accepts the complete representable 16-bit range in the first iteration. Gameplay expectations are expressed as warning diagnostics; they do not clamp, normalize, reject, or silently repair authored values.

Each operation should produce enough before/after state to apply and revert deterministically. Qt may wrap these operations in `QUndoCommand`, but the actual mutation and validation remain callable without Qt for tests.

Multi-selection edits should be one transaction so a paint stroke or bulk selector assignment is undone as one user action.

When selected triangles do not share one selector, the inspector shows `Mixed`. Choosing any selector from `0` through `8` assigns it to every selected triangle as one undoable transaction.

## Persistent working state

SKEWER is a portable application and maintains its working directory beside the executable. The executable directory must be writable; there is no AppData fallback. If the writeability probe fails, SKEWER warns the user to move the executable to a writable location before editing can continue.

One workspace represents one located FIELD dataset, not one selected field. It consists of a small workspace manifest plus one independent patch document for each changed field. The manifest stores dataset location and resumable UI/session state; patch documents store only auditable semantic changes to their field's MLD and ECT content. Switching fields within the same FIELD dataset checkpoints the current field patch and preserves every other field patch in that workspace.

The persistence formats are versioned and semantic rather than copied mutable game files. SKEWER atomically checkpoints the affected field patch after semantic edit transactions and again during orderly shutdown. Returning a value to its source baseline removes that edit from the patch; a patch with no remaining edits may be removed through a recoverable workspace operation. Undo history is not persisted.

On startup or field selection, SKEWER reparses the current ECT/MLD pair and materializes the document by applying that field's patch semantically. Loading a patch for inspection does not require source size, timestamp, or hash equality. Patch entries carry expected semantic values, however, so conflicts can be diagnosed. A uniquely resolved mismatch may be explicitly rebased into the current patch; unresolved or ambiguous content must never be silently redirected.

The workspace manifest may retain the game-data root, located FIELD identity, selected field, active ECT table, camera, resource visibility, selected SCT preset, and selected triangles. These UI details do not belong in the per-field patch documents. Only one FIELD workspace is active. Before replacing it with a different dataset, the session controller presents the complete patch set and requires export-and-archive, archive without export, confirmed discard, or cancel; merely selecting another field in the same workspace does not trigger an export prompt.

The patch schema and lifecycle are specified in [field_patch_schema.md](field_patch_schema.md).

## Build configuration

**Decision:** use native Visual Studio projects with:

- x64 Debug and Release configurations;
- MSVC platform toolset `v145`;
- C++20, Unicode, conformance mode, and `NOMINMAX`;
- the installed Qt 6.10.3 `msvc2022_64` package initially;
- Qt Core, Gui, Widgets, Quick, QuickWidgets, Quick3D, Qml, and Concurrent for `SkewerQt`.

SKEWER property sheets centralize compiler and Qt settings for SKEWER projects. The first implementation exposed one integration constraint: SPICE project files use `$(SolutionDir)` as the SPICE include root. Because those projects are consumed directly from `SKEWER.sln`, a repository-root `Directory.Build.props` adds only the checked-out `SPICE/` directory to compiler include paths. This is a solution-host adapter; it changes no submodule file or compiler policy and exists solely to preserve SPICE's own solution-root include contract.

QML and application assets should be stored in a Qt resource collection. Deployment should use a centralized `windeployqt --qmldir` target based on the working SavorQt3D arrangement.

Per the repository instructions, compiled-language changes must be validated with an elevated solution build using:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" `
    SKEWER.sln `
    /p:Configuration=Debug `
    /p:Platform=x64 `
    /m:1 `
    /nr:false `
    /v:minimal
```

## Architectural guardrails

- SPICE owns binary parsing, canonical ECT writing, and physical MLD patch mechanics.
- SKEWER owns field discovery, document state, semantic policy, joins, selection, validation, and user workflow.
- QML owns presentation, not document state.
- Source files are not overwritten implicitly.
- One workspace is scoped to one located FIELD dataset and may retain patches for multiple fields.
- Each changed field has one deterministic semantic patch document containing both its MLD-selector and ECT-table edits; UI/session state remains outside it.
- Field patches use human-readable JSON governed by a checked-in JSON Schema.
- Export provides field-patch multi-selection and a dedicated command to select every patch in the active workspace; there is no separate user-facing dry-run action.
- Export runs a hidden preflight and may publish only after every selected field has passed schema validation, source resolution, expected-value checks, SPICE planning/serialization, and reparsing.
- Publication is atomic across the selected fields.
- Successful export retains the semantic patches unchanged and writes a separate receipt containing source/output hashes, destination, warnings, already-applied entries, and result.
- Initial field loading and export are Dreamcast-only even though selected SPICE libraries also expose GameCube-capable surfaces. Detecting any GameCube ECT or MLD asset rejects the located FIELD directory as a unit.
- Platform-indeterminate or corrupt ECT/MLD assets also reject the located FIELD directory as a unit.
- GRND and GOBJ keys remain distinct until the SPICE patch-boundary conversion.
- Area 99 is deferred; initial abstractions should avoid making it impossible later without adding its multi-MLD coordination prematurely.
- `a099a` remains visible in the field catalog but disabled with the deferred-support reason.
- ALX rows enrich the editor but do not replace native ECT or MLD models.
- The ALX data directory is an optional global application preference. Both initially admitted CSV files are required to enable enrichment, but missing or invalid ALX data never blocks native field editing.
- The user owns selection of the correct ALX regional dataset. Join gaps and semantic disagreements appear as warnings rather than a separate region-compatibility gate.
- Export writes only dirty MLD and ECT assets, with their original basenames, into a user-selected destination directory.
- If any changed destination file already exists, export presents one aggregate overwrite confirmation before writing.

## Open questions

1. Should Qt configuration be selected by a checked-in Qt installation name, a developer-local property sheet, or a bootstrap-generated user property file?
2. What scene-coordinate normalization is needed to keep large MLD coordinates numerically stable in both rendering and picking?
