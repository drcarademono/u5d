# Workshop resource editors: implementation plan

Status: proposal, not implemented. Continue on `feature/native-modding` unless
instructed otherwise. This extends the [Other Resources audit](workshop-other-resources-audit.md)
and existing [map workspace plan](workshop-map-ui.md).

## Outcome and scope

A modder should be able to author all supported content in the 18 audited files
through named, contextual editors and export a package that actually changes
Impera. Preserve original game installations, existing modern saves, and existing
packages. Keep advanced import/export, but do not confuse package acceptance
with effective runtime support.

“All files covered” means every file has an identified schema, editor or explicit
compatibility status. It does not mean arbitrary DOS overlay bytes become engine
configuration. Hercules fonts and unknown overlay/scene fields require research;
they must remain visible with an explanation until their semantics are verified.

## Shared user experience

Use the current native Qt style: readable interface text, crisp artwork and
resizable panes. The game preview can use the original fonts; the editing UI
should not use oversized game text. Reuse map tools and existing keyboard
shortcuts instead of inventing different controls for every editor.

The left navigation should describe content:

| Workspace | Content |
| --- | --- |
| Maps | World, settlements, combat maps, with Signs and Starting objects layers |
| Dungeons | Named dungeons → levels → linked combat rooms |
| Descriptions | Terrain/objects and actor appearances |
| Narrative | Character creation, endings, karma-dependent speeches, shop messages |
| Special scenes | Blackthorn, shrines, Codex, final rescue, creation sequence |
| Fonts | Regular, runic, legacy Hercules; proportional font preview support |
| Game rules and tables | Only verified, runtime-supported overlay concepts |
| Advanced resources | Raw data, unsupported fields and resource import/export |

Avoid duplicate editing surfaces: a scene's dialogue can be opened from Narrative
or Special scenes, but both routes use the same document and undo history. Signs
appear under their location, not only as an isolated `SIGNS.DAT` filename.

Common layout: searchable navigation on the left, canvas/text editor in the
center, contextual properties and preview on the right. Preserve location,
selection, zoom and scroll when following links or undoing. On small windows,
use inspector tabs and collapsible navigation rather than adding permanent rows.

Every action/control gets a concise tooltip explaining its effect. Show useful
names in controls, numeric IDs in details/tooltips, and unresolved values as
“Unrecognized value (… )” rather than guessing. Read-only properties explain why.
Use current game/project artwork in previews; keep inspection usable when custom
artwork cannot be decoded.

Provide consistent actions: Undo/Redo, Revert selected entry, Revert resource,
Find usages, and Open in map/scene when applicable. Revert resource must state
its scope. Multi-field changes and cross-resource operations form one undoable
transaction. New entries get meaningful default labels; annotations that do not
change game behavior stay in project metadata.

Diagnostics should identify content, explain the consequence, and navigate to
the relevant field. Example: “Empath Abbey sign: text does not fit the game sign
panel,” rather than an offset-only error. Separate invalid data, likely problems,
and intentional limitations. Show byte budgets only where they constrain an edit.

## Architecture and data contracts

Split new codecs and widgets out of `window.cpp`/`formats.cpp` as they are added.
Keep `WorkshopWindow` responsible for navigation and document selection.

- Add a resource capability registry shared by tree grouping, editor dispatch,
  validation and package preview. Record authorable concepts, runtime consumers,
  new-game requirements, legacy-only status and supported schema revisions.
- Add typed codecs/documents for dungeons, signs, descriptions, narrative entries,
  special scenes, fonts and world objects. They retain original bytes, unknown
  regions, padding, shared references and source spans.
- Use the existing `Project::resources` model and package writer. Do not maintain
  competing saved copies of a resource inside editor widgets.
- Factor reusable text editing/preview, token insertion, searchable entry lists,
  map overlays and glyph canvas components. Different text formats use different
  encoders even when they share the same widget.
- Add a small shared C format/schema layer for runtime addressing tables and
  supported feature definitions. Workshop's C++ documents wrap it. Avoid adding
  another fragile source-code regex extractor for these definitions.
- Add project-schema migration only when project metadata needs it. Preserve
  resource bytes in old projects and explain unmigratable annotations.

A no-op decode/encode must reproduce the original bytes, including opaque data.
For a changed document, write only understood sections, unless the schema permits
rebuilding offsets. Unknown regions must never be silently zeroed or discarded.
Apply import, edit and export validation through the same codecs. A malformed
resource should produce diagnostics and retain access to the raw inspector,
not crash Workshop or silently replace the file with defaults.

## Phase 1 — Establish reliable runtime compatibility

### Britannia chunk index (`DATA.OVL` and `BRIT.DAT`)

