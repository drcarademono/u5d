#include "map_editor.h"
#include "../src/tiles.h"
#include "dialogue.h"
#include <cmath>
using U5::require;

bool MapDocument::supported(const QString &r) {
    return r.endsWith(".CBT") || QStringList{"BRIT.DAT",   "UNDER.DAT", "TOWNE.DAT",
                                             "CASTLE.DAT", "KEEP.DAT",  "DWELLING.DAT"}
                                     .contains(r);
}
MapDocument::MapDocument(Project *p, const QString &r) : project(p), resource(r) {
    pages = U5::mapPages(resource, project->data(resource));
}
QByteArray MapDocument::terrain(int index, bool original) const {
    const auto &p = pages.at(index);
    auto data = original ? project->resources[resource].original : project->data(resource);
    if (p.side == 256)
        return U5::worldMap(
            resource, data,
            resource == "BRIT.DAT"
                ? (original ? project->resources["DATA.OVL"].original : project->data("DATA.OVL"))
                : QByteArray());
    QByteArray cells;
    for (int y = 0; y < p.side; ++y)
        cells += data.mid(p.offset + y * p.stride, p.side);
    require(cells.size() == p.side * p.side, "Map terrain is truncated");
    return cells;
}
QMap<QString, QByteArray> MapDocument::changes(int index, const QByteArray &cells) const {
    const auto &p = pages.at(index);
    require(cells.size() == p.side * p.side, "Map terrain dimensions changed");
    auto data = project->data(resource);
    QMap<QString, QByteArray> result;
    if (p.side == 256) {
        auto overlay = resource == "BRIT.DAT" ? project->data("DATA.OVL") : QByteArray();
        U5::writeWorld(resource, cells, data, overlay);
        if (resource == "BRIT.DAT")
            result["DATA.OVL"] = overlay;
    } else
        for (int y = 0; y < p.side; ++y)
            data.replace(p.offset + y * p.stride, p.side, cells.mid(y * p.side, p.side));
    result[resource] = data;
    return result;
}

MapNpcDocument::MapNpcDocument(Project *p, QString r) : project(p), resource(r) {
    require(project->data(resource).size() == 4608, "NPC schedules must be 4608 bytes");
}
int MapNpcDocument::locationIndex(int slot) {
    require(slot >= 0 && slot < 4, "Invalid NPC time slot");
    return slot == 0 ? 0 : slot == 2 ? 2 : 1;
}
int MapNpcDocument::offset(int settlement, int npc) const {
    require(settlement >= 0 && settlement < 8 && npc >= 0 && npc < 32, "Invalid NPC record");
    return settlement * 576 + npc * 16;
}
NpcLocation MapNpcDocument::location(int settlement, int npc, int slot) const {
    int base = offset(settlement, npc), loc = locationIndex(slot);
    auto data = project->data(resource);
    int z = U5::byte(data, base + 9 + loc);
    return {int(U5::byte(data, base + 3 + loc)), int(U5::byte(data, base + 6 + loc)),
            z >= 128 ? z - 256 : z, int(U5::byte(data, base + loc))};
}
int MapNpcDocument::hour(int settlement, int npc, int slot) const {
    locationIndex(slot);
    return U5::byte(project->data(resource), offset(settlement, npc) + 12 + slot);
}
int MapNpcDocument::sprite(int settlement, int npc) const {
    offset(settlement, npc);
    return 256 + U5::byte(project->data(resource), settlement * 576 + 512 + npc);
}
int MapNpcDocument::dialogue(int settlement, int npc) const {
    offset(settlement, npc);
    return U5::byte(project->data(resource), settlement * 576 + 544 + npc);
}
QByteArray MapNpcDocument::move(int settlement, int npc, int slot, NpcLocation destination) const {
    int base = offset(settlement, npc), loc = locationIndex(slot);
    require(destination.x >= 0 && destination.x < 32 && destination.y >= 0 && destination.y < 32 &&
                destination.floor >= -128 && destination.floor <= 127,
            "NPC destination is outside map bounds");
    auto bytes = project->data(resource);
    bytes[base + 3 + loc] = char(destination.x);
    bytes[base + 6 + loc] = char(destination.y);
    bytes[base + 9 + loc] = char(destination.floor);
    return bytes;
}

MapCanvas::MapCanvas(QWidget *parent) : QWidget(parent) {
    setObjectName("mapCanvas");
    setAccessibleName("Map canvas");
    setToolTip("Arrow keys choose a tile; Enter applies the selected tool or moves the selected "
               "character. Shift+arrows extend a terrain selection. B/I/V/R/F select tools. Escape "
               "cancels a gesture.");
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}
void MapCanvas::resizeMap() {
    setFixedSize(int(std::ceil(side * 16 * zoom)), int(std::ceil(side * 16 * zoom)));
    update();
}
QPoint MapCanvas::cellAt(QPointF p) const {
    if (p.x() < 0 || p.y() < 0 || p.x() >= side * 16 * zoom || p.y() >= side * 16 * zoom)
        return {-1, -1};
    return {int(std::floor(p.x() / (16 * zoom))), int(std::floor(p.y() / (16 * zoom)))};
}
void MapCanvas::paintEvent(QPaintEvent *e) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    double cell = 16 * zoom;
    auto r = e->rect();
    p.fillRect(r, QColor("#141b27"));
    for (int y = qMax(0, int(r.top() / cell)); y <= qMin(side - 1, int(r.bottom() / cell)); ++y)
        for (int x = qMax(0, int(r.left() / cell)); x <= qMin(side - 1, int(r.right() / cell));
             ++x) {
            int offset = y * side + x;
            if (offset >= ids.size())
                continue;
            int id = U5::byte(ids, offset);
            QRectF target(x * cell, y * cell, cell, cell);
            if (id < tiles.size())
                p.drawImage(target, tiles[id]);
            else
                p.fillRect(target, Qt::magenta);
            if (comparison && original.size() == ids.size() && ids[offset] != original[offset]) {
                p.fillRect(target, QColor(255, 150, 30, 65));
                p.setPen(QColor("#ffae42"));
                p.drawRect(target.adjusted(1, 1, -1, -1));
            }
            if (grid) {
                p.setPen(QColor(255, 255, 255, 45));
                p.drawRect(target.adjusted(0, 0, -1, -1));
            }
        }
    if (hasFocus()) {
        p.setPen(QPen(QColor("#ffea80"), 2, Qt::DashLine));
        p.drawRect(QRectF(keyboardCell.x() * cell, keyboardCell.y() * cell, cell, cell)
                       .adjusted(2, 2, -2, -2));
    }
    for (auto a : actors)
        if (a.x >= 0 && a.y >= 0 && a.x < side && a.y < side && a.tile >= 0 &&
            a.tile < tiles.size() && a.label.isEmpty())
            p.drawImage(QRectF(a.x * cell, a.y * cell, cell, cell), tiles[a.tile]);
    if (tool == InspectNpc) {
        for (auto actor : actors)
            if (!actor.label.isEmpty()) {
                QRectF r(actor.x * cell, actor.y * cell, cell, cell);
                if (actor.x < 0 || actor.y < 0 || actor.x >= side || actor.y >= side)
                    continue;
                p.fillRect(r.adjusted(2, 2, -2, -2), QColor(20, 30, 60, 180));
                p.setPen(actor.label.startsWith("T") ? QColor("#ffbd55") : QColor("#72caff"));
                p.drawRect(r.adjusted(2, 2, -2, -2));
                p.drawText(r, Qt::AlignCenter, actor.label);
            }
        for (auto actor : ghosts) {
            p.setOpacity(0.35);
            if (actor.tile >= 0 && actor.tile < tiles.size())
                p.drawImage(QRectF(actor.x * cell, actor.y * cell, cell, cell), tiles[actor.tile]);
            p.setOpacity(1);
            p.setPen(QColor("#72caff"));
            p.drawText(QRectF(actor.x * cell, actor.y * cell, cell, cell),
                       Qt::AlignTop | Qt::AlignLeft,
                       actor.label.isEmpty() ? QString::number(actor.npc + 1) : actor.label);
        }
        for (auto actor : actors)
            if (actor.npc == selectedNpc) {
                p.setPen(QPen(QColor("#72caff"), 2));
                p.drawRect(
                    QRectF(actor.x * cell, actor.y * cell, cell, cell).adjusted(1, 1, -1, -1));
                if (npcDrag && npcDestination.x() >= 0 && actor.tile < tiles.size()) {
                    p.setOpacity(0.65);
                    p.drawImage(
                        QRectF(npcDestination.x() * cell, npcDestination.y() * cell, cell, cell),
                        tiles[actor.tile]);
                    p.setOpacity(1);
                    if (!actor.label.isEmpty()) {
                        p.setPen(QColor("#72caff"));
                        p.drawText(QRectF(npcDestination.x() * cell, npcDestination.y() * cell,
                                          cell, cell),
                                   Qt::AlignCenter, actor.label);
                    }
                }
            }
    }
    if (tool != InspectNpc && !selection.isEmpty()) {
        QRectF area(selection.x() * cell, selection.y() * cell, selection.width() * cell,
                    selection.height() * cell);
        p.fillRect(area, QColor(80, 170, 255, 35));
        p.setPen(QPen(QColor("#72caff"), 2, Qt::DashLine));
        p.drawRect(area.adjusted(1, 1, -1, -1));
    }
    if (pasting() && hover.x() >= 0) {
        p.setOpacity(0.6);
        for (int y = qMax(0, int(r.top() / cell) - hover.y());
             y <= qMin(stampSize.height() - 1, int(r.bottom() / cell) - hover.y()); ++y)
            for (int x = qMax(0, int(r.left() / cell) - hover.x());
                 x <= qMin(stampSize.width() - 1, int(r.right() / cell) - hover.x()); ++x) {
                int id = U5::byte(stamp, y * stampSize.width() + x);
                if (id < tiles.size())
                    p.drawImage(QRectF((hover.x() + x) * cell, (hover.y() + y) * cell, cell, cell),
                                tiles[id]);
            }
        p.setOpacity(1);
        p.setPen(QPen(pasteFits() ? QColor("#83e28b") : QColor("#ff6868"), 2));
        p.drawRect(QRectF(hover.x() * cell, hover.y() * cell, stampSize.width() * cell,
                          stampSize.height() * cell)
                       .adjusted(1, 1, -1, -1));
    }
    if (hover.x() >= 0) {
        QRectF target(hover.x() * cell, hover.y() * cell, cell, cell);
        if (!pasting() && tool == 0 && brush >= 0 && brush < tiles.size()) {
            p.setOpacity(0.35);
            p.drawImage(target, tiles[brush]);
            p.setOpacity(1);
        }
        p.setPen(QPen(QColor("#f2ce65"), 2));
        p.drawRect(target.adjusted(1, 1, -1, -1));
    }
}
void MapCanvas::line(QPoint a, QPoint b) {
    int x = a.x(), y = a.y(), dx = std::abs(b.x() - x), sx = x < b.x() ? 1 : -1;
    int dy = -std::abs(b.y() - y), sy = y < b.y() ? 1 : -1, error = dx + dy;
    for (;;) {
        if (editable({x, y}))
            ids[y * side + x] = char(brush);
        if (x == b.x() && y == b.y())
            break;
        int e = 2 * error;
        if (e >= dy) {
            error += dy;
            x += sx;
        }
        if (e <= dx) {
            error += dx;
            y += sy;
        }
    }
    update(QRectF(qMin(a.x(), b.x()) * 16 * zoom, qMin(a.y(), b.y()) * 16 * zoom,
                  (std::abs(a.x() - b.x()) + 1) * 16 * zoom,
                  (std::abs(a.y() - b.y()) + 1) * 16 * zoom)
               .toAlignedRect());
}
void MapCanvas::point(QPointF position, bool draw) {
    auto cell = cellAt(position);
    auto previous = hover;
    hover = cell;
    if (pasting())
        update();
    if (previous != hover) {
        for (auto cell : {previous, hover})
            if (cell.x() >= 0)
                update(QRectF(cell.x() * 16 * zoom, cell.y() * 16 * zoom, 16 * zoom, 16 * zoom)
                           .toAlignedRect()
                           .adjusted(-2, -2, 2, 2));
    }
    if (cell.x() < 0) {
        last = {-1, -1};
        return;
    }
    int offset = cell.y() * side + cell.x();
    if (offset >= ids.size())
        return;
    if (inspect)
        inspect(cell.x(), cell.y(), U5::byte(ids, offset));
    if (pasting())
        say(pasteFits() ? QString("Paste preview: %1 changed cells · click to place · Esc cancels")
                              .arg(pasteChangedCells())
                        : "Paste is outside map bounds; placement rejected");
    if (draw && stroke) {
        if (tool == Rectangle)
            previewRectangle(cell);
        else if (tool == Select) {
            selection = QRect(
                QPoint(qMin(start.x(), cell.x()), qMin(start.y(), cell.y())),
                QSize(std::abs(start.x() - cell.x()) + 1, std::abs(start.y() - cell.y()) + 1));
            update();
            if (selectionChanged)
                selectionChanged();
        } else {
            line(last.x() < 0 ? cell : last, cell);
            last = cell;
        }
    }
}
void MapCanvas::mousePressEvent(QMouseEvent *e) {
    setFocus(Qt::MouseFocusReason);
    auto cell = cellAt(e->position());
    if (cell.x() < 0)
        return;
    keyboardCell = cell;
    if (e->button() == Qt::RightButton ||
        (e->button() == Qt::LeftButton && tool == 1 && !pasting())) {
        if (pick)
            pick(U5::byte(ids, cell.y() * side + cell.x()));
        point(e->position(), false);
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    if (pasting()) {
        pasteAt(cell);
        return;
    }
    if (tool == InspectNpc) {
        QList<int> candidates;
        for (auto a : actors)
            if (a.x == cell.x() && a.y == cell.y())
                candidates.append(a.npc);
        bool chooser = candidates.size() > 1 && !candidates.contains(selectedNpc);
        int npc = candidates.contains(selectedNpc)       ? selectedNpc
                  : candidates.size() == 1               ? candidates[0]
                  : candidates.isEmpty() || !chooseActor ? -1
                                                         : chooseActor(candidates);
        if (npc >= 0) {
            selectedNpc = npc;
            if (chooseNpc)
                chooseNpc(npc);
            npcOrigin = npcDestination = cell;
            // The modal chooser consumes the release that initiated selection.
            // A subsequent drag moves the chosen actor, without reopening it.
            npcDrag = !chooser;
            update();
        }
        return;
    }

    if (tool != Pencil && tool != Select && tool != Rectangle && tool != Fill)
        return;
    if (tool != Select && !editable(cell))
        return;
    before = ids;
    previousSelection = selection;
    start = cell;
    operation = tool == Rectangle ? "Rectangle" : tool == Fill ? "Fill" : "Paint";
    if (tool == Fill) {
        flood(cell);
        finishEdit();
        return;
    }
    stroke = true;
    last = {-1, -1};
    point(e->position(), true);
}
void MapCanvas::mouseMoveEvent(QMouseEvent *e) {
    if (npcDrag) {
        npcDestination = cellAt(e->position());
        say(npcDestination.x() >= 0
                ? QString("NPC move preview: X %1 · Y %2 · release to place · Esc cancels")
                      .arg(npcDestination.x())
                      .arg(npcDestination.y())
                : "NPC destination outside map; release cancels");
        update();
        return;
    }
    point(e->position(), stroke && (e->buttons() & Qt::LeftButton));
}
void MapCanvas::mouseReleaseEvent(QMouseEvent *e) {
    if (npcDrag && e->button() == Qt::LeftButton) {
        npcDestination = cellAt(e->position());
        npcDrag = false;
        if (npcDestination.x() >= 0 && npcDestination != npcOrigin && moveNpc)
            moveNpc(selectedNpc, npcDestination);
        update();
        return;
    }
    if (!stroke || e->button() != Qt::LeftButton)
        return;
    point(e->position(), true);
    stroke = false;
    last = {-1, -1};
    if (tool == Select) {
        before.clear();
        if (selectionChanged)
            selectionChanged();
    } else
        finishEdit();
}
void MapCanvas::cancelStroke() {
    npcDrag = false;
    update();
    if (stroke) {
        ids = before;
        if (tool == Select)
            selection = previousSelection;
        before.clear();
        stroke = false;
        last = {-1, -1};
        update();
        if (selectionChanged)
            selectionChanged();
    }
    if (pasting()) {
        stamp.clear();
        update();
        say("Paste cancelled");
    }
}
void MapCanvas::say(const QString &message) {
    if (feedback)
        feedback(message);
}
bool MapCanvas::editable(QPoint cell) const {
    return cell.x() >= 0 && cell.y() >= 0 && cell.x() < side && cell.y() < side &&
           (selection.isEmpty() || selection.contains(cell));
}
void MapCanvas::finishEdit() {
    auto rollback = before;
    before.clear();
    stroke = false;
    if (ids == rollback)
        return;
    if (!commit || !commit(ids))
        ids = rollback;
    update();
    if (changed)
        changed();
}
void MapCanvas::previewRectangle(QPoint cell) {
    ids = before;
    auto area =
        QRect(QPoint(qMin(start.x(), cell.x()), qMin(start.y(), cell.y())),
              QSize(std::abs(start.x() - cell.x()) + 1, std::abs(start.y() - cell.y()) + 1));
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x)
            if (editable({x, y}) && (!outline || x == area.left() || x == area.right() ||
                                     y == area.top() || y == area.bottom()))
                ids[y * side + x] = char(brush);
    update();
}
void MapCanvas::flood(QPoint cell) {
    char source = ids[cell.y() * side + cell.x()];
    if (source == char(brush))
        return;
    QVector<QPoint> queue{cell};
    ids[cell.y() * side + cell.x()] = char(brush);
    for (int i = 0; i < queue.size(); ++i) {
        auto current = queue[i];
        for (auto delta : {QPoint(-1, 0), QPoint(1, 0), QPoint(0, -1), QPoint(0, 1)}) {
            auto next = current + delta;
            if (editable(next) && ids[next.y() * side + next.x()] == source) {
                ids[next.y() * side + next.x()] = char(brush);
                queue.append(next);
            }
        }
    }
}
void MapCanvas::clearSelection() {
    selection = {};
    update();
    if (selectionChanged)
        selectionChanged();
}
bool MapCanvas::copySelection() {
    if (selection.isEmpty()) {
        say("Select terrain before copying");
        return false;
    }
    auto area = selection.intersected(QRect(0, 0, side, side));
    if (area != selection || ids.size() != side * side)
        return false;
    QByteArray payload("IMPTILE1", 8);
    for (int value : {area.width(), area.height()}) {
        payload.append(char(value & 255));
        payload.append(char(value >> 8));
    }
    for (int y = area.top(); y <= area.bottom(); ++y)
        payload += ids.mid(y * side + area.left(), area.width());
    auto mime = new QMimeData;
    mime->setData("application/x-impera-terrain", payload);
    QApplication::clipboard()->setMimeData(mime);
    say(QString("Copied %1 × %2 terrain tiles; NPCs and combat records excluded")
            .arg(area.width())
            .arg(area.height()));
    return true;
}
bool MapCanvas::beginPaste() {
    cancelStroke();
    if (tool == InspectNpc) {
        say("Switch to a terrain tool before pasting");
        return false;
    }
    auto mime = QApplication::clipboard()->mimeData();
    auto payload = mime ? mime->data("application/x-impera-terrain") : QByteArray();
    if (payload.size() < 12 || payload.left(8) != "IMPTILE1") {
        say("Clipboard does not contain Impera terrain tiles");
        return false;
    }
    int width = U5::byte(payload, 8) + 256 * U5::byte(payload, 9);
    int height = U5::byte(payload, 10) + 256 * U5::byte(payload, 11);
    if (width < 1 || height < 1 || width > 256 || height > 256 ||
        payload.size() != 12 + width * height) {
        say("Clipboard terrain dimensions are invalid");
        return false;
    }
    stampSize = {width, height};
    stamp = payload.mid(12);
    update();
    say("Move pointer to preview terrain paste · click to place · Esc cancels");
    return true;
}
bool MapCanvas::pasteFits() const {
    return hover.x() >= 0 && hover.y() >= 0 && hover.x() + stampSize.width() <= side &&
           hover.y() + stampSize.height() <= side;
}
int MapCanvas::pasteChangedCells() const {
    if (!pasting() || !pasteFits())
        return 0;
    int count = 0;
    for (int y = 0; y < stampSize.height(); ++y)
        for (int x = 0; x < stampSize.width(); ++x)
            count +=
                ids[(hover.y() + y) * side + hover.x() + x] != stamp[y * stampSize.width() + x];
    return count;
}
void MapCanvas::pasteAt(QPoint cell) {
    hover = cell;
    if (!pasteFits()) {
        say("Paste rejected: outside map bounds");
        return;
    }
    before = ids;
    operation = "Paste terrain";
    for (int y = 0; y < stampSize.height(); ++y)
        ids.replace((cell.y() + y) * side + cell.x(), stampSize.width(),
                    stamp.mid(y * stampSize.width(), stampSize.width()));
    stamp.clear();
    finishEdit();
}

