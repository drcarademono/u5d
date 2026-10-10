# Impera Workshop map editor — UI plan

Status: Phases 1–3 implemented; subsequent phases remain proposals. The original
implementation audit below describes the baseline before this change.

## Recommendation

Build one **map workspace** around a persistent canvas. Keep map navigation on the left, drawing tools above the canvas, and a contextual inspector on the right. Terrain, NPC schedules and combat setup should share that canvas, with explicit editing modes so that inspecting or moving an actor cannot accidentally paint terrain.

Prioritize navigation, dependable brush behavior and state preservation before adding more drawing tools. Then integrate NPC and combat editing. Keep the byte inspector available for advanced work; it should not be the normal route for placing an NPC or configuring an encounter.

The workspace should remain a native Qt desktop editor, consistent with Workshop's conversation editor. Use crisp tile thumbnails and restrained overlays, but normal readable interface text. Do not replicate the game's oversized pixel-font menus in a tool that needs to display many controls.

## What exists, and where it falls short

This audit covers `MapCanvas`, `WorkshopWindow::mapEditor`, `npcEditor`, `combatEditor`, `resourceEditor` and `rebuildEditor` in `workshop/window.cpp`, plus the serializers in `workshop/formats.cpp`.

| Current capability | Problem to address |
| --- | --- |
| Britannia and Underworld, settlement floors, and combat terrain are editable | Navigation begins with resource filenames; settlement and floor are combined in one long dropdown |
| Left-drag paints; right-click picks a tile; each stroke is undoable | The brush paints only the cells receiving mouse events, so fast drags can leave gaps; picked tiles do not synchronize the palette selection |
| Four integer zoom levels and scrollbars | No fit view, minimap, direct coordinate navigation or drag panning; zoom does not anchor the map point under the pointer |
| Numeric palette of terrain IDs 0–255 | No search, categories, favorites, selected-tile explanation or recent brushes |
| Grid and coordinate readout | Coordinates update during pressed-button interaction, rather than ordinary hover; no brush footprint or clear selection overlay |
| NPC sprites at four schedule slots; clicking can open an NPC record | `t0`–`t3` are opaque labels; clicking leaves the map for a numeric table, losing spatial context |
| Combat terrain and a separate setup table | Setup has a separate map selector and exposes all 21 metadata bytes per row, including padding and unknown values; no placement markers |
| PNG rendering and tile-ID image export | Useful export actions compete with routine editing controls; rendered export currently contains terrain, not NPC overlays |
| Undo/redo and some navigation restoration | Map page and schedule slot are remembered, but zoom, grid, brush and inspection mode reset when the editor is rebuilt; scroll position alone is not enough |

These are implementation observations, not claims that every issue has been reproduced interactively. Preserve the existing format support and per-stroke undo as foundations.

## Proposed layout

```text
Project toolbar: New / Open / Save / Undo / Redo / Export Package

Maps [search]          Britain › Ground floor                    Inspector
                      Terrain | NPCs | Encounter                Brush / selection
World                 Pencil  Fill  Rectangle  Select            Tile artwork + ID
  Britannia           Zoom: Fit / 100% / …   Grid   Overlays     Contextual properties
  Underworld          ┌─────────────────────────────────────┐   NPC / encounter list
Settlements           │                                     │   Tile browser
  Britain             │             MAP CANVAS              │   Search / categories
    Ground floor      │                                     │   Favorites / recent
    Upper floor       │                                     │
  Lord British …      └─────────────────────────────────────┘   Collapsible minimap
Combat maps           X: 14  Y: 8  Tile: 29  Tool: Pencil         Diagnostics / changes
```

Use resizable splitters. The center gets the remaining width and never shrinks below a useful editing area. The right panel uses tabs **Inspector / Tiles / Diagnostics**, rather than stacking every control into an excessively tall column. Keep a compact selected-brush strip visible even when Tiles is not the active tab. Put the minimap in a collapsible panel or canvas corner, with a viewport rectangle.

On smaller windows, collapse map navigation and show the right panel one tab at a time. Do not place another permanent resource-file tree beside the map tree. Resource Library remains an optional application panel; map navigation replaces it while this workspace is active.

### Navigation

