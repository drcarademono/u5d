#include "dialogue.h"
#include "dialogue_editor.h"
#include "dungeon_editor.h"
#include "mod/package.h"
#include "mod/world_resources.h"
#include "resource_document.h"
#include "text_preview.h"
#include "window.h"
#include <QTemporaryDir>
#include <QtWidgets>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
extern "C" int WorkshopEngineDungeonRoom(int, unsigned char);
extern "C" int WorkshopEngineMatch(const unsigned char *, char *);
extern "C" int WorkshopEngineDecode(const void *, size_t, void **, unsigned *);
static void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> static void rejects(F fn) {
    bool failed = false;
    try {
        fn();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, "Expected invalid resource to be rejected");
}
static void file(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly), "Cannot make fixture");
    check(f.write(bytes) == bytes.size(), "Cannot write fixture");
}
static Project fixture(const QString &directory) {
    file(directory + "/tiles.16", QByteArray(65536, 0));
    QByteArray init(4192, 0);
    init[0x2b5] = 1;
    file(directory + "/init.gam", init);
    Project p;
    p.openGame(directory);
    return p;
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir settingsDirectory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    app.setOrganizationName("ImperaTests");
    app.setApplicationName("WorkshopTests");
    int count = 0;
    auto test = [&](const char *name, auto fn) {
        fn();
        ++count;
        std::cout << "PASS " << name << "\n";
    };
    try {
        test("Dungeon codec, feature validation and engine room mapping", [] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            QByteArray base(4096, char(0xb0));
            base[511] = char(0x9a); // Unusual original encoding is preserved.
            file(dir.path() + "/DUNGEON.DAT", base);
            file(dir.path() + "/DUNGEON.CBT", QByteArray(112 * 352, 0));
            p.openGame(dir.path());
            Workshop::DungeonDocument document(&p);
            check(document.changes(0, 0, document.level(0, 0))["DUNGEON.DAT"] == base,
                  "No-op dungeon changed bytes");
            for (int d = 0; d < 8; ++d)
                for (int room = 0; room < 16; ++room)
                    check(Workshop::DungeonDocument::roomIndex(d, 0xa0 | room) * 352 ==
                              WorkshopEngineDungeonRoom(0x21 + d, 0xa0 | room),
                          "Engine room mapping mismatch");
            check(Workshop::DungeonDocument::roomIndex(1, 0xf3) == 3, "Despise room bank mismatch");
            rejects([&] { document.level(8, 0); });
            rejects([&] { document.level(0, 8); });
            rejects([&] { document.changes(0, 0, QByteArray(63, 0)); });
            auto level = document.level(2, 3);
            level[9] = char(0x23);
            level[10] = char(0xa5);
            p.resources["DUNGEON.DAT"].edited = document.changes(2, 3, level)["DUNGEON.DAT"];
            p.validate();
            check(p.data("DUNGEON.DAT").left(1216) == base.left(1216) &&
                      p.data("DUNGEON.DAT").mid(1280) == base.mid(1280),
                  "Other dungeon levels changed");
            check(!document.warnings(2, 3, 1, 1).isEmpty(), "Unpaired ladder not diagnosed");
            check(document.level(2, 3)[10] == char(0xa5), "Room reference changed");
            p.save(dir.path() + "/dungeon.imperaproject");
            Project loaded;
            loaded.load(dir.path() + "/dungeon.imperaproject");
            check(loaded.data("DUNGEON.DAT") == p.data("DUNGEON.DAT"),
                  "Dungeon project round trip failed");
            auto package = p.package();
            const auto exported = qEnvironmentVariable("IMPERA_DUNGEON_TEST_PACKAGE");
            if (!exported.isEmpty())
                file(exported, package);
            p.resources["DUNGEON.DAT"].edited = base;
            p.importPackage(package);
            check(document.level(2, 3) == level, "Dungeon package round trip failed");
            auto diagnostics = validateMod(p);
            bool found = false;
            for (auto diagnostic : diagnostics)
                if (diagnostic.entryId == "dungeon/2/level/3/cell/9")
                    found = true;
            check(found, "Dungeon diagnostic lost stable cell ID");
            p.resources["DUNGEON.DAT"].edited[0] = char(0x90);
            rejects([&] { p.validate(); });
            p.resources["DUNGEON.DAT"].edited = base;
            p.resources["DUNGEON.DAT"].edited[0] = char(0xaf);
            p.resources["DUNGEON.CBT"].edited.resize(352);
            rejects([&] { p.validate(); });
            p.resources["DUNGEON.DAT"].edited = base;
            p.resources["DUNGEON.DAT"].edited[0] = p.resources["DUNGEON.DAT"].original[0] =
                char(0xaf);
            rejects([&] {
                p.validate();
            }); // Shrinking combat data also checks unchanged dungeon references.
            p.resources["DUNGEON.DAT"].edited.resize(4095);
            rejects([&] { Workshop::DungeonDocument invalid(&p); });
        });
        test("Dungeon UI edits, undo and room return", [] {
            QTemporaryDir dir;
            fixture(dir.path());
            file(dir.path() + "/DUNGEON.DAT", QByteArray(4096, 0));
            file(dir.path() + "/DUNGEON.CBT", QByteArray(112 * 352, 0));
            WorkshopWindow window;
            window.openGame(dir.path());
            window.selectResource("DUNGEON.DAT");
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            auto canvas = dynamic_cast<MapCanvas *>(window.findChild<QWidget *>("dungeonCanvas"));
            check(canvas, "Dungeon canvas missing");
            canvas->selection = QRect(0, 0, 1, 1);
            check(canvas->copySelection(), "Dungeon copy failed");
            MapCanvas terrain;
            terrain.side = 8;
            terrain.ids = QByteArray(64, 0);
            check(!terrain.beginPaste(), "Dungeon features accepted as terrain");
            terrain.selection = QRect(0, 0, 1, 1);
            check(terrain.copySelection(), "Terrain copy failed");
            check(!canvas->beginPaste(), "Terrain accepted as dungeon features");
            canvas->inspect(2, 3, 0);
            auto palette = window.findChild<QListWidget *>("dungeonPalette");
            for (int i = 0; i < palette->count(); ++i)
                if (palette->item(i)->data(Qt::UserRole).toInt() == 0xa0)
                    palette->setCurrentRow(i);
            window.findChild<QComboBox *>("dungeonSubtype")->setCurrentIndex(3);
            for (auto button : window.findChildren<QPushButton *>())
                if (button->text() == "Apply brush to selected cell")
                    button->click();
            check(window.projectForTests().data("DUNGEON.DAT")[26] == char(0xa3),
                  "Configured room not applied");
            window.findChild<QPushButton *>("openDungeonRoom")->click();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            auto back = window.findChild<QPushButton *>("backToDungeon");
            check(back, "Room return missing");
            back->click();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            canvas = dynamic_cast<MapCanvas *>(window.findChild<QWidget *>("dungeonCanvas"));
            check(canvas && canvas->keyboardCell == QPoint(2, 3), "Room return lost dungeon cell");
            for (auto action : window.findChildren<QAction *>())
                if (action->text().startsWith("Undo ")) {
                    action->trigger();
                    break;
                }
            QApplication::processEvents();
            check(window.projectForTests().data("DUNGEON.DAT")[26] == 0, "Dungeon undo failed");
        });
        test("Shared resource documents preserve bytes and stable identities", [] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            file(dir.path() + "/INIT.OOL", QByteArray(256, 3));
            p.openGame(dir.path());
            Workshop::ResourceDocument document(p, "init.ool");
            auto entry = document.entries()[2];
            check(entry.id == "object/2" && document.id() == "INIT.OOL",
                  "Unstable document identity");
            check(document.replace(entry, document.read(entry))["INIT.OOL"] == p.data("INIT.OOL"),
                  "No-op altered bytes");
            auto changes = document.replace(entry, QByteArray(8, 9));
            check(changes["INIT.OOL"].left(16) == QByteArray(16, 3) &&
                      changes["INIT.OOL"].mid(24) == QByteArray(232, 3),
                  "Unknown bytes lost");
            rejects(
                [&] { document.read({"bad", "Bad", std::numeric_limits<qsizetype>::max(), 8}); });
            rejects([&] { document.replace(entry, QByteArray(9, 1)); });
            p.resources["INIT.OOL"].edited = changes["INIT.OOL"];
            check(document.read(entry) == QByteArray(8, 9), "Document kept stale state");
            p.save(dir.path() + "/document.imperaproj");
            Project loaded;
            loaded.load(dir.path() + "/document.imperaproj");
            check(loaded.data("INIT.OOL") == p.data("INIT.OOL"), "Project round trip failed");
            auto package = p.package();
            p.resources["INIT.OOL"].edited = p.resources["INIT.OOL"].original;
            p.importPackage(package);
            check(document.read(entry) == QByteArray(8, 9), "Package round trip failed");
            p.resources["INIT.OOL"].edited.resize(255);
            rejects([&] { document.entries(); });
            check(Workshop::capability("UNKNOWN.DAT").editor == Workshop::EditorKind::Inspector,
                  "Unknown file lost inspection");
        });
        test("Resource registry and inspector navigation", [] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            file(dir.path() + "/INIT.OOL", QByteArray(256, 3));
            WorkshopWindow window;
            window.openGame(dir.path());
            window.selectResource("INIT.OOL");
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            auto entries = window.findChild<QListWidget *>("resourceEntries");
            check(entries && entries->count() == 32, "Resource shell missing stable entries");
            check(entries->item(2)->data(Qt::UserRole).toString() == "INIT.OOL/object/2",
                  "Navigation uses display labels");
            QMetaObject::invokeMethod(entries, "itemDoubleClicked", Qt::DirectConnection,
                                      Q_ARG(QListWidgetItem *, entries->item(2)));
            auto offset = window.findChild<QSpinBox *>("resourceByteOffset");
            check(offset && offset->value() == 16, "Entry did not navigate to byte span");
            auto table = window.findChild<QTableWidget *>("byteInspector");
            check(table, "Inspector absent");
            table->item(0, 0)->setText("09");
            check(window.projectForTests().data("INIT.OOL")[16] == 9, "Raw edit not committed");
            for (auto action : window.findChildren<QAction *>())
                if (action->text().startsWith("Undo ")) {
                    action->trigger();
                    break;
                }
            QApplication::processEvents();
            check(window.projectForTests().data("INIT.OOL")[16] == 3, "Document undo failed");
        });
        test("Original proportional font compatibility (optional)", [] {
            auto directory = qEnvironmentVariable("U5_GAME_DIR");
            if (directory.isEmpty())
                return;
            QString path;
            for (auto name : QDir(directory).entryList(QDir::Files))
                if (name.compare("PROPORT.PCS", Qt::CaseInsensitive) == 0)
                    path = QDir(directory).filePath(name);
            if (path.isEmpty())
                return;
            QFile font(path);
            check(font.open(QIODevice::ReadOnly), "Cannot read optional font");
            auto preview = Workshop::previewText(U5::decompress(font.readAll()), "The Avatar",
                                                 Workshop::FontMode::Proportional);
            check(preview.issues.isEmpty(), "Original proportional font incompatible");
        });
        test("Shared font previews reject corruption and unsupported glyphs", [] {
            QByteArray normal(1024, char(0xff));
            auto preview =
                Workshop::previewText(normal, QString::fromUtf8("Aé"), Workshop::FontMode::Normal);
            check(!preview.issues.isEmpty(), "Unsupported Unicode silently aliased");
            check(Workshop::previewText(normal, "A", Workshop::FontMode::Runic).issues.isEmpty(),
                  "Runic font rejected");
            rejects(
                [&] { Workshop::previewText(normal.left(1023), "A", Workshop::FontMode::Normal); });
            QByteArray proportional(8, 0);
            U5::setWord(proportional, 0, 1);
            U5::setWord(proportional, 2, 4);
            U5::setWord(proportional, 4, 3);
            U5::setWord(proportional, 6, 1);
            rejects([&] {
                Workshop::previewText(proportional, " ", Workshop::FontMode::Proportional);
            });
            proportional.append(char(0xe0));
            check(Workshop::previewText(proportional, " ", Workshop::FontMode::Proportional)
                      .issues.isEmpty(),
                  "Valid proportional glyph rejected");
            U5::setWord(proportional, 4, 0);
            check(Workshop::previewText(proportional, " ", Workshop::FontMode::Proportional)
                      .issues.isEmpty(),
                  "Blank proportional space rejected");
        });
        test("Export Britannia package for engine integration", [] {
            QTemporaryDir dir;
            auto project = fixture(dir.path());
            QByteArray overlay(U5_WORLD_INDEX_OFFSET + 256, 0);
            std::memcpy(overlay.data() + U5_WORLD_INDEX_OFFSET, U5_BritanniaDefaultIndex, 256);
            QByteArray chunks(65536, 5);
            chunks.replace(256, 256, QByteArray(256, 7));
            file(dir.path() + "/DATA.OVL", overlay);
            file(dir.path() + "/BRIT.DAT", chunks);
            project.openGame(dir.path());
            auto &changed = project.resources["DATA.OVL"].edited;
            changed.replace(U5_WORLD_INDEX_OFFSET, 256, QByteArray(256, char(255)));
            changed[U5_WORLD_INDEX_OFFSET] = 1;
            changed[U5_WORLD_INDEX_OFFSET + 255] = 0;
            auto package = project.package();
            Project loaded;
            loaded.openGame(dir.path());
            loaded.importPackage(package);
            check(loaded.data("DATA.OVL") == changed, "World package round trip");
            // Optional bridge to save_slots_test's production world-loader test.
            if (qEnvironmentVariableIsSet("IMPERA_WORLD_TEST_PACKAGE"))
                file(qEnvironmentVariable("IMPERA_WORLD_TEST_PACKAGE"), package);
        });
        test("World resource validation and capability guidance", [] {
            QTemporaryDir dir;
            auto project = fixture(dir.path());
            QByteArray overlay(U5_WORLD_INDEX_OFFSET + 256, 0);
            std::memcpy(overlay.data() + U5_WORLD_INDEX_OFFSET, U5_BritanniaDefaultIndex, 256);
            file(dir.path() + "/DATA.OVL", overlay);
            file(dir.path() + "/BRIT.DAT", QByteArray(65536, 5));
            file(dir.path() + "/INIT.OOL", QByteArray(256, 0));
            file(dir.path() + "/UNDER.OOL", QByteArray(256, 0));
            project.openGame(dir.path());
            auto &changed = project.resources["DATA.OVL"].edited;
            changed.replace(U5_WORLD_INDEX_OFFSET, 256, QByteArray(256, char(255)));
            changed[U5_WORLD_INDEX_OFFSET] = 0;
            project.resources["BRIT.DAT"].edited = QByteArray(256, 7);
            project.validate();
            auto report = validateMod(project);
            check(std::none_of(report.begin(), report.end(), [](const auto &d) {
                return d.severity == ModDiagnostic::Error;
            }), "Paired Britannia map/index edit reported invalid");
            changed[U5_WORLD_INDEX_OFFSET] = 1;
            rejects([&] { project.validate(); });
            changed[U5_WORLD_INDEX_OFFSET] = 0;
            changed[0] = 1;
            project.resources["INIT.OOL"].edited[0] = 1;
            project.resources["UNDER.OOL"].edited[0] = 2;
            report = validateMod(project);
            check(std::any_of(report.begin(), report.end(), [](const auto &d) {
                return d.message.contains("outside the Britannia map index");
            }), "Unsupported overlay edits were not explained");
            check(std::any_of(report.begin(), report.end(), [](const auto &d) {
                return d.message.contains("UNDER.OOL overrides this");
            }), "Initial object precedence was not explained");
            project.resources["INIT.OOL"].edited.resize(255);
            rejects([&] { project.validate(); });
        });
        test("Lossless dialogue structure", [] {
            auto bytes =
                U5::encodeText("Name<Entry>Description<Entry>Greeting<Entry>Job<Entry>Bye<Entry>"
                               "tele<Entry><Or><Entry>star<Entry>Hello<Label 1><Entry><Any><Label "
                               "1>Question?<Entry>No<Entry>y<Entry>Yes<End "
                               "Conversation><Entry><Any><Label 15>@");
            auto d = Dialogue::parse(bytes);
            check(d.bytes() == bytes, "Dialogue no-op changed bytes");
            check(d.structuralError.isEmpty(), "Structured parse failed");
            check(d.topics.size() == 1 && d.topics[0].keywords.size() == 2,
                  "Alias chain not grouped");
            check(d.questions.size() == 1 && d.tail >= 0, "Tail mistaken for question");
            check(d.questions[0].replies.size() == 1, "Reply missing");
            auto changed = Dialogue::replaceAliases(d, d.topics[0], {"moon", "sun"});
            auto next = Dialogue::parse(changed);
            check(next.entries[next.topics[0].response].bytes ==
                      d.entries[d.topics[0].response].bytes,
                  "Alias edit touched response");
            rejects([&] { Dialogue::removeQuestion(d, 1); });
            check(Dialogue::parse(Dialogue::addQuestion(d)).questions.size() == 2,
                  "Add question failed");
            auto malformed = QByteArray::fromHex("85b1");
            check(Dialogue::parse(malformed).bytes() == malformed, "Malformed bytes lost");
            auto operands = Dialogue::parse(QByteArray::fromHex("8c919100"));
            check(operands.bytes() == QByteArray::fromHex("8c919100"),
                  "Operand/control collision lost");
            Dialogue::Simulator sim(d, {});
            sim.start();
            auto lines = sim.respond("star");
            check(!lines.isEmpty(), "Simulation produced no lines");
            sim.respond("yes");
            check(sim.ended, "Question reply did not end conversation");
        });
        test("Dialogue semantics and engine keyword matching", [] {
            auto make = [](const QString &body) {
                return Dialogue::parse(U5::encodeText(
                    "Name<Entry>Description<Entry>Greeting<Entry>Job<Entry>Bye<Entry>" + body +
                    "<Any><Label 15>@"));
            };
            for (auto key : QStringList{"y", "tele", "planet", "password"})
                for (auto input : QStringList{"YES", "TELESCOPE", "PLANETS", "PASSWORD", "NO"}) {
                    auto encoded = Dialogue::encodeKeyword(key);
                    auto upper = input.toLatin1();
                    bool expected = WorkshopEngineMatch(reinterpret_cast<const unsigned char *>(
                                                            encoded.constData()),
                                                        upper.data()) == 0;
                    auto d = make(key + "<Entry>Matched<End Conversation><Entry>");
                    Dialogue::Simulator sim(d, {});
                    sim.start();
                    auto lines = sim.respond(input);
                    bool matched = false;
                    for (auto line : lines)
                        if (line.trace && line.text.startsWith("Matched keyword"))
                            matched = true;
                    check(expected == matched, "Sandbox keyword matching differs from engine");
                }
            auto d = make("help<Entry><Label 1><Entry><Any><Label "
                          "1>Question?<Entry>No<Entry>y<Entry><Gold><Byte 176><Byte "
                          "176><Byte 180>Thanks<End Conversation><Entry>");
            Dialogue::Sandbox state;
            state.gold = 3;
            Dialogue::Simulator poor(d, state);
            poor.start();
            poor.respond("help");
            poor.respond("yes");
            check(!poor.ended && poor.state.gold == 3,
                  "Insufficient payment changed state or ended conversation");
            state.gold = 10;
            Dialogue::Simulator rich(d, state);
            rich.start();
            rich.respond("help");
            rich.respond("yes");
            check(rich.ended && rich.state.gold == 6, "Payment did not use encoded amount");
            auto loop = make("help<Entry><Label 1><Entry><Any><Label 1><Label 1><Entry>No<Entry>");
            Dialogue::Simulator bounded(loop, {});
            bounded.start();
            bounded.respond("help");
            check(bounded.ended, "Recursive question was not bounded");
            check(Dialogue::parse(QByteArray::fromHex("8c919000")).bytes() ==
                      QByteArray::fromHex("8c919000"),
                  "Label-like operand changed");
            auto renamed = Dialogue::addQuestion(d);
            auto next = Dialogue::parse(renamed);
            auto removed = Dialogue::parse(Dialogue::removeQuestion(next, 1, 2));
            check(removed.question(1) < 0 && Dialogue::references(removed, 2).size() > 0,
                  "Retargeted deletion lost callers");
            rejects([] { Dialogue::encodeKeyword(" "); });
            auto missing = make("help<Entry><Label 8><Entry>");
            bool error = false;
            for (auto issue : missing.issues())
                error |= issue.error;
            check(error, "Missing question reference not diagnosed");
        });
        test("LZW width changes and dictionary resets", [] {
            std::mt19937 rng(1948);
            for (int size : {0, 1, 254, 255, 256, 257, 512, 4096, 65536, 150000}) {
                QByteArray bytes;
                for (int i = 0; i < size; i++)
                    bytes.append(char(rng()));
                auto compressed = U5::compress(bytes);
                check(U5::decompress(compressed) == bytes, "LZW roundtrip");
                void *output = nullptr;
                unsigned length = 0;
                check(WorkshopEngineDecode(compressed.constData(), compressed.size(), &output,
                                           &length),
                      "Engine decoder rejected native output");
                check(length == unsigned(bytes.size()) &&
                          QByteArray(static_cast<char *>(output), length) == bytes,
                      "Engine/native codec mismatch");
                free(output);
                check(U5::decompress(U5::compress(QByteArray(size, 'x'))) == QByteArray(size, 'x'),
                      "Repeated dictionary roundtrip");
            }
            rejects([] { U5::decompress(QByteArray::fromHex("0500000000")); });
        });
        test("Graphics palette, mask padding and roundtrip", [] {
            QByteArray raw(6, 0);
            U5::setWord(raw, 0, 1);
            U5::setWord(raw, 2, 6);
            raw += QByteArray::fromHex("09000100");
            raw += QByteArray(8, char(0x11));
            U5::setWord(raw, 4, raw.size());
            raw += QByteArray::fromHex("090001008000");
            auto g = U5::readGraphics("ITEMS.16", U5::compress(raw));
            check(qAlpha(g.images[0].pixel(0, 0)) == 0, "Mask alpha");
            check(qAlpha(g.images[0].pixel(8, 0)) == 255, "Padded alpha");
            check(U5::writeGraphics(g) == g.original, "Graphics no-op identity");
            g.images[0].setPixel(8, 0, qRgba(0, 0, 0, 0));
            auto next = U5::readGraphics("ITEMS.16", U5::writeGraphics(g));
            check(qAlpha(next.images[0].pixel(8, 0)) == 0, "Changed alpha");
            g.images[0].setPixel(0, 0, qRgba(1, 2, 3, 255));
            rejects([&] { U5::writeGraphics(g); });
        });
        test("Raw tiles and PNG alpha rejection", [] {
            auto g = U5::readGraphics("TILES.16", QByteArray(65536, 0));
            check(g.images.size() == 512, "Tile count");
            g.images[0].setPixel(0, 0, qRgba(0, 0, 0, 0));
            rejects([&] { U5::writeGraphics(g); });
        });
        test("Dialogue control operands and token identity", [] {
            QByteArray bytes = QByteArray::fromHex("0181c18285");
            rejects([&] { U5::decodeText(bytes); });
            bytes = QByteArray::fromHex("0181c18285b0b1b286ff8cfffe01ffc000909fc00000");
            check(U5::encodeText(U5::decodeText(bytes)) == bytes, "Dialogue exact byte identity");
            check(U5::encodeText("<thee><thee,><great><Great>") == QByteArray::fromHex("0d393861"),
                  "Canonical word indices");
            rejects([] { U5::encodeText("<Set Flag>"); });
            rejects([] { U5::encodeText("<Byte 133><Byte 0><Byte 1><Byte 2>"); });
        });
        test("TLK rebuild, tails, IDs and limits", [] {
            QVector<U5::Conversation> entries = {{1, QByteArray::fromHex("c100909fc000")},
                                                 {2, QByteArray::fromHex("c200")}};
            auto b = U5::writeDialogue(entries);
            auto decoded = U5::readDialogue(b);
            check(decoded.size() == 2 && decoded[0].bytes == entries[0].bytes, "TLK segments");
            entries[1].id = 1;
            rejects([&] { U5::writeDialogue(entries); });
            entries[1].id = 2;
            entries[1].bytes = QByteArray(1025, 'x');
            rejects([&] { U5::writeDialogue(entries); });
        });
        test("Britannia implicit water and shared chunks", [] {
            QByteArray ovl(0x3986, 0);
            ovl.replace(0x3886, 256, QByteArray(256, char(255)));
            ovl[0x3886] = 0;
            ovl[0x3887] = 0;
            QByteArray chunks(256, char(2));
            auto map = U5::worldMap("BRIT.DAT", chunks, ovl);
            map[0] = 3;
            map[32] = 4;
            U5::writeWorld("BRIT.DAT", map, chunks, ovl);
            check(U5::worldMap("BRIT.DAT", chunks, ovl) == map,
                  "Shared chunks and water preserved");
            check((unsigned char)map[16] == 2, "Original alias unaffected");
        });
        test("Underworld extensions and settlement basement mapping", [] {
            QByteArray b(65536, 2);
            b.append(QByteArray(256, 9));
            auto map = U5::worldMap("UNDER.DAT", b, {});
            map[0] = 4;
            QByteArray unused;
            U5::writeWorld("UNDER.DAT", map, b, unused);
            check(b.right(256) == QByteArray(256, 9), "Underworld tail");
            auto pages = U5::mapPages("TOWNE.DAT", QByteArray(16384, 0));
            check(pages[6].floor == -1 && pages[6].settlement == 3, "Yew basement");
            auto keep = U5::mapPages("KEEP.DAT", QByteArray(16384, 0));
            check(keep[13].floor == -1 && keep[13].settlement == 7, "Serpent's Hold basement");
        });
        test("Fixed story windows and rejection", [] {
            auto offsets = U5::storyOffsets();
            QByteArray b(offsets.last() + 20, 0);
            for (int offset : offsets)
                b.replace(offset, 5, "HELLO");
            auto pages = U5::storyPages(b);
            check(pages.size() == 20, "Story pages");
            auto changed = U5::changeStory(b, 3, "HI");
            check(changed.size() == b.size() && U5::storyPages(changed)[4] == "HELLO",
                  "Fixed offsets");
            rejects([&] { U5::changeStory(b, 0, "TOO LONG"); });
        });
        test("Sparse packages, projects, checksum and original preservation", [] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            p.title = "Test mod";
            p.resources["INIT.GAM"].edited[0x202] = 9;
            auto bytes = p.package();
            check(bytes.size() < 150, "Sparse package");
            auto next = fixture(dir.path());
            next.importPackage(bytes);
            check(next.data("INIT.GAM") == p.data("INIT.GAM"), "Shared C decoder");
            p.save(dir.path() + "/mod.imperaproject");
            Project restored;
            restored.load(dir.path() + "/mod.imperaproject");
            check(restored.package() == bytes, "Project roundtrip");
            auto corrupt = bytes;
            corrupt[corrupt.size() - 1] ^= 1;
            rejects([&] { next.importPackage(corrupt); });
            check(next.data("INIT.GAM") == p.data("INIT.GAM"), "Rejected package changed project");
            next.resources["INIT.GAM"].original[0] = 1;
            rejects([&] { next.importPackage(bytes); });
            QFile f(dir.path() + "/init.gam");
            f.open(QIODevice::ReadOnly);
            check(f.readAll() == p.resources["INIT.GAM"].original, "Original file mutated");
        });
        test("Map document edits preserve floor bytes and combat metadata", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            QByteArray settlement(16384, 4), combat(704, char(173));
            p.resources["TOWNE.DAT"] = {settlement, settlement};
            p.resources["TEST.CBT"] = {combat, combat};
            MapDocument town(&p, "TOWNE.DAT");
            auto cells = town.terrain(6);
            cells[45] = 9;
            auto changed = town.changes(6, cells)["TOWNE.DAT"];
            check(changed[6 * 1024 + 45] == 9 &&
                      changed.left(6 * 1024) == settlement.left(6 * 1024),
                  "Settlement floor changed unrelated data");
            MapDocument encounter(&p, "TEST.CBT");
            auto terrain = encounter.terrain(1);
            terrain[2 * 11 + 3] = 8;
            auto after = encounter.changes(1, terrain)["TEST.CBT"];
            for (int i = 0; i < combat.size(); ++i)
                check(after[i] == (i == 352 + 2 * 32 + 3 ? char(8) : combat[i]),
                      "Combat terrain damaged metadata");
            rejects([&] { encounter.changes(1, QByteArray(120, 0)); });
            check(p.changed().isEmpty(), "Map proposals mutated project before commitment");
            QByteArray ovl(0x3986, 0);
            ovl.replace(0x3886, 256, QByteArray(256, char(255)));
            p.resources["DATA.OVL"] = {ovl, ovl};
            p.resources["BRIT.DAT"] = {{}, {}};
            MapDocument world(&p, "BRIT.DAT");
            auto water = world.terrain(0);
            water[0] = 2;
            auto pair = world.changes(0, water);
            check(pair.size() == 2 &&
                      U5::worldMap("BRIT.DAT", pair["BRIT.DAT"], pair["DATA.OVL"]) == water,
                  "World proposal lost paired-resource edit");
            auto tooMany = water;
            for (int i = 0; i < 256; ++i) {
                int off = (i / 16 * 16) * 256 + (i % 16 * 16);
                tooMany[off] = char(i);
                tooMany[off + 1] = char(3);
            }
            rejects([&] { world.changes(0, tooMany); });
            check(p.changed().isEmpty(), "Failed capacity check modified project");
        });
        test("Continuous map gestures, boundary picking and cancellation", [&] {
            MapCanvas canvas;
            canvas.side = 16;
            canvas.zoom = 1;
            canvas.ids = QByteArray(256, 0);
            canvas.brush = 7;
            canvas.resizeMap();
            int commits = 0;
            canvas.commit = [&](const QByteArray &) {
                ++commits;
                return true;
            };
            auto mouse = [&](QEvent::Type type, QPointF pos, Qt::MouseButton button,
                             Qt::MouseButtons buttons) {
                QMouseEvent event(type, pos, pos, button, buttons, Qt::NoModifier);
                QApplication::sendEvent(&canvas, &event);
            };
            mouse(QEvent::MouseButtonPress, {8, 8}, Qt::LeftButton, Qt::LeftButton);
            mouse(QEvent::MouseMove, {248, 248}, Qt::NoButton, Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease, {248, 248}, Qt::LeftButton, Qt::NoButton);
            check(commits == 1, "Continuous drag committed multiple commands");
            for (int i = 0; i < 16; ++i)
                check(canvas.ids[i * 16 + i] == 7, "Fast diagonal stroke has gaps");
            auto original = canvas.ids;
            mouse(QEvent::MouseButtonPress, {-0.1, 8}, Qt::LeftButton, Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease, {-0.1, 8}, Qt::LeftButton, Qt::NoButton);
            check(canvas.ids == original && commits == 1, "Outside click changed edge cell");
            mouse(QEvent::MouseButtonPress, {24, 8}, Qt::LeftButton, Qt::LeftButton);
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(&canvas, &escape);
            mouse(QEvent::MouseButtonRelease, {24, 8}, Qt::LeftButton, Qt::NoButton);
            check(canvas.ids == original && commits == 1, "Escape did not discard whole gesture");
            mouse(QEvent::MouseButtonPress, {24, 8}, Qt::LeftButton, Qt::LeftButton);
            QFocusEvent lost(QEvent::FocusOut);
            QApplication::sendEvent(&canvas, &lost);
            check(canvas.ids == original && !canvas.painting(),
                  "Lost focus left uncommitted painting");
            mouse(QEvent::MouseButtonPress, {24, 8}, Qt::LeftButton, Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease, {500, 500}, Qt::LeftButton, Qt::NoButton);
            check(commits == 2 && !canvas.painting(), "Release outside did not end stroke");
            original = canvas.ids;
            canvas.commit = [&](const QByteArray &) { return false; };
            mouse(QEvent::MouseButtonPress, {40, 8}, Qt::LeftButton, Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease, {40, 8}, Qt::LeftButton, Qt::NoButton);
            check(canvas.ids == original, "Rejected edit left painted cells visible");
            int picked = -1;
            canvas.pick = [&](int id) { picked = id; };
            mouse(QEvent::MouseButtonPress, {8, 8}, Qt::RightButton, Qt::RightButton);
            check(picked == 7 && canvas.ids == original, "Eyedropper painted terrain");
            int hovered = -1;
            canvas.inspect = [&](int x, int, int) { hovered = x; };
            mouse(QEvent::MouseMove, {40, 8}, Qt::NoButton, Qt::NoButton);
            check(hovered == 2, "Hover requires a pressed button");
            canvas.zoom = 0.35;
            canvas.resizeMap();
            check(canvas.cellAt({16 * 0.35 * 3 + 0.1, 16 * 0.35 * 5 + 0.1}) == QPoint(3, 5),
                  "Fractional zoom picking disagrees with canvas");
        });
        test("Terrain selection, rectangle previews and bounded iterative fill", [&] {
            MapCanvas canvas;
            canvas.side = 8;
            canvas.zoom = 1;
            canvas.ids = QByteArray(64, 0);
            canvas.brush = 7;
            canvas.resizeMap();
            int commits = 0;
            canvas.commit = [&](const QByteArray &) {
                ++commits;
                return true;
            };
            auto mouse = [&](QEvent::Type type, int x, int y, Qt::MouseButtons buttons) {
                QPointF pos(x * 16 + 8, y * 16 + 8);
                QMouseEvent event(type, pos, pos,
                                  type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                                  buttons, Qt::NoModifier);
                QApplication::sendEvent(&canvas, &event);
            };
            auto drag = [&](int x1, int y1, int x2, int y2) {
                mouse(QEvent::MouseButtonPress, x1, y1, Qt::LeftButton);
                mouse(QEvent::MouseMove, x2, y2, Qt::LeftButton);
                mouse(QEvent::MouseButtonRelease, x2, y2, Qt::NoButton);
            };
            canvas.tool = MapCanvas::Select;
            drag(4, 4, 2, 2);
            check(canvas.selection == QRect(2, 2, 3, 3) && commits == 0 &&
                      canvas.ids == QByteArray(64, 0),
                  "Reverse selection changed terrain or bounds");
            mouse(QEvent::MouseButtonPress, 0, 0, Qt::LeftButton);
            QFocusEvent focusOut(QEvent::FocusOut);
            QApplication::sendEvent(&canvas, &focusOut);
            check(canvas.selection == QRect(2, 2, 3, 3), "Cancelled selection lost prior bounds");
            canvas.clearSelection();
            canvas.tool = MapCanvas::Rectangle;
            mouse(QEvent::MouseButtonPress, 1, 1, Qt::LeftButton);
            mouse(QEvent::MouseMove, 4, 4, Qt::LeftButton);
            check(commits == 0 && canvas.ids[4 * 8 + 4] == 7, "Rectangle committed before release");
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(&canvas, &escape);
            check(canvas.ids == QByteArray(64, 0), "Rectangle cancellation did not roll back");
            canvas.outline = true;
            drag(4, 4, 1, 1);
            check(commits == 1 && canvas.ids[2 * 8 + 2] == 0 && canvas.ids[1 * 8 + 2] == 7,
                  "Outline rectangle filled its interior or split history");
            drag(4, 4, 1, 1);
            check(commits == 1, "No-op rectangle added history");
            canvas.selection = QRect(2, 2, 2, 2);
            canvas.tool = MapCanvas::Fill;
            canvas.brush = 9;
            mouse(QEvent::MouseButtonPress, 2, 2, Qt::LeftButton);
            check(commits == 2 && canvas.ids[3 * 8 + 3] == 9 && canvas.ids[2 * 8 + 4] == 7 &&
                      canvas.ids[5 * 8 + 5] == 0,
                  "Fill escaped selection or crossed a barrier");
            canvas.clearSelection();
            canvas.ids = QByteArray(64, 1);
            canvas.ids[0] = canvas.ids[9] = 0;
            mouse(QEvent::MouseButtonPress, 0, 0, Qt::LeftButton);
            check(canvas.ids[0] == 9 && canvas.ids[9] == 0,
                  "Fill connected diagonal cells or wrapped edges");
            canvas.side = 256;
            canvas.ids = QByteArray(65536, 0);
            mouse(QEvent::MouseButtonPress, 0, 0, Qt::LeftButton);
            check(canvas.ids == QByteArray(65536, 9), "World-sized fill failed");
        });
        test("Typed terrain clipboard, paste preview, cancellation and bounds", [&] {
            MapCanvas source, target;
            source.side = 8;
            source.ids = QByteArray(64, 0);
            source.ids[9] = 5;
            source.ids[10] = 6;
            source.ids[17] = 7;
            source.ids[18] = 8;
            source.selection = QRect(1, 1, 2, 2);
            check(source.copySelection(), "Terrain copy failed");
            target.side = 11;
            target.zoom = 1;
            target.ids = QByteArray(121, 0);
            target.resizeMap();
            target.actors.append({3, 3, 256, 2});
            int commits = 0;
            target.commit = [&](const QByteArray &) {
                ++commits;
                return true;
            };
            auto mouse = [&](QEvent::Type type, int x, int y) {
                QPointF pos(x * 16 + 8, y * 16 + 8);
                QMouseEvent event(
                    type, pos, pos, type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                    type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(&target, &event);
            };
            check(target.beginPaste(), "Cross-map paste did not read typed clipboard");
            mouse(QEvent::MouseMove, 3, 3);
            check(target.pasteChangedCells() == 4 && target.ids == QByteArray(121, 0) &&
                      commits == 0,
                  "Paste preview modified destination");
            mouse(QEvent::MouseButtonPress, 10, 10);
            check(target.pasting() && commits == 0 && target.ids == QByteArray(121, 0),
                  "Out-of-bounds paste was silently clipped");
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(&target, &escape);
            check(!target.pasting() && target.ids == QByteArray(121, 0),
                  "Paste cancellation changed data");
            target.beginPaste();
            target.tool = MapCanvas::Eyedropper;
            mouse(QEvent::MouseButtonPress, 3, 3);
            check(commits == 1 && target.ids[3 * 11 + 3] == 5 && target.ids[4 * 11 + 4] == 8 &&
                      target.actors.size() == 1 && target.actors[0].npc == 2,
                  "Paste failed or copied actors");
            auto previous = target.ids;
            target.commit = [&](const QByteArray &) { return false; };
            target.beginPaste();
            mouse(QEvent::MouseButtonPress, 5, 5);
            check(target.ids == previous, "Rejected paste left preview terrain behind");
            auto mime = new QMimeData;
            mime->setData("application/x-impera-terrain",
                          QByteArray("IMPTILE1", 8) + QByteArray(4, char(255)));
            QApplication::clipboard()->setMimeData(mime);
            check(!target.beginPaste(), "Malformed clipboard dimensions accepted");
            QApplication::clipboard()->setText("Not terrain");
            check(!target.beginPaste(), "Plain text mistaken for terrain");
        });
        test("World paste capacity failure is atomic and combat paste preserves records", [&] {
            QTemporaryDir dir;
            auto project = fixture(dir.path());
            QByteArray overlay(0x3986, 0);
            overlay.replace(0x3886, 256, QByteArray(256, char(255)));
            project.resources["DATA.OVL"] = {overlay, overlay};
            project.resources["BRIT.DAT"] = {{}, {}};
            MapDocument world(&project, "BRIT.DAT");
            MapCanvas stamp;
            stamp.side = 256;
            stamp.ids = world.terrain(0);
            for (int i = 0; i < 256; ++i) {
                int offset = (i / 16 * 16) * 256 + i % 16 * 16;
                stamp.ids[offset] = char(i);
                stamp.ids[offset + 1] = 3;
            }
            stamp.selection = QRect(0, 0, 256, 256);
            stamp.copySelection();
            MapCanvas target;
            target.side = 256;
            target.zoom = 1;
            target.ids = world.terrain(0);
            auto before = target.ids;
            bool rejected = false;
            target.commit = [&](const QByteArray &cells) {
                try {
                    world.changes(0, cells);
                } catch (const std::exception &) {
                    rejected = true;
                    return false;
                }
                return true;
            };
            target.beginPaste();
            QMouseEvent place(QEvent::MouseButtonPress, {8, 8}, {8, 8}, Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&target, &place);
            check(rejected && target.ids == before && project.changed().isEmpty() &&
                      project.data("DATA.OVL") == overlay,
                  "Over-capacity paste mutated paired resources");
            QByteArray combat(352, char(173));
            project.resources["TEST.CBT"] = {combat, combat};
            MapDocument encounter(&project, "TEST.CBT");
            stamp.side = 8;
            stamp.ids = QByteArray(64, 9);
            stamp.selection = QRect(0, 0, 2, 2);
            stamp.copySelection();
            target.side = 11;
            target.ids = encounter.terrain(0);
            target.commit = [&](const QByteArray &cells) {
                project.resources["TEST.CBT"].edited = encounter.changes(0, cells)["TEST.CBT"];
                return true;
            };
            target.beginPaste();
            QApplication::sendEvent(&target, &place);
            auto after = project.data("TEST.CBT");
            for (int i = 0; i < 352; ++i)
                check(after[i] == (i == 0 || i == 1 || i == 32 || i == 33 ? char(9) : combat[i]),
                      "Combat paste touched metadata or padding");
            check(encounter.terrain(0, true) == QByteArray(121, char(173)),
                  "Original comparison reads edited terrain");
        });
        test("Pointer-anchored map zoom and read-only panning", [&] {
            auto canvas = new MapCanvas;
            canvas->side = 32;
            canvas->zoom = 2;
            canvas->ids = QByteArray(1024, 0);
            canvas->resizeMap();
            MapView view(canvas);
            view.resize(460, 380);
            view.show();
            app.processEvents();
            view.centerMap({16, 16});
            QPoint anchor(130, 90);
            auto before = QPointF(canvas->mapFrom(view.viewport(), anchor)) / (16 * canvas->zoom);
            view.setZoom(4, anchor);
            app.processEvents();
            auto after = QPointF(canvas->mapFrom(view.viewport(), anchor)) / (16 * canvas->zoom);
            check(QLineF(before, after).length() < 0.03, "Zoom moved map point under pointer");
            auto original = canvas->ids;
            int old = view.horizontalScrollBar()->value();
            auto pan = [&](QEvent::Type type, QPointF pos, Qt::MouseButton button,
                           Qt::MouseButtons buttons) {
                QMouseEvent event(type, pos, pos, button, buttons, Qt::NoModifier);
                QApplication::sendEvent(canvas, &event);
            };
            pan(QEvent::MouseButtonPress, {100, 100}, Qt::MiddleButton, Qt::MiddleButton);
            pan(QEvent::MouseMove, {140, 100}, Qt::NoButton, Qt::MiddleButton);
            pan(QEvent::MouseButtonRelease, {140, 100}, Qt::MiddleButton, Qt::NoButton);
            check(view.horizontalScrollBar()->value() == old - 40 && canvas->ids == original,
                  "Pan painted or failed to move view");
            view.fitMap();
            app.processEvents();
            check(view.fitting && canvas->width() <= view.viewport()->width() &&
                      canvas->height() <= view.viewport()->height(),
                  "Fit clipped map");
        });
        test("Map workspace brush, navigation and undo preserve view state", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            file(dir.path() + "/TOWNE.DAT", QByteArray(16384, 0));
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Map view fixture load");
            w.show();
            w.selectResource("TOWNE.DAT");
            app.processEvents();
            auto active = [&] {
                return w.findChild<QStackedWidget *>("workspaces")->currentWidget();
            };
            auto findCanvas = [&] {
                return dynamic_cast<MapCanvas *>(active()->findChild<QWidget *>("mapCanvas"));
            };
            auto palette = active()->findChild<QListWidget *>("mapPalette");
            palette->setCurrentRow(9);

            active()->findChild<QComboBox *>("mapZoom")->setCurrentIndex(4);
            auto view = dynamic_cast<MapView *>(active()->findChild<QScrollArea *>("canvasView"));
            view->centerMap({12, 13});
            auto center = view->mapCenter();
            auto canvas = findCanvas();
            canvas->selection = QRect(0, 0, 3, 3);
            canvas->selectionChanged();
            active()->findChild<QCheckBox *>("mapComparison")->setChecked(true);
            active()->findChild<QPushButton *>("mapFavorite")->click();
            QMouseEvent down(QEvent::MouseButtonPress, {40, 40}, {40, 40}, Qt::LeftButton,
                             Qt::LeftButton, Qt::NoModifier);
            QMouseEvent up(QEvent::MouseButtonRelease, {40, 40}, {40, 40}, Qt::LeftButton,
                           Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &down);
            QApplication::sendEvent(canvas, &up);
            check(U5::byte(w.projectForTests().data("TOWNE.DAT"), 0) == 9,
                  "Selected brush did not paint");
            for (auto action : w.findChildren<QAction *>())
                if (action->text().startsWith("Undo ")) {
                    action->trigger();
                    break;
                }
            app.processEvents();
            app.processEvents(); // Rebuild and queued viewport restoration use separate event
                                 // turns.
            canvas = findCanvas();
            view = dynamic_cast<MapView *>(active()->findChild<QScrollArea *>("canvasView"));
            check(canvas->selection == QRect(0, 0, 3, 3) && canvas->comparison &&
                      canvas->brush == 9 && !canvas->grid && canvas->zoom == 4 &&
                      QLineF(center, view->mapCenter()).length() < 0.1,
                  "Undo reset view settings");
            check(w.projectForTests().data("TOWNE.DAT") == QByteArray(16384, 0),
                  "Undo failed terrain restoration");
            for (auto action : w.findChildren<QAction *>())
                if (action->text().startsWith("Redo ")) {
                    action->trigger();
                    break;
                }
            app.processEvents();
            app.processEvents();
            view = dynamic_cast<MapView *>(active()->findChild<QScrollArea *>("canvasView"));
            check(U5::byte(w.projectForTests().data("TOWNE.DAT"), 0) == 9 &&
                      QLineF(center, view->mapCenter()).length() < 0.1,
                  "Redo lost terrain or view center");
            for (auto action : w.findChildren<QAction *>())
                if (action->text().startsWith("Undo ")) {
                    action->trigger();
                    break;
                }
            app.processEvents();
            app.processEvents();
            auto page = active()->findChild<QComboBox *>("mapPage");
            page->setCurrentIndex(page->findData(1));
            app.processEvents();
            active()->findChild<QListWidget *>("mapPalette")->setCurrentRow(11);
            page->setCurrentIndex(0);
            app.processEvents();
            check(findCanvas()->brush == 9, "Floor navigation did not restore per-page brush");
            auto filter = active()->findChild<QComboBox *>("mapPaletteFilter");
            auto tiles = active()->findChild<QListWidget *>("mapPalette");
            filter->setCurrentIndex(1);
            check(!tiles->item(9)->isHidden() && tiles->item(11)->isHidden(),
                  "Favorites lost across rebuild");
            filter->setCurrentIndex(2);
            check(!tiles->item(11)->isHidden(), "Recent brushes lost across navigation");
            filter->setCurrentIndex(0);
            auto mini = active()->findChild<QWidget *>("mapMinimap");
            view = dynamic_cast<MapView *>(active()->findChild<QScrollArea *>("canvasView"));
            auto oldCenter = view->mapCenter();
            auto pos = QPointF(mini->width() / 2, mini->height() / 2);
            QMouseEvent miniClick(QEvent::MouseButtonPress, pos, pos, Qt::LeftButton,
                                  Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(mini, &miniClick);
            check(QLineF(oldCenter, view->mapCenter()).length() > 1 &&
                      w.projectForTests().changed().isEmpty(),
                  "Minimap failed read-only navigation");

            QMouseEvent pick(QEvent::MouseButtonPress, {40, 40}, {40, 40}, Qt::RightButton,
                             Qt::RightButton, Qt::NoModifier);
            QApplication::sendEvent(findCanvas(), &pick);
            check(active()->findChild<QListWidget *>("mapPalette")->currentRow() == 0 &&
                      active()->findChild<QLabel *>("mapBrushLabel")->text().contains("Tile 0"),
                  "Eyedropper and palette disagree");
            w.selectResource("TILES.16");
            app.processEvents();
            auto back = active()->findChild<QPushButton *>("backToMap");
            check(back, "Linked workspace lacks return to map");
            back->click();
            app.processEvents();
            check(!findCanvas()->grid && findCanvas()->zoom == 4 && findCanvas()->brush == 0,
                  "Return to map lost view state");
            check(w.projectForTests().changed().isEmpty(),
                  "Navigation or view settings changed resources");
        });
        test("Native terrain fill and paste each undo as one operation", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            file(dir.path() + "/TOWNE.DAT", QByteArray(16384, 0));
            WorkshopWindow window;
            check(window.openGame(dir.path()), "Native terrain fixture failed");
            window.show();
            window.selectResource("TOWNE.DAT");
            app.processEvents();
            auto active = [&] {
                return window.findChild<QStackedWidget *>("workspaces")->currentWidget();
            };
            auto canvas = [&] {
                return dynamic_cast<MapCanvas *>(active()->findChild<QWidget *>("mapCanvas"));
            };
            auto click = [&](int x, int y) {
                QPointF point((x + 0.5) * 16 * canvas()->zoom, (y + 0.5) * 16 * canvas()->zoom);
                QMouseEvent down(QEvent::MouseButtonPress, point, point, Qt::LeftButton,
                                 Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(canvas(), &down);
                QMouseEvent up(QEvent::MouseButtonRelease, point, point, Qt::LeftButton,
                               Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(canvas(), &up);
            };
            auto history = [&](const QString &prefix) {
                bool triggered = false;
                for (auto action : window.findChildren<QAction *>())
                    if (action->text().startsWith(prefix + " ") && action->isEnabled()) {
                        action->trigger();
                        triggered = true;
                        break;
                    }
                check(triggered, "Missing terrain history command");
                app.processEvents();
                app.processEvents();
            };
            active()->findChild<QListWidget *>("mapPalette")->setCurrentRow(9);
            canvas()->selection = QRect(1, 1, 2, 2);
            canvas()->selectionChanged();
            active()->findChild<QButtonGroup *>("mapTools")->button(MapCanvas::Fill)->click();
            click(1, 1);
            auto filled = window.projectForTests().data("TOWNE.DAT");
            check(filled.count(char(9)) == 4, "Native fill escaped selection");
            history("Undo");
            check(window.projectForTests().data("TOWNE.DAT") == QByteArray(16384, 0),
                  "Fill did not undo atomically");
            history("Redo");
            check(window.projectForTests().data("TOWNE.DAT") == filled &&
                      canvas()->selection == QRect(1, 1, 2, 2),
                  "Fill redo lost data or selection");
            active()->findChild<QPushButton *>("mapCopy")->click();
            active()->findChild<QPushButton *>("mapClearSelection")->click();
            active()->findChild<QPushButton *>("mapPaste")->click();
            check(canvas()->pasting(), "Toolbar did not start paste");
            click(6, 6);
            check(window.projectForTests().data("TOWNE.DAT").count(char(9)) == 8,
                  "Native paste failed");
            history("Undo");
            check(window.projectForTests().data("TOWNE.DAT") == filled,
                  "Paste did not undo as one operation");
            history("Undo");
            check(window.projectForTests().changed().isEmpty(),
                  "Terrain history did not return to original");
        });
        test("NPC document shared slots, signed floors and exact byte preservation", [&] {
            QTemporaryDir dir;
            auto project = fixture(dir.path());
            QByteArray bytes(4608, char(173));
            project.resources["TOWNE.NPC"] = {bytes, bytes};
            MapNpcDocument npc(&project, "TOWNE.NPC");
            int settlement = 3, actor = 7, base = settlement * 576 + actor * 16;
            auto moved = npc.move(settlement, actor, 3, {4, 5, -1, 0});
            for (int i = 0; i < bytes.size(); ++i)
                check(moved[i] == (i == base + 4    ? char(4)
                                   : i == base + 7  ? char(5)
                                   : i == base + 10 ? char(255)
                                                    : bytes[i]),
                      "NPC move touched unrelated bytes");
            project.resources["TOWNE.NPC"].edited = moved;
            auto first = npc.location(settlement, actor, 1),
                 third = npc.location(settlement, actor, 3);
            check(first.x == 4 && third.x == 4 && first.floor == -1 && third.floor == -1 &&
                      first.ai == 173 && npc.hour(settlement, actor, 3) == 173,
                  "Shared schedule or unusual values were normalized");
            rejects([&] { npc.move(settlement, actor, 0, {32, 5, 0, 0}); });
            rejects([&] { npc.move(settlement, actor, 0, {1, 5, -129, 0}); });
            rejects([&] { npc.location(8, actor, 0); });
            rejects([&] { npc.location(settlement, 32, 0); });
            rejects([&] { npc.location(settlement, actor, 4); });
        });
        test("NPC drag preview, overlapping selection and cancellation protect terrain", [&] {
            MapCanvas canvas;
            canvas.tool = MapCanvas::InspectNpc;
            canvas.side = 32;
            canvas.zoom = 1;
            canvas.ids = QByteArray(1024, 4);
            canvas.actors = {{2, 2, 256, 1}, {2, 2, 257, 2}};
            canvas.resizeMap();
            QList<int> choices;
            int moved = 0, selected = -1;
            QPoint destination;
            canvas.chooseActor = [&](const QList<int> &actors) {
                choices = actors;
                return 2;
            };
            canvas.chooseNpc = [&](int npc) { selected = npc; };
            canvas.moveNpc = [&](int npc, QPoint cell) {
                check(npc == 2, "Moved wrong overlapping actor");
                ++moved;
                destination = cell;
                return true;
            };
            auto mouse = [&](QEvent::Type type, int x, int y, Qt::MouseButtons buttons) {
                QPointF pos(x * 16 + 8, y * 16 + 8);
                QMouseEvent event(type, pos, pos,
                                  type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                                  buttons, Qt::NoModifier);
                QApplication::sendEvent(&canvas, &event);
            };
            mouse(QEvent::MouseButtonPress, 2, 2, Qt::LeftButton);
            check(!canvas.painting(), "Overlap chooser left a drag active after modal release");
            mouse(QEvent::MouseButtonPress, 2, 2, Qt::LeftButton);
            mouse(QEvent::MouseMove, 5, 6, Qt::LeftButton);
            check(selected == 2 && choices == QList<int>({1, 2}) && moved == 0 &&
                      canvas.actors[1].x == 2,
                  "NPC preview changed records or ignored overlap chooser");
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(&canvas, &escape);
            mouse(QEvent::MouseButtonRelease, 5, 6, Qt::NoButton);
            check(moved == 0 && !canvas.painting(), "Escape committed NPC movement");
            mouse(QEvent::MouseButtonPress, 2, 2, Qt::LeftButton);
            QFocusEvent focus(QEvent::FocusOut);
            QApplication::sendEvent(&canvas, &focus);
            mouse(QEvent::MouseButtonRelease, 5, 6, Qt::NoButton);
            check(moved == 0, "Focus loss committed NPC movement");
            mouse(QEvent::MouseButtonPress, 2, 2, Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease, -1, -1, Qt::NoButton);
            check(moved == 0, "Outside NPC destination accepted");
            mouse(QEvent::MouseButtonPress, 2, 2, Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease, 5, 6, Qt::NoButton);
            check(moved == 1 && destination == QPoint(5, 6) && canvas.ids == QByteArray(1024, 4),
                  "NPC move changed terrain or split its transaction");
            check(!canvas.beginPaste(), "NPC mode permitted terrain paste");
        });
        test("Native NPC schedules, floors, undo and conversation return", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            QByteArray terrain(16384, 4), npcs(4608, char(73));
            for (int settlement = 0; settlement < 8; ++settlement)
                for (int npc = 0; npc < 32; ++npc) {
                    int base = settlement * 576 + npc * 16;
                    for (int loc = 0; loc < 3; ++loc) {
                        npcs[base + 3 + loc] = char(255);
                        npcs[base + 6 + loc] = char(255);
                        npcs[base + 9 + loc] = 0;
                    }
                    npcs[settlement * 576 + 512 + npc] = 1;
                    npcs[settlement * 576 + 544 + npc] = char(255);
                }
            int base = 16;
            for (int loc = 0; loc < 3; ++loc) {
                npcs[base + 3 + loc] = char(2 + loc);
                npcs[base + 6 + loc] = char(2 + loc);
            }
            npcs[base + 10] = 1;
            npcs[544 + 1] = 37;
            // Yew's basement and shared record are exercised independently.
            int yew = 3 * 576 + 16;
            npcs[yew + 5] = 4;
            npcs[yew + 8] = 5;
            npcs[yew + 11] = char(255);
            file(dir.path() + "/TOWNE.DAT", terrain);
            file(dir.path() + "/TOWNE.NPC", npcs);
            file(dir.path() + "/TOWNE.TLK",
                 U5::writeDialogue(
                     {{37,
                       U5::encodeText("Chamfort the traveller with an unusually long but valid "
                                      "character name<Entry>Description<Entry>Greeting<Entry>Job<"
                                      "Entry>Bye<Entry><Any><Label 15>@")}}));
            WorkshopWindow window;
            check(window.openGame(dir.path()), "NPC fixture load failed");
            window.show();
            window.selectResource("TOWNE.DAT");
            app.processEvents();
            auto active = [&] {
                return window.findChild<QStackedWidget *>("workspaces")->currentWidget();
            };
            auto canvas = [&] {
                return dynamic_cast<MapCanvas *>(active()->findChild<QWidget *>("mapCanvas"));
            };
            active()->findChild<QButtonGroup *>("mapTools")->button(MapCanvas::InspectNpc)->click();
            active()->findChild<QComboBox *>("mapNpcList")->setCurrentIndex(1);
            check(active()->findChild<QLabel *>("mapNpcInfo")->text().contains("Chamfort"),
                  "NPC name not resolved through dialogue ID");
            app.processEvents();
            for (auto scroll : active()->findChildren<QScrollArea *>())
                if (scroll->findChild<QLabel *>("mapNpcInfo"))
                    check(scroll->horizontalScrollBar()->maximum() == 0,
                          "Long NPC names forced inspector horizontal overflow");
            active()->findChild<QComboBox *>("mapSchedule")->setCurrentIndex(1);
            app.processEvents();
            active()->findChild<QPushButton *>("mapNpcLocate")->click();
            app.processEvents();
            app.processEvents();
            check(active()->findChild<QComboBox *>("mapPage")->currentData().toInt() == 1 &&
                      active()->findChild<QComboBox *>("mapSchedule")->currentIndex() == 1 &&
                      canvas()->selectedNpc == 1,
                  "Locate reset actor or shared schedule slot");
            auto drag = [&](int x1, int y1, int x2, int y2) {
                auto send = [&](QEvent::Type type, int x, int y, Qt::MouseButtons buttons) {
                    QPointF pos((x + 0.5) * 16 * canvas()->zoom, (y + 0.5) * 16 * canvas()->zoom);
                    QMouseEvent event(type, pos, pos,
                                      type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                                      buttons, Qt::NoModifier);
                    QApplication::sendEvent(canvas(), &event);
                };
                send(QEvent::MouseButtonPress, x1, y1, Qt::LeftButton);
                send(QEvent::MouseMove, x2, y2, Qt::LeftButton);
                send(QEvent::MouseButtonRelease, x2, y2, Qt::NoButton);
            };
            drag(3, 3, 6, 7);
            auto moved = window.projectForTests().data("TOWNE.NPC");
            for (int i = 0; i < npcs.size(); ++i)
                check(moved[i] == (i == base + 4   ? char(6)
                                   : i == base + 7 ? char(7)
                                                   : npcs[i]),
                      "Native drag damaged schedule fields");
            auto history = [&](QString prefix) {
                bool found = false;
                for (auto action : window.findChildren<QAction *>())
                    if (action->isEnabled() && action->text().startsWith(prefix + " ")) {
                        action->trigger();
                        found = true;
                        break;
                    }
                check(found, "NPC move lacked history");
                app.processEvents();
                app.processEvents();
            };
            history("Undo");
            check(window.projectForTests().data("TOWNE.NPC") == npcs &&
                      canvas()->selectedNpc == 1 && canvas()->tool == MapCanvas::InspectNpc,
                  "Undo lost NPC data or selection");
            history("Redo");
            active()->findChild<QComboBox *>("mapSchedule")->setCurrentIndex(3);
            app.processEvents();
            check(active()->findChild<QSpinBox *>("mapNpcX")->value() == 6 &&
                      active()->findChild<QLabel *>("mapNpcInfo")->text().contains("share"),
                  "Slot 3 failed shared location display");
            check(active()->findChild<QComboBox *>("mapSchedule")->currentText() == "4" &&
                      !active()->findChild<QLabel *>("mapNpcInfo")->text().contains("AI byte"),
                  "Mixed NPC timings should show compact numbered schedule changes");
            active()->findChild<QComboBox *>("mapPage")->setCurrentIndex(0);
            app.processEvents();
            check(canvas()->selectedNpc == 1 && canvas()->tool == MapCanvas::InspectNpc &&
                      active()->findChild<QComboBox *>("mapSchedule")->currentIndex() == 3,
                  "Manual floor navigation lost actor or slot");
            active()->findChild<QPushButton *>("mapNpcLocate")->click();
            app.processEvents();
            app.processEvents();
            active()->findChild<QCheckBox *>("mapNpcGhosts")->setChecked(true);
            active()->findChild<QPushButton *>("mapNpcConversation")->click();
            app.processEvents();
            check(active()->findChild<QWidget *>("conversationEditor"), "Conversation link failed");
            active()->findChild<QPushButton *>("backToMap")->click();
            app.processEvents();
            app.processEvents();
            check(canvas()->selectedNpc == 1 && canvas()->tool == MapCanvas::InspectNpc &&
                      active()->findChild<QComboBox *>("mapSchedule")->currentIndex() == 3 &&
                      active()->findChild<QCheckBox *>("mapNpcGhosts")->isChecked(),
                  "Dialogue return lost NPC state");
            auto tree = active()->findChild<QTreeWidget *>("mapNavigation");
            QTreeWidgetItemIterator location(tree);
            while (*location && ((*location)->childCount() == 0 || (*location)->text(0) != "Yew"))
                ++location;
            check(bool(*location), "Yew location heading missing");
            tree->setCurrentItem(*location);
            app.processEvents();
            app.processEvents();
            active()->findChild<QComboBox *>("mapNpcList")->setCurrentIndex(1);
            active()->findChild<QComboBox *>("mapSchedule")->setCurrentIndex(2);
            app.processEvents();
            active()->findChild<QPushButton *>("mapNpcLocate")->click();
            app.processEvents();
            check(active()->findChild<QSpinBox *>("mapNpcFloor")->value() == -1,
                  "Basement floor was unsigned");
            active()->findChild<QSpinBox *>("mapNpcX")->setValue(8);
            active()->findChild<QSpinBox *>("mapNpcFloor")->setValue(0);
            active()->findChild<QPushButton *>("mapNpcMove")->click();
            app.processEvents();
            app.processEvents();
            check(active()->findChild<QComboBox *>("mapPage")->currentData().toInt() == 7 &&
                      active()->findChild<QComboBox *>("mapSchedule")->currentIndex() == 2 &&
                      U5::byte(window.projectForTests().data("TOWNE.NPC"), yew + 11) == 0,
                  "Cross-floor move failed");
            check(window.projectForTests().data("TOWNE.DAT") == terrain, "NPC UI changed terrain");
            auto finalNpc = window.projectForTests().data("TOWNE.NPC");
            for (int i = 0; i < npcs.size(); ++i)
                check(finalNpc[i] == (i == base + 4   ? char(6)
                                      : i == base + 7 ? char(7)
                                      : i == yew + 5  ? char(8)
                                      : i == yew + 11 ? char(0)
                                                      : npcs[i]),
                      "Cross-floor movement changed unrelated record bytes");
            active()->findChild<QComboBox *>("mapNpcList")->setCurrentIndex(2);
            check(!active()->findChild<QPushButton *>("mapNpcConversation")->isEnabled() &&
                      active()->findChild<QSpinBox *>("mapNpcX")->value() == 255,
                  "Missing conversation or unusual position was rewritten");
            auto scheduleBefore = window.projectForTests().data("TOWNE.NPC");
            auto hour = active()->findChild<QSpinBox *>("mapNpcStartHour");
            check(hour && hour->isEnabled(), "Start hour control missing");
            hour->setValue(13);
            QMetaObject::invokeMethod(hour, "editingFinished", Qt::DirectConnection);
            auto expectedSchedule = scheduleBefore;
            expectedSchedule[3 * 576 + 2 * 16 + 14] = 13;
            check(window.projectForTests().data("TOWNE.NPC") == expectedSchedule,
                  "Start hour edit changed another schedule or destination");
            hour->setValue(25);
            QMetaObject::invokeMethod(hour, "editingFinished", Qt::DirectConnection);
            check(window.projectForTests().data("TOWNE.NPC") == expectedSchedule &&
                      hour->value() == 13,
                  "Invalid new start hour was accepted");
            history("Undo");
            check(window.projectForTests().data("TOWNE.NPC") == scheduleBefore,
                  "Start hour undo did not restore exact schedule bytes");
        });
        test("Native widgets are read-only until an edit", [&] {
            QTemporaryDir dir;
            auto p = fixture(dir.path());
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Window game load");
            for (auto name : p.resources.keys()) {
                w.selectResource(name);
                app.processEvents();
            }
            check(w.projectForTests().changed().isEmpty(), "GUI load mutated resources");
            w.selectResource("INIT.GAM");
            auto table = w.findChild<QStackedWidget *>("workspaces")
                             ->currentWidget()
                             ->findChild<QTableWidget *>("byteInspector");
            check(table, "Native byte inspector missing");
            table->item(0, 0)->setText("01");
            check(w.projectForTests().data("INIT.GAM")[0] == 1, "Byte edit did not persist");
            app.processEvents();
        });
        test("Native painting, undo and unapplied dialogue safety", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            file(dir.path() + "/TOWNE.DAT", QByteArray(16384, 0));
            file(
                dir.path() + "/TOWNE.TLK",
                U5::writeDialogue({{1, U5::encodeText("Name<Entry>Description<Entry>Greeting<Entry>"
                                                      "Job<Entry>Bye<Entry><Any><Label 15>@")}}));
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Fixture load");
            w.show();
            app.processEvents();
            auto active = [&] {
                return w.findChild<QStackedWidget *>("workspaces")->currentWidget();
            };
            auto click = [&](QWidget *widget, QPointF position) {
                QMouseEvent down(QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton,
                                 Qt::NoModifier);
                QMouseEvent up(QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton,
                               Qt::NoModifier);
                QApplication::sendEvent(widget, &down);
                QApplication::sendEvent(widget, &up);
                app.processEvents();
            };
            w.selectResource("TOWNE.DAT");
            auto canvas = active()->findChild<QWidget *>("mapCanvas");
            check(canvas, "Map canvas absent");
            click(canvas, {40, 40});
            check(U5::byte(w.projectForTests().data("TOWNE.DAT"), 33) == 1, "Paint stroke lost");
            auto undo = [&] {
                for (auto action : w.findChildren<QAction *>())
                    if (action->text().startsWith("Undo ")) {
                        action->trigger();
                        app.processEvents();
                        return;
                    }
                throw std::runtime_error("Undo action absent");
            };
            undo();
            check(w.projectForTests().data("TOWNE.DAT") == QByteArray(16384, 0),
                  "Map undo lost original bytes");
            w.selectResource("TILES.16");
            canvas = active()->findChild<QWidget *>("pixelCanvas");
            check(canvas, "Pixel canvas absent");
            click(canvas, {3, 3});
            auto graphics = U5::readGraphics("TILES.16", w.projectForTests().data("TILES.16"));
            check(graphics.images[0].pixel(0, 0) == qRgb(255, 255, 255),
                  "Native pixel brush failed");
            undo();
            check(w.projectForTests().data("TILES.16") == QByteArray(65536, 0),
                  "Pixel undo changed original encoding");
            w.selectResource("TOWNE.TLK");
            auto scope = active()->findChild<QComboBox *>("conversationSourceScope");
            scope->setCurrentIndex(1);
            auto text = active()->findChild<QPlainTextEdit *>("dialogueText");
            check(text, "Dialogue editor absent");
            text->setPlainText("<Gold><Byte 177>");
            auto answer = [&](QMessageBox::StandardButton which) {
                QTimer::singleShot(0, [which] {
                    for (auto widget : QApplication::topLevelWidgets())
                        if (auto box = qobject_cast<QMessageBox *>(widget))
                            box->button(which)->click();
                });
            };
            answer(QMessageBox::Cancel);
            w.selectResource("TILES.16");
            check(active()->findChild<QPlainTextEdit *>("dialogueText"), "Cancel discarded draft");
            check(!w.projectForTests().changed().contains("TOWNE.TLK"),
                  "Draft applied without consent");
            text->setPlainText("Hello<Entry>Description<Entry>Greeting<Entry>Job<"
                               "Entry>Bye<Entry><Any><Label 15>@");
            w.selectResource("TILES.16");
            check(w.projectForTests().changed().contains("TOWNE.TLK"),
                  "Validated automatic apply on navigation failed");
            auto package = w.projectForTests().package();
            Project restored;
            restored.openGame(dir.path());
            restored.importPackage(package);
            check(restored.data("TOWNE.TLK") == w.projectForTests().data("TOWNE.TLK"),
                  "Native edited package roundtrip");
        });
        test("Structured conversation widgets, aliases, undo and annotations", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            auto bytes =
                U5::encodeText("Name<Entry>Description<Entry>Greeting<Entry>Job<Entry>Bye<Entry>"
                               "help<Entry><Or><Entry>hint<Entry>Hello<Label 1><Entry><Any><Label "
                               "1>Question?<Entry>No<Entry>y<Entry>Yes<End "
                               "Conversation><Entry><Any><Label 15>@");
            auto original = U5::writeDialogue({{1, bytes}});
            file(dir.path() + "/TOWNE.TLK", original);
            WorkshopWindow w;
            check(w.openGame(dir.path()), "Cannot open structured fixture");
            w.show();
            w.selectResource("TOWNE.TLK");
            app.processEvents();
            auto active = [&] {
                return w.findChild<QStackedWidget *>("workspaces")->currentWidget();
            };
            auto editor = dynamic_cast<ConversationEditor *>(
                active()->findChild<QWidget *>("conversationEditor"));
            check(editor, "Structured conversation editor absent");
            auto block = editor->findChild<QPlainTextEdit *>("conversationBlockText");
            check(block && block->toPlainText() == "Name", "Basic text not readable");
            block->setPlainText("New Name");
            check(editor->flush(), "Valid basic text did not apply");
            auto next =
                Dialogue::parse(U5::readDialogue(w.projectForTests().data("TOWNE.TLK"))[0].bytes);
            check(next.entries[1].bytes == Dialogue::parse(bytes).entries[1].bytes,
                  "Basic edit changed unrelated entry");
            for (auto action : w.findChildren<QAction *>())
                if (action->text().startsWith("Undo ")) {
                    action->trigger();
                    break;
                }
            app.processEvents();
            check(w.projectForTests().data("TOWNE.TLK") == original,
                  "Structured undo did not restore exact bytes");
            editor = dynamic_cast<ConversationEditor *>(
                active()->findChild<QWidget *>("conversationEditor"));
            auto tree = editor->findChild<QTreeWidget *>("conversationOutline");
            QTreeWidgetItemIterator it(tree);
            while (*it) {
                if ((*it)->text(0) == "help / hint") {
                    tree->setCurrentItem(*it);
                    break;
                }
                ++it;
            }
            auto aliases = editor->findChild<QLineEdit *>("conversationAliases");
            check(aliases && aliases->text() == "help, hint", "Shared aliases absent");
            aliases->setText("help, clue");
            QMetaObject::invokeMethod(aliases, "editingFinished", Qt::DirectConnection);
            next =
                Dialogue::parse(U5::readDialogue(w.projectForTests().data("TOWNE.TLK"))[0].bytes);
            check(Dialogue::keyword(next.entries[next.topics[0].keywords[1]].bytes) == "clue",
                  "Alias edit failed");
            check(
                next.entries[next.topics[0].response].bytes ==
                    Dialogue::parse(bytes).entries[Dialogue::parse(bytes).topics[0].response].bytes,
                "Alias edit changed shared response");
            auto before = w.projectForTests().data("TOWNE.TLK");
            aliases->setText("help, ");
            QMetaObject::invokeMethod(aliases, "editingFinished", Qt::DirectConnection);
            check(w.projectForTests().data("TOWNE.TLK") == before,
                  "Invalid alias draft changed the project");
            aliases->setText("help, clue");
            QMetaObject::invokeMethod(aliases, "editingFinished", Qt::DirectConnection);
            check(editor->flush(), "Corrected alias draft did not apply");
            for (auto b : editor->findChildren<QPushButton *>())
                if (b->text() == "Reset") {
                    b->click();
                    break;
                }
            auto input = editor->findChild<QLineEdit *>("conversationInput");
            input->setText("help");
            QMetaObject::invokeMethod(input, "returnPressed", Qt::DirectConnection);
            auto transcript = editor->findChild<QTreeWidget *>("conversationTranscript");
            check(transcript->topLevelItemCount() > 1, "Test conversation transcript absent");
            check(w.projectForTests().data("TOWNE.TLK") == before, "Simulation changed project");
            w.projectForTests().dialogueNames["TOWNE.TLK/1/1"] = "Password question";
            QString path = dir.path() + "/test.imperaproject";
            w.projectForTests().save(path);
            Project loaded;
            loaded.load(path);
            check(loaded.dialogueNames.value("TOWNE.TLK/1/1") == "Password question",
                  "Question names not saved");
            check(loaded.data("TOWNE.TLK") == before, "Annotation changed game data");
            check(!Dialogue::inventoryOptions().value(65).isEmpty() &&
                      Dialogue::inventoryOptions().value(16) == "Dagger",
                  "Engine inventory labels incorrect");
        });
        test("New Mod reuses the selected game folder without a picker", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            WorkshopWindow window;
            check(window.openGame(dir.path()), "New Mod fixture open");
            for (auto action : window.findChildren<QAction *>())
                if (action->text() == "New Mod") {
                    action->trigger();
                    break;
                }
            check(window.projectForTests().sourceDirectory == QDir(dir.path()).absolutePath() &&
                      window.projectForTests().changed().isEmpty(),
                  "New Mod did not reuse the remembered source");
        });
        test("Mod validation, decoder verification and installed-package conflicts", [&] {
            QTemporaryDir dir;
            auto project = fixture(dir.path());
            project.resources["INIT.GAM"].edited[0] = 1;
            auto report = validateMod(project);
            check(std::none_of(report.begin(), report.end(),
                               [](const auto &d) { return d.severity == ModDiagnostic::Error; }),
                  "Valid mod failed validation");
            QDir(dir.path()).mkdir("Mods");
            file(dir.path() + "/Mods/A.imperamod", project.package());
            file(dir.path() + "/Mods/B.imperamod", project.package());
            file(dir.path() + "/Mods/broken.imperamod", "broken");
            report = validateMod(project);
            check(std::any_of(report.begin(), report.end(),
                              [](const auto &d) {
                                  return d.resource == "INIT.GAM" &&
                                         d.message.contains("also replaces");
                              }),
                  "Installed resource conflict not reported");
            check(std::any_of(report.begin(), report.end(),
                              [](const auto &d) {
                                  return d.message.contains("earlier installed package");
                              }),
                  "Installed package rejection order not reported");
            check(std::any_of(report.begin(), report.end(),
                              [](const auto &d) { return d.message.contains("cannot load"); }),
                  "Broken installed package not reported");
            project.resources["INIT.GAM"].edited.resize(3);
            report = validateMod(project);
            check(std::any_of(report.begin(), report.end(),
                              [](const auto &d) {
                                  return d.resource == "INIT.GAM" &&
                                         d.severity == ModDiagnostic::Error;
                              }),
                  "Validation error does not link to resource");
        });
        test("Isolated test sessions preserve originals and exclude personal data", [&] {
            QTemporaryDir source, cache;
            auto project = fixture(source.path());
            project.resources["INIT.GAM"].edited[0] = 42;
            QDir(source.path()).mkdir("Mods");
            QDir(source.path()).mkdir("SAVEGAME");
            file(source.path() + "/Mods/other.imperamod", "other");
            file(source.path() + "/SAVEGAME/slot", "personal");
            file(source.path() + "/DATA.CFG", "personal-config");
            file(source.path() + "/SAVED.GAM", "personal-save");
            auto before = project.resources["INIT.GAM"].original;
            QString first = prepareModTest(project, cache.path());
            QString second = prepareModTest(project, cache.path());
            check(first != second && QDir(first).exists(), "Test sessions are not separate");
            auto read = [](const QString &path) {
                QFile file(path);
                check(file.open(QIODevice::ReadOnly), "Cannot read test output");
                return file.readAll();
            };
            check(read(source.path() + "/init.gam") == before &&
                      read(first + "/INIT.GAM") == before,
                  "Test creation changed source or applied edits to copied base");
            Project test;
            test.openGame(first);
            test.importPackage(read(first + "/Mods/workshop.imperamod"));
            check(test.data("INIT.GAM") == project.data("INIT.GAM"),
                  "Isolated package does not reproduce editor data");
            check(!QFile::exists(first + "/SAVED.GAM") && !QDir(first + "/SAVEGAME").exists() &&
                      QDir(first + "/Mods").entryList(QDir::Files).size() == 1 &&
                      read(first + "/DATA.CFG") == (first + "\n\n").toUtf8(),
                  "Test session copied personal saves, settings or installed mods");
            file(source.path() + "/init.gam", QByteArray(4192, 7));
            rejects([&] { prepareModTest(project, cache.path()); });
            check(QDir(cache.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size() == 2,
                  "Failed test preparation left a partial runtime");
        });
        test("Compact map-wide schedule hours and floor selector alignment", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            file(dir.path() + "/TOWNE.DAT", QByteArray(16384, 5));
            QByteArray npcs(4608, 0);
            for (int actor = 0; actor < 32; ++actor)
                for (int destination = 0; destination < 3; ++destination) {
                    npcs[actor * 16 + 3 + destination] = char(255);
                    npcs[actor * 16 + 6 + destination] = char(255);
                }
            for (int destination = 0; destination < 3; ++destination) {
                npcs[3 + destination] = 2;
                npcs[6 + destination] = 2;
            }
            for (int change = 0; change < 4; ++change)
                npcs[12 + change] = char(8 + change * 4);
            file(dir.path() + "/TOWNE.NPC", npcs);
            auto project = fixture(dir.path());
            project.openGame(dir.path());
            QMap<QString, MapViewState> states;
            QMap<QString, int> navigation;
            MapWorkspace workspace(
                &project, "TOWNE.DAT", &states, &navigation,
                [](const auto &, const auto &) { return true; }, [](const auto &) {});
            workspace.resize(1024, 720);
            workspace.show();
            app.processEvents();
            auto schedule = workspace.findChild<QComboBox *>("mapSchedule");
            auto tools = workspace.findChild<QButtonGroup *>("mapTools");
            check(schedule->itemText(0) == "08:00" && schedule->itemText(3) == "20:00",
                  "Consistent map-wide hours are not shown");
            auto floors = workspace.findChild<QComboBox *>("mapPage");
            check(qAbs(schedule->mapTo(&workspace, QPoint()).y() -
                       floors->mapTo(&workspace, QPoint()).y()) < 10,
                  "Schedule selector is not beside the floor selector");
            check(!workspace.findChild<QCheckBox *>("mapGrid") &&
                      !workspace.findChild<QCheckBox *>("mapRectangleOutline"),
                  "Removed map controls are still present");
            for (const char *name : {"mapCopy", "mapPaste", "mapClearSelection", "mapComparison"}) {
                auto control = workspace.findChild<QWidget *>(name);
                check(control &&
                          qAbs(control->mapTo(&workspace, QPoint()).y() -
                               tools->button(MapCanvas::Pencil)->mapTo(&workspace, QPoint()).y()) <
                              10,
                      "Terrain action is not on the tool row");
            }
            check(floors->count() == 2 && floors->findData(0) >= 0 && floors->findData(1) >= 0 &&
                      floors->findData(6) < 0,
                  "Floor selector includes other locations");
            auto tree = workspace.findChild<QTreeWidget *>("mapNavigation");
            QTreeWidgetItemIterator location(tree);
            while (*location && ((*location)->childCount() == 0 || (*location)->text(0) != "Yew"))
                ++location;
            check(bool(*location), "Yew location heading missing");
            tree->setCurrentItem(*location);
            app.processEvents();
            check(floors->currentData().toInt() == 7 && floors->count() == 2 &&
                      floors->findData(0) < 0,
                  "Location heading did not open its main floor");
            QTreeWidgetItemIterator moonglow(tree);
            while (*moonglow &&
                   ((*moonglow)->childCount() == 0 || (*moonglow)->text(0) != "Moonglow"))
                ++moonglow;
            tree->setCurrentItem(*moonglow);
            app.processEvents();
            npcs[16 + 3] = 3;
            npcs[16 + 6] = 3;
            npcs[16 + 12] = 9;
            project.resources["TOWNE.NPC"].edited = npcs;
            MapWorkspace mixed(
                &project, "TOWNE.DAT", &states, &navigation,
                [](const auto &, const auto &) { return true; }, [](const auto &) {});
            auto mixedSchedule = mixed.findChild<QComboBox *>("mapSchedule");
            for (int change = 0; change < 4; ++change)
                check(mixedSchedule->itemText(change) == QString::number(change + 1),
                      "Different NPC hours should produce numbered schedule labels");
            mixed.findChild<QComboBox *>("mapNpcList")->setCurrentIndex(0);
            mixed.findChild<QComboBox *>("mapNpcList")->setCurrentIndex(1);
            check(mixedSchedule->itemText(0) == "1",
                  "Selected NPC must not redefine map-wide schedule labels");
        });
        test("Verified tile descriptions, category search and document-only exports", [&] {
            check(MapDocument::tileName(5) == "Grass" && MapDocument::tileCategory(5) == "Ground",
                  "Verified grass label/category missing");
            check(MapDocument::tileName(0x4f) == "Wall" &&
                      MapDocument::tileCategory(0x4f) == "Buildings",
                  "Verified wall category missing");
            check(MapDocument::tileName(6) == "Grass variation" &&
                      MapDocument::tileCategory(6) == "Ground",
                  "Grass variation missing from catalog");
            for (int id = 0; id < 256; ++id)
                check(QStringList{"Overworld", "Ground", "Buildings", "Objects", "Other"}.contains(
                          MapDocument::tileCategory(id)) &&
                          MapDocument::tileName(id) != "Unidentified tile",
                      "Tile catalog is incomplete");
            QTemporaryDir dir;
            auto project = fixture(dir.path());
            QByteArray bytes(352, char(0xee));
            for (int y = 0; y < 11; ++y)
                for (int x = 0; x < 11; ++x)
                    bytes[y * 32 + x] = char((y * 11 + x) % 256);
            file(dir.path() + "/BRIT.CBT", bytes);
            QByteArray worldChunks(512, char(0xfa));
            worldChunks.replace(0, 256, QByteArray(256, 5));
            worldChunks[0] = char(0x4f);
            worldChunks[1] = char(0xd4);
            QByteArray overlay(0x3986, char(255));
            overlay[0x3886] = 0;
            file(dir.path() + "/BRIT.DAT", worldChunks);
            file(dir.path() + "/DATA.OVL", overlay);
            project.openGame(dir.path());
            MapDocument document(&project, "BRIT.CBT");
            QVector<QImage> tiles;
            for (int id = 0; id < 256; ++id) {
                QImage tile(16, 16, QImage::Format_RGB32);
                tile.fill(QColor(id, 0, 0));
                tiles.append(tile);
            }
            auto image = document.terrainImage(0, tiles, false);
            auto ids = document.terrainImage(0, tiles, true);
            check(image.size() == QSize(176, 176) && ids.size() == QSize(11, 11),
                  "Export dimensions wrong");
            for (int y = 0; y < 11; ++y)
                for (int x = 0; x < 11; ++x)
                    check(ids.constScanLine(y)[x] == y * 11 + x &&
                              image.pixelColor(x * 16, y * 16).red() == y * 11 + x,
                          "Exports do not reflect stored terrain");
            WorkshopWindow window;
            check(window.openGame(dir.path()), "Palette fixture open");
            window.selectResource("BRIT.CBT");
            window.resize(1024, 720);
            window.show();
            app.processEvents();
            app.processEvents();
            auto palette = window.findChild<QListWidget *>("mapPalette");
            auto search = window.findChild<QLineEdit *>("mapTileSearch");
            auto category = window.findChild<QComboBox *>("mapPaletteFilter");
            check(palette && search && category, "Tile browser absent");
            for (int id = 0; id < 256; ++id)
                check(palette->item(id)->text().isEmpty() &&
                          !palette->item(id)->toolTip().isEmpty(),
                      "Palette tiles must be icon-only with descriptive tooltips");
            auto tools = window.findChild<QButtonGroup *>("mapTools");
            check(tools && tools->buttons().size() == 7 &&
                      !window.findChild<QComboBox *>("mapTool"),
                  "Icon toolbar missing");
            for (auto button : tools->buttons())
                check(!button->toolTip().isEmpty() && !button->accessibleName().isEmpty(),
                      "Tool is missing help or an accessible name");
            search->setText("grass");
            check(!palette->item(5)->isHidden() && palette->item(0x4f)->isHidden(),
                  "Name search failed");
            search->clear();
            category->setCurrentText("Buildings");
            check(palette->item(5)->isHidden() && !palette->item(0x50)->isHidden() &&
                      !palette->item(0x4f)->isHidden(),
                  "Buildings should include structural tiles also used on world maps");
            check(category->count() == 8, "Palette should offer exactly five tile categories");
            category->setCurrentText("Overworld");
            check(!palette->item(5)->isHidden() && !palette->item(0x4f)->isHidden() &&
                      !palette->item(0xd7)->isHidden() && palette->item(0xfa)->isHidden(),
                  "Overworld filter missed used tiles or counted unused chunk storage");
            check(palette->item(5)->toolTip().contains("Overworld · Ground"),
                  "World tile tooltip should describe both categories");
            category->setCurrentText("Ground");
            check(!palette->item(5)->isHidden() && !palette->item(0x27)->isHidden() &&
                      palette->item(0x4f)->isHidden(),
                  "Ground should include terrain and floors, including world tiles");
            category->setCurrentText("Objects");
            check(!palette->item(0xfa)->isHidden() && palette->item(0x4f)->isHidden(),
                  "Objects should contain fixtures, not walls");
            category->setCurrentText("Other");
            check(!palette->item(0x70)->isHidden() && palette->item(5)->isHidden(),
                  "Other should contain rendering masks, not terrain");
            int memberships[256]{};
            for (const QString &name :
                 {QString("Ground"), QString("Buildings"), QString("Objects"), QString("Other")}) {
                category->setCurrentText(name);
                for (int id = 0; id < 256; ++id)
                    memberships[id] += !palette->item(id)->isHidden();
            }
            for (int membership : memberships)
                check(membership == 1, "Each tile must have exactly one base category in addition "
                                       "to optional Overworld membership");

            check(!search->accessibleName().isEmpty() && !category->accessibleName().isEmpty(),
                  "Accessible control labels absent");
        });
        test("Keyboard canvas editing, selection, paste and character movement", [&] {
            MapCanvas canvas;
            canvas.side = 4;
            canvas.ids = QByteArray(16, 1);
            canvas.original = canvas.ids;
            canvas.brush = 5;
            canvas.resizeMap();
            canvas.show();
            canvas.setFocus();
            app.processEvents();
            int commits = 0;
            canvas.commit = [&](const QByteArray &) {
                ++commits;
                return true;
            };
            auto key = [&](int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
                QKeyEvent event(QEvent::KeyPress, code, modifiers);
                QApplication::sendEvent(&canvas, &event);
            };
            key(Qt::Key_Right);
            key(Qt::Key_Down);
            key(Qt::Key_Return);
            check(commits == 1 && U5::byte(canvas.ids, 5) == 5 && U5::byte(canvas.ids, 0) == 1,
                  "Keyboard pencil failed or painted wrong tile");
            key(Qt::Key_Right, Qt::ShiftModifier);
            check(canvas.selection == QRect(1, 1, 2, 1), "Keyboard selection not extended");
            key(Qt::Key_C, Qt::ControlModifier);
            key(Qt::Key_Down);
            key(Qt::Key_V, Qt::ControlModifier);
            check(canvas.pasting(), "Keyboard paste preview missing");
            key(Qt::Key_Return);
            check(commits == 2 && !canvas.pasting(), "Enter did not commit paste once");
            canvas.tool = MapCanvas::InspectNpc;
            canvas.selectedNpc = 7;
            QPoint destination;
            int selected = -1;
            canvas.moveNpc = [&](int actor, QPoint point) {
                selected = actor;
                destination = point;
                return true;
            };
            auto before = canvas.ids;
            key(Qt::Key_Left);
            key(Qt::Key_Return);
            check(selected == 7 && destination == QPoint(1, 2) && canvas.ids == before,
                  "Keyboard actor movement changed terrain or wrong position");
        });
        test("Persistent game directory restoration and stale folder fallback", [&] {
            QTemporaryDir dir;
            fixture(dir.path());
            WorkshopWindow first;
            check(first.openGame(dir.path()), "Remembered folder open");
            WorkshopWindow second;
            check(second.restoreGameDirectory(), "Game folder did not restore in new window");
            check(second.projectForTests().sourceDirectory ==
                      first.projectForTests().sourceDirectory,
                  "Restored wrong game folder");
            QSettings().setValue("paths/game", dir.path() + "/missing");
            WorkshopWindow missing;
            check(!missing.restoreGameDirectory() && missing.projectForTests().resources.isEmpty(),
                  "Stale game folder did not return to welcome screen");
        });
        test("Combat encounter coordinates and read-only trigger preview", [&] {
            QTemporaryDir dir;
            auto project = fixture(dir.path());
            QByteArray bytes(352, char(0xee));
            for (int row = 0; row < 11; ++row)
                for (int x = 0; x < 11; ++x)
                    bytes[row * 32 + x] = 1;
            const int rows[] = {4, 1, 3, 2};
            for (int row : rows) {
                bytes[row * 32 + 11] = 2;
                bytes[row * 32 + 17] = 3;
            }
            bytes[6 * 32 + 11] = 2;
            bytes[7 * 32 + 11] = 3;
            bytes[8 * 32 + 11] = 2;
            bytes[8 * 32 + 19] = 3;
            bytes[11] = 42;
            bytes[9 * 32 + 11] = 4;
            bytes[9 * 32 + 19] = 5;
            bytes[10 * 32 + 11] = 6;
            bytes[10 * 32 + 19] = 7;
            file(dir.path() + "/BRIT.CBT", bytes);
            WorkshopWindow window;
            check(window.openGame(dir.path()), "Combat fixture open");
            check(QSettings().value("paths/game").toString() ==
                      window.projectForTests().sourceDirectory,
                  "Selected game directory was not persisted");
            window.selectResource("BRIT.CBT");
            window.show();
            app.processEvents();
            app.processEvents();
            // QWidget subclasses without Q_OBJECT use dynamic_cast rather than Qt casts.
            MapCanvas *canvas = nullptr;
            for (auto widget : window.findChildren<QWidget *>())
                if (auto candidate = dynamic_cast<MapCanvas *>(widget))
                    canvas = candidate;
            check(canvas != nullptr, "Combat canvas missing");
            window.findChild<QButtonGroup *>("mapTools")->button(MapCanvas::InspectNpc)->click();
            auto entry = window.findChild<QComboBox *>("combatEntry");
            auto entity = window.findChild<QComboBox *>("combatEntity");
            check(entry && entity && entity->count() == 30, "Encounter record list missing");
            auto mouse = [&](QEvent::Type type, QPoint cell, Qt::MouseButtons held) {
                QPointF point((cell.x() + 0.5) * 16 * canvas->zoom,
                              (cell.y() + 0.5) * 16 * canvas->zoom);
                QMouseEvent event(type, point, point, Qt::LeftButton, held, Qt::NoModifier);
                QApplication::sendEvent(canvas, &event);
            };
            for (int direction = 0; direction < 4; ++direction) {
                entry->setCurrentIndex(direction);
                entity->setCurrentIndex(0);
                auto before = window.projectForTests().data("BRIT.CBT");
                mouse(QEvent::MouseButtonPress, {2, 3}, Qt::LeftButton);
                mouse(QEvent::MouseMove, {8, 9}, Qt::LeftButton);
                check(window.projectForTests().data("BRIT.CBT") == before, "Drag committed early");
                QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                QApplication::sendEvent(canvas, &escape);
                mouse(QEvent::MouseButtonRelease, {8, 9}, Qt::NoButton);
                check(window.projectForTests().data("BRIT.CBT") == before,
                      "Cancelled drag mutated data");
                mouse(QEvent::MouseButtonPress, {2, 3}, Qt::LeftButton);
                mouse(QEvent::MouseMove, {8, 9}, Qt::LeftButton);
                mouse(QEvent::MouseButtonRelease, {8, 9}, Qt::NoButton);
                before[rows[direction] * 32 + 11] = 8;
                before[rows[direction] * 32 + 17] = 9;
                check(window.projectForTests().data("BRIT.CBT") == before,
                      "Party coordinate edit changed unrelated bytes");
            }
            for (int id : {6, 22}) {
                entity->setCurrentIndex(id);
                auto expected = window.projectForTests().data("BRIT.CBT");
                mouse(QEvent::MouseButtonPress, {2, 3}, Qt::LeftButton);
                mouse(QEvent::MouseMove, {8, 9}, Qt::LeftButton);
                mouse(QEvent::MouseButtonRelease, {8, 9}, Qt::NoButton);
                int xo = id == 6 ? 6 * 32 + 11 : 8 * 32 + 11;
                int yo = id == 6 ? 7 * 32 + 11 : 8 * 32 + 19;
                expected[xo] = 8;
                expected[yo] = 9;
                check(window.projectForTests().data("BRIT.CBT") == expected,
                      "Encounter edit changed unrelated bytes");
            }
            auto beforePreview = window.projectForTests().data("BRIT.CBT");
            window.findChild<QCheckBox *>("combatPreview")->setChecked(true);
            check(U5::byte(canvas->ids, 5 * 11 + 4) == 42 &&
                      U5::byte(canvas->ids, 7 * 11 + 6) == 42,
                  "Trigger preview did not replace both linked cells");
            check(window.projectForTests().data("BRIT.CBT") == beforePreview,
                  "Trigger preview mutated stored data");
            MapDocument exportDocument(&window.projectForTests(), "BRIT.CBT");
            check(exportDocument.terrainImage(0, canvas->tiles, true).constScanLine(5)[4] == 1,
                  "Terrain export leaked displayed trigger preview");
            window.findChild<QButtonGroup *>("mapTools")->button(MapCanvas::Pencil)->click();
            check(U5::byte(canvas->ids, 5 * 11 + 4) == 1,
                  "Trigger preview leaked into terrain tools");
            window.findChild<QButtonGroup *>("mapTools")->button(MapCanvas::InspectNpc)->click();
            for (auto action : window.findChildren<QAction *>())
                if (action->text().startsWith("Undo ")) {
                    action->trigger();
                    break;
                }
            app.processEvents();
            app.processEvents();
            auto undone = beforePreview;
            undone[8 * 32 + 11] = 2;
            undone[8 * 32 + 19] = 3;
            check(window.projectForTests().data("BRIT.CBT") == undone,
                  "Encounter undo changed unrelated bytes");
            check(window.findChild<QButtonGroup *>("mapTools")->checkedId() ==
                          MapCanvas::InspectNpc &&
                      window.findChild<QComboBox *>("combatEntity")->currentIndex() == 22 &&
                      window.findChild<QComboBox *>("combatEntry")->currentIndex() == 3,
                  "Encounter undo lost selected record, mode or entry direction");
            for (auto action : window.findChildren<QAction *>())
                if (action->text().startsWith("Redo ")) {
                    action->trigger();
                    break;
                }
            app.processEvents();
            app.processEvents();
            check(window.projectForTests().data("BRIT.CBT") == beforePreview,
                  "Encounter redo failed");
        });
        if (qEnvironmentVariableIsSet("U5_GAME_DIR"))
            test("Original resource compatibility and native workspaces", [&] {
                Project p;
                p.openGame(qEnvironmentVariable("U5_GAME_DIR"));
                for (auto name : p.resources.keys()) {
                    auto b = p.data(name);
                    if (name.endsWith(".TLK"))
                        for (auto e : U5::readDialogue(b)) {
                            auto d = Dialogue::parse(e.bytes);
                            check(d.bytes() == e.bytes, "Original dialogue structure no-op");
                            check(d.structuralError.isEmpty(),
                                  "Original dialogue has unsupported structure");
                            for (auto issue : d.issues())
                                check(!issue.error,
                                      "Original dialogue rejected by semantic validation");
                        }
                    if (name.endsWith(".16")) {
                        auto g = U5::readGraphics(name, b);
                        check(U5::writeGraphics(g) == b, "Original graphics no-op");
                    } else if (name.endsWith(".TLK")) {
                        for (auto e : U5::readDialogue(b))
                            check(U5::encodeText(U5::decodeText(e.bytes)) == e.bytes,
                                  "Original dialogue text roundtrip");
                    }
                }
                U5::storyPages(p.data("STORY.DAT"));
                WorkshopWindow w;
                check(w.openGame(p.sourceDirectory), "Original folder load");
                for (auto name : p.resources.keys()) {
                    w.selectResource(name);
                    app.processEvents();
                }
                check(w.projectForTests().changed().isEmpty(), "Original GUI mutation");
            });
        std::cout << count << " test groups passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