void MapCanvas::leaveEvent(QEvent *) {
    hover = {-1, -1};
    update();
}
void MapCanvas::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) {
        bool pending = stroke || npcDrag || pasting();
        cancelStroke();
        if (!pending)
            clearSelection();
        e->accept();
        return;
    }
    if (e->matches(QKeySequence::Copy)) {
        cancelStroke();
        copySelection();
        e->accept();
        return;
    }
    if (e->matches(QKeySequence::Paste)) {
        beginPaste();
        e->accept();
        return;
    }
    if (e->modifiers() == Qt::NoModifier || e->modifiers() == Qt::ShiftModifier) {
        QPoint direction;
        if (e->key() == Qt::Key_Left)
            direction = {-1, 0};
        if (e->key() == Qt::Key_Right)
            direction = {1, 0};
        if (e->key() == Qt::Key_Up)
            direction = {0, -1};
        if (e->key() == Qt::Key_Down)
            direction = {0, 1};
        if (!direction.isNull()) {
            QPoint previous = keyboardCell;
            keyboardCell += direction;
            keyboardCell.setX(qBound(0, keyboardCell.x(), side - 1));
            keyboardCell.setY(qBound(0, keyboardCell.y(), side - 1));
            if (e->modifiers() == Qt::ShiftModifier && tool != InspectNpc) {
                if (selection.isEmpty())
                    selection = QRect(previous, QSize(1, 1));
                selection = selection.united(QRect(keyboardCell, QSize(1, 1)));
                if (selectionChanged)
                    selectionChanged();
            }
            if (inspect)
                inspect(keyboardCell.x(), keyboardCell.y(),
                        U5::byte(ids, keyboardCell.y() * side + keyboardCell.x()));
            if (auto scroll = parentWidget()
                                  ? dynamic_cast<QScrollArea *>(parentWidget()->parentWidget())
                                  : nullptr)
                scroll->ensureVisible((keyboardCell.x() + 0.5) * 16 * zoom,
                                      (keyboardCell.y() + 0.5) * 16 * zoom, 20, 20);
            update();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            if (painting())
                cancelStroke();
            if (tool == InspectNpc) {
                if (selectedNpc >= 0 && moveNpc)
                    moveNpc(selectedNpc, keyboardCell);
            } else {
                QPointF position((keyboardCell.x() + 0.5) * 16 * zoom,
                                 (keyboardCell.y() + 0.5) * 16 * zoom);
                QMouseEvent press(QEvent::MouseButtonPress, position, position, Qt::LeftButton,
                                  Qt::LeftButton, Qt::NoModifier);
                mousePressEvent(&press);
                QMouseEvent release(QEvent::MouseButtonRelease, position, position, Qt::LeftButton,
                                    Qt::NoButton, Qt::NoModifier);
                mouseReleaseEvent(&release);
            }
            e->accept();
            return;
        }
    }
    const QMap<int, int> shortcuts{{Qt::Key_B, Pencil},
                                   {Qt::Key_I, Eyedropper},
                                   {Qt::Key_V, Select},
                                   {Qt::Key_R, Rectangle},
                                   {Qt::Key_F, Fill}};
    if (e->modifiers() == Qt::NoModifier && shortcuts.contains(e->key())) {
        cancelStroke();
        if (chooseTool)
            chooseTool(shortcuts[e->key()]);
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}
void MapCanvas::focusOutEvent(QFocusEvent *e) {
    cancelStroke();
    QWidget::focusOutEvent(e);
}
bool MapCanvas::event(QEvent *e) {
    if (e->type() == QEvent::UngrabMouse || e->type() == QEvent::WindowDeactivate)
        cancelStroke();
    return QWidget::event(e);
}