- Group maps as **World / Settlements / Combat maps**. Retain the underlying filename in a tooltip and inspector details.
- Nest settlement floors beneath their location. Display **Basement**, **Ground floor**, **Upper floor 1**, etc., alongside the actual engine floor number in details. Derive these labels from `U5::mapPages`; do not assume all locations start at floor zero. Yew and several castles/keeps have basements.
- Search location names and filenames. A coordinate box supports **Go to X/Y**, checks bounds, and centers the target.
- Remember viewport center, zoom, brush, tool, selection and visible overlays per map page. Preserve them across undo/redo and linked dialogue navigation.
- Returning from an NPC's conversation restores the NPC selection and map view. Offer an explicit **Back to map** action rather than relying on the resource list.
- Do not invent names for combat maps that have not been identified. Use the existing number and filename, with optional project-only annotations later.

### Canvas and navigation controls

Support fit-to-view and additional integer zooms. A fit overview may use fractional nearest-neighbor scaling; clearly show the actual zoom and use integer scaling for detailed editing. Never blur the tile artwork.

Use mouse wheel for vertical scrolling, Shift+wheel for horizontal scrolling, and Ctrl+wheel for pointer-anchored zoom. Middle-drag or Space+left-drag pans. Scrollbars remain available. Clicking the minimap centers the main view; dragging its viewport rectangle pans. Provide keyboard-accessible zoom and coordinate navigation.

Render a hover-cell outline and brush footprint, with live coordinates even when no button is held. A translucent preview shows a pending rectangle, fill or paste before commitment. The pointer and hit testing must use the same canvas-to-map transform, including zoom and scroll offsets. Reject negative coordinates before integer conversion, rather than allowing truncation to turn an outside click into tile zero.

Do not enable gameplay darkness by default. The editor must show the whole map clearly. Preserve tile geometry and source artwork; an optional DOS-aspect presentation can be a later view-only setting, with correctly transformed picking. It must never change map coordinates or tile IDs.

## Terrain editing

### Tile browser

Show artwork, decimal ID and hexadecimal ID, with a larger preview of the selected tile. Offer **All / Favorites / Recent** immediately. Add named categories and text search only where verified naming/classification data exists.

Terrain storage is one byte per cell: the map browser must restrict paintable IDs to **0–255**. NPC appearance uses the separate **256+** range. Do not expose all 512 graphics tiles as interchangeable map brushes.

Read graphics from the project's current `TILES.16`, including the user's artwork edits. Keep alternate-platform/custom renderer tilesets separate from authored map data. A future alternate-art preview must not bake artwork into maps or change IDs.

Reuse engine tile properties and existing audited editor data where practical. Names or behavior that cannot be established should remain **Tile N** or **Unknown**, not an invented description. Movement and visibility rules can differ by game context; show only verified properties and label their scope. A tile artwork thumbnail alone is not enough to promise that a tile is enterable or transparent to sight.

Right-click eyedropper updates the brush strip, selected browser item and recent list together. Selecting a brush never changes terrain. Changing the artwork resource refreshes thumbnails without resetting map navigation.

### Tools and commit behavior

| Tool | Intended behavior | Priority |
| --- | --- | --- |
| Pencil | Single-cell brush, continuous interpolated strokes, preview footprint | First |
| Eyedropper | Pick terrain from the clicked cell; right-click shortcut | First |
| Select | Rectangular selection with visible bounds and dimensions | Second |
| Rectangle | Outline or solid fill, preview until mouse release | Second |
| Flood fill | Iterative four-connected fill of the starting tile ID, restricted to selection when present | Second |
| Stamp / paste | Copy terrain within or between maps; preview destination and changed-cell count | Second |
| Line | Interpolated straight line with preview | Later |
| Multi-tile brushes | Saved project-local stamps, optional rectangular brush sizes | Later |

A gesture becomes one undo command. Escape cancels a pending gesture and restores its pre-gesture state. Losing pointer capture or focus must not leave a stroke half-committed. Releasing outside the canvas must still terminate the gesture safely. No-op gestures create no history entry.

Selections initially contain terrain only. Copying a room must not silently duplicate NPC schedules, dialogue links, combat metadata or dynamic objects. Show a ghost preview for paste; reject out-of-bounds placement by default instead of silently clipping it. Explicit clipping can be added later. Transforms should not rotate directional tile artwork automatically; defer rotation/mirroring until a verified tile substitution map exists.

Treat a world map's wrapping behavior as a separate, engine-verified rule. The first fill and selection implementation stops at the visible map boundary; do not accidentally introduce wraparound through byte arithmetic.

