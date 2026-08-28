# SKEWER

Skies Keyed Encounters: Weighting, Editing, and Regions.

SKEWER is a portable C++/Qt tool for inspecting and editing *Skies of Arcadia*
random-encounter regions and encounter tables. Current support targets
ordinary Dreamcast fields.

## Current capabilities

- Select an extracted game-data root; SKEWER recursively requires exactly one
  directory named `FIELD` (case-insensitive).
- Enumerate direct-child ECT files as fields. Missing MLD pairs and deferred
  Area 99 (`a099a`) remain visible but disabled.
- Parse a selected ordinary Dreamcast ECT/MLD pair through the pinned SPICE
  submodule and reject compressed/GameCube or malformed selected assets.
- Render all decoded GRND and GOBJ collision triangles in Qt Quick 3D, colored
  by encounter selector `0` through `8` (invalid selector digits are magenta).
- Render `wall`, `walluv`, and `doorwall` object resources as a separate,
  non-editable field-context layer in their authored bind pose, without
  textures or animation.
- Orbit, pan, zoom, frame, hide resources, and select exact triangles with a
  CPU BVH picker. GRND and GOBJ selections retain different semantic keys.
- Edit triangle encounter selectors and all eight fixed ECT tables, with
  undo/redo and one auditable semantic patch document per changed field.
- Invisibly preflight selected workspace patches and publish only changed ECT
  or MLD files to a user-selected output directory.
- Optionally load ALX 5.0.0 `enemy.csv` and `enemyencounter.csv` through
  SpiceTrade and inspect the selected ECT row's formation and enemy names in a
  separate read-only dock.
- Resume the most recent root, field, table, camera, field-context opacity, visibility, selection,
  ALX directory, and field patches from the portable `workspace` directory.

Area 99, GameCube, ALX editing/export, and detailed enemy-stat inspection remain
deferred.

## Build

Requirements:

- Visual Studio with MSVC toolchain `v145`;
- Qt 6.10.3 `msvc2022_64` registered with Qt/MSBuild as
  `6.10.3_msvc2022_64`;
- initialized `SPICE` git submodule.

From an elevated PowerShell prompt:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" `
    SKEWER.sln `
    /p:Configuration=Debug `
    /p:Platform=x64 `
    /p:QtMsBuild="$env:LOCALAPPDATA\QtMsBuild" `
    /m:1 `
    /nr:false `
    /v:minimal
```

The portable application and its deployed Qt runtime are written to
`bin/x64/Debug/` (or `Release/`). Run the tests with:

```powershell
.\bin\x64\Debug\SkewerTests.exe
```

## Use

Launch `SkewerQt.exe`, choose **Open Game Data Root...**, and select either the
extracted game root or its `FIELD` directory. Select any enabled ordinary
field. Left-click selects a triangle; Shift adds and Ctrl toggles. Left-drag
orbits, right/middle-drag pans, and the mouse wheel zooms.
The left scene tree controls encounter resources separately from the Wall,
WallUV, and Doorwall context types.

For optional enemy context, choose **ALX > Select ALX Data Directory...** and
select the exact ALX 5.0.0 directory containing both `enemy.csv` and
`enemyencounter.csv`. Select an ECT row to inspect its formation.

The executable directory must be writable because SKEWER is portable and does
not fall back to AppData. If it cannot create its local `workspace` directory,
the viewer remains usable but resume persistence is disabled and a warning is
shown.

Architecture and product decisions are maintained under [planning](planning/README.md).