MapView::MapView(MapCanvas *c) : canvas(c) {
    setObjectName("canvasView");
    setWidget(canvas);
    setAlignment(Qt::AlignCenter);
    viewport()->installEventFilter(this);
    canvas->installEventFilter(this);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (changed)
            changed();
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (changed)
            changed();
    });
}
QPointF MapView::mapCenter() const {
    auto p = canvas->mapFrom(viewport(), viewport()->rect().center());
    return QPointF(p) / (16 * canvas->zoom);
}
void MapView::centerMap(QPointF cell) {
    horizontalScrollBar()->setValue(
        qRound(cell.x() * 16 * canvas->zoom - viewport()->width() / 2.0));
    verticalScrollBar()->setValue(
        qRound(cell.y() * 16 * canvas->zoom - viewport()->height() / 2.0));
}
void MapView::setZoom(double scale, QPoint anchor, bool fit) {
    canvas->cancelStroke();
    if (anchor.x() < 0)
        anchor = viewport()->rect().center();
    QPointF at = QPointF(canvas->mapFrom(viewport(), anchor)) / (16 * canvas->zoom);
    fitting = fit;
    canvas->zoom = qBound(fit ? 0.001 : 0.02, scale, 8.0);
    canvas->resizeMap();
    horizontalScrollBar()->setValue(qRound(at.x() * 16 * canvas->zoom - anchor.x()));
    verticalScrollBar()->setValue(qRound(at.y() * 16 * canvas->zoom - anchor.y()));
    if (changed)
        changed();
}
void MapView::fitMap() {
    auto s = viewport()->size() - QSize(4, 4);
    setZoom(qMin(s.width(), s.height()) / double(canvas->side * 16), {-1, -1}, true);
}
void MapView::resizeEvent(QResizeEvent *e) {
    QScrollArea::resizeEvent(e);
    if (fitting)
        fitMap();
}
bool MapView::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        auto key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Space) {
            if (!key->isAutoRepeat())
                space = event->type() == QEvent::KeyPress;
            canvas->setCursor(space ? Qt::OpenHandCursor : Qt::CrossCursor);
            return true;
        }
        if (event->type() == QEvent::KeyPress &&
            (key->key() == Qt::Key_Plus || key->key() == Qt::Key_Equal ||
             key->key() == Qt::Key_Minus)) {
            setZoom(canvas->zoom * (key->key() == Qt::Key_Minus ? 0.5 : 2));
            return true;
        }
        if (key->key() == Qt::Key_Escape && panning) {
            panning = false;
            canvas->setCursor(Qt::CrossCursor);
            return true;
        }
    }
    if (event->type() == QEvent::FocusOut || event->type() == QEvent::WindowDeactivate) {
        space = false;
        panning = false;
        canvas->setCursor(Qt::CrossCursor);
    }
    if (event->type() == QEvent::Wheel) {
        auto wheel = static_cast<QWheelEvent *>(event);
        if (wheel->modifiers() & Qt::ControlModifier) {
            QPoint anchor = watched == canvas
                                ? canvas->mapTo(viewport(), wheel->position().toPoint())
                                : wheel->position().toPoint();
            int delta = wheel->angleDelta().y();
            if (delta)
                setZoom(canvas->zoom * (delta > 0 ? 2 : 0.5), anchor);
            return true;
        }
        if (wheel->modifiers() & Qt::ShiftModifier) {
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() -
                                            wheel->angleDelta().y());
            return true;
        }
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto mouse = static_cast<QMouseEvent *>(event);
        // QScrollArea tries to reveal its focused child. For a map-sized child
        // that can recenter the entire map; focusing must preserve the viewport.
        int horizontal = horizontalScrollBar()->value(), vertical = verticalScrollBar()->value();
        canvas->setFocus(Qt::MouseFocusReason);
        horizontalScrollBar()->setValue(horizontal);
        verticalScrollBar()->setValue(vertical);
        if (mouse->button() == Qt::MiddleButton ||
            (mouse->button() == Qt::LeftButton && (space || canvas->tool == 2))) {
            canvas->cancelStroke();
            canvas->setFocus(Qt::MouseFocusReason);
            panning = true;
            panAt = mouse->globalPosition().toPoint();
            canvas->setCursor(Qt::ClosedHandCursor);
            return true;
        }
    }
    if (event->type() == QEvent::MouseMove && panning) {
        auto mouse = static_cast<QMouseEvent *>(event);
        auto now = mouse->globalPosition().toPoint();
        auto delta = now - panAt;
        panAt = now;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease && panning) {
        panning = false;
        canvas->setCursor(space || canvas->tool == 2 ? Qt::OpenHandCursor : Qt::CrossCursor);
        return true;
    }
    return QScrollArea::eventFilter(watched, event);
}

MapMinimap::MapMinimap(MapCanvas *c, MapView *v) : canvas(c), view(v) {
    setObjectName("mapMinimap");
    setMinimumSize(100, 100);
    setMaximumHeight(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setToolTip("Click or drag to navigate; outline shows the visible map area");
}
QRectF MapMinimap::imageRect() const {
    double edge = qMax(1, qMin(width(), height()) - 8);
    return {(width() - edge) / 2, (height() - edge) / 2, edge, edge};
}
void MapMinimap::refresh() {
    QVector<QImage> thumbnails;
    for (const auto &image : canvas->tiles)
        thumbnails.append(image.scaled(4, 4, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    cache = QImage(canvas->side * 4, canvas->side * 4, QImage::Format_RGB32);
    cache.fill(Qt::black);
    QPainter painter(&cache);
    for (int y = 0; y < canvas->side; ++y)
        for (int x = 0; x < canvas->side; ++x) {
            int offset = y * canvas->side + x;
            if (offset >= canvas->ids.size())
                continue;
            int id = U5::byte(canvas->ids, offset);
            if (id < canvas->tiles.size())
                painter.drawImage(QRect(x * 4, y * 4, 4, 4), thumbnails[id]);
            if (canvas->comparison && canvas->original.size() == canvas->ids.size() &&
                canvas->original[offset] != canvas->ids[offset])
                painter.fillRect(QRect(x * 4, y * 4, 4, 4), QColor("#ffae42"));
        }
    update();
}
void MapMinimap::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    auto area = imageRect();
    painter.fillRect(rect(), QColor("#141b27"));
    painter.drawImage(area, cache);
    double scale = area.width() / canvas->side;
    QPointF top = QPointF(canvas->mapFrom(view->viewport(), QPoint(0, 0))) / (16 * canvas->zoom);
    QSizeF visible = QSizeF(view->viewport()->size()) / (16 * canvas->zoom);
    auto region = QRectF(top, visible).intersected(QRectF(0, 0, canvas->side, canvas->side));
    painter.setPen(QPen(QColor("#ffffff"), 1));
    painter.drawRect(QRectF(area.topLeft() + region.topLeft() * scale, region.size() * scale));
    if (!canvas->selection.isEmpty()) {
        painter.setPen(QPen(QColor("#72caff"), 1));
        painter.drawRect(QRectF(area.topLeft() + QPointF(canvas->selection.topLeft()) * scale,
                                QSizeF(canvas->selection.size()) * scale));
    }
}
void MapMinimap::navigate(QPointF position) {
    auto area = imageRect();
    if (area.contains(position))
        view->centerMap((position - area.topLeft()) * (canvas->side / area.width()));
}
void MapMinimap::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton)
        navigate(event->position());
}
void MapMinimap::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton)
        navigate(event->position());
}