Proposed map-local shortcuts: **B** Pencil, **I** Eyedropper, **F** Fill, **R** Rectangle, **V** Select, **Space** temporary Pan, **Esc** cancel gesture/clear selection, **+ / −** zoom. Use the existing application Undo/Redo and platform-standard Copy/Paste shortcuts. Tool shortcuts must not fire while typing in a field or while another workspace has focus. Terrain “Delete” needs an explicit replacement tile; there is no universal empty terrain tile.

## NPC editing on the map

Replace **Inspect NPCs** with an explicit **NPCs** mode. In that mode, left-click selects an actor; dragging previews a new location and commits only on release. Terrain stays protected. Multiple actors on one cell open a chooser or cycle deterministically; do not silently select only the first record.

The inspector should show:

- Sprite preview, NPC slot, readable name when available, and dialogue ID.
- Selected schedule slot, its transition hour, and the location record it actually references.
- X/Y, floor and AI value; use named AI behaviors only after verifying them against the engine.
- **Locate on map**, **Move here**, **Edit conversation**, and an advanced record view.

Start with **Slot 0 / Slot 1 / Slot 2 / Slot 3** and show each NPC's real transition hours. Do not label these universally as morning/day/evening/night. Current files have three location records for four time slots: slots 1 and 3 share location record 1. Editing either must visibly explain that shared relationship. A drag must update only the selected location's X/Y/Z, preserving other positions, AI values, hours, sprite and dialogue linkage.

Show other schedule positions as optional numbered ghost markers. Indicate positions on another floor with a floor badge and a navigation action. Distinguish stored schedule positions from a simulation of where a wandering NPC would be at a particular hour. Actual-hour simulation is later work and must reuse the engine's schedule semantics.

Resolve NPC names through the companion `.TLK` and the new dialogue model; fall back to **NPC slot N** when unavailable. Keep NPC slot and dialogue ID separate: they are not interchangeable and multiple actors can reference the same conversation.

Do not infer that a zero-filled or unusual record is an unused NPC slot. Creating/deleting NPCs requires an audited allocation/absence policy; initially edit existing records only. Preserve unusual values in Advanced, and display diagnostics rather than rewriting them during navigation.

## Combat setup on the map

Use **Encounter** mode for combat maps, sharing the exact same selected map and canvas as Terrain. Replace the normal metadata table with typed controls and overlays:

- Party start markers numbered 1–6; choose the **North / East / South / West** entry configuration.
- Monster markers with slot, sprite and X/Y controls.
- Trigger markers with their linked changed-tile positions and replacement tile information.
- An entity list for overlapping markers and records that cannot be drawn.

Use marker colors, shapes and labels together so meaning does not depend on color alone. Selected triggers highlight their related cells. A read-only **trigger result preview** can show before/after terrain without applying those changes to the base map.

Before implementing trigger editing, verify the exact correspondence of the eight replacement tiles, trigger records and both changed-position sets against engine code and the audited Python editor. The current row labels alone are insufficient to establish runtime behavior. Do not treat every metadata byte as a position or every out-of-range coordinate as an error.

Dragging a marker changes only its verified coordinates. Terrain edits preserve all 21 metadata bytes after each row's 11 terrain bytes. Marker edits preserve unrelated metadata, unknown fields and padding. Sentinel/disabled records remain in an Advanced section; draw only coordinates valid for the 11×11 map. Do not clamp unusual values to the nearest tile or guess which values mean absent.

## Layers, diagnostics and safety

“Layers” are editor views of existing data, not a new map format. Terrain, NPCs, party starts, monsters, triggers, grid and changed-cell highlighting have separate visibility switches. Only the current editing mode permits writes to its data. Show the active mode beside the canvas and in the status bar.

Keep dynamic object lists, dungeon-specific formats, exits, moongates and other world metadata out of movable overlays until their storage and runtime semantics have been implemented. A chest-shaped terrain tile is not necessarily a separately editable object. Explain unsupported data without presenting inert tools as working features.

Useful diagnostics:

- Unrenderable/out-of-bounds NPC or combat positions, with a link to their record.
- Missing companion resources or dialogue references.
- Unverified AI/sentinel values, clearly distinguished from confirmed invalid values.
- Changed-cell count and a toggle to compare against the project's original terrain.
- Britannia chunk usage/capacity and failures associated with the proposed edit.

