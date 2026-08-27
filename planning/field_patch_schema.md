# Per-Field Patch Schema and Export-Preflight Contract

Status: initial schema and lifecycle decisions; detailed schema drafting remains
Last reviewed: 2026-08-27

## Purpose

One SKEWER workspace corresponds to one located Dreamcast `FIELD` directory. The workspace may contain changes for many ordinary fields at once. Each changed field has one independent patch file that describes all of that field's semantic MLD-selector and ECT-table changes.

Patch files serve three purposes:

- durable working state across fields and application runs;
- a human-auditable description of intended changes;
- the sole input to the hidden export preflight and subsequent publication.

They are not patched MLD files, replacement ECT files, UI session files, or cached SPICE parse output.

## Workspace layout

```text
<directory containing SKEWER.exe>/workspace/
|- workspace.json                 Dataset path and resumable UI state
`- patches/
   |- a106a.skewer.patch.json
   |- a106c.skewer.patch.json
   `- ...                         One file per changed field stem
```

`workspace.json` identifies the active game-data root and located FIELD directory and stores UI state such as the current field, active table, camera, visibility, and selected triangles. It does not contain semantic MLD or ECT edits.

Each field patch contains semantic edits only. Switching between fields in the same workspace checkpoints the old field's patch and loads or creates the new field's patch without discarding other fields' changes.

Only one FIELD workspace is active. Before opening a different FIELD dataset, the user must explicitly export and archive, archive without export, discard, or cancel. Archived workspaces may remain beside the executable for audit, but they are not simultaneously active editing workspaces.

## Patch representation

**Decision:** field patches use human-readable JSON governed by a published, checked-in JSON Schema. The schema identifier and integer version in each patch select the exact validation contract. One patch contains both the MLD selector edits and ECT table edits for its field.

Illustrative version 1 shape:

```json
{
  "format": "skewer-field-patch",
  "version": 1,
  "field": {
    "stem": "a106a",
    "platform": "dreamcast",
    "ectFile": "a106a.ect",
    "mldFile": "a106a.mld"
  },
  "mld": {
    "triangleSelectorEdits": [
      {
        "key": {
          "kind": "grnd",
          "resourceAddress": "0x00012340",
          "triangleIndex": 17
        },
        "expectedSelector": 2,
        "selector": 5
      },
      {
        "key": {
          "kind": "gobj",
          "resourceAddress": "0x00045670",
          "nodeIndex": 3,
          "triangleIndex": 9
        },
        "expectedSelector": 1,
        "selector": 0
      }
    ]
  },
  "ect": {
    "tableEdits": [
      {
        "table": 5,
        "stage": {
          "expected": 12,
          "value": 18
        },
        "overallEncounterRate": {
          "expected": 32,
          "value": 24
        },
        "rowEdits": [
          {
            "row": 0,
            "encounterId": {
              "expected": 41,
              "value": 52
            },
            "weight": {
              "expected": 30,
              "value": 25
            }
          }
        ]
      }
    ]
  }
}
```

The example is illustrative rather than the final checked-in JSON Schema. Exact address encoding and forward-compatibility rules still require implementation-level drafting.

## Schema invariants

The eventual version 1 schema should require:

- exact top-level format identifier and integer schema version;
- lowercase normalized field stem and source basenames, with no arbitrary source paths inside a field patch;
- platform exactly `dreamcast` for the initial version;
- selector values in `0` through `8`;
- table numbers in `1` through `8`;
- row indices in `0` through `31`;
- all ECT expected/replacement values in `0` through `65535`;
- a GRND key with resource address and triangle index but no node index;
- a GOBJ key with resource address, node index, and triangle index;
- no duplicate triangle key, table field, or table/row field edit;
- at least one semantic change;
- no UI state, absolute paths, rendered colors, derived ALX names, diagnostics, output offsets, or replacement byte buffers.

Fields absent from an edit object are unchanged. An expected value equal to its replacement is not a change and should be removed during canonicalization.

Patch serialization should be deterministic so source-control review remains useful. MLD edits should sort by resource kind, resource address, node index where applicable, and triangle index. ECT edits should sort by table and row.

