# Field Workspace and Encounter Data

Status: initial data-contract baseline
Last reviewed: 2026-08-28

## Located FIELD directory

**Decision:** SKEWER does not require a hand-authored field manifest and the user does not need to select the `FIELD` directory directly. The user selects an extracted game-data root. SKEWER compares the selected directory itself and recursively searches its descendants for directories named `FIELD`, case-insensitively.

Exactly one `FIELD` directory is required. If none is found, opening fails with a clear diagnostic. If more than one is found, SKEWER reports every candidate and refuses to continue rather than guessing which extracted dataset the user intended.

Field enumeration operates on direct children of the unique `FIELD` directory:

1. Enumerate regular files with a case-insensitive `.ect` extension.
2. Create one field-list entry for every unambiguous case-insensitive ECT stem.
3. Look for a direct-child MLD with the same case-insensitive stem.
4. Enable the entry when exactly one matching MLD exists.
5. Keep the entry visible but disabled and grayed out when no matching MLD exists.
6. Keep the `a099a` entry visible but disabled with an Area 99 support-deferred reason even when its pair exists.
7. Report ambiguous duplicate folded stems rather than choosing one path arbitrarily.
8. Sort fields naturally/case-insensitively by stem for display.
9. Retain the actual spelling and complete paths for loading and export diagnostics.

```cpp
struct FieldCatalogEntry {
    std::string fieldStem;
    std::filesystem::path ectPath;
    std::optional<std::filesystem::path> mldPath;
    std::optional<std::filesystem::path> sctPath;
    FieldAvailability availability;
};
```

MLD files without a matching ECT do not create list entries. An ECT without an MLD remains visible to explain the dataset but cannot be selected. Duplicate case-folded stems or multiple files competing for one side of a pair are ambiguous and should be reported rather than selected arbitrarily. A `FIELD` directory with no ECT files is an open failure with a clear diagnostic, not an empty successful workspace.

For an ordinary field, SKEWER may also pair one optional direct-child SCT by comparing basenames case-insensitively. Its expected basename is `ME` plus the field stem with its leading `A` removed; for example, `A111C` pairs with `ME111C.SCT`. The actual path and spelling are retained. A missing, ambiguous, or unparseable SCT prevents derived state presets but does not disable ordinary MLD/ECT browsing or editing.

Initial SKEWER support is Dreamcast-only. Field discovery should not infer platform from directory names. It probes relevant ECT and MLD assets across the located FIELD directory before presenting a usable field catalog. If any asset is identified as GameCube, SKEWER rejects the whole directory with `GameCube is not yet supported.` The directory is treated as one extracted platform dataset rather than a mixture of independently supported pairs.

Unsupported compression or malformed files must be reported without installing a partially loaded document. If any ECT or MLD is malformed enough that its platform cannot be determined, SKEWER rejects the whole FIELD directory as corrupted. It never guesses Dreamcast merely to continue.

The user-selected game-data root, located `FIELD` directory, and field stem are UI/session state. They are not binary-format identity and must not be embedded into SPICE models.

## Field document baselines

A loaded field has two different editing contracts:

- The MLD is an immutable parsed baseline plus a SKEWER-owned triangle-selector edit overlay.
- The ECT has a parsed baseline plus an editable semantic working copy.

This distinction follows the current SPICE APIs. The Dreamcast MLD patch planner validates original source provenance and an unmodified parsed resource. The ECT writer accepts a semantic `EctFile` for canonical serialization.

The document should track source diagnostics separately from edit diagnostics. A field may be viewable even when it is not safely exportable; the UI must state that mode explicitly.

## Separate GRND and GOBJ triangle keys

**Decision:** GRND and GOBJ triangles have different key types. They are not represented by one structure with an optional node index inside SKEWER's domain model.

```cpp
struct GrndTriangleKey {
    std::uint32_t resourceAddress;
    std::size_t triangleIndex;
};

struct GobjTriangleKey {
    std::uint32_t resourceAddress;
    std::size_t nodeIndex;
    std::size_t triangleIndex;
};

using TriangleKey = std::variant<GrndTriangleKey, GobjTriangleKey>;
```

The distinction makes invalid states unrepresentable:

- A GRND key cannot accidentally carry a node index.
- A GOBJ key cannot omit its required node index.
- Resource-specific lookup and validation can be exhaustive through `std::visit`.

