# SKEWER Living Planning Documents

Status: active planning index
Last reviewed: 2026-08-27

These documents record current product decisions, evidence, unresolved questions, and implementation sequencing. They are living documents: implementation discoveries should update the relevant document in the same change rather than leave the plan knowingly stale.

## Documents

- [Project Goals and Background](project_goals_and_background.md) defines the product purpose, research baseline, confirmed format behavior, scope, and non-goals.
- [Solution and Application Architecture](solution_and_application_architecture.md) defines project boundaries, dependency direction, GUI composition, selection architecture, and build setup.
- [Field Workspace and Encounter Data](field_workspace_and_encounter_data.md) defines directory-based field discovery, document identity, GRND/GOBJ keys, ECT relationships, and the initial ALX joins.
- [Per-Field Patch Schema and Export-Preflight Contract](field_patch_schema.md) defines the auditable JSON patch representation, single-workspace layout, field multiselection, receipt, and hidden validation pipeline.
- [Implementation Roadmap](implementation_roadmap.md) defines staged vertical slices, acceptance criteria, and validation expectations.

## Decision labels

The documents use three kinds of statements:

- **Decision**: the current design direction and the default for implementation.
- **Evidence**: behavior confirmed by research, source, or corpus inspection.
- **Open question**: a choice or uncertainty that still requires evidence or a product decision.

When evidence overturns a decision, update both the decision and the affected roadmap acceptance criteria. Historical alternatives belong in version control rather than accumulating as contradictory prose in these files.
