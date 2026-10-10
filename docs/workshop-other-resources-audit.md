# Workshop: Other Resources audit

This audit covers the current Workshop resource tree and Impera's consumers, using
one original DOS installation as a sample. **Other Resources is a UI catch-all,
not a game-file format.** Its 18 files include dungeon maps, text, fonts, world
object state, special scenes, and an original DOS data overlay.

We should add focused editors for most of this content, rather than build one
universal DAT editor. The highest-value additions are **Dungeons**, **Signs**, and
**Object descriptions**, followed by narrative text and special scenes. Two
runtime compatibility issues need attention before promising complete support:
`DATA.OVL` is not read by Impera, and saved world-object lists supersede resource
files after initialization.

## Complete inventory

Sizes below are from the audited installation, not universal length constraints.
Text files may legitimately differ between editions or mods.

| File | Bytes | Actual contents | Recommended UI |
| --- | ---: | --- | --- |
| `DUNGEON.DAT` | 4,096 | Eight dungeon layouts, each with eight 8×8 levels; terrain and feature codes packed into each byte's nibbles. These are not ordinary tileset IDs. | Dedicated dungeon editor with a symbolic palette, levels, stairs/ladders and combat-room links. |
| `SIGNS.DAT` | 8,364 | Map-indexed signs with floor and coordinates, plus formatted regular/runic text. | Signs layer in the map editor and a sign text/preview pane. |
| `LOOK2.DAT` | 3,622 | Tile and actor appearance descriptions selected by two 256-entry little-endian offset tables. | Object descriptions editor with tile/actor thumbnails and text preview. |
| `QUESTION.DAT` | 7,746 | Character-creation narrative and virtue-pair questions. | Character creation text editor, with named virtue pairs and proportional-font previews. |
| `END.DAT` | 3,698 | Six illustrated ending text pages. | Ending pages editor using existing artwork previews. |
| `ENDMSG.DAT` | 786 | Final rescue scene message data, consumed by the ending scene logic. | Messages within a named final-rescue scene editor. |
| `KARMA.DAT` | 761 | Karma-dependent speeches used by Blackthorn and the familiar old man's apparition. It does not define karma rules. | Narrative messages editor with context and karma-tier labels. |
| `SHOPPE.DAT` | 10,135 | Shopkeeper messages and substitution templates, with additional indexed data read by shop routines. It is not a standalone inventory/price configuration file. | Shop text editor with token insertion, contextual labels and sample substitutions. |
| `MISCMSG.DAT` | 2,745 | Message streams for Blackthorn's scene and shrine/Codex interactions. | Named special-scene message editor. |
| `MISCMAPS.DAT` | 1,871 | Four small special-scene maps, followed by character-creation scene data and an instruction stream. | Special-scene map editor first; structured character-creation sequence editor later. |
| `IBM.CH` | 1,024 | DOS regular 8×8 monochrome character bitmap set: 128 glyphs, including UI decorations. | Font editor with glyph grid and live UI/text preview. |
| `RUNES.CH` | 1,024 | DOS runic 8×8 character bitmap set, also including decorative glyphs. | Same font editor, with rune previews. |
| `IBM.HCS` | 3,072 | Regular character set for the original Hercules rendering path. | Defer; explain its legacy renderer context. |
| `RUNES.HCS` | 3,072 | Runic character set for the original Hercules rendering path. | Defer alongside `IBM.HCS`. |
| `INIT.OOL` | 256 | Initial world-object list used when creating a character; becomes the underworld half of the initial saved object lists. | Starting world objects editor, coordinated with Starting state. |
| `BRIT.OOL` | 256 | Britannia world-object list used to seed persistent world state. | World objects editor only after defining its initialization/save semantics. |
| `UNDER.OOL` | 256 | Underworld world-object list used to seed persistent world state. | Same as `BRIT.OOL`. |
| `DATA.OVL` | 48,464 | Original DOS data overlay: many tables and strings. Workshop uses its Britannia chunk index; Impera instead uses compiled tables. | No generic overlay editor. Expose individual engine-supported concepts after fixing runtime support. |

## Format details and editor boundaries

### Dungeons

