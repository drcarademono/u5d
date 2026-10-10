#include "dungeon_editor.h"
#include "dungeon_names.h"
namespace Workshop {
DungeonDocument::DungeonDocument(const Project *p) : project(p) {
    U5::require(project->data("DUNGEON.DAT").size() == 4096,
                "Dungeons must contain eight 512-byte blocks");
}
int DungeonDocument::offset(int dungeon, int floor) {
    U5::require(dungeon >= 0 && dungeon < 8 && floor >= 0 && floor < 8,
                "Dungeon or level is out of range");
    return dungeon * 512 + floor * 64;
}
QByteArray DungeonDocument::level(int dungeon, int floor, bool original) const {
    return (original ? project->resources["DUNGEON.DAT"].original : project->data("DUNGEON.DAT"))
        .mid(offset(dungeon, floor), 64);
}
int DungeonDocument::roomIndex(int dungeon, int cell) {
    offset(dungeon, 0);
    U5::require((cell & 0xf0) == 0xa0 || (cell & 0xf0) == 0xf0,
                "This feature does not enter a combat room");
    // DUNGEON_0000: map ID minus 0x21, then subtract one except for Deceit.
    return qMax(0, dungeon - 1) * 16 + (cell & 15);
}
QString DungeonDocument::feature(int cell) {
    static const QStringList names = {"Passage",
                                      "Ladder up",
                                      "Ladder down",
                                      "Ladder up and down",
                                      "Chest",
                                      "Fountain",
                                      "Pit / trap",
                                      "Open chest",
                                      "Field",
                                      "Unused / impossible",
                                      "Encounter-room door",
                                      "Wall / wall message",
                                      "Special wall",
                                      "Hidden door",
                                      "Special door",
                                      "Encounter-room door (alternate)"};
    U5::require(cell >= 0 && cell <= 255, "Dungeon feature byte out of range");
    return names[cell >> 4];
}
QString DungeonDocument::variant(int cell) {
    const int kind = cell >> 4, i = cell & 15;
    if (kind == 10 || kind == 15)
        return QString("Room %1").arg(i + 1);
    if (kind == 8) {
        if ((i & 7) < 4)
            return QStringList{"Sleep", "Poison gas", "Fire", "Electricity"}[i & 7] +
                   (i & 8 ? " · ceiling opening" : "");
        return QString("Advanced energy field variant %1").arg(i);
    }
    if (kind == 5)
        return QStringList{"Cure poison", "Heal", "Poison", "Bad taste / injury"}.value(
            i, "Bad taste / injury (advanced)");
    if (kind == 6 && (i & 7) < 3)
        return QStringList{"Open pit", "Hidden pit trap", "Bomb trap"}[i & 7] +
               (i & 8 ? " · ceiling opening" : "");
    if (kind == 11)
        return i == 0 ? "Plain wall" : QString("Wall message %1").arg(i);
    if (i == 0)
        return "Default";
    if (i == 8 && kind < 9)
        return "Opening in ceiling";
    return QString("Advanced variant %1").arg(i);
}
QMap<QString, QByteArray> DungeonDocument::changes(int dungeon, int floor,
                                                   const QByteArray &cells) const {
    U5::require(cells.size() == 64, "Dungeon levels must remain 8 by 8");
    auto bytes = project->data("DUNGEON.DAT");
    bytes.replace(offset(dungeon, floor), 64, cells);
    return {{"DUNGEON.DAT", bytes}};
}
void DungeonDocument::validate(const Project &p, bool allRoomReferences) {
    DungeonDocument document(&p);
    const auto &bytes = p.data("DUNGEON.DAT");
    const auto &original = p.resources["DUNGEON.DAT"].original;
    for (int i = 0; i < bytes.size(); ++i) {
        int value = U5::byte(bytes, i);
        const bool changed = i >= original.size() || bytes[i] != original[i];
        if (!changed && !allRoomReferences)
            continue;
        U5::require(!changed || (value & 0xf0) != 0x90,
                    "Cannot introduce an unused / impossible dungeon feature");
        if ((value & 0xf0) == 0xa0 || (value & 0xf0) == 0xf0) {
            int room = roomIndex(i / 512, value);
            U5::require(p.resources.contains("DUNGEON.CBT") &&
                            p.data("DUNGEON.CBT").size() >= (room + 1) * 352,
                        "Encounter-room reference has no matching DUNGEON.CBT room");
        }
    }
}
QStringList DungeonDocument::warnings(int dungeon, int floor, int x, int y) const {
    U5::require(x >= 0 && x < 8 && y >= 0 && y < 8, "Dungeon coordinates out of range");
    int value = U5::byte(level(dungeon, floor), y * 8 + x), kind = value >> 4;
    QStringList result;
    if ((kind == 1 || kind == 3) && floor > 0) {
        int other = U5::byte(level(dungeon, floor - 1), y * 8 + x) >> 4;
        if (other != 2 && other != 3)
            result << "Ladder up has no matching ladder down on the level above.";
    }
    if (kind == 2 || kind == 3) {
        if (floor == 7)
            result << "Ladder down points below the deepest level.";
        else {
            int other = U5::byte(level(dungeon, floor + 1), y * 8 + x) >> 4;
            if (other != 1 && other != 3)
                result << "Ladder down has no matching ladder up on the level below.";
        }
    }
    if (kind == 9)
        result << "The engine describes this feature as impossible; preserved from the original.";
    if (kind == 10 || kind == 15) {
        int room = roomIndex(dungeon, value);
        if (!project->resources.contains("DUNGEON.CBT") ||
            project->data("DUNGEON.CBT").size() < (room + 1) * 352)
            result << "The linked combat room is missing.";
        if (dungeon < 2)
            result << "Deceit and Despise share this combat-room bank.";
    }
    if (kind != 10 && kind != 15 && (value & 7))
        result << "Subtype / message bits are retained. Their effect depends on this feature.";
    return result;
}
DungeonWorkspace::DungeonWorkspace(
    Project *p, QMap<QString, int> *state,
    std::function<bool(const QMap<QString, QByteArray> &, const QString &)> commit,
    std::function<void(int)> room, QWidget *parent)
    : QWidget(parent) {
    setObjectName("dungeonWorkspace");
    auto document = std::make_shared<DungeonDocument>(p);
    auto layout = new QVBoxLayout(this);
    auto row = new QHBoxLayout;
    auto dungeon = new QComboBox;
    dungeon->setObjectName("dungeonSelector");
    for (auto name : engineDungeonNames)
        dungeon->addItem(QString::fromLatin1(name));
    dungeon->setToolTip("Choose a dungeon. Every dungeon has eight underground levels.");
    dungeon->setCurrentIndex(qBound(0, state->value("DUNGEON.DAT/dungeon"), 7));
    auto floor = new QComboBox;
    floor->setObjectName("dungeonLevelSelector");
    for (int i = 0; i < 8; ++i)
        floor->addItem(QString("Level %1").arg(i + 1));
    floor->setToolTip(
        "Level 1 is nearest the surface; Level 8 is deepest. Engine indices are 0–7.");
    floor->setCurrentIndex(qBound(0, state->value("DUNGEON.DAT/level"), 7));
    row->addWidget(new QLabel("Dungeon:"));
    row->addWidget(dungeon);
    row->addWidget(new QLabel("Level:"));
    row->addWidget(floor);
    row->addStretch();
    layout->addLayout(row);
    auto tools = new QHBoxLayout;
    canvas = new MapCanvas;
    canvas->setObjectName("dungeonCanvas");
    canvas->clipboardMime = "application/x-impera-dungeon-features";
    canvas->clipboardMagic = "IMPDUNG1";
    auto toolGroup = new QButtonGroup(this);
    canvas->side = 8;
    canvas->zoom = 3;
    canvas->grid = true;
    for (auto pair : QList<QPair<QString, int>>{{"Pencil", MapCanvas::Pencil},
                                                {"Pick", MapCanvas::Eyedropper},
                                                {"Pan", MapCanvas::Pan},
                                                {"Select", MapCanvas::Select},
                                                {"Rectangle", MapCanvas::Rectangle},
                                                {"Fill", MapCanvas::Fill}}) {
        auto button = new QToolButton;
        button->setIcon(MapCanvas::toolIcon(pair.second));
        button->setIconSize({22, 22});
        button->setFixedSize(28, 28);
        button->setCheckable(true);
        toolGroup->addButton(button, pair.second);
        button->setObjectName(QString("dungeonTool%1").arg(pair.second));
        button->setAccessibleName(pair.first);
        button->setStyleSheet("QToolButton:checked { background:#365f8d; border:2px solid #86b9ed; "
                              "border-radius:3px; }");
        const QMap<int, QString> hints = {
            {MapCanvas::Pencil, "Pencil (B): drag to paint the current feature and variant."},
            {MapCanvas::Eyedropper, "Eyedropper (I): pick a feature with its variant / room link. "
                                    "Right-click also picks."},
            {MapCanvas::Pan, "Pan: drag the view without changing dungeon cells."},
            {MapCanvas::Select,
             "Select (V): drag an area to copy. Shift+arrow keys extend the selection."},
            {MapCanvas::Rectangle,
             "Rectangle (R): paint a filled rectangle with the current brush."},
            {MapCanvas::Fill,
             "Fill (F): replace connected cells with the same feature and variant."}};
        button->setToolTip(hints.value(pair.second));
        connect(button, &QPushButton::clicked, this, [=] {
            canvas->cancelStroke();
            canvas->tool = pair.second;
        });
        tools->addWidget(button);
    }
    for (auto text : QStringList{"Copy", "Paste", "Clear selection"}) {
        auto button = new QPushButton(text);
        button->setToolTip(text + " feature bytes; copying preserves subtypes and room links.");
        connect(button, &QPushButton::clicked, this, [=] {
            if (text == "Copy")
                canvas->copySelection();
            else if (text == "Paste")
                canvas->beginPaste();
            else
                canvas->clearSelection();
        });
        tools->addWidget(button);
    }
    auto highlight = new QCheckBox("Highlight changes");
    highlight->setToolTip("Compare this level with the original game file.");
    connect(highlight, &QCheckBox::toggled, this, [=](bool value) {
        canvas->comparison = value;
        canvas->update();
    });
    tools->addWidget(highlight);
    layout->addLayout(tools);
    auto split = new QHBoxLayout;
    auto palette = new QListWidget;
    palette->setObjectName("dungeonPalette");
    palette->setMaximumWidth(240);
    for (int i = 0; i < 16; ++i) {
        if (i == 9)
            continue;
        auto item = new QListWidgetItem(DungeonDocument::feature(i * 16), palette);
        item->setData(Qt::UserRole, i * 16);
        item->setToolTip("Paint " + item->text() + ". Configure subtype / room below.");
    }
    split->addWidget(palette);
    auto scroll = new MapView(canvas);
    split->addWidget(scroll, 1);
    auto side = new QVBoxLayout;
    auto details = new QLabel;
    details->setWordWrap(true);
    details->setObjectName("dungeonCellDetails");
    side->addWidget(details);
    auto subtype = new QComboBox;
    subtype->setObjectName("dungeonSubtype");
    subtype->setToolTip("Low feature bits: room number 1–16 for room doors (encoded 0–15); "
                        "otherwise feature-specific subtype, message, or ceiling opening flag.");
    side->addWidget(new QLabel("Brush variant / room:"));
    side->addWidget(subtype);
    auto apply = new QPushButton("Apply brush to selected cell");
    apply->setToolTip("Replace the inspected cell with the selected feature and subtype.");
    side->addWidget(apply);
    auto open = new QPushButton("Open combat room");
    open->setObjectName("openDungeonRoom");
    open->setToolTip("Edit the combat room entered through the inspected door. Back to dungeon "
                     "restores this cell.");
    side->addWidget(open);
    auto stack = new QListWidget;
    stack->setObjectName("dungeonFloorStack");
    stack->setToolTip(
        "Vertical connections at the inspected X/Y position. Select a level to inspect it.");
    side->addWidget(stack);
    auto warning = new QLabel;
    warning->setWordWrap(true);
    side->addWidget(warning);
    side->addStretch();
    split->addLayout(side);
    layout->addLayout(split, 1);
    auto status = new QLabel;
    status->setToolTip("Feedback for copy, paste, and map gestures.");
    layout->addWidget(status);
    canvas->feedback = [=](const QString &text) { status->setText(text); };
    auto note = new QLabel("Dungeon edges wrap. Feature symbols are schematic, not terrain tiles. "
                           "Already-loaded dungeons in saves may retain their old layout.");
    note->setWordWrap(true);
    layout->addWidget(note);
    QVector<QImage> symbols;
    for (int value = 0; value < 256; ++value) {
        QImage image(16, 16, QImage::Format_RGB32);
        image.fill((value >> 4) == 11 ? QColor("#738098") : QColor("#192638"));
        QPainter painter(&image);
        painter.setPen(Qt::white);
        auto font = painter.font();
        font.setPixelSize(9);
        painter.setFont(font);
        static const QStringList signs = {"·", "↑", "↓", "↕", "C", "~", "!", "c",
                                          "*", "?", "R", "#", "X", "H", "D", "R"};
        painter.drawText(image.rect(), Qt::AlignCenter,
                         (value >> 4) == 10 || (value >> 4) == 15
                             ? QString::number((value & 15) + 1)
                             : signs[value >> 4]);
        if (value < 0x90 && (value & 8)) {
            painter.setPen(QColor("#78c9df"));
            painter.drawEllipse(11, 1, 3, 3);
        }
        symbols.append(image);
    }
    canvas->tiles = symbols;
    canvas->brush = qBound(0, state->value("DUNGEON.DAT/brush", 0), 255);
    canvas->chooseTool = [=](int tool) {
        canvas->tool = tool;
        if (auto button = toolGroup->button(tool))
            button->setChecked(true);
    };
    toolGroup->button(MapCanvas::Pencil)->setChecked(true);
    auto selected = std::make_shared<QPoint>(QPoint(qBound(0, state->value("DUNGEON.DAT/x"), 7),
                                                    qBound(0, state->value("DUNGEON.DAT/y"), 7)));
    auto inspect = [=] {
        int d = dungeon->currentIndex(), f = floor->currentIndex(),
            v = U5::byte(canvas->ids, selected->y() * 8 + selected->x());
        (*state)["DUNGEON.DAT/x"] = selected->x();
        (*state)["DUNGEON.DAT/y"] = selected->y();
        details->setText(QString("X %1 · Y %2\n%3\nFeature byte 0x%4\nBrush: %5")
                             .arg(selected->x())
                             .arg(selected->y())
                             .arg(DungeonDocument::feature(v) + " · " + DungeonDocument::variant(v))
                             .arg(v, 2, 16, QChar('0'))
                             .arg(DungeonDocument::feature(canvas->brush)));
        open->setEnabled((v >> 4 == 10 || v >> 4 == 15) && p->resources.contains("DUNGEON.CBT") &&
                         p->data("DUNGEON.CBT").size() >=
                             (DungeonDocument::roomIndex(d, v) + 1) * 352);
        warning->setText(document->warnings(d, f, selected->x(), selected->y()).join("\n"));
        QSignalBlocker block(stack);
        stack->clear();
        for (int i = 0; i < 8; ++i)
            stack->addItem(QString("Level %1 — %2")
                               .arg(i + 1)
                               .arg(DungeonDocument::feature(U5::byte(
                                   document->level(d, i), selected->y() * 8 + selected->x()))));
        stack->setCurrentRow(f);
    };
    auto load = [=] {
        canvas->cancelStroke();
        (*state)["DUNGEON.DAT/dungeon"] = dungeon->currentIndex();
        (*state)["DUNGEON.DAT/level"] = floor->currentIndex();
        canvas->clearSelection();
        canvas->ids = document->level(dungeon->currentIndex(), floor->currentIndex());
        canvas->original = document->level(dungeon->currentIndex(), floor->currentIndex(), true);
        canvas->keyboardCell = *selected;
        canvas->resizeMap();
        inspect();
    };
    auto brush = [=](int value) {
        canvas->brush = value;
        (*state)["DUNGEON.DAT/brush"] = value;
        QSignalBlocker block(subtype);
        subtype->clear();
        for (int i = 0; i < 16; ++i)
            subtype->addItem(DungeonDocument::variant((value & 0xf0) | i), i);
        {
            QSignalBlocker paletteBlock(palette);
            palette->setCurrentRow(-1);
            for (int i = 0; i < palette->count(); ++i)
                if (palette->item(i)->data(Qt::UserRole).toInt() == (value & 0xf0))
                    palette->setCurrentRow(i);
        }
        subtype->setCurrentIndex(value & 15);
        inspect();
    };
    canvas->pick = brush;
    canvas->inspect = [=](int x, int y, int) {
        *selected = {x, y};
        inspect();
    };
    canvas->commit = [=](const QByteArray &cells) {
        return commit(document->changes(dungeon->currentIndex(), floor->currentIndex(), cells),
                      "Edit dungeon level");
    };
    canvas->changed = inspect;
    connect(palette, &QListWidget::currentItemChanged, this, [=](QListWidgetItem *item) {
        if (item)
            brush(item->data(Qt::UserRole).toInt());
    });
    connect(subtype, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [=](int value) { brush((canvas->brush & 0xf0) | value); });
    connect(apply, &QPushButton::clicked, this, [=] {
        auto cells = canvas->ids;
        cells[selected->y() * 8 + selected->x()] = char(canvas->brush);
        if (commit(document->changes(dungeon->currentIndex(), floor->currentIndex(), cells),
                   "Configure dungeon cell"))
            load();
    });
    connect(open, &QPushButton::clicked, this, [=] {
        int value = U5::byte(canvas->ids, selected->y() * 8 + selected->x());
        room(DungeonDocument::roomIndex(dungeon->currentIndex(), value));
    });
    connect(stack, &QListWidget::currentRowChanged, this, [=](int i) {
        if (i >= 0)
            floor->setCurrentIndex(i);
    });
    connect(dungeon, qOverload<int>(&QComboBox::currentIndexChanged), this, [=](int) { load(); });
    connect(floor, qOverload<int>(&QComboBox::currentIndexChanged), this, [=](int) { load(); });
    load();
    brush(canvas->brush);
}
} // namespace Workshop