MapWorkspace::MapWorkspace(
    Project *p, const QString &r, QMap<QString, MapViewState> *s, QMap<QString, int> *n,
    std::function<bool(const QMap<QString, QByteArray> &, const QString &)> commit,
    std::function<void(const QString &)> navigate, MapBrushState *brushState, QWidget *parent)
    : QWidget(parent), project(p), resource(r), document(p, r), states(s), navigation(n),
      brushes(brushState ? brushState : &localBrushes), commitResources(commit),
      navigateResource(navigate) {
    setObjectName("mapWorkspace");
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    auto split = new QSplitter;
    layout->addWidget(split, 1);
    auto left = new QWidget;
    auto leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    auto search = new QLineEdit;
    search->setToolTip("Filter the map list by location or resource name.");
    search->setObjectName("mapSearch");
    search->setPlaceholderText("Find a map…");
    leftLayout->addWidget(search);
    maps = new QTreeWidget;
    maps->setToolTip("Choose a world map, settlement floor, or combat map to edit.");
    maps->setObjectName("mapNavigation");
    maps->setHeaderLabel("Maps");
    leftLayout->addWidget(maps);
    auto world = new QTreeWidgetItem(maps, {"World"});
    auto towns = new QTreeWidgetItem(maps, {"Settlements"});
    auto combat = new QTreeWidgetItem(maps, {"Combat maps"});
    for (auto name : project->resources.keys())
        if (MapDocument::supported(name)) {
            auto pages = U5::mapPages(name, project->data(name));
            QMap<QString, QTreeWidgetItem *> locations;
            QTreeWidgetItem *combatGroup = nullptr;
            if (name.endsWith(".CBT"))
                combatGroup = new QTreeWidgetItem(combat, {name});
            for (int i = 0; i < pages.size(); ++i) {
                auto mp = pages[i];
                QTreeWidgetItem *parentItem = world;
                QString label = mp.name;
                if (mp.settlement >= 0) {
                    auto location = mp.name.section(" — ", 0, 0);
                    if (!locations.contains(location))
                        locations[location] = new QTreeWidgetItem(towns, {location});
                    parentItem = locations[location];
                    const auto previous = parentItem->data(0, Qt::UserRole + 1);
                    if (!previous.isValid() ||
                        std::abs(mp.floor) < std::abs(pages[previous.toInt()].floor)) {
                        parentItem->setData(0, Qt::UserRole, name);
                        parentItem->setData(0, Qt::UserRole + 1, i);
                        parentItem->setToolTip(0, "Open the main floor of " + location);
                    }
                    label = mp.floor < 0    ? "Basement"
                            : mp.floor == 0 ? "Ground floor"
                                            : QString("Upper floor %1").arg(mp.floor);
                } else if (combatGroup)
                    parentItem = combatGroup;
                auto item = new QTreeWidgetItem(parentItem, {label});
                item->setData(0, Qt::UserRole, name);
                item->setData(0, Qt::UserRole + 1, i);
                item->setToolTip(
                    0, QString("%1 · %2 · floor %3").arg(name).arg(mp.name).arg(mp.floor));
            }
        }
    maps->expandToDepth(0);
    left->setMinimumWidth(170);
    split->addWidget(left);
    auto center = new QWidget;
    auto centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    auto row = new QHBoxLayout;
    page = new QComboBox;
    page->setToolTip("Switch floors within the current location. Choose another location in the "
                     "map list; edits and view state are preserved.");
    page->setObjectName("mapPage");
    populateFloors(qBound(0, navigation->value(resource + "/map"), int(document.pages.size() - 1)));
    row->addWidget(page, 1);
    auto exportButton = new QPushButton("Export");
    exportButton->setToolTip("Export terrain, tile IDs, or the visible preview as a PNG.");
    auto exportMenu = new QMenu(exportButton);
    exportMenu->addAction("Terrain PNG", this, [this] { exportImage(false); })
        ->setToolTip("Export map terrain artwork without grid or character overlays.");
    exportMenu->addAction("Tile-ID PNG", this, [this] { exportImage(true); })
        ->setToolTip(
            "Export one grayscale pixel per tile containing its ID, for lossless terrain import.");
    exportMenu->setToolTipsVisible(true);
    auto previewExport = exportMenu->addAction("Visible preview PNG (includes overlays)", this,
                                               [this] { exportImage(false, true); });
    previewExport->setToolTip("Export the visible canvas, including grid, markers and trigger "
                              "preview. Not suitable for terrain import.");
    exportButton->setMenu(exportMenu);
    row->addWidget(exportButton);
    centerLayout->addLayout(row);
    auto tools = new QHBoxLayout;
    tool = new QButtonGroup(this);
    tool->setObjectName("mapTools");
    QString companion = resource.left(resource.size() - 4) + ".NPC";
    const QStringList names{
        "Pencil", "Eyedropper", "Pan", resource.endsWith(".CBT") ? "Encounter" : "NPCs",
        "Select", "Rectangle",  "Fill"};
    const QStringList descriptions{
        "Pencil (B): paint tiles with the current brush.",
        "Eyedropper (I): pick a tile from the map as your brush.",
        "Pan: drag to move the view without changing the map. Space or middle-drag also pans.",
        "Characters: select and move NPC destinations or encounter markers.",
        "Select (V): drag a rectangle to copy or replace terrain.",
        "Rectangle (R): draw a filled rectangle with the current brush.",
        "Fill (F): replace connected tiles of the same kind with the current brush."};
    for (int i = 0; i < names.size(); ++i) {
        auto button = new QToolButton;
        button->setObjectName(QString("mapTool%1").arg(i));
        button->setAccessibleName(names[i]);
        button->setToolTip(descriptions[i]);
        button->setCheckable(true);
        button->setFixedSize(28, 28);
        button->setStyleSheet("QToolButton:checked { background:#365f8d; border:2px solid #86b9ed; "
                              "border-radius:3px; }");
        button->setIconSize({22, 22});
        QPixmap icon(24, 24);
        icon.fill(Qt::transparent);
        QPainter painter(&icon);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor("#e0e8f0"), 2));
        if (i == MapCanvas::Pencil) {
            painter.drawLine(5, 19, 18, 6);
            painter.drawLine(8, 21, 21, 8);
            painter.drawLine(5, 19, 4, 22);
            painter.drawLine(18, 6, 21, 8);
        } else if (i == MapCanvas::Eyedropper) {
            painter.drawLine(5, 19, 17, 7);
            painter.drawLine(8, 21, 20, 9);
            painter.drawLine(14, 5, 22, 13);
            painter.drawLine(5, 19, 8, 21);
        } else if (i == MapCanvas::Pan) {
            painter.drawLine(12, 2, 12, 22);
            painter.drawLine(2, 12, 22, 12);
            painter.drawPolyline(QPolygon{{8, 6}, {12, 2}, {16, 6}});
            painter.drawPolyline(QPolygon{{8, 18}, {12, 22}, {16, 18}});
            painter.drawPolyline(QPolygon{{6, 8}, {2, 12}, {6, 16}});
            painter.drawPolyline(QPolygon{{18, 8}, {22, 12}, {18, 16}});
        } else if (i == MapCanvas::InspectNpc) {
            painter.drawEllipse(8, 2, 8, 8);
            painter.drawArc(4, 11, 16, 16, 0, 180 * 16);
            painter.drawLine(4, 19, 20, 19);
        } else if (i == MapCanvas::Select) {
            painter.setPen(QPen(QColor("#e0e8f0"), 2, Qt::DashLine));
            painter.drawRect(3, 3, 18, 18);
        } else if (i == MapCanvas::Rectangle) {
            painter.setBrush(QColor("#729ac4"));
            painter.drawRect(3, 5, 18, 14);
        } else {
            painter.drawPolygon(QPolygon{{3, 12}, {12, 3}, {20, 11}, {11, 20}});
            painter.drawLine(6, 12, 18, 12);
            painter.setBrush(QColor("#729ac4"));
            painter.drawEllipse(18, 17, 4, 5);
        }
        painter.end();
        button->setIcon(QIcon(icon));
        tool->addButton(button, i);
        if (i == MapCanvas::InspectNpc && !project->resources.contains(companion) &&
            !resource.endsWith(".CBT")) {
            button->setEnabled(false);
            button->setToolTip("Characters: this map has no NPC schedule or encounter data.");
        }
        tools->addWidget(button);
    }
    schedule = new QComboBox;
    schedule->setObjectName("mapSchedule");
    schedule->setAccessibleName("NPC schedule change");
    schedule->addItems({"1", "2", "3", "4"});
    schedule->setToolTip("Choose which scheduled NPC destinations to show and edit. Hours appear "
                         "when NPCs on this map agree; otherwise changes are numbered 1–4. Changes "
                         "2 and 4 share destinations and behavior.");
    schedule->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    if (project->resources.contains(companion)) {
        auto scheduleLabel = new QLabel("NPC schedule:");
        scheduleLabel->setBuddy(schedule);
        scheduleLabel->setToolTip(schedule->toolTip());
        row->insertWidget(1, scheduleLabel);
        row->insertWidget(2, schedule);
    } else
        schedule->hide();
    tools->addStretch();
    centerLayout->addLayout(tools);
    zoom = new QComboBox;
    zoom->setObjectName("mapZoom");
    zoom->addItems({"Fit", "100%", "200%", "300%", "400%", "800%"});
    zoom->setToolTip(
        "Magnify tiles, or fit the whole map in the view. Ctrl+mouse wheel also zooms.");
    auto copy = new QPushButton("Copy");
    copy->setToolTip("Copy the selected rectangle of terrain (Ctrl+C).");
    copy->setObjectName("mapCopy");
    auto paste = new QPushButton("Paste");
    paste->setToolTip("Place copied terrain on the map; click to apply, Esc to cancel (Ctrl+V).");
    paste->setObjectName("mapPaste");
    auto clear = new QPushButton("Clear selection");
    clear->setToolTip("Remove the selection boundary without deleting terrain.");
    clear->setObjectName("mapClearSelection");
    tools->insertWidget(tools->count() - 1, copy);
    tools->insertWidget(tools->count() - 1, paste);
    tools->insertWidget(tools->count() - 1, clear);
    comparison = new QCheckBox("Highlight changes");
    comparison->setObjectName("mapComparison");
    comparison->setToolTip("Orange marks terrain that differs from original game files");
    tools->insertWidget(tools->count() - 1, comparison);
    tools->addWidget(zoom);
    canvas = new MapCanvas;
    // The small location icons are intended for world maps, including unused ruins.
    for (int id = TILE_MAP_HUT; id <= TILE_MAP_LIGHTHOUSE; ++id)
        overworldTiles.insert(id);
    overworldTiles.insert(TILE_MAP_PALACEBT);
    overworldTiles.insert(TILE_MAP_CASTLELB);
    for (const QString &name : {QString("BRIT.DAT"), QString("UNDER.DAT")}) {
        if (!project->resources.contains(name))
            continue;
        MapDocument world(project, name);
        for (bool original : {false, true})
            for (unsigned char id : world.terrain(0, original))
                overworldTiles.insert(id);
    }
    // These families animate in the engine even when only one frame is stored in a map.
    for (auto family : {QPair<int, int>{0xd4, 0xd8},
                        {0xd8, 0xdc},
                        {0xec, 0xf0},
                        {0x80, 0x82},
                        {0x82, 0x84},
                        {0xfa, 0xfc},
                        {0xfc, 0xfe}}) {
        bool used = false;
        for (int id = family.first; id < family.second; ++id)
            used |= overworldTiles.contains(id);
        if (used)
            for (int id = family.first; id < family.second; ++id)
                overworldTiles.insert(id);
    }
    canvas->tiles = U5::readGraphics("TILES.16", project->data("TILES.16")).images;
    view = new MapView(canvas);
    view->setToolTip("Edit terrain or characters with the selected tool. Middle-drag or Space "
                     "pans; Ctrl+wheel zooms.");
    centerLayout->addWidget(view, 1);
    auto coordinates = new QHBoxLayout;
    coordinate = new QLabel;
    coordinate->setToolTip("Map coordinates and terrain tile under the pointer.");
    coordinate->setObjectName("mapCoordinates");
    coordinates->addWidget(coordinate, 1);
    auto x = new QSpinBox;
    x->setToolTip("Horizontal tile coordinate to center on; zero is the left edge.");
    auto y = new QSpinBox;
    y->setToolTip("Vertical tile coordinate to center on; zero is the top edge.");
    x->setObjectName("mapGoX");
    y->setObjectName("mapGoY");
    coordinates->addWidget(new QLabel("X"));
    coordinates->addWidget(x);
    coordinates->addWidget(new QLabel("Y"));
    coordinates->addWidget(y);
    auto go = new QPushButton("Go");
    go->setToolTip("Center the map on these coordinates without changing any tiles.");
    coordinates->addWidget(go);
    connect(go, &QPushButton::clicked, this, [=] {
        view->centerMap({double(x->value()) + 0.5, double(y->value()) + 0.5});
        canvas->setFocus();
    });
    centerLayout->addLayout(coordinates);
    auto hints = new QLabel(
        "Left-drag paints · Right-click picks · Middle-drag / Space pans · "
        "Ctrl+wheel zooms · Arrows target / Enter applies · Shift+arrows select · Esc cancels");
    hints->setWordWrap(true);
    centerLayout->addWidget(hints);
    split->addWidget(center);
    auto right = new QWidget;
    auto rightOuter = new QVBoxLayout(right);
    rightOuter->setContentsMargins(0, 0, 0, 0);
    inspector = new QStackedWidget;
    rightOuter->addWidget(inspector);
    auto terrainInspector = new QWidget;
    inspector->addWidget(terrainInspector);
    auto rightLayout = new QVBoxLayout(terrainInspector);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    brushImage = new QLabel;
    brushImage->setToolTip(
        "Current terrain brush artwork. Select a palette tile or use the Eyedropper to change it.");
    brushImage->setFixedHeight(70);
    brushImage->setAlignment(Qt::AlignCenter);
    rightLayout->addWidget(brushImage);
    brushLabel = new QLabel;
    brushLabel->setToolTip("Name and ID of the tile that painting tools will place.");
    brushLabel->setObjectName("mapBrushLabel");
    brushLabel->setAlignment(Qt::AlignCenter);
    brushLabel->setWordWrap(true);
    rightLayout->addWidget(brushLabel);
    selectionLabel = new QLabel;
    selectionLabel->setToolTip(
        "Selected rectangle and its size; selection itself does not change terrain.");
    selectionLabel->setObjectName("mapSelectionInfo");
    selectionLabel->setWordWrap(true);
    rightLayout->addWidget(selectionLabel);
    minimap = new MapMinimap(canvas, view);
    minimap->setToolTip("Click or drag to move the main map view.");
    rightLayout->addWidget(minimap);
    auto brushControls = new QHBoxLayout;
    paletteFilter = new QComboBox;
    paletteFilter->setToolTip(
        "Filter tiles by category, your favorites, or recently used brushes.");
    paletteFilter->setObjectName("mapPaletteFilter");
    paletteFilter->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    paletteFilter->setMinimumContentsLength(8);
    paletteFilter->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    paletteFilter->addItems({"All tiles", "Favorites", "Recent", "Overworld", "Ground", "Buildings",
                             "Objects", "Other"});
    const QStringList categoryHelp{
        "Every terrain tile.",
        "Your favorite brushes.",
        "Recently used brushes.",
        "Tiles used in Britannia or the Underworld, including their animation frames. Also "
        "includes small world-map location icons. Overworld can overlap Ground, Buildings, "
        "Objects, or Other.",
        "Terrain and floor surfaces: water, grass, roads, forests, rock, lava, carpets, and "
        "paving.",
        "Building components: walls, towers, doors, windows, bridges, stairs, and ladders.",
        "Furniture, fixtures, plants, signs, and other placeable objects.",
        "Special effects, rendering masks, and other nonstandard tiles."};
    for (int i = 0; i < categoryHelp.size(); ++i)
        paletteFilter->setItemData(i, categoryHelp[i], Qt::ToolTipRole);
    brushControls->addWidget(paletteFilter, 1);
    favorite = new QPushButton("Favorite");
    favorite->setToolTip("Add or remove the current brush from your favorites.");
    favorite->setObjectName("mapFavorite");
    favorite->setCheckable(true);
    brushControls->addWidget(favorite);
    rightLayout->addLayout(brushControls);
    tileSearch = new QLineEdit;
    tileSearch->setToolTip("Find tiles by name, decimal number, or hexadecimal ID.");
    tileSearch->setPlaceholderText("Find tile by name or number");
    tileSearch->setObjectName("mapTileSearch");
    tileSearch->setAccessibleName("Find terrain tile");
    paletteFilter->setAccessibleName("Tile category");
    rightLayout->addWidget(tileSearch);
    palette = new QListWidget;
    palette->setToolTip(
        "Select a terrain brush. Hover over a tile for its name, category, and ID.");
    palette->setObjectName("mapPalette");
    palette->setViewMode(QListView::IconMode);
    palette->setResizeMode(QListView::Adjust);
    palette->setIconSize({32, 32});
    palette->setGridSize({40, 40});
    palette->setSpacing(0);
    for (int i = 0; i < 256; ++i) {
        auto item = new QListWidgetItem(
            QIcon(QPixmap::fromImage(canvas->tiles[i])
                      .scaled(32, 32, Qt::KeepAspectRatio, Qt::FastTransformation)),
            QString(), palette);
        item->setData(Qt::AccessibleTextRole,
                      QString("%1 · Tile %2").arg(MapDocument::tileName(i)).arg(i));
        item->setToolTip(QString("%1\n%2 · Tile %3 (0x%4)\nDescription from DOS artwork and engine "
                                 "definitions; appearance may differ with custom artwork.")
                             .arg(MapDocument::tileName(i))
                             .arg(tileCategories(i).join(" · "))
                             .arg(i)
                             .arg(i, 2, 16, QChar('0')));
    }
    rightLayout->addWidget(palette, 1);
    npcResource = companion;
    talkResource = resource.left(resource.size() - 4) + ".TLK";
    auto npcPanel = new QScrollArea;
    npcPanel->setWidgetResizable(true);
    auto npcContent = new QWidget;
    auto npcLayout = new QVBoxLayout(npcContent);
    if (resource.endsWith(".CBT"))
        createCombatInspector(npcLayout);
    else
        createNpcInspector(npcLayout);
    npcPanel->setWidget(npcContent);
    inspector->addWidget(npcPanel);
    right->setMinimumWidth(220);
    split->addWidget(right);
    split->setStretchFactor(1, 1);
    split->setSizes({190, 780, 220});
    connect(tileSearch, &QLineEdit::textChanged, this, [this] { filterPalette(); });
    connect(paletteFilter, &QComboBox::currentIndexChanged, this, [this] { filterPalette(); });
    connect(favorite, &QPushButton::toggled, this, [this](bool enabled) {
        int id = canvas->brush;
        brushes->favorites.removeAll(id);
        if (enabled)
            brushes->favorites.append(id);
        filterPalette();
    });
    connect(copy, &QPushButton::clicked, this, [this] { canvas->copySelection(); });
    connect(paste, &QPushButton::clicked, this, [this] {
        canvas->setFocus();
        canvas->beginPaste();
    });
    connect(clear, &QPushButton::clicked, this, [this] { canvas->clearSelection(); });
    connect(comparison, &QCheckBox::toggled, this, [this](bool enabled) {
        canvas->comparison = enabled;
        canvas->update();
        minimap->refresh();
        saveView();
    });
    canvas->selectionChanged = [this] {
        updateSelection();
        minimap->update();
        saveView();
    };
    canvas->feedback = [this](const QString &message) { coordinate->setText(message); };
    canvas->changed = [this] {
        minimap->refresh();
        updateSelection();
    };
    connect(search, &QLineEdit::textChanged, this, [=](const QString &text) {
        std::function<bool(QTreeWidgetItem *, bool)> filter = [&](QTreeWidgetItem *item,
                                                                  bool parentMatch) {
            bool match = parentMatch || item->text(0).contains(text, Qt::CaseInsensitive) ||
                         item->data(0, Qt::UserRole).toString().contains(text, Qt::CaseInsensitive);
            bool child = false;
            for (int i = 0; i < item->childCount(); ++i)
                child = filter(item->child(i), match) || child;
            item->setHidden(!match && !child);
            if (!text.isEmpty() && child)
                item->setExpanded(true);
            return match || child;
        };
        for (int i = 0; i < maps->topLevelItemCount(); ++i)
            filter(maps->topLevelItem(i), false);
    });
    connect(maps, &QTreeWidget::currentItemChanged, this, [=](QTreeWidgetItem *item) {
        if (restoring || !item)
            return;
        auto name = item->data(0, Qt::UserRole).toString();
        if (name.isEmpty())
            return;
        int index = item->data(0, Qt::UserRole + 1).toInt();
        if (name == resource)
            selectPage(index);
        else {
            cancelGesture();
            saveView();
            (*navigation)[name + "/map"] = index;
            navigate(name);
        }
    });
    connect(page, &QComboBox::currentIndexChanged, this, [this] {
        if (page->currentIndex() >= 0)
            selectPage(currentPage());
    });
    connect(tool, &QButtonGroup::idClicked, this, [=](int i) {
        cancelGesture();
        canvas->tool = i;
        inspector->setCurrentIndex(i == MapCanvas::InspectNpc ? 1 : 0);
        if (resource.endsWith(".CBT"))
            refreshCombat();
        findChild<QPushButton *>("mapPaste")->setEnabled(i != MapCanvas::InspectNpc);
        canvas->update();
        canvas->setCursor(i == 2 ? Qt::OpenHandCursor : Qt::CrossCursor);
        saveView();
    });
    connect(schedule, &QComboBox::currentIndexChanged, this, [=] {
        saveView();
        loadPage();
    });
    connect(zoom, &QComboBox::currentIndexChanged, this, [=](int i) {
        if (restoring)
            return;
        if (i == 0)
            view->fitMap();
        else
            view->setZoom(QVector<double>{1, 2, 3, 4, 8}[i - 1]);
    });
    connect(palette, &QListWidget::currentRowChanged, this, [=](int i) {
        if (i >= 0)
            setBrush(i);
    });
    canvas->pick = [=](int id) {
        tileSearch->clear();
        paletteFilter->setCurrentIndex(0);
        setBrush(id);
        palette->scrollToItem(palette->item(id));
    };
    canvas->chooseTool = [=](int i) {
        if (auto button = tool->button(i); button && button->isEnabled())
            button->click();
    };
    canvas->inspect = [=](int cx, int cy, int id) {
        coordinate->setText(QString("X %1   Y %2   Tile %3   ·   %4")
                                .arg(cx)
                                .arg(cy)
                                .arg(id)
                                .arg(tool->checkedButton()->accessibleName()));
    };
    canvas->chooseNpc = [this](int npc) { selectNpc(npc); };
    canvas->chooseActor = [this](const QList<int> &candidates) {
        QStringList names;
        for (int npc : candidates)
            names.append(resource.endsWith(".CBT")
                             ? combatEntity->itemText(npc)
                             : QString("Character %1 · %2").arg(npc + 1).arg(npcName(npc)));
        bool ok = false;
        QString selected =
            QInputDialog::getItem(this, "Choose NPC", "Actors on this tile", names, 0, false, &ok);
        canvas->setFocus();
        return ok ? candidates[names.indexOf(selected)] : -1;
    };
    canvas->moveNpc = [this](int npc, QPoint destination) {
        if (resource.endsWith(".CBT"))
            return moveCombat(npc, destination);
        return moveNpcTo(npc, destination.x(), destination.y(),
                         document.pages[currentPage()].floor);
    };
    canvas->commit = [=](const QByteArray &ids) {
        try {
            return commit(document.changes(currentPage(), ids),
                          canvas->operation + " " + document.pages[currentPage()].name);
        } catch (const std::exception &e) {
            QMessageBox::warning(this, "Map edit rejected", QString::fromUtf8(e.what()));
            return false;
        }
    };
    view->changed = [this] {
        if (!restoring) {
            updateZoom();
            saveView();
            minimap->update();
        }
    };
    loadPage();
    x->setRange(0, canvas->side - 1);
    y->setRange(0, canvas->side - 1);
}
int MapWorkspace::currentPage() const { return page->currentData().toInt(); }
void MapWorkspace::populateFloors(int index) {
    QSignalBlocker block(page);
    page->clear();
    const auto &current = document.pages[index];
    for (int i = 0; i < document.pages.size(); ++i) {
        const auto &candidate = document.pages[i];
        if (current.settlement >= 0 ? candidate.settlement == current.settlement : i == index) {
            QString label = candidate.settlement < 0 ? candidate.name
                            : candidate.floor < 0    ? "Basement"
                            : candidate.floor == 0   ? "Ground floor"
                                                   : QString("Upper floor %1").arg(candidate.floor);
            page->addItem(label, i);
        }
    }
    page->setCurrentIndex(page->findData(index));
}
void MapWorkspace::selectPage(int index) {
    if (index < 0 || index >= document.pages.size())
        return;
    cancelGesture();
    saveView();
    int oldPage = navigation->value(resource + "/map");
    if (canvas->tool == MapCanvas::InspectNpc && canvas->selectedNpc >= 0 &&
        document.pages[oldPage].settlement == document.pages[index].settlement) {
        auto &target = (*states)[resource + "/" + QString::number(index)];
        target.tool = MapCanvas::InspectNpc;
        target.npc = canvas->selectedNpc;
        target.schedule = schedule->currentIndex();
        target.ghosts = npcGhosts->isChecked();
    }
    (*navigation)[resource + "/map"] = index;
    populateFloors(index);
    loadPage();
    findChild<QSpinBox *>("mapGoX")->setRange(0, canvas->side - 1);
    findChild<QSpinBox *>("mapGoY")->setRange(0, canvas->side - 1);
}
void MapWorkspace::cancelGesture() { canvas->cancelStroke(); }
void MapWorkspace::setBrush(int id) {
    canvas->brush = qBound(0, id, 255);
    {
        QSignalBlocker block(palette);
        palette->setCurrentRow(canvas->brush);
    }
    brushImage->setPixmap(QPixmap::fromImage(canvas->tiles[canvas->brush])
                              .scaled(64, 64, Qt::KeepAspectRatio, Qt::FastTransformation));
    brushLabel->setText(QString("%1\nTile %2 · %3")
                            .arg(MapDocument::tileName(canvas->brush))
                            .arg(canvas->brush)
                            .arg(tileCategories(canvas->brush).join(" · ")));
    {
        QSignalBlocker block(favorite);
        favorite->setChecked(brushes->favorites.contains(canvas->brush));
    }
    if (!restoring) {
        brushes->recent.removeAll(canvas->brush);
        brushes->recent.prepend(canvas->brush);
        while (brushes->recent.size() > 16)
            brushes->recent.removeLast();
    }
    filterPalette();
    canvas->update();
    saveView();
}
QStringList MapWorkspace::tileCategories(int id) const {
    QStringList categories{MapDocument::tileCategory(id)};
    if (overworldTiles.contains(id))
        categories.prepend("Overworld");
    return categories;
}
void MapWorkspace::filterPalette() {
    QString query = tileSearch->text().trimmed();
    for (int i = 0; i < 256; ++i) {
        bool matches =
            query.isEmpty() || MapDocument::tileName(i).contains(query, Qt::CaseInsensitive) ||
            tileCategories(i).join(" ").contains(query, Qt::CaseInsensitive) ||
            QString::number(i).contains(query) ||
            QString("0x%1").arg(i, 2, 16, QChar('0')).contains(query, Qt::CaseInsensitive);
        bool group =
            paletteFilter->currentIndex() == 0 ||
            (paletteFilter->currentIndex() < 3
                 ? (paletteFilter->currentIndex() == 1 ? brushes->favorites : brushes->recent)
                       .contains(i)
                 : tileCategories(i).contains(paletteFilter->currentText()));
        palette->item(i)->setHidden(!matches || !group);
    }
}
void MapWorkspace::updateSelection() {
    int changed = 0;
    if (canvas->ids.size() == canvas->original.size())
        for (int i = 0; i < canvas->ids.size(); ++i)
            changed += canvas->ids[i] != canvas->original[i];
    auto area = canvas->selection;
    selectionLabel->setText((area.isEmpty() ? QString("No selection")
                                            : QString("Selection: %1 × %2 at %3, %4")
                                                  .arg(area.width())
                                                  .arg(area.height())
                                                  .arg(area.x())
                                                  .arg(area.y())) +
                            QString("\n%1 changed terrain cells").arg(changed));
}
void MapWorkspace::updateZoom() {
    QSignalBlocker block(zoom);
    int index = view->fitting ? 0 : QVector<double>{1, 2, 3, 4, 8}.indexOf(canvas->zoom) + 1;
    if (index == 0 && !view->fitting) {
        zoom->setCurrentIndex(-1);
        zoom->setPlaceholderText(QString("%1%").arg(qRound(canvas->zoom * 100)));
    } else
        zoom->setCurrentIndex(index);
}
void MapWorkspace::saveView() {
    if (restoring || key.isEmpty())
        return;
    auto &s = (*states)[key];
    s.zoom = canvas->zoom;
    s.fit = view->fitting;
    s.grid = canvas->grid;
    s.selection = canvas->selection;
    s.comparison = canvas->comparison;
    s.outline = canvas->outline;
    s.keyboardCell = canvas->keyboardCell;
    s.brush = canvas->brush;
    s.tool = canvas->tool;
    s.schedule = schedule->currentIndex();
    s.npc = canvas->selectedNpc;
    s.ghosts = npcGhosts->isChecked();
    if (isVisible())
        s.center = view->mapCenter();
}
void MapWorkspace::loadPage() {
    restoring = true;
    key = resource + "/" + QString::number(currentPage());
    auto state = states->value(key);
    const auto &mp = document.pages[currentPage()];
    canvas->side = mp.side;
    canvas->ids = document.terrain(currentPage());
    canvas->original = document.terrain(currentPage(), true);
    canvas->keyboardCell = state.keyboardCell;
    canvas->selection = state.selection.intersected(QRect(0, 0, mp.side, mp.side));
    canvas->comparison = state.comparison;
    canvas->outline = false;
    {
        QSignalBlocker block(comparison);
        comparison->setChecked(state.comparison);
    }
    canvas->zoom = state.zoom;
    canvas->grid = false;
    canvas->tool = qBound(0, state.tool, tool->buttons().size() - 1);
    {
        QSignalBlocker block(tool);
        tool->button(canvas->tool)->setChecked(true);
    }
    {
        QSignalBlocker block(schedule);
        schedule->setCurrentIndex(state.schedule);
    }
    canvas->selectedNpc =
        state.npc >= 0
            ? state.npc
            : navigation->value(resource + "/actor/" + QString::number(mp.settlement), -1);
    {
        QSignalBlocker block(npcGhosts);
        npcGhosts->setChecked(state.ghosts);
    }
    inspector->setCurrentIndex(canvas->tool == MapCanvas::InspectNpc ? 1 : 0);
    findChild<QPushButton *>("mapPaste")->setEnabled(canvas->tool != MapCanvas::InspectNpc);
    refreshNpcs();
    setBrush(state.brush);
    canvas->setCursor(canvas->tool == 2 ? Qt::OpenHandCursor : Qt::CrossCursor);
    canvas->resizeMap();
    minimap->refresh();
    updateSelection();
    view->fitting = state.fit;
    updateZoom();
    coordinate->setText(QString("%1 · %2 × %2 tiles").arg(mp.name).arg(mp.side));
    {
        QSignalBlocker block(maps);
        QTreeWidgetItemIterator it(maps);
        while (*it) {
            auto item = *it;
            if (item->childCount() == 0 && item->data(0, Qt::UserRole).toString() == resource &&
                item->data(0, Qt::UserRole + 1).toInt() == currentPage()) {
                maps->setCurrentItem(item);
                maps->scrollToItem(item);
                break;
            }
            ++it;
        }
    }
    QString loadedKey = key;
    QTimer::singleShot(0, this, [this, state, loadedKey] {
        if (key != loadedKey)
            return;
        if (state.fit)
            view->fitMap();
        else if (state.center.x() >= 0)
            view->centerMap(state.center);
        else
            view->centerMap({canvas->side / 2.0, canvas->side / 2.0});
        restoring = false;
        updateZoom();
        saveView();
    });
}
QString MapWorkspace::npcName(int npc) const {
    MapNpcDocument npcs(project, npcResource);
    int id = npcs.dialogue(document.pages[currentPage()].settlement, npc);
    return npcNames.value(id, QString("Unnamed character %1").arg(npc + 1));
}
void MapWorkspace::createNpcInspector(QVBoxLayout *layout) {
    auto title = new QLabel("NPC schedule");
    layout->addWidget(title);
    npcList = new QComboBox;
    npcList->setToolTip(
        "Select a character, including characters whose current destination is on another floor.");
    npcList->setObjectName("mapNpcList");
    npcList->setAccessibleName("Character");
    npcList->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    npcList->setMinimumContentsLength(12);
    npcList->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    layout->addWidget(npcList);
    npcSprite = new QLabel;
    npcSprite->setToolTip("Artwork used for the selected character.");
    npcSprite->setAlignment(Qt::AlignCenter);
    layout->addWidget(npcSprite);
    npcInfo = new QLabel;
    npcInfo->setToolTip("Details of the selected character and schedule change; shared "
                        "destinations are identified here.");
    npcInfo->setObjectName("mapNpcInfo");
    npcInfo->setWordWrap(true);
    npcInfo->setTextFormat(Qt::PlainText);
    npcInfo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    npcInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(npcInfo);
    auto form = new QFormLayout;
    npcStartHour = new QSpinBox;
    npcStartHour->setObjectName("mapNpcStartHour");
    npcStartHour->setRange(0, 255);
    npcStartHour->setSuffix(":00");
    npcStartHour->setToolTip("Start hour for the selected schedule change. Normal hours are 0–23; "
                             "existing unusual values are preserved.");
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->addRow("Start hour", npcStartHour);
    npcX = new QSpinBox;
    npcX->setToolTip("Destination column for the selected schedule change. Changes 2 and 4 share a "
                     "destination.");
    npcY = new QSpinBox;
    npcY->setToolTip(
        "Destination row for the selected schedule change. Changes 2 and 4 share a destination.");
    npcFloor = new QSpinBox;
    npcFloor->setToolTip(
        "Destination floor: -1 is basement, 0 is ground floor. Click Move here to apply.");
    npcX->setObjectName("mapNpcX");
    npcY->setObjectName("mapNpcY");
    npcFloor->setObjectName("mapNpcFloor");
    npcX->setRange(0, 255);
    npcY->setRange(0, 255);
    npcFloor->setRange(-128, 127);
    form->addRow("X", npcX);
    form->addRow("Y", npcY);
    form->addRow("Floor", npcFloor);
    layout->addLayout(form);
    auto apply = new QPushButton("Move here");
    apply->setToolTip("Apply the destination X/Y/floor entered above");
    apply->setObjectName("mapNpcMove");
    layout->addWidget(apply);
    auto locate = new QPushButton("Locate on map");
    locate->setToolTip("Center the view on the selected destination without moving the character.");
    locate->setObjectName("mapNpcLocate");
    layout->addWidget(locate);
    npcGhosts = new QCheckBox("Other destinations");
    npcGhosts->setObjectName("mapNpcGhosts");
    npcGhosts->setToolTip("Show the selected character’s other scheduled destinations. Numbers "
                          "correspond to schedule changes 1–4.");
    layout->addWidget(npcGhosts);
    npcConversation = new QPushButton("Edit conversation");
    npcConversation->setToolTip("Open this character’s dialogue for editing.");
    npcConversation->setObjectName("mapNpcConversation");
    layout->addWidget(npcConversation);
    auto advanced = new QPushButton("Advanced schedule details");
    advanced->setToolTip("Edit behavior and other schedule fields; unknown values are preserved.");
    advanced->setObjectName("mapNpcAdvanced");
    layout->addWidget(advanced);
    auto hint =
        new QLabel("Select a character, then drag to preview its destination. Release to move; "
                   "Esc cancels. Numbers identify schedule changes 1–4, not simulated movement.");
    hint->setWordWrap(true);
    hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(hint);
    layout->addStretch();
    connect(npcStartHour, &QSpinBox::editingFinished, this, [this] {
        if (restoring || canvas->selectedNpc < 0)
            return;
        const int settlement = document.pages[currentPage()].settlement;
        const int offset =
            settlement * 576 + canvas->selectedNpc * 16 + 12 + schedule->currentIndex();
        auto bytes = project->data(npcResource);
        if (U5::byte(bytes, offset) == npcStartHour->value())
            return;
        if (npcStartHour->value() > 23) {
            npcStartHour->setValue(U5::byte(bytes, offset));
            coordinate->setText("Choose a start hour from 0 to 23. Unusual stored hours can be "
                                "edited in Advanced schedule details.");
            return;
        }
        bytes[offset] = char(npcStartHour->value());
        if (commitResources({{npcResource, bytes}}, "Change NPC schedule start hour")) {
            refreshNpcs();
            saveView();
        }
    });
    connect(npcList, &QComboBox::currentIndexChanged, this, [this](int npc) {
        if (!restoring && npc >= 0)
            selectNpc(npc);
    });
    connect(npcGhosts, &QCheckBox::toggled, this, [this] {
        refreshNpcs();
        saveView();
    });
    connect(locate, &QPushButton::clicked, this, [this] { locateNpc(); });
    connect(apply, &QPushButton::clicked, this, [this] {
        if (moveNpcTo(canvas->selectedNpc, npcX->value(), npcY->value(), npcFloor->value()))
            locateNpc();
    });
    connect(advanced, &QPushButton::clicked, this, [this] {
        if (canvas->selectedNpc < 0)
            return;
        saveView();
        (*navigation)[npcResource + "/settlement"] = document.pages[currentPage()].settlement;
        (*navigation)[npcResource + "/npc"] = canvas->selectedNpc;
        navigateResource(npcResource);
    });
    connect(npcConversation, &QPushButton::clicked, this, [this] {
        if (canvas->selectedNpc < 0 || !project->resources.contains(talkResource))
            return;
        MapNpcDocument npcs(project, npcResource);
        int id = npcs.dialogue(document.pages[currentPage()].settlement, canvas->selectedNpc);
        auto conversations = U5::readDialogue(project->data(talkResource));
        for (int i = 0; i < conversations.size(); ++i)
            if (int(conversations[i].id) == id) {
                saveView();
                (*navigation)[talkResource + "/conversation"] = i;
                (*navigation)[talkResource + "/entry"] = 0;
                navigateResource(talkResource);
                return;
            }
    });
}
void MapWorkspace::refreshNpcs() {
    if (resource.endsWith(".CBT")) {
        refreshCombat();
        return;
    }
    canvas->actors.clear();
    canvas->ghosts.clear();
    const auto &mp = document.pages[currentPage()];
    QSignalBlocker blocked(npcList);
    npcList->clear();
    npcConversation->setEnabled(false);
    for (auto name : {"mapNpcMove", "mapNpcLocate", "mapNpcAdvanced"})
        findChild<QPushButton *>(name)->setEnabled(canvas->selectedNpc >= 0 && mp.settlement >= 0 &&
                                                   project->resources.contains(npcResource));
    if (mp.settlement < 0 || !project->resources.contains(npcResource) ||
        project->data(npcResource).size() != 4608) {
        npcInfo->setText("No valid NPC schedule resource for this map");
        return;
    }
    npcNames.clear();
    conversationIndices.clear();
    try {
        if (project->resources.contains(talkResource)) {
            auto conversations = U5::readDialogue(project->data(talkResource));
            for (int i = 0; i < conversations.size(); ++i) {
                int id = conversations[i].id;
                conversationIndices[id] = i;
                auto doc = Dialogue::parse(conversations[i].bytes);
                if (!doc.entries.isEmpty()) {
                    auto name = Dialogue::plain(doc.entries[0].bytes).simplified();
                    if (!name.isEmpty())
                        npcNames[id] = name.left(60);
                }
            }
        }
    } catch (const std::exception &) {
    }
    MapNpcDocument npcs(project, npcResource);
    int slot = schedule->currentIndex();
    for (int npc = 0; npc < 32; ++npc) {
        npcList->addItem(QString("%1 · %2").arg(npc + 1).arg(npcName(npc)));
        auto loc = npcs.location(mp.settlement, npc, slot);
        if (loc.floor == mp.floor && loc.x < 32 && loc.y < 32)
            canvas->actors.append({loc.x, loc.y, npcs.sprite(mp.settlement, npc), npc});
    }
    {
        int hours[4]{-1, -1, -1, -1};
        bool consistent = true;
        for (int actor = 0; actor < 32; ++actor) {
            bool onMap = false;
            for (int change = 0; change < 4; ++change) {
                auto loc = npcs.location(mp.settlement, actor, change);
                onMap |= loc.floor == mp.floor && loc.x < 32 && loc.y < 32;
            }
            if (!onMap)
                continue;
            for (int change = 0; change < 4; ++change) {
                int hour = npcs.hour(mp.settlement, actor, change);
                if (hour >= 24 || (hours[change] >= 0 && hours[change] != hour))
                    consistent = false;
                hours[change] = hour;
            }
        }
        QSignalBlocker block(schedule);
        for (int change = 0; change < 4; ++change)
            schedule->setItemText(change,
                                  consistent && hours[change] >= 0
                                      ? QString("%1:00").arg(hours[change], 2, 10, QChar('0'))
                                      : QString::number(change + 1));
    }
    int npc = canvas->selectedNpc;
    npcList->setCurrentIndex(npc);
    if (npc < 0 || npc >= 32) {
        npcInfo->setText("Select a character on the map or choose one above");
        npcSprite->clear();
        npcStartHour->setEnabled(false);
        canvas->update();
        return;
    }
    auto loc = npcs.location(mp.settlement, npc, slot);
    npcStartHour->setEnabled(true);
    npcStartHour->setValue(npcs.hour(mp.settlement, npc, slot));
    npcX->setValue(loc.x);
    npcY->setValue(loc.y);
    npcFloor->setValue(loc.floor);
    int sprite = npcs.sprite(mp.settlement, npc), id = npcs.dialogue(mp.settlement, npc);
    npcSprite->setPixmap(QPixmap::fromImage(canvas->tiles[sprite])
                             .scaled(64, 64, Qt::KeepAspectRatio, Qt::FastTransformation));
    QString info =
        QString("%1\nSchedule change %2 starts at %3\n")
            .arg(npcName(npc))
            .arg(slot + 1)
            .arg(npcs.hour(mp.settlement, npc, slot) < 24
                     ? QString("%1:00").arg(npcs.hour(mp.settlement, npc, slot), 2, 10, QChar('0'))
                     : "an unusual time (see advanced details)");
    if (slot == 1 || slot == 3)
        info +=
            "Changes 2 and 4 share this destination and behavior. Moving either changes both.\n";
    if (loc.x >= 32 || loc.y >= 32)
        info += "Stored position is outside map bounds; it is preserved.\n";
    if (loc.floor != mp.floor)
        info += QString("On floor %1; use Locate to change floors.\n").arg(loc.floor);
    QStringList positions;
    for (int s = 0; s < 4; ++s) {
        auto other = npcs.location(mp.settlement, npc, s);
        positions.append(QString("Change %1 starts at %2:00: (%3,%4), floor %5")
                             .arg(s + 1)
                             .arg(npcs.hour(mp.settlement, npc, s))
                             .arg(other.x)
                             .arg(other.y)
                             .arg(other.floor));
        if (npcGhosts->isChecked() && s != slot && other.floor == mp.floor && other.x < 32 &&
            other.y < 32)
            canvas->ghosts.append({other.x, other.y, sprite, s});
    }
    info += "Scheduled destinations:\n" + positions.join("\n");
    npcConversation->setEnabled(conversationIndices.contains(id));
    if (!npcConversation->isEnabled())
        info += "\nNo editable conversation is available for this character.";
    if (id >= 128)
        info += "\nMerchant or special conversation is handled by the game.";
    npcInfo->setText(info);
    canvas->update();
}
void MapWorkspace::selectNpc(int npc) {
    if (resource.endsWith(".CBT")) {
        canvas->selectedNpc = npc;
        refreshCombat();
        saveView();
        return;
    }
    canvas->selectedNpc = npc;
    (*navigation)[resource + "/actor/" +
                  QString::number(document.pages[currentPage()].settlement)] = npc;
    refreshNpcs();
    saveView();
}
bool MapWorkspace::moveNpcTo(int npc, int x, int y, int floor) {
    try {
        require(npc >= 0 && npc < 32, "Select an NPC before moving");
        int settlement = document.pages[currentPage()].settlement;
        bool available = false;
        for (auto mp : document.pages)
            if (mp.settlement == settlement && mp.floor == floor)
                available = true;
        require(available, "That floor does not exist in this settlement");
        MapNpcDocument npcs(project, npcResource);
        auto destination = npcs.location(settlement, npc, schedule->currentIndex());
        destination.x = x;
        destination.y = y;
        destination.floor = floor;
        auto bytes = npcs.move(settlement, npc, schedule->currentIndex(), destination);
        if (bytes != project->data(npcResource) &&
            !commitResources({{npcResource, bytes}}, "Move NPC " + npcName(npc)))
            return false;
        refreshNpcs();
        saveView();
        return true;
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "NPC move rejected", QString::fromUtf8(e.what()));
        return false;
    }
}
void MapWorkspace::locateNpc() {
    if (canvas->selectedNpc < 0 || !project->resources.contains(npcResource))
        return;
    int npc = canvas->selectedNpc;
    auto current = document.pages[currentPage()];
    MapNpcDocument npcs(project, npcResource);
    auto loc = npcs.location(current.settlement, npc, schedule->currentIndex());
    if (loc.x >= 32 || loc.y >= 32) {
        coordinate->setText("Stored NPC position is outside map bounds");
        return;
    }
    for (int i = 0; i < document.pages.size(); ++i)
        if (document.pages[i].settlement == current.settlement &&
            document.pages[i].floor == loc.floor) {
            if (currentPage() != i) {
                auto &target = (*states)[resource + "/" + QString::number(i)];
                target.schedule = schedule->currentIndex();
                target.npc = npc;
                target.ghosts = npcGhosts->isChecked();
                target.tool = MapCanvas::InspectNpc;
                selectPage(i);
            }
            selectNpc(npc);
            QTimer::singleShot(0, this,
                               [this, loc] { view->centerMap({loc.x + 0.5, loc.y + 0.5}); });
            return;
        }
    coordinate->setText("Stored NPC floor has no map page; record is preserved");
}

