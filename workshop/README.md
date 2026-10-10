# Impera Workshop — native mod authoring (first version)

Impera Workshop is a C++17 / Qt 6 desktop application, not a Python wrapper.
It reads original DOS Ultima 5 resources, keeps edits in a project, and exports
**`.imperamod` packages**. Neither editing nor exporting rewrites the game files.
The app is an initial implementation; it is not yet a finished replacement for
all of the specialized editor interfaces.

## Build and run

Install a C++ compiler, CMake 3.16 or newer, and Qt 6.2+ Widgets development tools.
On Ubuntu/Debian, the additional development package is `qt6-base-dev`.
On Windows/macOS, use the Qt installer and set `CMAKE_PREFIX_PATH` to its kit.

Build Workshop independently (SDL and Python are not required):

```sh
cmake -S workshop -B workshop-build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build workshop-build --config Release --parallel 3
ctest --test-dir workshop-build -C Release --output-on-failure
```

The executable is `workshop-build/ImperaWorkshop` on Linux,
`workshop-build/Release/ImperaWorkshop.exe` on Windows, or
`workshop-build/ImperaWorkshop.app` on macOS. Development builds require Qt runtime libraries. GitHub releases also provide
ready-to-run Workshop downloads beside Impera for all four platforms, with Qt
bundled: Linux AppImage, Windows portable ZIP and macOS app bundles. Launch
`Run Workshop.cmd` on Windows, `ImperaWorkshop.app` on macOS, or the executable
AppImage on Linux. Release deployment uses Qt 6.8.3 and
`-DIMPERA_WORKSHOP_DEPLOY_QT=ON`; normal local builds still support Qt 6.2+. The
Windows resource, macOS bundle and embedded window icon use Impera's artwork.

Alternatively, add `-DIMPERA_BUILD_WORKSHOP=ON` to the engine's CMake configure
command. Normal engine builds do **not** need Qt. CI builds and tests the native
application on Linux x86_64, Windows x86_64 and both macOS architectures.

```sh
workshop-build/ImperaWorkshop --game '/path/to/Ultima 5'
workshop-build/ImperaWorkshop --project my-mod.imperaproject
```

## Authoring workflow

1. **New Mod**: choose the original DOS game directory. Workshop does not load
   installed mods into the base game; you can explicitly import a package.
2. **Name Mod**: give the project a name used in Impera's diagnostics.
3. Choose a resource from the searchable library and edit it.
4. **Save Project** creates `.imperaproject` JSON containing the original folder
   reference/checksums and changed resources. Keep this project for future editing;
   it requires the same original files when reopened. It is not a distribution file.
5. **Export Package** creates a sparse, validated `.imperamod` file.
6. Copy the package into **`<selected Ultima 5 folder>/Mods/`**, then restart Impera.
   Create `Mods` if it does not exist. To disable a mod, move its package out and
   restart. There is no in-game mod-selection UI yet.

Keyboard: **Ctrl+N** new project, **Ctrl+O** open project, **Ctrl+S** save,
**Ctrl+Z** undo, **Ctrl+Shift+Z** redo, **Ctrl+E** export. Qt uses the usual Command
shortcuts on macOS for standard editing/project actions. Dialogue edits apply after a brief typing pause; **Apply** commits immediately.
Story edits use **Apply**. Navigation, saving and export check invalid or unapplied
drafts rather than silently discarding them. Package imports become undoable edits.

## Workspaces