Each dungeon occupies 512 bytes: `level * 64 + y * 8 + x`. The engine loads one
512-byte layout on entering a dungeon. High and low nibbles select terrain and
feature subtypes; the `0xF` high-nibble family links combat rooms using the low
nibble. A dungeon editor must use these feature definitions, not let users paint
arbitrary outdoor tile IDs. `DUNGEON.CBT` already has a combat-map editor; room
links should navigate to it. Validate feature codes and linked rooms, and show
connectivity between floors. Existing saves can hold an already loaded dungeon
layout, so testing a resource change requires entering the dungeon afresh.

Sources: [dungeon loading](../src/mainout.c), [addressing macros](../src/macros.h),
[dungeon logic](../src/dungeon.c), [room encounters](../src/dnglook.c).

### Signs and descriptions

`SIGNS.DAT` starts with 33 little-endian 16-bit map offsets (66 bytes). Zero means
no sign list. Records contain a four-byte header, including floor/x/y, followed
by terminated text; list termination is distinct from text termination. Its text
uses font switching, runic substitutions and decoration conventions. A plain
Unicode text box without an encoder would corrupt those conventions. Preserve
uninterpreted header fields until their semantics are established. Some signs,
including a wanted poster and default law notice, are generated in code rather
than authored solely in this file.

`LOOK2.DAT` starts with two tables: 256 tile offsets at `0x000`, then 256 actor
appearance offsets at `0x200`. Descriptions are terminated print strings, not
conversation compression. Preserve shared references and validate every offset.
Some Look behavior is special-cased in code; changing a description does not
change an object's interaction rules. Comments mentioning LOOK2 artwork do not
make this file an alternative tileset: its active consumers here read text.

Source: [Look and sign rendering](../src/lookobj.c).

### Narrative and shop text

These files require separate schemas even if they share a text-editor component.
Character-creation questions and ending pages use the proportional text renderer,
with wrapping and special character conventions. Question locations are selected
through compiled tables, while the six ending pages begin at `0`, `0x1A8`,
`0x3BC`, `0x5FA`, `0x8E8`, and `0xB74`. Arbitrarily repacking strings would leave
those compiled offsets pointing to the wrong text. Initially enforce each
entry's available byte budget; longer text requires a runtime addressing change.

`KARMA.DAT` is similarly selected using fixed offsets and karma tiers. Shop
messages contain tokens such as `#`, `$`, `@`, `%`, `^`, `&`, `*`, and high-bit
bytes for substitutions. Decode each token from the actual shop formatter and
show a preview with sample context. Do not label these punctuation characters
as unrestricted prose. Shop stock, prices, virtue mechanics and character
creation outcomes also depend on compiled engine data; their presence in the
original DOS overlay does not make them editable through these text files.

Sources: [proportional text and creation](../src/font.c),
[ending pages](../src/endgame.c), [shop substitutions](../src/shoppes.c),
[other shop consumers](../src/shoppes2.c), [shop services](../src/shoppes3.c),
[karma speeches](../src/outsubs.c), [Blackthorn scene](../src/blckthrn.c),
[compiled tables](../src/vars.c).

### Special scenes

The first four `MISCMAPS.DAT` blocks each occupy 176 bytes (`0xB0`): an 11×11
visible map stored with a 16-byte row stride. They begin at:

| Offset | Scene |
| --- | --- |
| `0x000` | Blackthorn imprisonment/interrogation |
| `0x0B0` | Shrine ritual |
| `0x160` | Codex interaction |
| `0x210` | Final rescue |

Reuse the map canvas, but preserve padding and do not apply the CBT header/entry
schema. At `0x2C0`, character-creation scene data begins; its instruction stream
is interpreted starting at offset `0x200` within that loaded block. It includes
actor changes and other operations, so it cannot be treated as more terrain rows.

`MISCMSG.DAT` is read at zero for Blackthorn and `0x3AB` for shrine/Codex logic.
`ENDMSG.DAT` accompanies the final rescue map. Those scenes have coded timing,
actors and branching; editing messages is not equivalent to editing all scene
behavior. Start with map and message editing, then document the instruction
schemas before adding a sequence editor.

Sources: [Blackthorn](../src/blckthrn.c), [shrines/Codex](../src/cast2.c),
[rescue](../src/endgame.c), [creation sequence](../src/font.c).

### Fonts

