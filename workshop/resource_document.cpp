#include "resource_document.h"
#include "dialogue.h"
#include "location_text.h"
#include "dungeon_editor.h"
#include "mod/world_resources.h"
using U5::require;
namespace Workshop {
Capability capability(const QString &resource) {
    const QString n = resource.toUpper();
    if (n.endsWith(".16"))
        return {n, "Artwork", "Editable palette artwork", EditorKind::Artwork};
    if (n.endsWith(".TLK"))
        return {n, "Conversations", "Editable structured dialogue", EditorKind::Conversation};
    if (n.endsWith(".NPC"))
        return {n, "NPC schedules", "Editable daily routines", EditorKind::Schedule};
    if (n == "INIT.GAM")
        return {"Starting state", "Starting state", "Applied to new games", EditorKind::State};
    if (n == "STORY.DAT")
        return {"Introduction story", "Story", "Editable fixed text windows", EditorKind::Story};
    if (n == "SIGNS.DAT" || n == "LOOK2.DAT")
        return {n == "SIGNS.DAT" ? "Signs" : "Look descriptions", "Text", "Editable location text", EditorKind::LocationText};
    if (n == "DUNGEON.DAT")
        return {"Dungeons", "Maps", "Editable dungeon features and room links",
                EditorKind::Dungeon};
    if (n.endsWith(".CBT") ||
        QStringList{"BRIT.DAT", "UNDER.DAT", "TOWNE.DAT", "DWELLING.DAT", "KEEP.DAT", "CASTLE.DAT"}
            .contains(n))
        return {n, "Maps", "Editable terrain", EditorKind::Map};
    static const QMap<QString, QString> titles = {{"DUNGEON.DAT", "Dungeon levels"},
                                                  {"SIGNS.DAT", "Signs"},
                                                  {"LOOK2.DAT", "Object descriptions"},
                                                  {"QUESTION.DAT", "Character creation questions"},
                                                  {"END.DAT", "Ending story"},
                                                  {"ENDMSG.DAT", "Ending messages"},
                                                  {"KARMA.DAT", "Virtue messages"},
                                                  {"SHOPPE.DAT", "Shop dialogue"},
                                                  {"MISCMSG.DAT", "Game messages"},
                                                  {"MISCMAPS.DAT", "Special scenes"},
                                                  {"IBM.CH", "Normal font"},
                                                  {"RUNES.CH", "Runic font"},
                                                  {"IBM.HCS", "Legacy normal font"},
                                                  {"RUNES.HCS", "Legacy runic font"},
                                                  {"INIT.OOL", "Starting world objects"},
                                                  {"BRIT.OOL", "Britannia world objects"},
                                                  {"UNDER.OOL", "Underworld objects"},
                                                  {"DATA.OVL", "Engine tables"}};
    if (n.endsWith(".HCS"))
        return {titles.value(n, n), "Other resources",
                "Legacy font data: Impera currently uses .CH fonts, so changes here have no "
                "verified runtime effect.",
                EditorKind::Inspector};
    return {titles.value(n, n), "Other resources",
            titles.contains(n)
                ? "Advanced inspection; a dedicated editor is planned. Unknown bytes are preserved."
                : "Unrecognized layout; advanced inspection remains available. Unknown bytes are "
                  "preserved.",
            EditorKind::Inspector};
}
ResourceDocument::ResourceDocument(const Project &p, QString r)
    : project(p), resource(r.toUpper()) {
    project.data(resource);
}
QVector<Entry> ResourceDocument::entries() const {
    const auto size = project.data(resource).size();
    if(resource=="SIGNS.DAT") {
        QVector<Entry> result;
        int offset = 66;
        for (const auto &sign : readSigns(project.data(resource))) {
            result.append(
                {QString("sign/%1/%2/%3/%4").arg(sign.map).arg(sign.floor).arg(sign.x).arg(sign.y),
                 QString("Sign at (%1,%2), floor %3").arg(sign.x).arg(sign.y).arg(sign.floor),
                 offset, 5 + sign.text.size()});
            offset += 5 + sign.text.size();
        }
        return result;
    }
    if (resource == "LOOK2.DAT") {
        QVector<Entry> result;
        for (int i = 0; i < 512; i++) {
            auto text = description(project.data(resource), i);
            result.append({QString("description/%1/%2").arg(i / 256).arg(i % 256),
                           QString("%1 %2: %3")
                               .arg(i < 256 ? "Terrain/object" : "Actor")
                               .arg(i % 256)
                               .arg(QString::fromLatin1(text)),
                           U5::word(project.data(resource), i * 2), text.size() + 1});
        }
        return result;
    }
    if (resource == "DUNGEON.DAT") {
        DungeonDocument document(&project);
        QVector<Entry> result;
        for (int d = 0; d < 8; ++d)
            for (int f = 0; f < 8; ++f)
                result.append({QString("dungeon/%1/level/%2").arg(d).arg(f),
                               QString("Dungeon %1 · Level %2").arg(d + 1).arg(f + 1),
                               DungeonDocument::offset(d, f), 64});
        return result;
    }
    QVector<Entry> result;
    if (resource == "IBM.CH" || resource == "RUNES.CH") {
        require(size == 1024, "Font must contain 128 eight-byte glyphs");
        for (int i = 0; i < 128; ++i)
            result.append({QString("glyph/%1").arg(i), QString("Glyph %1").arg(i), i * 8, 8});
    } else if (resource.endsWith(".OOL")) {
        require(size == 256, "World objects must contain 32 eight-byte records");
        for (int i = 0; i < 32; ++i)
            result.append(
                {QString("object/%1").arg(i), QString("World object %1").arg(i + 1), i * 8, 8});
    } else
        result.append({"resource", capability(resource).title, 0, size});
    return result;
}
QByteArray ResourceDocument::read(const Entry &e) const {
    const auto &b = project.data(resource);
    require(e.offset >= 0 && e.length >= 0 && e.offset <= b.size() &&
                e.length <= b.size() - e.offset,
            resource + ": entry extends beyond resource");
    return b.mid(e.offset, e.length);
}
QMap<QString, QByteArray> ResourceDocument::replace(const Entry &e, const QByteArray &bytes) const {
    read(e);
    require(bytes.size() == e.length, "Fixed entries cannot change size");
    QByteArray result = project.data(resource);
    result.replace(e.offset, e.length, bytes);
    Project candidate = project;
    candidate.resources[resource].edited = result;
    candidate.validate();
    return {{resource, result}};
}
void validateResource(const Project &project, const QString &name) {
    const auto &b = project.data(name);
    const auto kind = capability(name).editor;
    if (kind == EditorKind::LocationText) {
        if (name == "SIGNS.DAT") {
            const QStringList maps = {"TOWNE.DAT", "DWELLING.DAT", "CASTLE.DAT", "KEEP.DAT"};
            for (const auto &r : readSigns(b)) {
                if (r.map == 0)
                    continue;
                QString map = maps[(r.map - 1) / 8];
                require(project.resources.contains(map), "Sign refers to an absent location map");
                bool found = false;
                for (const auto &page : U5::mapPages(map, project.data(map)))
                    if (page.settlement == (r.map - 1) % 8 && page.floor == r.floor)
                        found = true;
                require(found, "Sign refers to a floor absent from its location");
            }
        } else
            for (int i = 0; i < 512; i++)
                description(b, i);
        return;
    }
    if (kind == EditorKind::Dungeon) {
        DungeonDocument::validate(project);
        return;
    }
    if (name == "IBM.CH" || name == "RUNES.CH") {
        require(b.size() == 1024, "Bitmap fonts must contain 128 eight-byte glyphs");
        return;
    }
    if (kind == EditorKind::Artwork)
        U5::readGraphics(name, b);
    else if (kind == EditorKind::Conversation) {
        auto entries = U5::readDialogue(b);
        auto originals = U5::readDialogue(project.resources[name].original);
        for (auto e : entries) {
            U5::validateText(e.bytes);
            bool unchanged = false;
            for (auto original : originals)
                if (original.id == e.id && original.bytes == e.bytes)
                    unchanged = true;
            if (!unchanged)
                for (auto issue : Dialogue::parse(e.bytes).issues())
                    require(!issue.error, name + ": " + issue.message);
        }
        U5::writeDialogue(entries);
    } else if (kind == EditorKind::Schedule)
        require(b.size() == 4608, "NPC schedules must be 4608 bytes");
    else if (kind == EditorKind::State)
        require(b.size() == 4192, "INIT.GAM must be 4192 bytes");
    else if (name == "INIT.OOL" || name == "BRIT.OOL" || name == "UNDER.OOL")
        require(b.size() == U5_WORLD_OBJECT_SIZE,
                name + ": world objects must contain 32 eight-byte records");
    else if (kind == EditorKind::Story)
        U5::storyPages(b);
    else if (name == "BRIT.DAT" || name == "DATA.OVL") {
        const auto &overlay = project.data("DATA.OVL");
        const uint8_t *index = U5_BritanniaDefaultIndex;
        if (project.resources["DATA.OVL"].original != overlay) {
            require(overlay.size() >= int(U5_WORLD_INDEX_OFFSET + U5_WORLD_INDEX_SIZE),
                    "DATA.OVL is missing its Britannia chunk index");
            index = reinterpret_cast<const uint8_t *>(overlay.constData()) + U5_WORLD_INDEX_OFFSET;
        }
        require(U5_ValidBritanniaIndex(index, project.data("BRIT.DAT").size()),
                "Britannia chunk index references missing/truncated BRIT.DAT chunks");
        U5::worldMap("BRIT.DAT", project.data("BRIT.DAT"), overlay);
    } else if (name == "UNDER.DAT")
        U5::worldMap(name, b, QByteArray());
    else if (kind == EditorKind::Map) {
        U5::mapPages(name, b);
        if (name == "DUNGEON.CBT" && project.resources.contains("DUNGEON.DAT"))
            DungeonDocument::validate(project, true);
    }
}
} // namespace Workshop