| Workspace | Available in this first version |
| --- | --- |
| Maps | Searchable world/settlement/combat navigation; continuous painting, synchronized eyedropper, hover/brush preview, pan, anchored zoom, fit view, coordinate navigation, per-map view restoration and per-stroke undo; terrain selection, solid/outline rectangles, bounded flood fill, typed copy/paste previews, minimap, favorite/recent brushes, original-change highlighting; terrain and tile-ID PNG exports |
| Settlement NPCs | NPC mode with actor selection, overlap chooser, drag previews and atomic moves; schedule inspector, signed floors, other-position ghosts, conversation links and advanced record editing |
| Combat setup | Edit starting party positions for four directions, monster tiles/positions, triggers and changed-tile metadata; retain all padding/sentinel bytes |
| Graphics | All DOS `.16` image containers and 512 tiles; thumbnail gallery, EGA pixel painting, PNG import/export, full tilesheet import/export, one-bit alpha masks |
| Conversations | Searchable NPCs, Basics/Topics/Questions outline, shared keyword aliases, ordered text/action forms, reference-safe question deletion, byte budgets, diagnostics, Advanced source, conversation sandbox and DOS font preview |
| Story | Twenty fixed-offset text pages; original capacity, offsets, padding and terminators retained |
| Starting state | Party position, time, supplies, plot items, reagents, moonstones, shrine/dungeon flags, Shadowlord settings; sixteen character records, statistics/equipment, safe party joining/removal |
| Resource inspector | Paged hex editing, per-edit undo, importing outputs from the Python editors, reverting a resource; other resource types remain available here |

### Map workspace controls

Use the Maps tree or page selector to switch locations/floors. The tile browser
accepts decimal or hexadecimal IDs and shows the active brush separately. Maps
use edited project artwork from `TILES.16`; only terrain IDs 0–255 are paintable.

- Left-drag paints one continuous, undoable stroke; right-click picks a tile.
- **B** selects Pencil and **I** selects Eyedropper while the map has focus.
- Middle-drag, **Space+left-drag**, or the Pan tool moves the view without editing.
- **Ctrl+wheel** zooms around the pointer; **+ / −** zoom around the view center.
- Wheel scrolls vertically; **Shift+wheel** scrolls horizontally. Scrollbars remain available.
- Choose **Fit** for the whole map, or an integer zoom for detailed work. Fit uses crisp nearest-neighbor rendering.
- **Escape** or focus loss cancels the entire pending stroke. A failed edit rolls back its preview.
- **Go** centers the entered X/Y cell. Brush, zoom, tool, schedule slot and view center are remembered separately per map page during the session, including across undo/redo.
- **Back to map** returns from other resource editors without losing map navigation.
- **Export** distinguishes terrain PNG from tile-ID PNG; NPC overlays are excluded.

Terrain tools are map-local: **V** selects a rectangle, **R** paints a rectangle,
and **F** fills four-connected cells of the clicked tile ID. A selection constrains
Pencil, Rectangle and Fill; **Escape** clears it when no gesture is pending.
Selection bounds and dimensions appear beside the map.

Use platform-standard **Copy / Paste** shortcuts (Ctrl on Windows/Linux, Command
on macOS) while the map has focus, or the toolbar buttons. Copy includes only
terrain IDs. Paste previews its changed-cell count and accepts placement with a
left-click; it rejects placement outside map bounds rather than clipping, and
**Escape** or focus loss cancels it. Paste can place terrain outside the source
selection or into another map; it never copies NPCs or combat records. Each
terrain operation is one undo command; rejected operations leave resources intact.

Click/drag the **minimap** to navigate; the white outline shows the viewport.
**Highlight changes** marks terrain differing from original game files in orange,
with a changed-cell count. **Favorite** adds/removes the current brush; the browser
can show **All tiles / Favorites / Recent** (the last 16 brushes used). Favorites
and recent brushes last for the current project session and are not mod content.
Selection, comparison and rectangle mode also survive map navigation and undo.

### NPC mode

Choose **NPCs** in the map tool selector to work with existing NPC records. Click
an actor or select its slot in the inspector. Overlapping actors open a chooser
when none is selected; subsequent drags move the selected actor. The record list
also lets you select another actor sharing that tile.

Drag previews a destination; release commits one undoable move. **Escape**, focus
loss or releasing outside the map cancels it. NPC mode protects terrain, including
paste. Moves change only the selected location's X/Y/Z, preserving other
positions, AI, hours, appearance, dialogue linkage and unknown bytes.