void MapWorkspace::exportImage(bool ids, bool preview) {
    try {
        auto path = QFileDialog::getSaveFileName(this,
                                                 preview ? "Export visible preview with overlays"
                                                 : ids   ? "Export tile IDs"
                                                         : "Export terrain",
                                                 {}, "PNG (*.png)");
        if (path.isEmpty())
            return;
        require(QFileInfo(path).absolutePath() != project->sourceDirectory,
                "Export images outside the original game folder");
        QImage image;
        if (preview) {
            const QRect visible = canvas->rect().intersected(
                QRect(canvas->mapFrom(view->viewport(), QPoint(0, 0)), view->viewport()->size()));
            image = canvas->grab(visible).toImage();
        } else {
            // Use the document, never a displayed trigger-preview buffer.
            image = document.terrainImage(currentPage(), canvas->tiles, ids);
        }
        require(image.save(path, "PNG"), "Cannot export PNG");
        coordinate->setText(QString("Saved %1: %2")
                                .arg(preview ? "visible preview with overlays"
                                     : ids   ? "tile IDs"
                                             : "stored terrain")
                                .arg(QFileInfo(path).fileName()));
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "Map export", QString::fromUtf8(e.what()));
    }
}

// Metadata rows verified against src/macros.h and COMBAT_111a in src/combat.c.
namespace {
QPair<int, int> combatOffsets(int base, int entity, int entry) {
    if (entity < 6) {
        const int rows[] = {4, 1, 3, 2}; // North, East, South, West
        return {base + rows[entry] * 32 + 11 + entity, base + rows[entry] * 32 + 17 + entity};
    }
    if (entity < 22)
        return {base + 6 * 32 + 11 + entity - 6, base + 7 * 32 + 11 + entity - 6};
    return {base + 8 * 32 + 11 + entity - 22, base + 8 * 32 + 19 + entity - 22};
}
} // namespace
void MapWorkspace::createCombatInspector(QVBoxLayout *layout) {
    layout->addWidget(new QLabel("Encounter authoring"));
    npcGhosts = new QCheckBox(this);
    npcGhosts->hide();
    combatEntry = new QComboBox;
    combatEntry->setToolTip(
        "Choose the party entry direction whose starting positions are displayed.");
    combatEntry->setObjectName("combatEntry");
    combatEntry->setAccessibleName("Party entry direction");
    combatEntry->addItems({"North entry", "East entry", "South entry", "West entry"});
    combatEntry->setCurrentIndex(navigation->value(resource + "/entry", 0));
    layout->addWidget(combatEntry);
    combatEntity = new QComboBox;
    combatEntity->setToolTip(
        "Select a party start, monster start, or trigger to inspect and move.");
    combatEntity->setObjectName("combatEntity");
    combatEntity->setAccessibleName("Encounter character or trigger");
    combatEntity->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combatEntity->setMinimumContentsLength(12);
    combatEntity->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    layout->addWidget(combatEntity);
    combatInfo = new QLabel;
    combatInfo->setToolTip(
        "Details of the selected encounter marker, including disabled or out-of-bounds records.");
    combatInfo->setWordWrap(true);
    combatInfo->setTextFormat(Qt::PlainText);
    layout->addWidget(combatInfo);
    combatPreview = new QCheckBox("Preview trigger result");
    combatPreview->setToolTip(
        "Preview terrain changes caused by the selected trigger without modifying the map.");
    combatPreview->setObjectName("combatPreview");
    combatPreview->setChecked(navigation->value(resource + "/preview", 0));
    layout->addWidget(combatPreview);
    auto advanced = new QLabel(
        "Drag markers to move. P = party start, M = monster, T = trigger. Disabled/out-of-bounds "
        "records stay in the list. Use the Combat setup tab for raw metadata. Trigger preview is "
        "read-only; runtime AI and special encounters are not simulated.");
    advanced->setWordWrap(true);
    layout->addWidget(advanced);
    layout->addStretch();
    connect(combatEntry, &QComboBox::currentIndexChanged, this, [this](int value) {
        canvas->cancelStroke();
        (*navigation)[resource + "/entry"] = value;
        refreshCombat();
    });
    connect(combatEntity, &QComboBox::currentIndexChanged, this, [this](int value) {
        canvas->cancelStroke();
        canvas->selectedNpc = value;
        refreshCombat();
        saveView();
    });
    connect(combatPreview, &QCheckBox::toggled, this, [this](bool value) {
        canvas->cancelStroke();
        (*navigation)[resource + "/preview"] = value;
        refreshCombat();
    });
}
void MapWorkspace::refreshCombat() {
    canvas->actors.clear();
    canvas->ghosts.clear();
    canvas->ids = document.terrain(currentPage());
    const auto bytes = project->data(resource);
    const int base = document.pages[currentPage()].offset;
    QSignalBlocker blocked(combatEntity);
    combatEntity->clear();
    QString info;
    for (int entity = 0; entity < 30; ++entity) {
        auto offsets = combatOffsets(base, entity, combatEntry->currentIndex());
        int x = U5::byte(bytes, offsets.first), y = U5::byte(bytes, offsets.second);
        QString label = entity < 6    ? QString("P%1").arg(entity + 1)
                        : entity < 22 ? QString("M%1").arg(entity - 5)
                                      : QString("T%1").arg(entity - 21);
        bool enabled =
            entity < 6 || entity >= 22 || U5::byte(bytes, base + 5 * 32 + 11 + entity - 6) != 0;
        combatEntity->addItem(QString("%1 · (%2, %3)%4")
                                  .arg(label)
                                  .arg(x)
                                  .arg(y)
                                  .arg(!enabled           ? " · disabled"
                                       : x > 10 || y > 10 ? " · outside"
                                                          : ""));
        canvas->actors.append({x, y, 0, entity, label});
        if (entity != canvas->selectedNpc)
            continue;
        info = combatEntity->itemText(entity);
        if (entity >= 22) {
            int trigger = entity - 22, tile = U5::byte(bytes, base + 11 + trigger);
            info += QString("\nReplacement tile %1").arg(tile);
            for (int row : {9, 10}) {
                int cx = U5::byte(bytes, base + row * 32 + 11 + trigger);
                int cy = U5::byte(bytes, base + row * 32 + 19 + trigger);
                info += QString("\nChange %1: (%2, %3)%4")
                            .arg(row - 8)
                            .arg(cx)
                            .arg(cy)
                            .arg(cx > 10 || cy > 10 ? " (ignored by engine)" : "");
                if (cx < 11 && cy < 11) {
                    canvas->ghosts.append(
                        {cx, cy, tile, row - 8, QString("Change %1").arg(row - 8)});
                    if (combatPreview->isChecked() && canvas->tool == MapCanvas::InspectNpc)
                        canvas->ids[cy * 11 + cx] = char(tile);
                }
            }
        }
    }
    combatEntity->setCurrentIndex(canvas->selectedNpc);
    combatInfo->setText(info.isEmpty() ? "Select a marker or record." : info);
    combatPreview->setEnabled(canvas->selectedNpc >= 22);
    canvas->update();
}
bool MapWorkspace::moveCombat(int entity, QPoint destination) {
    if (entity < 0 || entity >= 30 || destination.x() < 0 || destination.y() < 0 ||
        destination.x() > 10 || destination.y() > 10)
        return false;
    auto bytes = project->data(resource);
    auto offsets =
        combatOffsets(document.pages[currentPage()].offset, entity, combatEntry->currentIndex());
    bytes[offsets.first] = char(destination.x());
    bytes[offsets.second] = char(destination.y());
    if (bytes != project->data(resource) &&
        !commitResources({{resource, bytes}}, "Move encounter marker"))
        return false;
    refreshCombat();
    saveView();
    return true;
}