Britannia's serializer maintains paired `BRIT.DAT` / `DATA.OVL` changes, implicit water and shared chunks, with a limit of 255 distinct non-water chunks. Every tool must use that serializer. A failed edit restores the previous canvas and leaves both resources and undo history unchanged. Report the affected operation and capacity issue in plain language; do not let fill/paste partially succeed.

Retain unknown bytes and extension data exactly. Viewing, changing floors, toggling overlays and exporting pictures must never mark resources modified. Edits continue to go into the Workshop project and `.imperamod` packages, never directly into original game files.

Move image export into a compact **Map → Export** menu. Keep **Terrain PNG** and **Tile-ID PNG** distinct. If overlay export is added, label it **Preview PNG with overlays** and make the included overlays explicit. PNG import is later work: importing tile IDs needs exact dimensions and ID validation; arbitrary artwork must not be silently interpreted as terrain.

## Implementation structure

Extract the map workspace from the large `window.cpp`, as was done for conversations:

1. **Map document adapter:** page identity, terrain and typed NPC/combat records, source-byte ranges, and serializer-backed atomic edits. Reuse `U5::mapPages`, `worldMap`, `writeWorld` and the existing package/history contract.
2. **Map canvas:** painting, transforms, hit testing, gestures and overlays. Emit selection/edit proposals; do not write resource files directly.
3. **Tool controllers:** stroke interpolation, fill, shapes, selection and paste; share bounds and cancellation logic.
4. **Map workspace:** navigation, inspector, tile browser, minimap and diagnostics. One selected-map model drives all panels.
5. **View state:** persistent per-page settings, separate from game bytes and dirty tracking. Optional favorites/annotations live in backward-compatible project metadata; OS clipboard data is not project content.

Avoid rebuilding the entire workspace after every undo. Refresh changed document regions and inspector values while preserving widget focus and navigation. If the application still rebuilds the workspace initially, restore explicit view state, including the viewport center, rather than just scrollbar values.

A 256×256 world has 65,536 cells. Keep repaint work proportional to the visible region. Cache tile thumbnails and the minimap, update only changed regions, and run flood fill iteratively. Commit a brush drag once, not on every mouse event. Recompute world serialization/capacity on commit rather than on each hover. Large operations should provide bounded progress/cancellation if measurements justify it; do not add background threading before there is evidence it is needed.

## Delivery plan

| Phase | Deliverable | Acceptance gate |
| --- | --- | --- |
| 1 — Dependable canvas | Extract document/canvas; continuous strokes, hover, synchronized eyedropper, pan/zoom/fit, explicit state restoration, map navigation and compact brush inspector | Existing formats/exports work; fast drags have no gaps; no state loss on undo or dialogue return |
| 2 — Terrain tools | Selection, rectangle, iterative fill, copy/paste preview, favorites/recent brushes, minimap, change comparison | Each operation is atomic and undoable; cancellation and capacity failures leave data unchanged |
| 3 — NPC integration | Select/move existing actors, schedule inspector, floor navigation and conversation links | Shared location semantics are visible; only intended record fields change |
| 4 — Combat integration | Typed party/monster/trigger views, marker placement and advanced metadata | Verified metadata mappings; terrain and unknown bytes preserved; all entry directions usable |
| 5 — Polish | Verified tile categories/properties, accessibility, export refinements, optional display previews | Keyboard workflows and small-window layouts remain usable; no misleading runtime simulation |

Implement Phase 1 first, with the eventual three-panel layout, instead of attempting all tools and entity types at once. No automatic wall matching, terrain generation, arbitrary map resizing, new map formats or full game simulation is part of this plan.

## Validation and review

- No-op round trips across world maps, every settlement floor and combat maps preserve bytes, including padding and extensions.
- Canvas picking agrees with rendering at every supported zoom and scroll position; outside clicks cannot edit an edge cell. Continuous drags, reverse drags, mouse release outside, focus loss and Escape are covered.
- Undo/redo restores both members of Britannia's paired-resource edit and preserves zoom, brush, selection, grid, schedule view and viewport center.
- Fill, selection and paste respect map bounds; large world edits, implicit water/shared chunks and over-capacity failures are covered. No partial project mutation on failure.
- NPC drag tests cover shared slots 1/3, signed basement floors, overlapping actors, missing dialogue and untouched schedule fields.
- Combat tests exercise all four party-entry sets, marker overlap, invalid/sentinel records, trigger linkage and exact preservation of non-target metadata.
- Cross-platform Qt smoke tests remain asset-free; optional original-game fixtures exercise real maps. Manually review world, basement, multi-floor castle and combat workflows on Windows, macOS and Linux.
- Visual review includes a small window, a high-DPI display, an edited tilesheet and a world map with the minimap open. Validate shortcut focus and keyboard access to the same editing operations.