The inspector resolves names from the companion conversation file, displays all
four real transition hours, and shows raw AI values. **Slots 1 and 3 share location
record 1**: moving either changes both. Floors are signed; **−1** is a basement.
**Locate on map** navigates to the selected position's floor. Enter X/Y/floor and
use **Move here** to relocate, including between a settlement's existing floors.
**Other schedule positions** shows numbered slot ghosts on the current floor,
with other-floor positions listed in the inspector. These are stored positions,
not a simulation of wandering NPCs.

**Edit conversation** opens the linked dialogue by its ID; **Back to map** restores
actor, schedule slot and viewport. **Advanced NPC record** opens the existing
record editor for AI, transition hours, appearance and dialogue ID. Missing
conversations disable the link; unusual stored positions remain visible in the
inspector without being clamped. No NPC creation/deletion is added in this phase.

Graphical combat-setup editing is the next phase. View preferences do not change
game bytes or get exported into a mod package.

Basements use the engine's actual floor numbers. Britannia changes rebuild both
`BRIT.DAT` and the chunk-index bytes of `DATA.OVL` together; implicit-water and
shared chunks are handled. More than 255 distinct non-water chunks is rejected.
Underworld and settlement extension bytes are retained.

Artwork must use the exact DOS EGA palette. Alpha is binary; `TILES.16` has no
alpha channel. Alternative-platform/custom PNG tilesets in Engine Options are
independent of the DOS graphics resources edited here.

Conversations use an NPC list, a conversation outline and a detail editor. Edit
normal text without compression tags; unchanged blocks keep their exact original
bytes. Actions have explicit amount, inventory and question-destination fields.
Topics and replies can be reordered, duplicated, added or deleted. Comma-separated
keywords share one response. Question names are project-only annotations. Deleting
a referenced question requires retargeting its callers. Undo/redo includes edits
and annotations.

Diagnostics identify missing destinations, invalid operands, byte limits,
overlapping keywords and other problems. Existing original oddities remain
loadable and are retained as raw sections. **Advanced** exposes lossless tagged
source for the selected entry or explicitly the whole conversation; unknown bytes
remain available there. `<Entry>` separates entries, `<New Line>` is an in-game
line break, word tags retain compression and `<Byte N>` exposes exact bytes.
Labels and action operands retain the original DOS semantics.

**Test Conversation** runs in a bounded sandbox with configurable party names,
gold, karma and introduction state. Click a trace row to inspect its source.
Inventory/world effects are traced rather than applied, and recruitment is
reported rather than fully simulated. The DOS preview uses the selected game's
original bitmap font, with approximate 18-column wrapping; it is not a complete
emulation of the game's quotation, pagination or rune rendering. Testing never
modifies the game or project. See [the dialogue design](../docs/workshop-dialogue-ui.md).

Starting-state mods primarily affect **new games**. The original character-
creation sequence still overwrites the fields it traditionally initialized.
Existing saves retain saved state, object lists and NPC data. Initial world
object overrides seed a fresh runtime save directory; they do not overwrite an
existing player's object files. Removing a mod does not undo changes already
captured in a saved game.

See [the map workspace UI plan](../docs/workshop-map-ui.md) for the proposed
canvas, terrain tools, NPC schedule inspector and combat placement interface.

## Scope and next work

This branch establishes native editing and end-to-end package loading. It covers
the audited formats and supports bringing existing editor outputs into packages.
An optional graphical dialogue flow diagram and interactive combat-entity
placement tools remain future work; the conversation outline and combat tables
provide structured access now. There is no flood fill, multi-tile selection, plugin scripting,
package dependency system, runtime hot reload, or mod manager yet. Unknown resources and fields use the byte inspector.
Windows/macOS CI is configured; local interactive validation was on Linux only.

## Validation

