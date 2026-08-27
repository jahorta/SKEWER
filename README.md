# SKEWER

Skies Keyed Encounters: Weighting, Editing, and Regions.

SKEWER is a portable C++/Qt tool for inspecting and, in later slices, editing
*Skies of Arcadia* random-encounter regions and encounter tables. The current
first implementation slice is a Dreamcast import and viewing environment.

## Current capabilities

- Select an extracted game-data root; SKEWER recursively requires exactly one
  directory named `FIELD` (case-insensitive).
- Enumerate direct-child ECT files as fields. Missing MLD pairs and deferred
  Area 99 (`a099a`) remain visible but disabled.
- Parse a selected ordinary Dreamcast ECT/MLD pair through the pinned SPICE
  submodule and reject compressed/GameCube or malformed selected assets.
- Render all decoded GRND and GOBJ collision triangles in Qt Quick 3D, colored
  by encounter selector `0` through `8` (invalid selector digits are magenta).
- Orbit, pan, zoom, frame, hide resources, and select exact triangles with a
  CPU BVH picker. GRND and GOBJ selections retain different semantic keys.
- Inspect the shared selector and optional raw metadata on the left; inspect
  all eight read-only ECT tables and their 32 entries on the right.
- Resume the most recent root, field, table, camera, visibility, and selection
  from `workspace/workspace.json` beside the executable.

Editing, ALX enrichment, patch documents, hidden export preflight, and output
publication remain intentionally outside this slice. Area 99 and GameCube are
also deferred.

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

The executable directory must be writable because SKEWER is portable and does
not fall back to AppData. If it cannot create its local `workspace` directory,
the viewer remains usable but resume persistence is disabled and a warning is
shown.

Architecture and product decisions are maintained under [planning](planning/README.md).