This document specifies planned behavior. Runtime interpretation that is not yet verified must be audited before its corresponding UI is enabled.


## Phase 1 outcome and next step

The workspace now has searchable grouped map navigation, a terrain document
adapter, continuous/cancellable canvas gestures, hover and brush feedback,
synchronized tile picking, pan/zoom/fit controls, coordinate navigation and
session-only per-page view state. Existing NPC schedule overlays and terrain
exports remain available. Returning from another resource and undo/redo restore
the map view. The native tests cover gesture gaps/cancellation, picking and
anchored zoom, read-only navigation, paired world edits and preservation of
combat metadata. The workspace still rebuilds on undo, restoring explicit state;
in-place refresh is an optimization to assess later.

## Phase 2 outcome

The terrain workspace now supports rectangular selection, solid/outline rectangle
painting, selection-bounded four-connected iterative fill, and typed terrain
copy/paste. Paste shows a ghost and changed-cell count, rejects map-edge overflow,
and copies no NPC, object or combat metadata. Escape/focus loss cancels pending
previews; each completed edit uses the existing serializer and undo transaction.
Over-capacity Britannia edits restore the canvas and leave both resources intact.

A cached terrain minimap supports click/drag navigation and shows the visible
region and selection. The brush browser has session-only Favorites and Recent
filters. Change highlighting and counts compare edited terrain to the original
resource, including original Britannia chunk mappings. Selection, comparison and
rectangle mode survive navigation and undo/redo. These view preferences and
brush lists never become exported game bytes.

Native tests cover reverse selections, cancelled selection/rectangle/paste,
outline/no-op rectangles, selected and world-sized fill, diagonal separation,
cross-map clipboard use, invalid clipboard payloads, edge rejection, world
capacity rollback, untouched combat metadata, comparison baselines, minimap
navigation and state restoration. Platform visual review remains appropriate
before shipping; the cloud validation uses Qt's offscreen Linux renderer.

## Phase 3 — NPC integration (implemented)

Replace the current Inspect NPCs action with an NPC mode on the same canvas:

1. Select existing actors without changing terrain. Provide a chooser when
   several actors share a cell, and identify each by NPC slot and resolved
   conversation name (with a slot fallback).
2. Add a schedule inspector with the real transition hours, X/Y, signed floor,
   AI value, appearance and dialogue ID. Clearly show that time slots 1 and 3
   share location record 1; changing one updates the same stored location.
3. Drag an actor with a placement preview; release commits a single undoable
   X/Y/Z edit to the selected location. Escape/focus loss cancels it. Preserve
   all other locations, AI values, hours, sprite, dialogue linkage and padding.
4. Add Locate / Move here, floor navigation, optional other-schedule-position
   ghosts, and Edit conversation / Back to map links that retain selection and
   viewport. Stored schedule locations must not be presented as a simulation of
   a wandering actor's position.

The acceptance gate is exact byte preservation outside the intended NPC location
fields, shared-slot behavior, basement-floor correctness, overlapping actors,
missing dialogue, and undo/redo without view or selection loss. Creation/deletion
of NPCs requires a separate audited allocation policy; Phase 3 begins with
existing actors. Combat-marker editing remains Phase 4.

### Phase 3 outcome

The NPC inspector and canvas now select existing records, resolve dialogue names,
choose among overlapping actors, preview dragging, and commit atomic X/Y/Z
moves. The inspector shows real transition hours, raw AI, sprite and dialogue ID,
shared-slot semantics, signed floors and optional numbered position ghosts.
Locate and explicit Move here navigation preserve the selected actor and slot
across floors; conversation/advanced-record links retain map view on return.
Terrain paste is disabled in NPC mode. Missing dialogue and unusual positions
are preserved rather than silently rewritten.