Keep existing Workshop exports working: teach Impera to read the modded 256-byte
index at the verified DOS overlay offset `0x3886`. Do not switch to a new package
resource just to fix this gap.

1. Trace package initialization, world loading and mod switching; add an explicit
   index initialization function after resource overrides are available.
2. Start from a pristine copy of the compiled index on every initialization.
   If an applicable mod replaces `DATA.OVL`, read and validate its index. Use the
   compiled default when no override applies, preserving unmodded behavior.
3. Validate overlay span, all-water sentinel, referenced chunk spans in effective
   `BRIT.DAT`, and existing chunk-count/format constraints. Validate the effective
   pair even when the resources originate in different packages.
4. Do not execute the overlay or treat its unrelated bytes as active engine data.
   An invalid override should reject the package with an actionable error through
   the existing package failure policy, not silently run the wrong map.
5. Reset cached map/index state on the supported reinitialization path. Keep
   resource precedence and overlap rejection consistent with existing packages.

Acceptance: change a chunk assignment in Workshop, export, run Impera and verify
the corresponding tiles; cover a water chunk, a boundary chunk, malformed index,
base game without `DATA.OVL`, and removal of the mod. This must exercise the actual
engine loader, not just Workshop's decoder.

### World objects and new games

Document current new-character state construction before changing it. Define a
single new-game initialization path with an explicit base-game baseline.

- Preserve unmodded behavior. Applying a mod must never overwrite an existing
  player's world files or reinterpret a loaded modern save as a new game.
- Resolve modded starting state at character creation, independently of whether
  `SAVEGAME` files already exist. Initialize both world-object lists together
  before creating the ordinary modern save slot.
- Recommended precedence: start from the normal base initialization; apply an
  explicit mod replacement for `BRIT.OOL` or `UNDER.OOL` to that world's initial
  objects; use effective `INIT.OOL` for the normal underworld seed when there is
  no explicit `UNDER.OOL` override. State this rule in Workshop and diagnostics.
- Keep regular travel and save loading governed by saved world state. Do not
  repeatedly apply resource templates on each map entry.
- Confirm how `INIT.GAM` object slots interact with Britannia initialization;
  reconcile them in the initializer rather than producing two contradictory
  starting lists. Add a diagnostic for incompatible edits.

Acceptance: identical new-game results in clean and previously used installations,
correct precedence for each combination, no change to old saves, no legacy-save
UI reintroduction, and a modern initial save containing the authored objects.

### Capability reporting

Mark `DATA.OVL` as “Britannia map index supported; other tables not yet supported.”
Mark OOL edits as “Applies to new games.” Package preview lists these effects.
Use precise minimum-engine requirements once a package depends on new support;
do not assign a release version until that version is actually chosen.

## Phase 2 — Shared codecs, validation and editor shell

Add the capability registry and document interfaces before multiplying widgets.
Extract shared presentation helpers without a broad rewrite of working editors.
Implement reusable text preview with normal, runic and proportional modes; maintain
format-specific tokens and encoders. Unsupported characters must be highlighted,
not silently transliterated.

Add common tests for unchanged round trips, truncated input, offset overflow,
undo/redo, project save/reopen and package import/export. Use synthetic redistributable
fixtures in CI and optional original-data checks locally; do not ship copyrighted
game data as test fixtures.

Acceptance: navigation and diagnostics resolve stable document/entry IDs; an
unrecognized file remains inspectable; editing survives save/reopen and undo.

## Phase 3 — Dungeons (`DUNGEON.DAT`)

UI: named dungeon and level selector, 8×8 symbolic map, feature palette, inspector,
and compact floor stack showing vertical connections. Offer the existing pencil,
fill, selection and copy/paste behavior where meaningful. Labels describe Walls,
Passages, Ladders, Traps and Encounter rooms using verified engine semantics.

Select a feature to configure its subtype; an encounter room offers **Open combat
room**, returning to the same dungeon cell afterward. Level labels use consistent
human-facing numbering, with engine index in details. Show missing or mismatched
vertical connections as warnings unless the engine proves them illegal.

Codec: eight 512-byte dungeon blocks; derive nibble definitions from dungeon and
encounter consumers. Preserve unusual recognized encodings. Validate lengths,
feature values and room references. Do not serialize rendered tile IDs here.

Acceptance: edit a ladder/room link and terrain, round-trip unchanged other levels,
visit the edited dungeon and open its room in Impera. Explain that an already
loaded dungeon in a save may retain its old layout.

## Phase 4 — Signs and descriptions (`SIGNS.DAT`, `LOOK2.DAT`)