The `.CH` files are small bitmap fonts and deserve a shared, simple glyph editor.
Preview letters, rune text and border glyphs together: changing decorative glyphs
also changes game UI. First-start setup loads its font separately, so a modded
font should not be advertised as affecting every pre-game picker.

The `.HCS` files belong to the Hercules video-driver selection in the original
code, rather than the normal SDL color presentation. Their 3,072-byte size is
not the `.CH` layout; do not silently reinterpret them as 8×8 fonts. An editor
for them is low priority until there is a supported presentation that uses them.

Sources: [font selection](../src/intro.c), [setup font](../src/common/data_setup.c).

### Starting and persistent world objects

OOL records are 32 eight-byte objects containing base/animated tile, coordinates,
z, and object-specific state (including ship hull state). They are not the
settlement NPC schedule format. Generic labels such as “NPC file” would be wrong.

New-character creation uses `INIT.OOL` to initialize saved object state. The
`BRIT.OOL` and `UNDER.OOL` resource files seed runtime files only when those do
not already exist. Subsequent reads use `SAVEGAME/BRIT.OOL` and
`SAVEGAME/UNDER.OOL`; modern saves also preserve world state. Merely replacing
these resources does not reliably change an existing playthrough. Before adding
spawn editing, specify which state is created for a new game and verify mods
are applied at that point. Test both a fresh installation and one with saves.

Sources: [record definition](../src/structs.h), [initialization](../src/font.c),
[world seeding](../src/common/data_setup.c), [save-path routing](../src/common/file.c),
[modern saves](../src/savegame.c).

## Runtime compatibility gap: DATA.OVL

Workshop reads the Britannia chunk index at `DATA.OVL[0x3886..0x3985]` and rewrites
it when rebuilding `BRIT.DAT`. Impera's world loader instead indexes the compiled
`D_3876` array in `vars.c`; there is no runtime read of `DATA.OVL` in the current
engine source. The package allowlist accepts the file, but that alone does not
make its replacements effective.

This means an exported Britannia edit that changes chunk ordering/assignments
needs an engine compatibility fix and an end-to-end test. Before broadening
editing, load the applicable modded index (or introduce a deliberate supported
world-index resource), preserving compatibility with existing game data. Other
compiled overlay tables need their own supported data path; exposing raw overlay
bytes would give modders controls that appear to work but do nothing.

Sources: [Workshop world codec](../workshop/formats.cpp),
[engine world loading](../src/outsubs.c), [compiled index](../src/vars.c),
[resource allowlist](../src/mod/package.c).

## Suggested implementation order

1. Fix and test the Britannia index compatibility gap; clarify object seeding.
2. Add Dungeons, Signs and Object descriptions, each with semantic validation.
3. Add shared narrative text editing with resource-specific entry schemas,
   fixed-offset budgets, formatting/token previews and meaningful context labels.
4. Add Special scenes for the four maps and their messages; retain a separate
   advanced view for the character-creation instruction stream.
5. Add Fonts and Starting world objects. Defer Hercules font editing and general
   DOS overlay editing.

Reorganize the tree into those user-facing groups as editors arrive. Keep a
Resource inspector for advanced import/export, rather than implying that all
remaining files are ordinary editable text.

## Validation and scope

Workshop currently provides raw byte editing/import for these resources, but
most of their formats have no dedicated semantic validator. Existing validation
covers the already supported artwork, dialogue, NPC schedules, starting state,
story and ordinary maps, plus the Workshop Britannia index interpretation.
Each new editor should validate pointer bounds, termination, fixed-entry budgets,
record sizes and feature codes as appropriate, then test the generated package
in Impera. Export acceptance is not proof that the engine consumes a resource.

The audit inventory is based on `Project::openGame`, the resource allowlist, tree
grouping, actual file sizes and engine read sites. It does not constitute a
complete reverse engineering of every scene instruction or unused overlay byte.
Other original files such as `.PTH`, `.BIT`, `.PCS`, `.4`, driver files, executables
and overlays other than `DATA.OVL` are currently excluded from package resources;
they are not hidden members of Other Resources. In particular `PROPORT.PCS`,
used for proportional text, would need explicit support for font authoring.

Sources: [project loading/validation](../workshop/project.cpp),
[tree and inspector](../workshop/window.cpp), [package rules](../src/mod/package.c).