At the SPICE export boundary only, each variant is translated into `DreamcastTriangleSelectorEdit`: GRND supplies no `gobjNodeIndex`, while GOBJ supplies its required node index.

Render batches, tree rows, selections, edit overlays, validation diagnostics, and undo records all use these stable keys. Pointers, transient array positions, QML object identities, and GPU vertex indices are not document keys.

## Event-ground groups and variants

An event-ground group represents one MLD entry's `groundAddresses` list. Its stable key is the MLD identity plus entry table index; the entry ID and signed `tblId` are retained as metadata. Entry table index, rather than `tblId` or physical resource address, preserves separate identities when multiple entries share a `tblId` or refer to the same resource.

Each group variant retains:

- its zero-based `groundAddresses` ordinal;
- its exact physical resource address as provenance;
- its decoded resource kind, GRND or GOBJ.

The semantic group state is either `Disabled` or `Variant N`. Selecting a GRND variant enables that GRND batch. Selecting a GOBJ variant enables every rendered node batch belonging to that GOBJ address as one logical resource. A single list may contain GRND and GOBJ variants at different ordinals; the decoded tag does not create a second ordinal namespace or alter the selection rule.

Reference role is explicit and survives scene construction. Membership in `groundAddresses` creates event-ground variant membership, while membership in `objectAddresses` creates an ordinary object reference. A physical GOBJ present in both lists has two references with different roles; SKEWER must not collapse them into one activation rule. Physical address remains provenance, not the semantic variant discriminator.

Opcode-114 resolution uses the signed `tblId` to locate the owning entry and its operand as the requested `groundAddresses` ordinal. Duplicate matching `tblId` entries and ordinals outside the selected entry's list are diagnostics. SKEWER does not guess among duplicates, wrap an ordinal, or choose a variant according to resource kind.

## Selector edit overlay

The overlay associates a stable triangle key with its requested selector digit. Reads use the edited value when present and otherwise use the decoded baseline value.

An edit operation must:

1. Resolve the key against the immutable MLD baseline.
2. Read the baseline raw third metadata word and decoded selector.
3. Validate that the requested selector is `0` for no encounters or in the table range `1` through `8`.
4. Add, replace, or remove the overlay entry.
5. Recompute affected display colors, table resolution, dirty state, and diagnostics.

Setting a selector back to its baseline value removes the overlay entry. The overlay contains semantic selector values, not replacement bytes; SPICE remains responsible for preserving the other packed digits and high bit when planning patches.

## ECT identities

An initially supported ordinary `EctFlatContent` must contain exactly eight physical tables. Each table always contains exactly 32 encounter rows through SPICE's fixed-size model. SKEWER edits values within those existing tables and rows; it does not insert or remove tables or rows.

Ordinary `EctFlatContent` uses:

```text
FlatTableKey = table index
EncounterRowKey = FlatTableKey + row index [0, 31]
```

Area 99 `EctOverworldContent` uses:

```text
OverworldTableKey = indexed-entry index + local table index [0, 7]
EncounterRowKey = OverworldTableKey + row index [0, 31]
```

The displayed encounter ID is the ECT row's `encounterId`; the row's `encounterRate` is its authored ordered weight. Table and row indices remain visible because row order is semantically meaningful and malformed totals must not be silently normalized.

The first editor permits the full 16-bit range represented by the ECT model for stage, overall encounter rate, encounter ID, and row weight. Expected gameplay ranges and suspicious row-weight totals produce warnings only. Loading, editing, and export do not clamp, normalize, or reject a representable value solely because it is unusual.

The initial warnings are:

- the 32 row weights in a table do not total 100;
- an individual row weight exceeds 100;
- a row with nonzero weight has an encounter ID that cannot be resolved in the selected optional ALX dataset.

No warning is generated merely because stage, overall rate, encounter ID, or weight is unusual but representable unless later research establishes a concrete gameplay expectation.

The eight physical ordinary ECT slots are presented as encounter tables `1` through `8`. Selector `0` is a separate no-encounters state; selectors `1` through `8` choose the correspondingly numbered physical table. The validated Catacombs map uses only selectors `1` through `7`, but that narrower observed range does not alter the complete editor model.

The Area 99 `EctOverworldContent` identities below are retained as future design context only. Area 99 is deferred from initial loading and editing because it coordinates multiple MLD files for world X/Z buckets in addition to its contextual lookup:

## Initial ALX inputs

**Decision:** initial SKEWER ALX support reads only:

- `enemyencounter.csv`;
- `enemy.csv`.

Although SPICE also has a typed `enemyevent.csv` codec, enemy events are outside the initial SKEWER workflow. ALX content is read-only enrichment at this stage; SKEWER does not write either CSV.

The ALX dataset directory is distinct from the located `FIELD` directory. The user selects it through a separate action, and the choice is remembered globally rather than associated with one game-data root. A valid selected directory contains both canonical files. If the remembered directory is missing, incomplete, or invalid, SKEWER disables enrichment and warns the user while preserving all native MLD/ECT functionality. The UI must not assume that the CSV files are stored beside the ECT/MLD pairs.

Selection of the correct Dreamcast regional ALX dataset is the user's responsibility. SKEWER does not block on a separate region-identification gate; it warns when formation joins are missing, names disagree, or other imported context is inconsistent with the selected field data.

## Field-to-formation join

SPICE's typed model confirms that an `EnemyEncounterRecord` contains:

- `entryId`;
- one `filter` string;
- initiative and magic-experience fields;
- exactly eight `EnemyReference` slots.

Each `EnemyReference` contains both an `enemyId` and a localized reference name. ALX 5.0.0 uses platform-specific field-group filter filenames:

```text
Dreamcast ordinary field:  A106A_EP.BIN
Dreamcast Area 99 zone:    A099A_01EP.BIN through A099A_13EP.BIN
GameCube ordinary field:   a106a_ep.enp
GameCube Area 99 zone:     a099a_01ep.enp through a099a_13ep.enp
```

**Decision:** preserve the imported filter string for provenance and use case-insensitive exact filename equality for association. Initial Dreamcast formation groups use `<FIELD-STEM>_EP.BIN`, so `a106a.ect` and `a106a.mld` associate with `A106A_EP.BIN`. SKEWER does not use prefix, suffix, or fuzzy matching. A selected dataset containing the GameCube `.enp` convention produces a non-blocking mismatch warning.

For ordinary fields, the initial join is:

```text
selected field stem
    -> matching enemyencounter filter group
    -> ECT encounterId == EnemyEncounterRecord.entryId
```

For future Area 99 support, the context resolver first identifies the active encounter zone. Zone numbers `01` through `13` in Dreamcast `A099A_01EP.BIN` through `A099A_13EP.BIN` (or the corresponding future GameCube `.enp` groups) are encounter-zone identities, not table identities. A zone group contains formation entries used by the zone, while the indexed ECT layout provides eight encounter tables per zone. After resolving the zone, the ECT encounter ID is joined to the formation `entryId` within that zone's exact ALX filter group.

The January 2023 overworld report is coarse historical support for the 13-used-zones/eight-tables-per-zone structure and for approximate geographic boundaries. Current `D:\SoAInvestigate` evidence owns concrete runtime distinctions: `fldEfcontrol` combines position, altitude band, progress, and authored lane, and the active Area 99 selection resolves to an ECT/ENP subtable pair before the encounter roll.

The encounter index should use a composite key such as `(exact filter, entryId)` and retain duplicate records as diagnostics. An ID is not assumed to be globally unique across all field filters.

## Formation-to-enemy join

SPICE's typed `EnemyRecord` contains an `entryId`, a list of filters, a localized name, and the detailed enemy statistics. Duplicate enemy IDs are deliberately preserved by SPICE. Initial SKEWER lookup uses only the canonical records whose ALX filter is exactly `*`; non-wildcard variants remain outside the join rather than competing with the canonical record.

For each nonempty enemy-reference slot:

1. Preserve the slot position, enemy ID, and localized reference name from `enemyencounter.csv`.
2. Use the enemy ID as the source-of-truth join key.
3. Look up the `enemy.csv` record with the same entry ID and a filter exactly equal to `*`.
4. Attach that record's canonical name and statistics when exactly one match exists.
5. Show a missing or duplicate-wildcard-record diagnostic when the join does not produce exactly one record.

Because the encounter reference already contains a name, SKEWER can always show that reference name without guessing among enemy records. A successfully joined wildcard `EnemyRecord` supplies canonical details and an independent name. A name disagreement is useful data-quality information and should be visible rather than silently overwritten. The name never changes which enemy ID is joined.

The observed `enemyId` value `255` with `None` names represents an unused formation slot. This sentinel should remain visible in expert details but should not be presented as a real enemy.

## Missing and inconsistent data

The editor must tolerate and diagnose:

- an ECT encounter ID with no record in the selected field's encounter group;
- duplicate `(filter, entryId)` formation rows;
- an encounter reference with no applicable enemy-table candidate;
- no wildcard enemy record or more than one wildcard enemy record for an ID;
- disagreement between the encounter-reference name and joined enemy-record name;
- ALX filters that cannot be associated with the selected field;
- a FIELD/ALX regional mismatch.

Missing ALX data does not prevent native MLD or ECT editing. It disables or qualifies enrichment only.

## Persistent working changes

Unexported edits are stored in a SKEWER working directory beside the portable executable and restored on the next run. SKEWER requires that executable directory to be writable. If it cannot create and atomically replace working files there, it warns the user to move the executable to a writable location rather than falling back to a per-user installation directory.

The working directory contains one active workspace for the located FIELD dataset. That workspace may retain edits for any number of its fields. Its data is divided deliberately:

- `workspace.json` identifies the source game-data root and located FIELD directory and retains resumable UI state such as selected field, active table, camera, resource visibility, selected state preset, and triangle selection;
- `patches/<field-stem>.skewer.patch.json` contains that field's sparse semantic MLD selector edits and ECT table edits, including expected and replacement values;
- one patch file contains both native asset domains for one field so the complete authored field change can be reviewed and exported as a unit.

Patch documents use distinct serialized GRND and GOBJ key shapes. They contain stable resource/node/triangle and table/row identities, never physical file offsets, replacement byte strings, absolute source paths, camera state, or selections. SPICE derives physical MLD offsets from the current parsed source during planning, while SpiceEct serializes a current in-memory semantic model.

Event-ground group state, selected SCT preset, and resulting resource visibility are presentation/session state only. They remain in `workspace.json` and never become MLD selector edits, ECT edits, SCT edits, or per-field patch content.

SKEWER checkpoints the affected patch atomically after semantic edit transactions, when switching fields, and again during orderly shutdown. Undo history is intentionally not persisted. Switching fields within the same FIELD workspace does not require exporting or discarding other fields' changes.

Restoration reparses the current ECT/MLD pair. If both files are well formed, SKEWER can load and display the patch without requiring matching source size, timestamp, or hash. Every edit includes its expected semantic source value, allowing SKEWER to detect source disagreement or an unresolved key without silently redirecting the edit. A uniquely resolved mismatch can be rebased explicitly into the current patch state; an unresolved or ambiguous identity cannot.

Only one FIELD workspace is active. Before replacing it by loading a game-data root with a different located FIELD directory, SKEWER presents the entire retained patch set and requires one of four explicit outcomes: export and archive it, archive it without export, discard it after confirmation, or cancel the transition. Archived workspaces remain audit material rather than active editing workspaces.

The versioned representation and validation pipeline are specified in [field_patch_schema.md](field_patch_schema.md).

## Hidden export preflight

SKEWER does not expose dry run as a separate user action. The export command presents a multi-selection of fields with current patches and includes a dedicated command to select every patch in the active workspace. The user selects fields, not individual edits or validation exceptions.

Before showing overwrite confirmation or publishing anything, Export silently validates the JSON Schema, resolves and parses each current ECT/MLD pair, compares expected values, generates SPICE MLD patch plans, serializes candidate ECT files, and reparses every candidate output. A current value already equal to the requested replacement is treated as already applied. Any other mismatch or unresolved identity blocks the selected batch and is reported by field.

Publication is atomic across the selection: if any selected field fails preflight, none of the selected outputs are written. If all pass, the same validated candidates continue to collision confirmation and publication without reinterpreting patch content.

## Changed-only export

The user selects an export directory independently of the source game-data root. SKEWER writes only native files whose working semantic content differs from their source baseline:

- selector-overlay changes make the selected field's MLD dirty;
- encounter-table changes make its ECT dirty;
- when both change, both files are emitted;
- an unchanged document emits no files.

Each output retains the source file's basename and is written directly into the selected destination directory. Source assets are never modified implicitly. A field patch that changes only one native asset emits only that asset even though the patch document can represent both MLD and ECT changes.

If one or more changed destination filenames already exist, SKEWER presents one confirmation listing all files that will be overwritten. It does not begin publishing outputs until that aggregate confirmation is accepted.

Successful export leaves the selected semantic patch files unchanged. SKEWER writes a separate export receipt recording the selected patches, source and output hashes, destination, timestamp, warnings, already-applied entries, and publication result.