namespace {
const QMap<int, QPair<QString, QString>> &tileCatalog() {
    // Descriptions are backed by the engine's named symbols in src/tiles.h.
    // Numeric symbols are classified from the original DOS artwork, including
    // animation frames and rendering masks (which are not ordinary terrain).
    static const QMap<int, QPair<QString, QString>> catalog{
        {TILE_MAP_WATER_1, {"Water 1", "Ground"}},
        {TILE_MAP_WATER_2, {"Water 2", "Ground"}},
        {TILE_MAP_WATER_3, {"Water 3", "Ground"}},
        {TILE_MAP_POISON, {"Poison", "Ground"}},
        {TILE_MAP_GRASS, {"Grass", "Ground"}},
        {TILE_MAP_HUT, {"Hut", "Buildings"}},
        {TILE_MAP_CODEX, {"Codex", "Buildings"}},
        {TILE_MAP_KEEP, {"Keep", "Buildings"}},
        {TILE_MAP_VILLAGE, {"Village", "Buildings"}},
        {TILE_MAP_TOWNE, {"Town", "Buildings"}},
        {TILE_MAP_CASTLE, {"Castle", "Buildings"}},
        {TILE_MAP_CAVE, {"Cave", "Ground"}},
        {TILE_MAP_MINE, {"Mine", "Ground"}},
        {TILE_MAP_DUNGEON, {"Dungeon", "Ground"}},
        {TILE_MAP_SHRINE, {"Shrine", "Buildings"}},
        {TILE_MAP_RUINS, {"Ruins", "Buildings"}},
        {TILE_MAP_LIGHTHOUSE, {"Lighthouse", "Buildings"}},
        {TILE_MAP_STUMP, {"Stump", "Ground"}},
        {TILE_MAP_CROPS_PICKED, {"Crops picked", "Ground"}},
        {TILE_MAP_CROPS, {"Crops", "Ground"}},
        {TILE_MAP_TREE, {"Tree", "Ground"}},
        {TILE_MAP_PALACEBT, {"Blackthorn’s palace", "Buildings"}},
        {TILE_MAP_CASTLELB, {"Lord British’s castle", "Buildings"}},
        {TILE_MAP_HIDDEN_DOOR, {"Hidden door", "Buildings"}},
        {TILE_MAP_WALL, {"Wall", "Buildings"}},
        {TILE_MAP_SHELF, {"Shelf", "Objects"}},
        {TILE_MAP_BOOKSHELF, {"Bookshelf", "Objects"}},
        {TILE_MAP_TRAPDOOR, {"Trapdoor", "Buildings"}},
        {TILE_MAP_LAVA, {"Lava", "Ground"}},
        {TILE_MAP_CHAIR_90, {"Chair", "Objects"}},
        {TILE_MAP_CHAIR_91, {"Chair", "Objects"}},
        {TILE_MAP_CHAIR_92, {"Chair", "Objects"}},
        {TILE_MAP_CHAIR_93, {"Chair", "Objects"}},
        {TILE_MAP_TABLE_94, {"Table", "Objects"}},
        {TILE_MAP_TABLE_95, {"Table", "Objects"}},
        {TILE_MAP_TABLE_96, {"Table", "Objects"}},
        {TILE_MAP_TABLE_9A, {"Table", "Objects"}},
        {TILE_MAP_TABLE_9B, {"Table", "Objects"}},
        {TILE_MAP_TABLE_9C, {"Table", "Objects"}},
        {TILE_MAP_MIRROR, {"Mirror", "Objects"}},
        {TILE_MAP_MIRROR_9E, {"Mirror", "Objects"}},
        {TILE_MAP_MIRROR_BROKEN, {"Mirror broken", "Objects"}},
        {TILE_MAP_WELL, {"Well", "Objects"}},
        {TILE_MAP_DESK, {"Desk", "Objects"}},
        {TILE_MAP_BARREL, {"Barrel", "Objects"}},
        {TILE_MAP_VANITY, {"Vanity", "Objects"}},
        {TILE_MAP_DRESSER, {"Dresser", "Objects"}},
        {TILE_MAP_TRUNK, {"Trunk", "Objects"}},
        {TILE_MAP_BRAZIER, {"Brazier", "Objects"}},
        {TILE_MAP_FLAME, {"Flame", "Objects"}},
        {TILE_MAP_CANNON_B4, {"Cannon", "Objects"}},
        {TILE_MAP_CANNON_B5, {"Cannon", "Objects"}},
        {TILE_MAP_CANNON_B6, {"Cannon", "Objects"}},
        {TILE_MAP_CANNON_B7, {"Cannon", "Objects"}},
        {TILE_MAP_DOOR_B8, {"Door", "Buildings"}},
        {TILE_MAP_DOOR_B9, {"Door", "Buildings"}},
        {TILE_MAP_DOOR_BA, {"Door", "Buildings"}},
        {TILE_MAP_DOOR_BB, {"Door", "Buildings"}},
        {TILE_MAP_FIREPLACE, {"Fireplace", "Buildings"}},
        {TILE_MAP_TABLE_BE, {"Table", "Objects"}},
        {TILE_MAP_STAIR, {"Stair", "Buildings"}},
        {TILE_MAP_LADDER_UP, {"Ladder up", "Buildings"}},
        {TILE_MAP_LADDER_DOWN, {"Ladder down", "Buildings"}},
        {TILE_MAP_WATERFALL, {"Waterfall", "Ground"}},
        {TILE_MAP_FOUNTAIN, {"Fountain", "Objects"}},
        {TILE_MAP_MOONGATE, {"Moongate", "Buildings"}},
        {TILE_MAP_WATER_E4, {"Water", "Ground"}},
        {TILE_MAP_WATER_E7, {"Water", "Ground"}},
        {TILE_MAP_SIGN_F0, {"Sign", "Objects"}},
        {TILE_MAP_SIGN_F8, {"Sign", "Objects"}},
        {TILE_MAP_CLOCK, {"Clock", "Objects"}},
        {TILE_MAP_BELLOWS_FC, {"Bellows", "Objects"}},
        {TILE_MAP_BELLOWS_FD, {"Bellows", "Objects"}},
        {0x00, {"Magic flash", "Other"}},
        {0x06, {"Grass variation", "Ground"}},
        {0x07, {"Swamp", "Ground"}},
        {0x08, {"Brush", "Ground"}},
        {0x09, {"Forest", "Ground"}},
        {0x0a, {"Dense forest", "Ground"}},
        {0x0b, {"Hills", "Ground"}},
        {0x0c, {"Mountains", "Ground"}},
        {0x0d, {"High mountains", "Ground"}},
        {0x0e, {"Grass and rocks", "Ground"}},
        {0x0f, {"Forest and rocks", "Ground"}},
        {0x1c, {"Banner", "Objects"}},
        {0x1d, {"Bridge", "Buildings"}},
        {0x1e, {"Swamp and grass", "Ground"}},
        {0x1f, {"Swamp edge", "Ground"}},
        {0x20, {"Road", "Ground"}},
        {0x21, {"Road edge", "Ground"}},
        {0x22, {"Road corner", "Ground"}},
        {0x23, {"Road corner", "Ground"}},
        {0x24, {"Road corner", "Ground"}},
        {0x25, {"Road corner", "Ground"}},
        {0x26, {"Road junction", "Ground"}},
        {0x27, {"Red carpet", "Ground"}},
        {0x28, {"Red carpet", "Ground"}},
        {0x29, {"Crystal ball", "Objects"}},
        {0x2a, {"Emblem", "Objects"}},
        {0x2f, {"Cactus", "Ground"}},
        {0x30, {"Grass edge", "Ground"}},
        {0x31, {"Grass edge", "Ground"}},
        {0x32, {"Road and grass", "Ground"}},
        {0x33, {"Road and grass", "Ground"}},
        {0x34, {"Riverbank", "Ground"}},
        {0x35, {"Riverbank", "Ground"}},
        {0x36, {"Riverbank", "Ground"}},
        {0x37, {"Riverbank", "Ground"}},
        {0x38, {"Castle emblem", "Objects"}},
        {0x3a, {"Castle tower", "Buildings"}},
        {0x3b, {"Castle battlements", "Buildings"}},
        {0x3c, {"Castle tower", "Buildings"}},
        {0x3d, {"Castle tower", "Buildings"}},
        {0x3f, {"Castle tower", "Buildings"}},
        {0x40, {"Wood floor", "Ground"}},
        {0x41, {"Open book", "Objects"}},
        {0x42, {"Food on counter", "Objects"}},
        {0x43, {"Wood panel", "Buildings"}},
        {0x44, {"Brick floor", "Ground"}},
        {0x45, {"Stone floor", "Ground"}},
        {0x46, {"Boulder", "Ground"}},
        {0x47, {"Bridge", "Buildings"}},
        {0x48, {"Wood panel", "Buildings"}},
        {0x49, {"Wood panel", "Buildings"}},
        {0x4a, {"Window", "Buildings"}},
        {0x4b, {"Portcullis", "Buildings"}},
        {0x4c, {"Diagonal stone wall", "Buildings"}},
        {0x4d, {"Diagonal stone wall", "Buildings"}},
        {0x50, {"Stone wall corner", "Buildings"}},
        {0x51, {"Stone wall corner", "Buildings"}},
        {0x52, {"Stone wall corner", "Buildings"}},
        {0x53, {"Stone wall corner", "Buildings"}},
        {0x54, {"Stone wall corner", "Buildings"}},
        {0x55, {"Stone wall corner", "Buildings"}},
        {0x56, {"Stone wall", "Buildings"}},
        {0x57, {"Stone wall", "Buildings"}},
        {0x58, {"Skeleton", "Objects"}},
        {0x59, {"Skeleton", "Objects"}},
        {0x5b, {"Potted plant", "Objects"}},
        {0x5d, {"Bookshelf", "Objects"}},
        {0x5e, {"Stone arch", "Buildings"}},
        {0x5f, {"Stone arch", "Buildings"}},
        {0x60, {"Riverbank", "Ground"}},
        {0x61, {"Riverbank", "Ground"}},
        {0x62, {"Riverbank", "Ground"}},
        {0x63, {"Riverbank", "Ground"}},
        {0x64, {"Riverbank", "Ground"}},
        {0x65, {"Riverbank", "Ground"}},
        {0x66, {"Riverbank", "Ground"}},
        {0x67, {"Riverbank", "Ground"}},
        {0x68, {"Riverbank", "Ground"}},
        {0x69, {"Riverbank", "Ground"}},
        {0x6a, {"Wooden bridge", "Buildings"}},
        {0x6b, {"Wooden bridge", "Buildings"}},
        {0x6c, {"Riverbank", "Ground"}},
        {0x6d, {"Riverbank", "Ground"}},
        {0x6e, {"Riverbank", "Ground"}},
        {0x6f, {"Riverbank", "Ground"}},
        {0x70, {"Dungeon corridor mask", "Other"}},
        {0x71, {"Dungeon corridor mask", "Other"}},
        {0x72, {"Dungeon corridor mask", "Other"}},
        {0x73, {"Dungeon corridor mask", "Other"}},
        {0x74, {"Dungeon corridor mask", "Other"}},
        {0x75, {"Dungeon corridor mask", "Other"}},
        {0x76, {"Dungeon corridor mask", "Other"}},
        {0x77, {"Dungeon corridor mask", "Other"}},
        {0x78, {"Dungeon corridor mask", "Other"}},
        {0x79, {"Dungeon corridor mask", "Other"}},
        {0x7a, {"Dungeon corridor mask", "Other"}},
        {0x7b, {"Dungeon corridor mask", "Other"}},
        {0x7c, {"Dungeon corridor mask", "Other"}},
        {0x7d, {"Dungeon corridor mask", "Other"}},
        {0x7e, {"Dungeon corridor mask", "Other"}},
        {0x7f, {"Dungeon corridor mask", "Other"}},
        {0x80, {"Forge", "Objects"}},
        {0x81, {"Forge", "Objects"}},
        {0x82, {"Forge by water", "Objects"}},
        {0x83, {"Forge by water", "Objects"}},
        {0x84, {"Fence", "Buildings"}},
        {0x85, {"Spider web", "Buildings"}},
        {0x86, {"Bars", "Buildings"}},
        {0x87, {"Brick wall", "Buildings"}},
        {0x88, {"Rubble", "Ground"}},
        {0x89, {"Cross", "Objects"}},
        {0x8a, {"Gravestone", "Objects"}},
        {0x8b, {"Dirt mound", "Ground"}},
        {0x8d, {"Brick floor variation", "Ground"}},
        {0x8e, {"Hanging cage", "Objects"}},
        {0x97, {"Portcullis", "Buildings"}},
        {0x98, {"Portcullis", "Buildings"}},
        {0x99, {"Bars", "Buildings"}},
        {0xa0, {"Signpost", "Objects"}},
        {0xa2, {"Ploughed ground", "Ground"}},
        {0xa3, {"Bench", "Objects"}},
        {0xa4, {"Stone cross", "Objects"}},
        {0xa7, {"Barrel", "Objects"}},
        {0xa9, {"Chair", "Objects"}},
        {0xaa, {"Rug", "Ground"}},
        {0xab, {"Bed", "Objects"}},
        {0xac, {"Bed", "Objects"}},
        {0xae, {"Desk", "Objects"}},
        {0xb0, {"Wall torch", "Objects"}},
        {0xb1, {"Wall torch", "Objects"}},
        {0xbd, {"Wall torch", "Objects"}},
        {0xbf, {"Fireplace", "Buildings"}},
        {0xc0, {"Stone fragment", "Ground"}},
        {0xc1, {"Stone fragment", "Ground"}},
        {0xc2, {"Stone fragment", "Ground"}},
        {0xc3, {"Small flowers", "Ground"}},
        {0xc5, {"Stairs", "Buildings"}},
        {0xc6, {"Stairs", "Buildings"}},
        {0xc7, {"Stairs", "Buildings"}},
        {0xca, {"Brick floor and grass", "Ground"}},
        {0xcb, {"Brick floor and grass", "Ground"}},
        {0xcc, {"Brick fragment", "Ground"}},
        {0xcd, {"Brick fragment", "Ground"}},
        {0xce, {"Grass fragment", "Ground"}},
        {0xcf, {"Grass fragment", "Ground"}},
        {0xd0, {"Diagonal wall mask", "Other"}},
        {0xd1, {"Diagonal wall mask", "Other"}},
        {0xd2, {"Diagonal wall mask", "Other"}},
        {0xd3, {"Diagonal wall mask", "Other"}},
        {0xd5, {"Waterfall animation", "Ground"}},
        {0xd6, {"Waterfall animation", "Ground"}},
        {0xd7, {"Waterfall animation", "Ground"}},
        {0xd9, {"Fountain animation", "Objects"}},
        {0xda, {"Fountain animation", "Objects"}},
        {0xdb, {"Fountain animation", "Objects"}},
        {0xdd, {"Swamp animation", "Ground"}},
        {0xde, {"Sacred flame", "Objects"}},
        {0xdf, {"Rocks", "Ground"}},
        {0xe0, {"Grass detail", "Ground"}},
        {0xe1, {"Grass detail", "Ground"}},
        {0xe2, {"Fence", "Buildings"}},
        {0xe3, {"Solid wall mask", "Other"}},
        {0xe5, {"Water edge", "Ground"}},
        {0xe6, {"Water edge", "Ground"}},
        {0xe8, {"Hourglass animation", "Other"}},
        {0xe9, {"Hourglass animation", "Other"}},
        {0xea, {"Hourglass animation", "Other"}},
        {0xeb, {"Hourglass animation", "Other"}},
        {0xec, {"Magic field", "Other"}},
        {0xed, {"Magic field", "Other"}},
        {0xee, {"Magic field", "Other"}},
        {0xef, {"Magic field", "Other"}},
        {0xf1, {"Wall sign", "Objects"}},
        {0xf2, {"Wall sign", "Objects"}},
        {0xf3, {"Wall sign", "Objects"}},
        {0xf4, {"Wall sign", "Objects"}},
        {0xf5, {"Wall sign", "Objects"}},
        {0xf6, {"Wall sign", "Objects"}},
        {0xf7, {"Wall sign", "Objects"}},
        {0xf9, {"Wall sign", "Objects"}},
        {0xfb, {"Clock animation", "Objects"}},
        {0xfe, {"Bellows", "Objects"}},
        {0xff, {"Bellows", "Objects"}},
    };
    return catalog;
}
} // namespace
QString MapDocument::tileName(int id) {
    return tileCatalog().value(id, {"Unidentified tile", "Uncategorized"}).first;
}
QString MapDocument::tileCategory(int id) {
    return tileCatalog().value(id, {"Unidentified tile", "Uncategorized"}).second;
}
QImage MapDocument::terrainImage(int page, const QVector<QImage> &tiles, bool ids) const {
    const auto cells = terrain(page);
    const int side = pages[page].side;
    QImage image(side * (ids ? 1 : 16), side * (ids ? 1 : 16),
                 ids ? QImage::Format_Grayscale8 : QImage::Format_RGB32);
    require(!image.isNull(), "Cannot allocate map export");
    if (ids) {
        for (int y = 0; y < side; ++y)
            memcpy(image.scanLine(y), cells.constData() + y * side, side);
    } else {
        QPainter painter(&image);
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x)
                painter.drawImage(x * 16, y * 16, tiles[U5::byte(cells, y * side + x)]);
    }
    return image;
}