Native tests cover compression width/reset boundaries **against the actual C
engine decoder**, paired graphics offsets/masks, PNG alpha/palette rejection,
dialogue token/control roundtrips, story offsets, map chunk allocation,
projects/packages/checksums and widget loading without mutation. Set
`U5_GAME_DIR` to your original game directory to include optional full-resource
and all-workspace checks. No game assets are included in generated test fixtures.

```sh
QT_QPA_PLATFORM=offscreen U5_GAME_DIR='/path/to/Ultima 5' \
  ctest --test-dir workshop-build --output-on-failure
```

The engine also has a `mod_packages` CTest covering corrupt/truncated packages,
all-or-nothing mounting, load order, conflicts, reloads and read/write isolation.
See [the package format](../docs/mod-packages.md) for the shared engine contract.


Combat maps now include **Encounter** mode: choose North/East/South/West entry,
select party starts, monsters or triggers from the map or record list, and drag
to move with one undoable edit. Escape cancels a drag. Trigger selection shows
both linked cells and the replacement tile; **Preview selected trigger result**
is read-only. **Combat setup** retains raw and unusual metadata values.

The game-files directory is remembered across startups. Explicit `--game` or
`--project` arguments override that preference. If the remembered folder moves
or becomes invalid, use **Open game / New mod** to select it again.


### Map editor polish

Find terrain tiles by name or number and filter by category. All 256 terrain tiles
are categorized from engine definitions and the original DOS artwork. The compact
palette shows artwork only; hover for names, categories, and IDs. Tool icons have
descriptive tooltips. The labeled NPC schedule selector beside the floor list shows shared start hours
or changes 1–4 when NPC timings differ.
On the map canvas, use arrow keys to target a tile and Enter to apply the tool
(or move the selected character/encounter marker). Shift+arrows extend a terrain
selection; standard Copy/Paste and Escape work alongside B/I/V/R/F tool shortcuts.
These shortcuts do not intercept typing in inspector fields.

**Terrain PNG** exports stored terrain artwork; **Tile-ID PNG** exports one
numeric tile ID per pixel. Both exclude NPCs, markers, grid and trigger previews.
**Visible preview PNG (includes overlays)** captures only the visible canvas,
including its current authoring overlays, at the current zoom.

NPC schedules have **four schedule changes** and **three destinations**. Each
change has its own start hour. Changes **2 and 4 share a destination and behavior**,
so moving one moves both. Their start hours remain independent. The map inspector
shows actual start hours and offers a Start hour field; unusual existing values
are preserved. Advanced schedule details explain the underlying behavior and
appearance codes. The editor does not simulate the character's runtime movement.

### Validate and test a mod (Phase 6)

Open **Mod Tools → Validate / Package Preview** to review replaced resources,
format errors, unusual schedules and conflicts with installed packages.
Double-click a resource to inspect it. Export Package shows this report first;
errors block export, while warnings preserve unusual values. Impera rejects
packages that overlap resources in an earlier package; it does not merge them.

Use **Set Impera Executable** once, then **Test Mod → Launch isolated test**.
Select the native Impera executable, Linux AppImage, or macOS Impera app/native
engine. Workshop copies game resources into a unique private test folder and
loads only the generated mod. Normal Mods, saves and settings stay untouched.
Create a new character to test changes to the starting game state.

**Open Last Test Folder** opens the retained saves and logs (`LOG.TXT`,
`engine-output.log`, `engine-errors.log`); older Linux launchers may put runtime
files in its `impera` subfolder. **Stop Mod Tests** requests that test processes
exit. Closing Workshop also stops remaining tests. Delete old test folders when
you no longer need their logs/saves. Distribute the `.imperamod` file, not these
folders containing copied game data.

**New Mod** reuses the remembered game folder. Use **Mod Tools → Change Game
Folder** when you actually want to select a different installation.

See [the implementation plan](../docs/workshop-map-ui.md) for the isolation and
validation contracts.

Copy, Paste, Clear Selection, and Highlight Changes share the tool icon row.
Zoom sits beside the tool icons; Grid and Rectangle Outline controls are removed.