Tests verify whole-resource byte preservation, shared slots 1/3, signed basement
floors, valid destination bounds, overlap selection, drag/focus/Escape cancellation,
atomic undo/redo, floor changes, missing conversation fallback and conversation
return. The engine's `NpcScheduleFmt`, `NPC_0000_LoadNpcFile` and `NPC_12e0` confirm
the record layout and shared-location mapping. No runtime wandering simulation or
NPC allocation policy has been introduced.

## Phase 4 — Combat integration (implemented)

Add an Encounter mode beside terrain editing for combat maps:

1. Audit the engine and editor metadata mappings before exposing controls,
   especially trigger records, the eight replacement tiles and both changed-cell
   position sets. Keep sentinel/disabled and unknown records in Advanced.
2. Show typed, selectable markers for six party starts, monsters and triggers.
   Support all four North/East/South/West party-entry configurations. Provide an
   entity list for overlapping markers and records outside drawable bounds.
3. Drag markers with previews and cancellation, committing only the verified
   coordinate bytes as one undoable edit. Preserve terrain, unrelated metadata,
   row padding and unusual values exactly.
4. Selecting a trigger highlights its linked cells and replacement tiles. Add a
   read-only trigger-result preview that shows the outcome without mutating the
   base terrain. Keep labels and marker shapes alongside colors.

Acceptance requires exact byte comparisons for every edit kind, all entry
configurations, overlap/sentinel handling, trigger linkage, cancellation and
undo/redo while preserving view state. Terrain tools must continue to preserve
all combat metadata. This remains authoring of stored encounter data, not a
simulation of combat AI or dynamic object state.


### Phase 4 outcome

Combat maps offer Encounter mode with labeled party, monster and trigger markers,
a complete record selector, all four entry directions, cancellable drag previews
and atomic coordinate edits. Selecting a trigger displays its replacement tile
and both linked positions; a read-only result preview never alters stored terrain.
Unknown, disabled and out-of-bounds records remain available in Combat setup.
The engine audit corrected the old party-row labels: rows 1/2/3/4 mean
East/West/South/North. Monster coordinates occupy rows 6/7; trigger coordinates
row 8; both target sets rows 9/10. Each trigger uses its row-0 replacement tile
for both valid targets. Runtime encounter overrides and AI are not simulated.

Workshop remembers a successfully opened game-files directory in native user
settings and restores it on startup. Explicit --game/--project paths take
precedence. A missing or invalid remembered directory leaves the welcome screen.

## Phase 5 — Polish (implemented)

1. Audit engine tile properties and provide verified terrain categories and
   searchable descriptions without guessing at unknown IDs.
2. Improve keyboard-only navigation, focus indicators, marker legibility and
   compact-window layouts; retain labels alongside color distinctions.
3. Refine image exports and diagnostics so terrain, encounter previews and
   authoring overlays are clearly distinguished.
4. Consider optional display previews using the engine's presentation settings,
   while keeping stored resource bytes and runtime simulation separate.

Acceptance: keyboard and small-window workflows remain usable, exports reflect
what their labels promise, tile labels are backed by engine data, and all existing
lossless-format, editing, undo and package tests continue to pass.


### Phase 5 outcome

The terrain browser offers name/number search and Overworld, Ground, Buildings,
Objects, and Other filters. All 256 terrain tiles
are cataloged using the engine definitions and an audit of the original DOS
artwork, including animation frames, wall sections, and masks.
These categories describe the authoring palette, not collision or visibility
rules, and custom graphics may depict tiles differently.

The canvas supports arrow-key targeting, Enter to apply a tool or move a selected
character/encounter marker, Shift+arrows to extend terrain selection, standard
copy/paste, and a visible keyboard focus cursor. Tool shortcuts remain local to
the canvas, so typing in inspector fields is safe. Toolbar rows are separated to
reduce minimum width; NPC inspector forms wrap on narrow layouts. Search,
category and tool controls carry accessible names.

Terrain PNG and Tile-ID PNG now render from the document rather than the current
preview buffer. Visible preview PNG explicitly includes grid, markers and other
canvas overlays; it is not a terrain-import image. Whole-map terrain exports
remain at native tile resolution, independent of zoom.

NPC schedules use four numbered changes with actual start hours, a directly
editable start-hour field and scheduled destinations. Changes 2 and 4 share a
destination and behavior, not a start time. This coupling is stated directly.
Unknown hours and behavior codes remain intact; technical fields are explained
in Advanced schedule details rather than presented as the normal map inspector.
The engine selects a destination from the schedule transition times
(`NPC_12e0`); this view is authoring, not a simulation of walking, wandering or AI.

