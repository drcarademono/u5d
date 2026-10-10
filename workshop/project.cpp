#include "resource_document.h"
#include "project.h"
#include "dialogue.h"
#include "mod/package.h"
#include "mod/world_resources.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <algorithm>
using U5::require;
static QByteArray readLimited(const QString &path, qint64 cap) {
    QFile f(path);
    require(f.open(QIODevice::ReadOnly), "Cannot open " + path + ": " + f.errorString());
    require(f.size() <= cap, "File exceeds supported size: " + path);
    QByteArray bytes = f.readAll();
    require(bytes.size() == f.size(), "Failed reading " + path);
    return bytes;
}
void Project::openGame(const QString &directory) {
    Project next;
    next.sourceDirectory = QDir(directory).absolutePath();
    QDir dir(next.sourceDirectory);
    require(dir.exists(), "Game folder does not exist");
    for (const auto &name : dir.entryList(QDir::Files, QDir::Name)) {
        QString upper = name.toUpper();
        if (!MOD_AllowedResource(upper.toLatin1().constData()))
            continue;
        require(!next.resources.contains(upper), "Ambiguous filename case in game folder: " + name);
        QByteArray b = readLimited(dir.filePath(name), MOD_MAX_RESOURCE);
        next.resources.insert(upper, {b, b});
    }
    require(next.resources.contains("TILES.16") && next.resources.contains("INIT.GAM"),
            "Select a DOS Ultima 5 folder containing TILES.16 and INIT.GAM");
    U5::readGraphics("TILES.16", next.data("TILES.16"));
    require(next.data("INIT.GAM").size() == 4192, "INIT.GAM must use the 4192-byte DOS layout");
    *this = next;
}
const QByteArray &Project::data(const QString &name) const {
    auto it = resources.constFind(name);
    require(it != resources.cend(), "Required companion resource is missing: " + name);
    return it->edited;
}
QStringList Project::changed() const {
    QStringList list;
    for (auto it = resources.begin(); it != resources.end(); ++it)
        if (it->edited != it->original)
            list << it.key();
    return list;
}
void Project::validate() const {
    require(!title.trimmed().isEmpty() && title.toUtf8().size() <= 256 && !title.contains(QChar(0)),
            "Mod name must contain 1–256 UTF-8 bytes");
    for (const auto &name : changed()) {
        const auto &b = data(name);
        require(!b.isEmpty() && b.size() <= MOD_MAX_RESOURCE, "Invalid resource size: " + name);
        Workshop::validateResource(*this, name);
    }
}
static void append32(QByteArray &b, unsigned v) {
    for (int i = 0; i < 4; i++)
        b.append(char(v >> (8 * i)));
}
QByteArray Project::package() const {
    validate();
    auto list = changed();
    require(!list.isEmpty(), "There are no changes to export");
    require(list.size() <= MOD_MAX_ENTRIES, "Too many modified resources");
    QByteArray out("IMOD0001", 8), nameBytes = title.toUtf8();
    append32(out, nameBytes.size());
    append32(out, list.size());
    out += nameBytes;
    size_t totalOutput = 0;
    for (auto name : list) {
        totalOutput += resources[name].edited.size();
        require(totalOutput <= MOD_MAX_PACKAGE, "Decoded package exceeds 32 MiB");
        const auto &r = resources[name];
        QByteArray spans;
        unsigned count = 0;
        for (int i = 0; i < r.edited.size();) {
            if (i < r.original.size() && r.edited[i] == r.original[i]) {
                ++i;
                continue;
            }
            int begin = i++;
            while (i < r.edited.size() && (i >= r.original.size() || r.edited[i] != r.original[i]))
                ++i;
            append32(spans, begin);
            append32(spans, i - begin);
            spans += r.edited.mid(begin, i - begin);
            ++count;
        }
        QByteArray n = name.toLatin1();
        append32(out, n.size());
        out += n;
        append32(out, r.original.size());
        append32(out, r.edited.size());
        append32(out, MOD_Crc32(r.original.constData(), r.original.size()));
        append32(out, MOD_Crc32(r.edited.constData(), r.edited.size()));
        append32(out, count);
        out += spans;
    }
    require(out.size() <= MOD_MAX_PACKAGE, "Package exceeds 32 MiB");
    return out;
}
static int readBase(void *context, const char *name, unsigned char **data, size_t *size) {
    auto p = static_cast<Project *>(context);
    auto it = p->resources.constFind(QString::fromLatin1(name));
    *data = nullptr;
    *size = 0;
    if (it == p->resources.cend())
        return 0;
    *size = it->original.size();
    *data = static_cast<unsigned char *>(malloc(*size ? *size : 1));
    if (!*data)
        return 0;
    memcpy(*data, it->original.constData(), *size);
    return 1;
}
void Project::importPackage(const QByteArray &bytes) {
    ModPackage decoded;
    char error[256];
    int ok =
        MOD_Decode(bytes.constData(), bytes.size(), readBase, this, &decoded, error, sizeof(error));
    require(ok, QString::fromUtf8(error));
    Project next = *this;
    next.title = QString::fromUtf8(decoded.title);
    for (unsigned i = 0; i < decoded.count; i++) {
        auto &e = decoded.entries[i];
        next.resources[QString::fromLatin1(e.name)].edited =
            QByteArray(reinterpret_cast<char *>(e.data), e.size);
    }
    MOD_Free(&decoded);
    next.validate();
    *this = next;
}
void Project::save(const QString &path) {
    validate();
    QJsonObject root{
        {"format", "impera-workshop-1"}, {"source", sourceDirectory}, {"title", title}};
    QJsonArray edits;
    for (auto name : changed()) {
        const auto &r = resources[name];
        edits.append(
            QJsonObject{{"name", name},
                        {"baseSize", r.original.size()},
                        {"baseCrc", double(MOD_Crc32(r.original.constData(), r.original.size()))},
                        {"bytes", QString::fromLatin1(r.edited.toBase64())}});
    }
    root["edits"] = edits;
    QJsonObject annotations;
    for (auto it = dialogueNames.cbegin(); it != dialogueNames.cend(); ++it)
        annotations.insert(it.key(), it.value());
    root["dialogueNames"] = annotations;
    QSaveFile f(path);
    require(f.open(QIODevice::WriteOnly), f.errorString());
    auto json = QJsonDocument(root).toJson();
    require(f.write(json) == json.size() && f.commit(), "Cannot save project: " + f.errorString());
    projectPath = path;
}
void Project::load(const QString &path) {
    auto bytes = readLimited(path, 64 * 1024 * 1024);
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(bytes, &error);
    require(error.error == QJsonParseError::NoError && doc.isObject(),
            "Invalid Workshop project JSON");
    auto root = doc.object();
    require(root["format"] == "impera-workshop-1", "Unsupported Workshop project version");
    Project next;
    next.openGame(root["source"].toString());
    next.title = root["title"].toString();
    if (root.contains("dialogueNames")) {
        require(root["dialogueNames"].isObject(), "Invalid question annotations");
        auto annotations = root["dialogueNames"].toObject();
        require(annotations.size() <= 4096, "Too many question annotations");
        for (auto it = annotations.begin(); it != annotations.end(); ++it) {
            require(it.key().size() <= 100 && it.value().isString() &&
                        it.value().toString().size() <= 120,
                    "Invalid question annotation");
            next.dialogueNames.insert(it.key(), it.value().toString());
        }
    }
    QSet<QString> names;
    require(root["edits"].isArray() && root["edits"].toArray().size() <= MOD_MAX_ENTRIES,
            "Invalid project edit list");
    for (auto value : root["edits"].toArray()) {
        auto edit = value.toObject();
        QString name = edit["name"].toString();
        require(next.resources.contains(name) && !names.contains(name),
                "Missing or duplicate resource in project");
        names.insert(name);
        auto &r = next.resources[name];
        require(edit["baseSize"].toInt(-1) == r.original.size() &&
                    edit["baseCrc"].toDouble(-1) ==
                        MOD_Crc32(r.original.constData(), r.original.size()),
                "Original files changed since project creation: " + name);
        auto decoded = QByteArray::fromBase64Encoding(edit["bytes"].toString().toLatin1(),
                                                      QByteArray::AbortOnBase64DecodingErrors);
        require(bool(decoded) && decoded.decoded.size() <= MOD_MAX_RESOURCE,
                "Invalid project resource bytes");
        r.edited = decoded.decoded;
    }
    next.validate();
    next.projectPath = path;
    *this = next;
}