Signs UI: a Signs layer in the existing location map, sign markers, list/search,
and inspector with floor, coordinates, text and exact regular/runic preview.
**Add sign here** uses the selected cell. A tile/sign mismatch is a useful warning;
creating a sign record must not silently paint terrain. Moving between floors
uses only the current location's floor list. Show code-generated signs as such;
do not offer an editable record that the engine will ignore.

Descriptions UI: searchable thumbnail list with Terrain/objects and Actors tabs,
plain editing, formatting controls and game-sized preview. Show usages and shared
text relationships. Default to changing the selected description only; offer an
explicit action to change all linked entries when supported.

Codecs: verify sign header fields, text/list terminators and charset rules;
preserve undecoded fields. Rebuild sign map offsets and description pointer tables
within 16-bit address limits. Deduplicate only deliberately, and keep no-op output
byte-identical. Validate coordinate bounds, absent-map entries and all offsets.

Acceptance: regular/runic signs render correctly at the selected floor/cell;
editing one shared description does not unexpectedly change another; generated
sign limitations are visible; malformed records cannot crash preview or runtime.

## Phase 5 — Narrative (`QUESTION.DAT`, `END.DAT`, `KARMA.DAT`, `SHOPPE.DAT`)

Build one Narrative workspace with resource-specific documents:

| Section | UI and interpretation |
| --- | --- |
| Character creation | Introduction plus named virtue-pair questions; two-choice preview, with outcomes shown read-only until rules are supported. |
| Ending | Six page thumbnails, artwork, editable text and proportional-font layout preview. |
| Karma speeches | Speaker/context and karma-range selectors, preview at sample karma values. |
| Shops | Service/context entry list, token buttons and sample customer/item/price/time values. |

First enumerate every runtime read offset, pointer subtable, token and entry
boundary. Include the separately indexed portions of `SHOPPE.DAT`; do not label
the entire file “shop dialogue” and lose non-prose sections. Map each entry to its
consumer. Unresolved portions remain preserved and visible in Advanced details.

Initial edits preserve fixed entry points and enforce byte budgets including
terminators. Explain a limit beside the text and offer shorter wording; never
silently truncate. Preview layout overflow separately from byte overflow.

For longer text, add an optional versioned text-entry companion resource containing
stable semantic entry IDs and lengths. Centralize relevant runtime lookups so all
consumers check it before falling back to original DAT data. Extend allowlist,
package validation and compatibility metadata together. Continue supporting raw
legacy DAT replacements; do not relocate strings behind compiled offsets. Companion
entries require verified identities and token schemas, including scene-message
lookups in the next phase. Define override precedence and resource conflicts before
shipping; existing whole-resource overlap rules must remain deterministic.

Acceptance: every identified original entry can be selected and previewed; tokens
survive round trips and sample substitution; all six ending pages and virtue-pair
questions resolve to the right entries. Extended text works in the engine and an
older engine gives a clear incompatibility error rather than silently ignoring it.

## Phase 6 — Special scenes (`MISCMAPS.DAT`, `MISCMSG.DAT`, `ENDMSG.DAT`)

UI: named scene list and tabs **Map / Messages / Sequence**. Map reuses the 11×11
canvas; the inspector distinguishes stored terrain from actors placed by engine
code. Add navigation between messages, scene steps and affected cells.

Implement the four known 176-byte maps first, preserving five padding bytes in
each row. Pair their messages with the correct runtime entry points. Preview
states must label coded actors/timing as fixed when they are not yet authorable.

Then decode the creation block and instruction stream completely enough to safely
edit each supported operation. Use a step list such as **Place actor**, **Move
actor**, **Show text**, **Wait** only where the actual opcode supports it; derive
names and parameters from consumers, not assumptions. Add/select/reorder steps
only after command lengths, termination, references and control flow are known.
Unknown operations remain read-only with original bytes preserved. Disable sequence
mutations that cannot preserve unknown control-flow references.

Do not promise a free-form cinematic editor: changing hardcoded scene behavior
requires a separate supported runtime scene schema. If needed, add that versioned
schema after the original streams have a documented model and interpreter tests.

Acceptance: all four maps edit independently; padding and creation data survive;
message references resolve; known creation steps preview accurately; unsupported
instructions remain intact; engine sequence and preview agree on supported steps.

## Phase 7 — Fonts (`IBM.CH`, `RUNES.CH`, `IBM.HCS`, `RUNES.HCS`)

UI: searchable glyph gallery, zoomed bitmap canvas, pencil/erase, undo, copy/paste,
and sample panels for normal text, runes, signs and game borders. Keep glyph numbers
in tooltips. Name decorative glyphs only after confirming their usage. Editing a
border glyph updates the UI preview, making its broader effect apparent.