Optional CRT/display previews were considered and deferred to in-engine testing:
a Workshop-only approximation could imply that it represents actual runtime
lighting, animation or encounter behavior.

## Phase 6 — Package validation and in-engine testing (implemented)

The original map-editor plan ended at Phase 5. A useful next phase would connect
editing with testing a mod, rather than add more cosmetic editor options:

1. Add a validation report before export, linking problems to the relevant map,
   schedule, conversation or resource. Distinguish errors from unusual-but-valid
   values; never silently repair unknown bytes.
2. Preview the package contents and override/conflict order, showing exactly what
   will change without modifying the original game files.
3. Provide a configurable Impera executable and an explicit Test mod action using
   an isolated test package/save folder. Preserve the user's normal Mods folder
   and saves; show launch errors and runtime log locations.
4. Add regression coverage for the export/launch handoff and platform-specific
   paths. Runtime visuals should come from Impera itself.

### Phase 6 outcome

Mod Tools → Validate / Package Preview shows errors, warnings and the exact
whole-resource replacements in the package. Double-click a resource to inspect
it. Changed resources are checked against the on-disk originals; structural
validation and a roundtrip through the engine decoder block invalid exports.
Unusual NPC hours/destinations are warnings, never silently corrected.

Installed packages are inspected in the engine's bytewise filename order.
The report shows accepted packages, resource overlaps, invalid packages and the
engine's 128-package/64-MiB memory limits. Overlap rejects the later package in
full, rather than merging or choosing the latest resource. Export Package opens
the report before writing. Warnings permit export; errors block it.

Mod Tools → Set Impera Executable remembers the executable/AppImage. Test Mod
prepares a unique private cache folder, copies flat DOS runtime resources, checks
them against the project's original snapshot, and writes exactly one generated
package. Original Mods, modern saves, legacy saves and personal settings are not
copied. Native engine startup honors U5D_RUNTIME_DIR before opening logs or
settings; packaged Linux/macOS launchers honor it too. U5D_DATA_DIR selects the
private game copy. Existing Linux launchers are additionally isolated through
XDG_DATA_HOME; macOS app bundles launch their native engine directly.

Test output/error logs and test saves remain in the session folder for debugging.
Open Last Test Folder provides access; Stop Mod Tests asks the launched processes
to exit. Workshop owns test processes, so closing Workshop stops remaining tests.
A new character is needed to exercise modified starting-state data. Test folders
contain copied game data and are not distribution packages. No runtime AI or
lighting is simulated by Workshop itself.

New Mod reuses a valid remembered game folder. Change Game Folder explicitly
opens the picker; invalid/moved folders fall back to it. Successfully opened
projects also update the remembered source folder.

Validation includes exact package roundtrips, resource-linked failures, installed
package conflicts, independent session directories, exclusion of personal data,
source-change rejection/cleanup, and Linux/macOS launcher isolation. Native
engine startup isolation is checked independently. The Windows empty-array
compile failure is fixed with a standard std::array; full MSVC validation remains
in the cross-platform release workflow.

### Compact map toolbar and palette

Tools are icon buttons with descriptive hover help and accessible names. The NPC
schedule selector is labeled “NPC schedule:” and sits to the right of the floor
selector. Floor choices are restricted to the current location; clicking a
location heading in the map tree opens its main floor (ground floor when present). It shows start hours when characters with
destinations on the current floor share all four timings; otherwise it shows
changes 1–4. Selecting a character never changes the meaning of these labels.
The inspector still shows that character’s exact start hour and shared destinations.

Palette cells contain only tile artwork. Hover for the name, category, and ID;
search still accepts names or numbers. Map navigation, view controls, terrain
actions, NPC fields, and encounter controls include explanatory tooltips.

The tool icon row also contains Copy, Paste, Clear Selection, Highlight
Changes, and Zoom. Export remains beside the floor/schedule controls. Grid and Rectangle Outline
are no longer UI options; rectangles are filled and the tile grid is hidden.

Overworld includes small location icons and all tile IDs present in the decoded
Britannia and Underworld maps, including original and edited terrain and animation
frames. Unused chunk storage is excluded. Overworld overlaps the four exclusive
base categories: Ground (terrain/floor surfaces), Buildings (structural components),
Objects (furniture/fixtures/scenery), or Other (effects and rendering masks).