## Expected-value role

Expected values make the patch auditable and allow export preflight to prove that it is changing the content the author reviewed. They are semantic expectations rather than file hashes:

- `expectedSelector` records the selector observed when the triangle edit was authored.
- ECT `expected` values record the field values observed when each sparse edit was authored.
- `value` or `selector` records the intended replacement.

The workspace may read and display a patch when current source values disagree. A mismatch is an edit-state conflict. When the semantic key still resolves uniquely, the user may explicitly accept the current source value as the new expected value for that edit. This updates the saved patch state before export; export itself does not ask the user to approve individual or aggregate mismatches.

An unresolved or ambiguous triangle/table identity remains a hard failure and cannot be rebased. If the current source value already equals the requested replacement, SKEWER classifies the edit as already applied, produces no output for that edit, and does not require user intervention. Physical MLD patch application still uses SPICE's expected-byte validation generated from the current parsed source.

## Hidden export-preflight contract

There is no separate user-facing dry-run command. Pressing Export runs a hidden preflight over the currently selected field patches before any overwrite confirmation or publication:

1. Parse and schema-validate the field patch.
2. Resolve its source basenames inside the workspace's unique FIELD directory.
3. Parse and validate the Dreamcast MLD and ECT inputs.
4. Require an ordinary non-Area-99 ECT with exactly eight tables and 32 rows per table.
5. Resolve every GRND/GOBJ key and ECT table/row reference.
6. Compare all stored expected values with current semantic values, treating current-equals-replacement entries as already applied and any other mismatch as an error.
7. Apply the ECT edits to an in-memory working model.
8. Translate MLD edits to SPICE `DreamcastTriangleSelectorEdit` requests and build an `MldPatchPlan`.
9. Apply both output transformations to memory or staging only.
10. Reparse the candidate MLD and ECT outputs and verify all requested semantic results.
11. Produce the internal publication plan and error/warning diagnostics without modifying the selected export directory.

If any selected field fails preflight, SKEWER publishes nothing from the selection and reports the errors by field. If all selected fields pass, Export proceeds to the ordinary aggregate destination-overwrite confirmation and atomically publishes the validated batch. Publication must not reinterpret the patches independently.

The export UI is a multi-selection list of fields that currently have patches. Export operates on their current saved patch state, and a dedicated `Select All Workspace Patches` command selects the complete active patch set. The user is not asked to select edits or approve preflight steps individually.

## Patch lifecycle

- The first edit to a field creates its patch file atomically.
- Later semantic edits rewrite that field's canonical patch atomically.
- Returning a value to its expected baseline removes that sparse edit.
- A patch with no remaining semantic edits does not represent a changed field and is removed automatically after the empty state is checkpointed safely.
- Switching fields checkpoints the current patch but retains every other field patch in the workspace.
- Successful export retains every selected patch unchanged as an auditable recipe.
- A successful export writes a separate receipt containing the selected patch identities, source and output hashes, destination, timestamp, warnings, already-applied entries, and publication result. Export state is not inserted into the semantic field patches.
- Loading a different FIELD dataset replaces the sole active workspace only after the user chooses export-and-archive, archive without export, confirmed discard, or cancel. Discard is destructive and requires confirmation.

## Relationship to SPICE

The SKEWER patch schema is an application-level semantic contract. It does not duplicate SPICE binary structures:

- GRND/GOBJ keys translate to `DreamcastTriangleSelectorEdit` only during planning.
- SPICE discovers physical word offsets and expected bytes from the current parsed MLD.
- ECT edits apply to a copy of `spice::ect::EctFile` and serialize through `EctFileWriter`.
- SPICE diagnostics and SKEWER semantic warnings appear in export diagnostics and receipts, not in the patch file itself.

## Canonical version 1 choices

- Resource addresses are lowercase `0x`-prefixed strings containing exactly eight hexadecimal digits, for example `"0x00012340"`.
- A current value equal to the requested replacement is already applied and is not an error.
- Multi-field export publication is atomic across the selection.
- Empty patches are removed automatically.
- Expected-value conflict resolution updates the current patch before export rather than adding an export-time approval step.