QVector<ModDiagnostic> validateMod(const Project &project) {
    QVector<ModDiagnostic> result;
    try {
        project.validate();
    } catch (const std::exception &error) {
        result.append({ModDiagnostic::Error, {}, QString::fromUtf8(error.what())});
    }
    const auto changes = project.changed();
    if (changes.isEmpty())
        result.append({ModDiagnostic::Warning,
                       {},
                       "No modified resources; there is no package to export or test."});
    for (const auto &name : changes) {
        try {
            QString actual;
            QDir source(project.sourceDirectory);
            for (const auto &file : source.entryList(QDir::Files))
                if (file.toUpper() == name) {
                    actual = source.filePath(file);
                    break;
                }
            require(!actual.isEmpty() &&
                        readLimited(actual, MOD_MAX_RESOURCE) == project.resources[name].original,
                    "Original game file changed or is missing since this project opened. Reopen "
                    "the project against the intended base files.");
        } catch (const std::exception &error) {
            result.append({ModDiagnostic::Error, name, QString::fromUtf8(error.what())});
        }
        try {
            Project single = project;
            single.title = "Validation";
            for (auto it = single.resources.begin(); it != single.resources.end(); ++it)
                if (it.key() != name && !(name == "BRIT.DAT" && it.key() == "DATA.OVL"))
                    it->original = it->edited;
            single.validate();
        } catch (const std::exception &error) {
            result.append({ModDiagnostic::Error, name, QString::fromUtf8(error.what())});
        }
        result.append({ModDiagnostic::Information, name,
                       QString("Package replaces this entire resource (%1 bytes → %2 bytes).")
                           .arg(project.resources[name].original.size())
                           .arg(project.data(name).size())});
        if (name.endsWith(".HCS"))
            result.append({ModDiagnostic::Warning, name, Workshop::capability(name).status});
        if (name == "INIT.GAM" || name == "INIT.OOL" || name == "BRIT.OOL" || name == "UNDER.OOL")
            result.append({ModDiagnostic::Information, name,
                           "Applies to new games only. Existing saves keep their saved party and object state. "
                           "New games use an empty Britannia list unless BRIT.OOL is overridden; "
                           "UNDER.OOL overrides take precedence over INIT.OOL. World starts use "
                           "that world's object list; settlement actors remain in INIT.GAM."});
        if (name == "INIT.OOL" && changes.contains("UNDER.OOL"))
            result.append({ModDiagnostic::Warning, name,
                           "UNDER.OOL overrides this initial underworld object list in new games."});
        if (name == "DATA.OVL") {
            result.append({ModDiagnostic::Information, name,
                           "Britannia map index supported by Impera; other DOS overlay tables "
                           "are not runtime configuration. Requires an engine with Workshop Phase 1 support."});
            auto before = project.resources[name].original;
            auto after = project.data(name);
            if (before.size() >= int(U5_WORLD_INDEX_OFFSET + U5_WORLD_INDEX_SIZE) &&
                after.size() >= int(U5_WORLD_INDEX_OFFSET + U5_WORLD_INDEX_SIZE)) {
                before.replace(U5_WORLD_INDEX_OFFSET, U5_WORLD_INDEX_SIZE, QByteArray(256, 0));
                after.replace(U5_WORLD_INDEX_OFFSET, U5_WORLD_INDEX_SIZE, QByteArray(256, 0));
                if (before != after)
                    result.append({ModDiagnostic::Warning, name,
                                   "Changes outside the Britannia map index have no supported runtime effect."});
            }
        }
        const auto &bytes = project.data(name);
        if (name.endsWith(".NPC") && bytes.size() == 4608) {
            int unusual = 0;
            for (int settlement = 0; settlement < 8; ++settlement)
                for (int actor = 0; actor < 32; ++actor) {
                    int base = settlement * 576 + actor * 16;
                    for (int time = 0; time < 4; ++time)
                        unusual += U5::byte(bytes, base + 12 + time) > 23;
                    for (int position = 0; position < 3; ++position)
                        unusual += U5::byte(bytes, base + 3 + position) > 31 ||
                                   U5::byte(bytes, base + 6 + position) > 31;
                }
            if (unusual)
                result.append({ModDiagnostic::Warning, name,
                               QString("%1 unusual schedule times or destinations. These values "
                                       "are preserved; review them in Advanced schedule details.")
                                   .arg(unusual)});
        }
    }
    // Match the engine: alphabetic scan, packages decoded against original bytes,
    // and any overlapping resource causes that package to be rejected in full.
    Project mounted = project;
    for (auto it = mounted.resources.begin(); it != mounted.resources.end(); ++it)
        it->edited = it->original;
    QSet<QString> overrides;
    unsigned mountedCount = 0;
    size_t mountedMemory = 0;
    QDir mods(QDir(project.sourceDirectory).filePath("Mods"));
    if (!mods.exists()) {
        QDir source(project.sourceDirectory);
        for (const auto &folder : source.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (folder.compare("Mods", Qt::CaseInsensitive) == 0) {
                mods.setPath(source.filePath(folder));
                break;
            }
    }
    if (mods.exists()) {
        auto installed = mods.entryList(QDir::Files, QDir::NoSort);
        std::sort(installed.begin(), installed.end(),
                  [](const QString &a, const QString &b) { return a.toUtf8() < b.toUtf8(); });
        for (const auto &file : installed) {
            if (!file.endsWith(".imperamod", Qt::CaseInsensitive))
                continue;
            try {
                require(mountedCount < 128, "Engine package limit (128) exceeded");
                auto bytes = readLimited(mods.filePath(file), MOD_MAX_PACKAGE);
                ModPackage decoded{};
                char error[256];
                require(MOD_Decode(bytes.constData(), bytes.size(), readBase, &mounted, &decoded,
                                   error, sizeof(error)),
                        QString::fromUtf8(error));
                QStringList names;
                size_t memory = 0;
                for (unsigned i = 0; i < decoded.count; ++i)
                    memory += decoded.entries[i].size;
                for (unsigned i = 0; i < decoded.count; ++i)
                    names.append(QString::fromLatin1(decoded.entries[i].name));
                MOD_Free(&decoded);
                bool conflict = false;
                for (const auto &name : names)
                    conflict |= overrides.contains(name);
                if (conflict) {
                    result.append(
                        {ModDiagnostic::Warning,
                         {},
                         file + ": overlaps an earlier installed package; the engine rejects it."});
                    continue;
                }
                require(memory <= 64u * 1024u * 1024u - mountedMemory,
                        "Engine total mod memory limit (64 MiB) exceeded");
                ++mountedCount;
                mountedMemory += memory;
                result.append({ModDiagnostic::Information,
                               {},
                               QString("Installed load order %1: %2 — %3")
                                   .arg(mountedCount)
                                   .arg(file)
                                   .arg(names.join(", "))});
                for (const auto &name : names) {
                    overrides.insert(name);
                    if (changes.contains(name))
                        result.append({ModDiagnostic::Warning, name,
                                       "Installed package " + file +
                                           " also replaces this resource. Impera rejects "
                                           "overlapping packages; remove the conflicting package "
                                           "when installing this mod."});
                }
            } catch (const std::exception &error) {
                result.append(
                    {ModDiagnostic::Warning,
                     {},
                     file + ": installed package cannot load: " + QString::fromUtf8(error.what())});
            }
        }
    }
    if (!changes.isEmpty()) {
        try {
            auto bytes = project.package();
            Project roundtrip = mounted;
            roundtrip.importPackage(bytes);
            for (const auto &name : changes)
                require(roundtrip.data(name) == project.data(name),
                        "Package verification changed " + name);
            result.append({ModDiagnostic::Information,
                           {},
                           QString("Package verified by engine decoder: %1 bytes. Test Mod uses "
                                   "only this package, not installed mods.")
                               .arg(bytes.size())});
        } catch (const std::exception &error) {
            result.append({ModDiagnostic::Error,
                           {},
                           "Package verification: " + QString::fromUtf8(error.what())});
        }
    }
    return result;
}
QString prepareModTest(const Project &project, const QString &sessionsDirectory) {
    const auto package = project.package();
    require(QDir().mkpath(sessionsDirectory), "Cannot create test-session folder");
    QTemporaryDir session(QDir(sessionsDirectory).filePath("test-XXXXXX"));
    require(session.isValid(), "Cannot create isolated test session");
    QDir source(project.sourceDirectory), destination(session.path());
    // Flat DOS runtime files only. Never copy personal saves, settings or installed Mods.
    const QSet<QString> excluded{"DATA.CFG", "ENGINE.CFG", "LOG.TXT", "SAVED.GAM", "SAVED.OOL"};
    qint64 total = 0;
    for (const auto &name : source.entryList(QDir::Files, QDir::Name)) {
        if (excluded.contains(name.toUpper()) || QFileInfo(source.filePath(name)).isSymLink())
            continue;
        const QString suffix = QFileInfo(name).suffix().toUpper();
        if (!QStringList{"DAT", "OVL", "BIT", "PTH", "CH", "16", "OOL", "GAM", "TLK", "NPC", "CBT"}
                 .contains(suffix))
            continue;
        const QString target = name.toUpper();
        QFileInfo info(source.filePath(name));
        total += info.size();
        require(total <= 512 * 1024 * 1024,
                "Game folder exceeds test-copy limit; choose a clean DOS game folder");
        require(QFile::copy(source.filePath(name), destination.filePath(target)),
                "Cannot copy test resource " + name);
        require(QFile::setPermissions(destination.filePath(target),
                                      QFile::ReadOwner | QFile::WriteOwner),
                "Cannot make test file writable");
    }
    for (const auto &name : project.resources.keys()) {
        auto bytes = readLimited(destination.filePath(name), MOD_MAX_RESOURCE);
        require(bytes == project.resources[name].original,
                "Original resource changed since this project opened: " + name);
    }
    require(destination.mkpath("Mods"), "Cannot create test Mods folder");
    auto save = [&](const QString &name, const QByteArray &bytes) {
        QSaveFile file(destination.filePath(name));
        require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() &&
                    file.commit(),
                "Cannot write test file " + name);
    };
    save("Mods/workshop.imperamod", package);
    save("DATA.CFG", (session.path() + "\n\n").toUtf8());
    save("WORKSHOP-TEST.txt",
         "Isolated Impera Workshop test session. Game files, Mods, settings, saves and logs here "
         "are disposable. Original files were not modified.\n");
    session.setAutoRemove(false); // Keep saves/logs available, including after Workshop exits.
    return session.path();
}