Implement the two verified 128×8-byte `.CH` codecs first, including bit order.
Respect intentional blank glyphs and preserve all slots. Preview regular/runic
font switching and offer project-only sample text.

For Hercules, research the complete 3,072-byte layout and driver glyph metrics;
file length alone is insufficient. Implement the correct codec and preview, then
allow editing through the same workspace. Label it **Legacy Hercules font — not
used by the SDL color renderer** unless a runtime display path is explicitly
added. Do not alter normal color rendering just to make these resources appear
supported. Thus both HCS files gain real editors without false runtime claims.

Proportional narrative preview also needs the actual `PROPORT.PCS` decoder. Read
that base resource for preview first. Supporting its edits is a deliberate scope
extension: add format validation and package support before enabling export.

Acceptance: known glyph bits render correctly, UI borders preview, all four fonts
round-trip; Hercules status remains visible in package preview; installed Workshop
uses project resources rather than relying on the checkout's font paths.

## Phase 8 — Starting world objects (`INIT.OOL`, `BRIT.OOL`, `UNDER.OOL`)

UI: **Starting world objects** layer on Britannia/Underworld, a named object list,
map placement and type-dependent inspector. Show object artwork, coordinates,
world/floor and supported properties such as ship condition. Display remaining
capacity out of the fixed 32 slots. Do not call objects settlement NPCs.

Present a single effective starting-state view, with provenance (“Normal start”,
“Initial underworld objects”, “World override”) in Advanced details. Editing should
materialize the appropriate override, using Phase 1 precedence; offer an explicit
revert-to-inherited action. Show how it relates to Starting state rather than
making users choose between three mysterious OOL files.

Verify object-specific meanings of all eight fields before exposing controls.
Preserve unknown bits; show unsupported properties read-only. Validate coordinates,
slots, tile/type consistency and cross-resource starting-state relationships.
Do not automatically remove objects because they look unusual.

Acceptance: create a ship/object, export and start a new game in both clean and
used installs; both worlds retain authored state after travel/save/reload. Existing
saves stay unchanged. Test Mod offers **Start a fresh game** with an explanation,
not a destructive reset of normal saves.

## Phase 9 — Remaining `DATA.OVL` concepts

The map-index part is covered by Phase 1. Finish coverage through a table-by-table
inventory of the overlay against `vars.c` and every runtime consumer:

1. Record byte spans, types, references, encoded strings and edition assumptions.
2. Classify each as supported data, compiled game rule/table, obsolete DOS-only
   data or unresolved. Publish this coverage inventory alongside diagnostics.
3. Expose useful concepts such as shop stock/prices or location data only after
   confirming their full semantics and adding a runtime-supported data path.
4. Prefer typed, versioned companion resources with shared validators for new
   concepts; retain original compiled defaults and existing packages. Parse exact
   legacy overlay spans only when compatibility can be established reliably.
5. Provide user-facing table editors with named items/locations, bounded values,
   references and previews. Keep offsets in Advanced details. Values compiled
   into algorithmic code require a separate engine feature, not an overlay toggle.

Acceptance is per table: change it in Workshop and demonstrate the effect in
Impera. Report unused or unresolved spans explicitly. No “everything supported”
badge until coverage is proven. Raw editing remains possible but unsupported
runtime effects are explained before export.

## Delivery, tests and completion gates

Implement phases as reviewable commits on the existing branch. Each phase includes
codec, UI, diagnostics, documentation and engine tests where applicable; update
this document's status as work lands. Phases 3/4 can follow Phase 2 independently;
Phase 8 requires Phase 1, and advanced text/sequence changes require their runtime
schemas. Do not defer compatibility until after the UI is shipped.

For each editor verify:

- Original-byte preservation; meaningful edits preserve other entries/padding.
- Bounds, terminators, tokens, unknown values and oversized/truncated inputs.
- Undo/redo across related resources and project/package persistence.
- A real engine effect from a Workshop-exported package, including save semantics.
- Qt interaction and navigation at compact and large window sizes, keyboard and
  mouse, with screenshots of the implemented workspace when available.
- Linux, Windows and both macOS builds through current release CI. Keep executable
  names, icons, packages and deployment unchanged unless the feature requires it.

Use the existing isolated Test Mod runtime. Avoid simulating full game AI in
Workshop; previews should state which behavior they reproduce. Add deterministic
engine oracle tests for shared decoding/addressing and a small set of guided
playthrough checks for visual/scene behavior.

A completed phase has no controls that silently fail in Impera, no byte-only UI
for its understood content, and no undocumented requirement to start a new game.
Unresolved formats are tracked explicitly rather than hidden by successful export.
