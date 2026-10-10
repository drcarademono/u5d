#include "window.h"
#include "dialogue_editor.h"
#include "dungeon_editor.h"
#include "mod/package.h"
#include "resource_document.h"
#include "text_preview.h"
#include <QMouseEvent>
#include <QPainter>
#include <QtWidgets>
#include <memory>
using U5::require;
namespace {
class Change : public QUndoCommand {
    Project *project;
    QMap<QString, QByteArray> before, after;
    std::function<void()> notify;

  public:
    Change(Project *p, const QMap<QString, QByteArray> &changes, const QString &title,
           std::function<void()> callback)
        : project(p), after(changes), notify(callback) {
        setText(title);
        for (auto it = changes.begin(); it != changes.end(); ++it)
            before[it.key()] = p->data(it.key());
    }
    void apply(const QMap<QString, QByteArray> &changes) {
        for (auto it = changes.begin(); it != changes.end(); ++it)
            project->resources[it.key()].edited = it.value();
        notify();
    }
    void undo() override { apply(before); }
    void redo() override { apply(after); }
};
class QuestionNameChange : public QUndoCommand {
    Project *project;
    QString key, before, after;
    bool existed;
    std::function<void()> notify;

  public:
    QuestionNameChange(Project *p, const QString &k, const QString &value, std::function<void()> fn)
        : project(p), key(k), before(p->dialogueNames.value(k)), after(value),
          existed(p->dialogueNames.contains(k)), notify(fn) {
        setText("Name question");
    }
    void redo() override {
        project->dialogueNames[key] = after;
        notify();
    }
    void undo() override {
        if (existed)
            project->dialogueNames[key] = before;
        else
            project->dialogueNames.remove(key);
        notify();
    }
};
QWidget *panel(QVBoxLayout **layout) {
    auto w = new QWidget;
    *layout = new QVBoxLayout(w);
    (*layout)->setContentsMargins(18, 16, 18, 16);
    (*layout)->setSpacing(12);
    return w;
}
QLabel *hint(const QString &text) {
    auto l = new QLabel(text);
    l->setWordWrap(true);
    l->setStyleSheet("color:#abb6c7;");
    return l;
}
QPushButton *button(QHBoxLayout *row, const QString &text, std::function<void()> action) {
    auto b = new QPushButton(text);
    row->addWidget(b);
    QObject::connect(b, &QPushButton::clicked, b, action);
    return b;
}
QScrollArea *makeScroll(QWidget *widget, bool resize = false) {
    auto area = new QScrollArea;
    area->setWidget(widget);
    area->setWidgetResizable(resize);
    return area;
}
class PixelCanvas : public QWidget {
  public:
    QImage image;
    QRgb brush = qRgb(255, 255, 255);
    int zoom = 6;
    QImage before;
    std::function<void(const QImage &)> commit;
    void size() {
        setFixedSize(image.width() * zoom, image.height() * zoom);
        update();
    }

  protected:
    void paintEvent(QPaintEvent *e) override {
        QPainter p(this);
        for (int y = e->rect().top() / 12; y <= e->rect().bottom() / 12; y++)
            for (int x = e->rect().left() / 12; x <= e->rect().right() / 12; x++)
                p.fillRect(x * 12, y * 12, 12, 12,
                           (x + y) % 2 ? QColor("#384254") : QColor("#222b39"));
        p.drawImage(rect(), image);
    }
    void point(QMouseEvent *e) {
        int x = e->position().x() / zoom, y = e->position().y() / zoom;
        if (x < 0 || y < 0 || x >= image.width() || y >= image.height())
            return;
        if (e->buttons() & Qt::RightButton)
            brush = image.pixel(x, y);
        else if (e->buttons() & Qt::LeftButton) {
            image.setPixel(x, y, brush);
            update();
        }
    }
    void mousePressEvent(QMouseEvent *e) override {
        before = image;
        point(e);
    }
    void mouseMoveEvent(QMouseEvent *e) override { point(e); }
    void mouseReleaseEvent(QMouseEvent *) override {
        if (image != before && commit)
            commit(image);
    }
};
QByteArray readFile(const QString &path) {
    QFile f(path);
    require(f.open(QIODevice::ReadOnly), f.errorString());
    require(f.size() <= MOD_MAX_PACKAGE, "File is too large");
    return f.readAll();
}
void writeFile(const QString &path, const QByteArray &bytes) {
    QSaveFile f(path);
    require(f.open(QIODevice::WriteOnly), f.errorString());
    require(f.write(bytes) == bytes.size() && f.commit(),
            "Cannot write " + path + ": " + f.errorString());
}
} // namespace
WorkshopWindow::WorkshopWindow() {
    setWindowTitle("Impera Workshop");
    resize(1320, 860);
    setWindowIcon(QIcon(":/impera.png"));
    auto toolbar = addToolBar("Project");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    const QMap<QString, QString> help{
        {"New Mod",
         "Start a mod using the remembered Ultima 5 game folder, or locate it if unavailable."},
        {"Open Project", "Open an editable Impera Workshop project."},
        {"Save Project", "Save your editable project; original game files are left untouched."},
        {"Name Mod", "Set the name included in the exported mod package."},
        {"Export Package", "Validate and export a mod package for Impera’s Mods folder."},
        {"Import Package", "Import a mod package into an editable Workshop project."},
        {"Change Game Folder", "Locate a different set of original Ultima 5 game files."},
        {"Validate / Package Preview",
         "Check resource formats, warnings, package contents, and installed mod conflicts."},
        {"Test Mod", "Launch Impera with this mod in an isolated copy of the game data."},
        {"Open Last Test Folder", "Open the last isolated runtime to inspect its files and logs."},
        {"Stop Mod Tests", "Terminate Impera test processes launched by Workshop."},
        {"Set Impera Executable",
         "Choose the Impera executable or AppImage used for mod testing."}};
    auto action = [&](const QString &label, const QKeySequence &key, std::function<void()> fn) {
        auto a = toolbar->addAction(label);
        a->setShortcut(key);
        a->setToolTip(help.value(label));
        connect(a, &QAction::triggered, this, fn);
        return a;
    };
    action("New Mod", QKeySequence::New, [this] {
        if (!canLeave())
            return;
        if (restoreGameDirectory())
            return;
        QString p = QFileDialog::getExistingDirectory(this, "Locate Ultima 5 game files");
        if (!p.isEmpty())
            openGame(p);
    });
    auto toolsMenu = new QMenu(this);
    toolsMenu->setToolTipsVisible(true);
    auto toolsButton = new QToolButton;
    toolsButton->setText("Mod Tools");
    toolsButton->setToolTip(
        "Validate packages, manage the game folder, and test your mod in Impera.");
    toolsButton->setMenu(toolsMenu);
    toolsButton->setPopupMode(QToolButton::InstantPopup);
    toolbar->addWidget(toolsButton);
    auto extra = [this, toolsMenu, help](const QString &label, std::function<void()> fn) {
        auto item = toolsMenu->addAction(label);
        item->setToolTip(help.value(label));
        connect(item, &QAction::triggered, this, fn);
    };
    extra("Change Game Folder", [this] {
        if (!canLeave())
            return;
        QString path = QFileDialog::getExistingDirectory(this, "Locate Ultima 5 game files",
                                                         project.sourceDirectory);
        if (!path.isEmpty())
            openGame(path);
    });
    extra("Validate / Package Preview", [this] { reviewPackage(false); });
    extra("Test Mod", [this] { reviewPackage(true); });
    extra("Open Last Test Folder", [this] {
        const QString path = QSettings().value("paths/testSession").toString();
        if (QDir(path).exists() && !path.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        else
            statusBar()->showMessage("No test session folder is available yet", 6000);
    });
    extra("Stop Mod Tests", [this] {
        for (auto process : findChildren<QProcess *>())
            if (process->state() != QProcess::NotRunning)
                process->terminate();
    });
    extra("Set Impera Executable", [this] {
        QString path = QFileDialog::getOpenFileName(this, "Choose Impera executable or AppImage",
                                                    QSettings().value("paths/engine").toString());
        if (!path.isEmpty())
            QSettings().setValue("paths/engine", path);
    });
    action("Open Project", QKeySequence::Open, [this] {
        if (!canLeave())
            return;
        QString p = QFileDialog::getOpenFileName(this, "Open mod project", {},
                                                 "Impera project (*.imperaproject)");
        if (!p.isEmpty())
            openProject(p);
    });
    action("Save Project", QKeySequence::Save, [this] { saveProject(); });
    toolbar->addSeparator();
    auto undo = history.createUndoAction(this, "Undo");
    undo->setShortcut(QKeySequence::Undo);
    undo->setToolTip("Undo the last committed edit.");
    toolbar->addAction(undo);
    QObject::disconnect(undo, nullptr, &history, nullptr);
    connect(undo, &QAction::triggered, this, [this] {
        if (flushDraft && !flushDraft())
            return;
        history.undo();
    });
    auto redo = history.createRedoAction(this, "Redo");
    redo->setShortcut(QKeySequence::Redo);
    redo->setToolTip("Reapply the last undone edit.");
    toolbar->addAction(redo);
    QObject::disconnect(redo, nullptr, &history, nullptr);
    connect(redo, &QAction::triggered, this, [this] {
        if (flushDraft && !flushDraft())
            return;
        history.redo();
    });
    action("Name Mod", {}, [this] {
        if (project.resources.isEmpty())
            return;
        bool ok;
        QString title = QInputDialog::getText(
            this, "Mod name", "Name shown in Impera's log:", QLineEdit::Normal, project.title, &ok);
        if (ok) {
            project.title = title;
            updateSummary();
        }
    });
    action("Export Package", QKeySequence(Qt::CTRL | Qt::Key_E), [this] {
        if (flushDraft && !flushDraft())
            return;
        guard([&] {
            if (!reviewPackage(false))
                return;
            auto bytes = project.package();
            QString path = QFileDialog::getSaveFileName(this, "Export mod package", {},
                                                        "Impera mod (*.imperamod)");
            if (path.isEmpty())
                return;
            if (!path.endsWith(".imperamod", Qt::CaseInsensitive))
                path += ".imperamod";
            require(QFileInfo(path).absolutePath() != project.sourceDirectory,
                    "Place mod packages in the Mods subfolder, or another "
                    "distribution folder");
            writeFile(path, bytes);
            statusBar()->showMessage(
                "Exported " + path + " — place it in Ultima 5/Mods, then restart Impera", 12000);
        });
    });
    action("Import Package", {}, [this] {
        if (flushDraft && !flushDraft())
            return;
        if (project.resources.isEmpty())
            return;
        QString path = QFileDialog::getOpenFileName(
            this, "Import a package against this game's original files", {},
            "Impera mod (*.imperamod)");
        if (path.isEmpty())
            return;
        guard([&] {
            Project next = project;
            next.importPackage(readFile(path));
            QMap<QString, QByteArray> changes;
            for (auto name : next.changed())
                changes[name] = next.data(name);
            edit(changes, "Import package");
            project.title = next.title;
            updateSummary();
        });
    });
    auto central = new QWidget;
    auto layout = new QVBoxLayout(central);
    summary = new QLabel;
    summary->setStyleSheet("font-size:18px;font-weight:600;padding:14px;");
    layout->addWidget(summary);
    auto split = new QSplitter;
    auto library = new QWidget;
    auto lv = new QVBoxLayout(library);
    auto search = new QLineEdit;
    search->setPlaceholderText("Find a resource…");
    lv->addWidget(search);
    assets = new QTreeWidget;
    assets->setHeaderHidden(true);
    assets->setMinimumWidth(230);
    lv->addWidget(assets);
    split->addWidget(library);
    auto showResources = menuBar()->addAction("Resource Library");
    showResources->setCheckable(true);
    showResources->setChecked(true);
    connect(showResources, &QAction::toggled, library, &QWidget::setVisible);
    library->setObjectName("resourceLibrary");
    content = new QStackedWidget;
    content->setObjectName("workspaces");
    split->addWidget(content);
    split->setStretchFactor(1, 1);
    layout->addWidget(split, 1);
    setCentralWidget(central);
    connect(search, &QLineEdit::textChanged, this, [this](const QString &query) {
        for (int i = 0; i < assets->topLevelItemCount(); i++) {
            auto group = assets->topLevelItem(i);
            bool visible = false;
            for (int j = 0; j < group->childCount(); j++) {
                auto item = group->child(j);
                bool match = item->text(0).contains(query, Qt::CaseInsensitive) ||
                    item->data(0, Qt::UserRole).toString().contains(query, Qt::CaseInsensitive);
                item->setHidden(!match);
                visible |= match;
            }
            group->setHidden(!visible);
        }
    });
    connect(assets, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if (item && !item->data(0, Qt::UserRole).toString().isEmpty())
            selectResource(item->data(0, Qt::UserRole).toString());
    });
    connect(&history, &QUndoStack::cleanChanged, this, [this] { updateSummary(); });
    QVBoxLayout *welcome;
    auto w = panel(&welcome);
    auto title = new QLabel("Build your Britannia");
    title->setStyleSheet("font-size:30px;font-weight:600;");
    welcome->addWidget(title);
    welcome->addWidget(hint("Create a mod from your DOS Ultima 5 folder. Edit maps, artwork, "
                            "conversations, "
                            "story and starting state in one workspace. Original game files are "
                            "read-only; "
                            "Save stores a project, Export creates a mod package."));
    welcome->addWidget(hint("Impera loads .imperamod files from the game "
                            "folder's Mods subfolder at startup. Existing saved "
                            "games keep their saved state; starting-state edits "
                            "apply to new games."));
    welcome->addStretch();
    content->addWidget(w);
    updateSummary();
}
bool WorkshopWindow::guard(const std::function<void()> &operation) {
    try {
        operation();
        return true;
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "Cannot complete edit", QString::fromUtf8(e.what()));
        return false;
    }
}
void WorkshopWindow::updateSummary() {
    summary->setText(
        project.resources.isEmpty()
            ? "Impera Workshop"
            : project.title +
                  QString("   ·   %1 modified resources").arg(project.changed().size()));
    setWindowModified(!history.isClean() || project.title != savedTitle);
    setWindowTitle("Impera Workshop — " + project.title + "[*]");
}
bool WorkshopWindow::canLeave() {
    if (flushDraft && !flushDraft())
        return false;
    if (project.resources.isEmpty() || !isWindowModified())
        return true;
    auto answer =
        QMessageBox::question(this, "Unsaved project", "Save your project before continuing?",
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel)
        return false;
    return answer != QMessageBox::Save || saveProject();
}
void WorkshopWindow::closeEvent(QCloseEvent *event) {
    if (canLeave())
        event->accept();
    else
        event->ignore();
}
bool WorkshopWindow::saveProject() {
    if (flushDraft && !flushDraft())
        return false;
    if (project.resources.isEmpty())
        return false;
    QString path = project.projectPath;
    if (path.isEmpty())
        path = QFileDialog::getSaveFileName(this, "Save mod project", {},
                                            "Impera project (*.imperaproject)");
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".imperaproject"))
        path += ".imperaproject";
    return guard([&] {
        project.save(path);
        history.setClean();
        savedTitle = project.title;
        updateSummary();
        statusBar()->showMessage("Project saved; original game files were not changed", 6000);
    });
}
bool WorkshopWindow::restoreGameDirectory() {
    const QString remembered = QSettings().value("paths/game").toString();
    if (remembered.isEmpty())
        return false;
    try {
        Project probe;
        probe.openGame(remembered);
        return openGame(remembered);
    } catch (const std::exception &) {
        // Stale preferences must not prevent opening the welcome screen.
        return false;
    }
}
bool WorkshopWindow::openGame(const QString &path) {
    return guard([&] {
        project.openGame(path);
        QSettings().setValue("paths/game", project.sourceDirectory);
        history.clear();
        savedTitle = project.title;
        current.clear();
        editorState.clear();
        mapViewStates.clear();
        mapBrushes = {};
        lastMap.clear();
        flushDraft = {};
        rebuildTree();
        selectResource("TILES.16");
        updateSummary();
    });
}
bool WorkshopWindow::openProject(const QString &path) {
    return guard([&] {
        project.load(path);
        QSettings().setValue("paths/game", project.sourceDirectory);
        history.clear();
        savedTitle = project.title;
        current.clear();
        editorState.clear();
        mapViewStates.clear();
        mapBrushes = {};
        lastMap.clear();
        flushDraft = {};
        rebuildTree();
        selectResource("TILES.16");
        updateSummary();
    });
}
void WorkshopWindow::rebuildTree() {
    QSignalBlocker block(assets);
    assets->clear();
    QMap<QString, QTreeWidgetItem *> groups;
    for (auto it = project.resources.begin(); it != project.resources.end(); ++it) {
        QString name = it.key(), group = Workshop::capability(name).group;
        if (!groups.contains(group)) {
            groups[group] = new QTreeWidgetItem(assets, {group});
            groups[group]->setExpanded(true);
        }
        auto item =
            new QTreeWidgetItem(groups[group], {Workshop::capability(name).title + (it->edited != it->original ? "  •" : "")});
        item->setData(0, Qt::UserRole, name);
        item->setToolTip(0, name + "\n" + Workshop::capability(name).status);
        if (name == current)
            assets->setCurrentItem(item);
    }
}
void WorkshopWindow::edit(const QMap<QString, QByteArray> &changes, const QString &description) {
    QMap<QString, QByteArray> actual;
    for (auto it = changes.begin(); it != changes.end(); ++it)
        if (it.value() != project.data(it.key()))
            actual.insert(it.key(), it.value());
    if (actual.isEmpty())
        return;
    history.push(new Change(&project, actual, description, [this, first = true]() mutable {
        updateSummary();
        rebuildTree();
        if (first) {
            first = false;
            return;
        }
        if (!rebuildPending) {
            rebuildPending = true;
            QTimer::singleShot(0, this, [this] {
                rebuildPending = false;
                rebuildEditor();
            });
        }
    }));
}
void WorkshopWindow::selectResource(const QString &name) {
    if (flushDraft && !flushDraft()) {
        rebuildTree();
        return;
    }
    flushDraft = {};
    current = name;
    if (name != "DUNGEON.CBT")
        dungeonRoomNavigation = false;
    if (MapDocument::supported(name))
        lastMap = name;
    rebuildEditor();
}
void WorkshopWindow::rebuildEditor() {
    if (current.isEmpty())
        return;
    flushDraft = {};
    QPoint scrollPosition;
    auto oldView = content->currentWidget()
                       ? content->currentWidget()->findChild<QScrollArea *>("canvasView")
                       : nullptr;
    if (oldView)
        scrollPosition =
            QPoint(oldView->horizontalScrollBar()->value(), oldView->verticalScrollBar()->value());
    QWidget *w = nullptr;
    try {
        w = resourceEditor(current);
    } catch (const std::exception &e) {
        QVBoxLayout *l;
        w = panel(&l);
        l->addWidget(hint(QString::fromUtf8(e.what())));
        l->addWidget(bytesEditor(current));
    }
    while (content->count()) {
        auto old = content->widget(0);
        content->removeWidget(old);
        old->deleteLater();
    }
    content->addWidget(w);
    content->setCurrentWidget(w);
    if (oldView && !w->findChild<QWidget *>("mapWorkspace"))
        QTimer::singleShot(0, w, [w, scrollPosition] {
            auto view = w->findChild<QScrollArea *>("canvasView");
            if (view) {
                view->horizontalScrollBar()->setValue(scrollPosition.x());
                view->verticalScrollBar()->setValue(scrollPosition.y());
            }
        });
}
QWidget *WorkshopWindow::resourceEditor(const QString &name) {
    auto tabs = new QTabWidget;
    if (!MapDocument::supported(name) && !lastMap.isEmpty()) {
        auto back = new QPushButton("Back to map");
        back->setObjectName("backToMap");
        tabs->setCornerWidget(back);
        connect(back, &QPushButton::clicked, this, [this] { selectResource(lastMap); });
    }
    if (name == "DUNGEON.CBT" && dungeonRoomNavigation) {
        auto back = new QPushButton("Back to dungeon");
        back->setObjectName("backToDungeon");
        back->setToolTip("Return to the dungeon level and cell that opened this room.");
        tabs->setCornerWidget(back);
        connect(back, &QPushButton::clicked, this, [this] { selectResource("DUNGEON.DAT"); });
    }
    QWidget *editor = nullptr;
    for (auto action : menuBar()->actions())
        if (action->text() == "Resource Library")
            action->setChecked(!name.endsWith(".TLK") && !MapDocument::supported(name));
    const auto kind = Workshop::capability(name).editor;
    if (kind == Workshop::EditorKind::Dungeon) {
        auto workspace = new Workshop::DungeonWorkspace(
            &project, &editorState,
            [this](const QMap<QString, QByteArray> &changes, const QString &description) {
                return guard([&] {
                    Project candidate = project;
                    for (auto it = changes.begin(); it != changes.end(); ++it)
                        candidate.resources[it.key()].edited = it.value();
                    candidate.validate();
                    edit(changes, description);
                });
            },
            [this](int room) {
                editorState["DUNGEON.CBT/map"] = room;
                dungeonRoomNavigation = true;
                selectResource("DUNGEON.CBT");
            });
        flushDraft = [workspace] {
            workspace->canvas->cancelStroke();
            return true;
        };
        editor = workspace;
    } else if (kind == Workshop::EditorKind::Artwork)
        editor = graphicsEditor(name);
    else if (kind == Workshop::EditorKind::Conversation)
        editor = dialogueEditor(name);
    else if (kind == Workshop::EditorKind::Story)
        editor = storyEditor();
    else if (kind == Workshop::EditorKind::State)
        editor = stateEditor();
    else if (kind == Workshop::EditorKind::Schedule)
        editor = npcEditor(name);
    else if (kind == Workshop::EditorKind::Map) {
        editor = mapEditor(name);
        if (name.endsWith(".CBT"))
            tabs->addTab(combatEditor(name), "Combat setup");
    }
    if (!editor) {
        QVBoxLayout *layout;
        editor = panel(&layout);
        const auto info = Workshop::capability(name);
        layout->addWidget(hint(info.title + "\n" + info.status));
        auto entries = new QListWidget;
        entries->setObjectName("resourceEntries");
        entries->setToolTip("Stable entries in this resource. Raw edits are available in Resource inspector.");
        Workshop::ResourceDocument document(project, name);
        for (const auto &entry : document.entries()) {
            auto item = new QListWidgetItem(entry.title, entries);
            item->setData(Qt::UserRole, document.id() + "/" + entry.id);
            item->setData(Qt::UserRole + 1, entry.offset);
            item->setToolTip(QString("Offset %1; %2 bytes").arg(entry.offset).arg(entry.length));
        }
        layout->addWidget(entries);
        connect(entries, &QListWidget::itemDoubleClicked, tabs, [tabs](QListWidgetItem *item) {
            tabs->setCurrentIndex(tabs->count()-1);
            if (auto offset = tabs->findChild<QSpinBox *>("resourceByteOffset"))
                offset->setValue(item->data(Qt::UserRole + 1).toInt());
        });
    }
    if (editor)
        tabs->insertTab(0, editor, kind == Workshop::EditorKind::Inspector ? "Resource overview" : "Visual editor");
    tabs->addTab(bytesEditor(name), "Resource inspector");
    tabs->setCurrentIndex(0);
    return tabs;
}
QWidget *WorkshopWindow::mapEditor(const QString &name) {
    auto workspace = new MapWorkspace(
        &project, name, &mapViewStates, &editorState,
        [this](const QMap<QString, QByteArray> &changes, const QString &description) {
            return guard([&] { edit(changes, description); });
        },
        [this](const QString &resource) { selectResource(resource); }, &mapBrushes);
    flushDraft = [workspace] {
        workspace->cancelGesture();
        return true;
    };
    return workspace;
}
QWidget *WorkshopWindow::graphicsEditor(const QString &name) {
    auto graphics = std::make_shared<U5::Graphics>(U5::readGraphics(name, project.data(name)));
    QVBoxLayout *layout;
    auto w = panel(&layout);
    auto row = new QHBoxLayout;
    auto slot = new QComboBox;
    for (int i = 0; i < graphics->images.size(); i++)
        slot->addItem(QString("%1 — %2").arg(i).arg(graphics->images[i].isNull()
                                                        ? "Empty"
                                                        : QString("%1 × %2")
                                                              .arg(graphics->images[i].width())
                                                              .arg(graphics->images[i].height())));
    slot->setObjectName("imageSlot");
    slot->setCurrentIndex(
        qBound(0, editorState.value(name + "/image"), int(graphics->images.size()) - 1));
    row->addWidget(slot, 1);
    auto zoom = new QComboBox;
    zoom->addItems({"1×", "2×", "4×", "6×", "8×", "12×"});
    zoom->setCurrentIndex(3);
    row->addWidget(zoom);
    layout->addLayout(row);
    auto canvas = new PixelCanvas;
    canvas->setObjectName("pixelCanvas");
    canvas->image = graphics->images[slot->currentIndex()];
    canvas->size();
    auto view = makeScroll(canvas);
    view->setObjectName("canvasView");
    auto imageSplit = new QSplitter;
    auto gallery = new QListWidget;
    gallery->setViewMode(QListView::IconMode);
    gallery->setResizeMode(QListView::Adjust);
    gallery->setIconSize(QSize(48, 48));
    gallery->setGridSize(QSize(76, 76));
    gallery->setMaximumWidth(280);
    for (int i = 0; i < graphics->images.size(); i++) {
        auto item = new QListWidgetItem(QIcon(QPixmap::fromImage(graphics->images[i])),
                                        QString::number(i), gallery);
        item->setToolTip(slot->itemText(i));
    }
    gallery->setCurrentRow(slot->currentIndex());
    imageSplit->addWidget(gallery);
    imageSplit->addWidget(view);
    imageSplit->setStretchFactor(1, 1);
    layout->addWidget(imageSplit, 1);
    connect(gallery, &QListWidget::currentRowChanged, w, [=](int i) {
        if (i >= 0)
            slot->setCurrentIndex(i);
    });
    connect(slot, &QComboBox::currentIndexChanged, w, [=](int i) {
        QSignalBlocker block(gallery);
        gallery->setCurrentRow(i);
    });
    auto paletteRow = new QHBoxLayout;
    for (QRgb color : U5::palette()) {
        auto b = new QPushButton;
        b->setFixedSize(30, 30);
        b->setStyleSheet(
            QString("background:%1;border:1px solid #8290a0;").arg(QColor(color).name()));
        paletteRow->addWidget(b);
        connect(b, &QPushButton::clicked, w, [=] { canvas->brush = color; });
    }
    if (!graphics->tiles)
        button(paletteRow, "Transparent", [=] { canvas->brush = qRgba(0, 0, 0, 0); });
    paletteRow->addStretch();
    layout->addLayout(paletteRow);
    connect(slot, &QComboBox::currentIndexChanged, w, [=](int i) {
        editorState[name + "/image"] = i;
        canvas->image = graphics->images[i];
        canvas->size();
    });
    connect(zoom, &QComboBox::currentIndexChanged, w, [=](int i) {
        canvas->zoom = QVector<int>{1, 2, 4, 6, 8, 12}[i];
        canvas->size();
    });
    auto change = [=](const QImage &img) {
        int i = slot->currentIndex();
        QImage old = graphics->images[i];
        graphics->images[i] = img;
        if (!guard([&] {
                edit({{name, U5::writeGraphics(*graphics)}}, "Edit image " + QString::number(i));
            })) {
            graphics->images[i] = old;
            canvas->image = old;
            canvas->update();
        } else {
            canvas->image = img;
            canvas->size();
            gallery->item(i)->setIcon(QIcon(QPixmap::fromImage(img)));
        }
    };
    canvas->commit = change;
    auto actions = new QHBoxLayout;
    button(actions, "Import PNG into slot", [=] {
        QString path = QFileDialog::getOpenFileName(this, "Import EGA PNG", {}, "PNG (*.png)");
        if (path.isEmpty())
            return;
        guard([&] {
            QImage img(path);
            require(!img.isNull() && img.size() == canvas->image.size(),
                    "PNG dimensions must match the selected image");
            change(img.convertToFormat(QImage::Format_ARGB32));
        });
    });
    button(actions, "Export slot PNG", [=] {
        guard([&] {
            QString path = QFileDialog::getSaveFileName(this, "Export PNG", {}, "PNG (*.png)");
            if (path.isEmpty())
                return;
            require(QFileInfo(path).absolutePath() != project.sourceDirectory,
                    "Export images outside the original game folder");
            require(canvas->image.save(path, "PNG"), "Cannot export PNG");
        });
    });
    if (graphics->tiles) {
        button(actions, "Export tilesheet", [=] {
            guard([&] {
                QString path = QFileDialog::getSaveFileName(this, "Export 512 × 256 tilesheet", {},
                                                            "PNG (*.png)");
                if (path.isEmpty())
                    return;
                require(QFileInfo(path).absolutePath() != project.sourceDirectory,
                        "Export outside original game folder");
                QImage sheet(512, 256, QImage::Format_ARGB32);
                sheet.fill(Qt::black);
                QPainter p(&sheet);
                for (int i = 0; i < 512; i++)
                    p.drawImage((i % 32) * 16, (i / 32) * 16, graphics->images[i]);
                p.end();
                require(sheet.save(path, "PNG"), "Cannot export PNG");
            });
        });
        button(actions, "Import tilesheet", [=] {
            QString path = QFileDialog::getOpenFileName(this, "Import 512 × 256 EGA tilesheet", {},
                                                        "PNG (*.png)");
            if (path.isEmpty())
                return;
            guard([&] {
                QImage sheet(path);
                require(sheet.size() == QSize(512, 256), "Tilesheet must be exactly 512 × 256");
                auto next = *graphics;
                for (int i = 0; i < 512; i++)
                    next.images[i] = sheet.copy((i % 32) * 16, (i / 32) * 16, 16, 16)
                                         .convertToFormat(QImage::Format_ARGB32);
                auto bytes = U5::writeGraphics(next);
                edit({{name, bytes}}, "Import tilesheet");
                *graphics = next;
                canvas->image = graphics->images[slot->currentIndex()];
                canvas->size();
                for (int i = 0; i < 512; i++)
                    gallery->item(i)->setIcon(QIcon(QPixmap::fromImage(graphics->images[i])));
            });
        });
    }
    actions->addStretch();
    layout->addLayout(actions);
    layout->addWidget(hint("Paint with EGA colors; right-click picks a pixel. "
                           "Import/export PNG includes the DOS one-bit "
                           "mask. TILES.16 has no alpha. Partial alpha and "
                           "non-EGA colors are rejected."));
    return w;
}
QWidget *WorkshopWindow::dialogueEditor(const QString &name) {
    auto editor = new ConversationEditor(
        &project, name,
        [this, name](const QByteArray &bytes, const QString &description) {
            edit({{name, bytes}}, description);
        },
        [this](const QString &key, const QString &value) {
            history.push(
                new QuestionNameChange(&project, key, value, [this, first = true]() mutable {
                    updateSummary();
                    if (first) {
                        first = false;
                        return;
                    }
                    QTimer::singleShot(0, this, [this] { rebuildEditor(); });
                }));
        },
        editorState.value(name + "/conversation"), editorState.value(name + "/entry"),
        [this, name](int npc, int entry) {
            editorState[name + "/conversation"] = npc;
            editorState[name + "/entry"] = entry;
        });
    flushDraft = [editor] { return editor->flush(); };
    return editor;
}
QWidget *WorkshopWindow::storyEditor() {
    QVBoxLayout *layout;
    auto w = panel(&layout);
    auto pages = U5::storyPages(project.data("STORY.DAT"));
    auto originalPages = U5::storyPages(project.resources["STORY.DAT"].original);
    auto choice = new QComboBox;
    for (int i = 0; i < pages.size(); i++)
        choice->addItem(QString("Page %1 — offset 0x%2")
                            .arg(i + 1)
                            .arg(U5::storyOffsets()[i], 4, 16, QChar('0')));
    choice->setCurrentIndex(qBound(0, editorState.value("STORY.DAT/page"), int(pages.size()) - 1));
    layout->addWidget(choice);
    auto text = new QPlainTextEdit;
    layout->addWidget(text, 1);
    layout->addWidget(Workshop::textPreviewPanel(&project, text));
    auto budget = new QLabel;
    layout->addWidget(budget);
    auto load = [=] {
        int i = choice->currentIndex();
        text->setPlainText(U5::storyPages(project.data("STORY.DAT"))[i]);
    };
    auto count = [=] {
        int i = choice->currentIndex();
        budget->setText(QString("%1 / %2 ASCII bytes")
                            .arg(QString(text->toPlainText()).replace("\n", "\r\n").size())
                            .arg(originalPages[i].size()));
    };
    load();
    count();
    auto selected = std::make_shared<int>(choice->currentIndex());
    connect(choice, &QComboBox::currentIndexChanged, w, [=](int next) {
        int previous = *selected;
        {
            QSignalBlocker block(choice);
            choice->setCurrentIndex(previous);
        }
        if (flushDraft && !flushDraft())
            return;
        {
            QSignalBlocker block(choice);
            choice->setCurrentIndex(next);
        }
        *selected = next;
        editorState["STORY.DAT/page"] = next;
        load();
        count();
    });
    connect(text, &QPlainTextEdit::textChanged, w, count);
    auto row = new QHBoxLayout;
    auto applyButton = button(row, "Apply page", [=] {
        guard([&] {
            int i = choice->currentIndex();
            auto original = project.resources["STORY.DAT"].original,
                 edited = project.data("STORY.DAT");
            QString input = text->toPlainText();
            input.replace("\n", "\r\n"); // DOS story line endings
            auto changed = U5::changeStory(original, i, input);
            int offset = U5::storyOffsets()[i], cap = originalPages[i].size();
            edited.replace(offset, cap, changed.mid(offset, cap));
            edit({{"STORY.DAT", edited}}, "Edit story page");
            load();
        });
    });
    row->addStretch();
    layout->addLayout(row);
    flushDraft = [=] {
        QString normalized = U5::storyPages(project.data("STORY.DAT"))[choice->currentIndex()];
        normalized.replace("\r\n", "\n");
        if (text->toPlainText() == normalized)
            return true;
        auto answer = QMessageBox::question(
            this, "Unapplied story edit", "Apply your story edit before continuing?",
            QMessageBox::Apply | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel)
            return false;
        if (answer == QMessageBox::Discard) {
            load();
            return true;
        }
        applyButton->click();
        normalized = U5::storyPages(project.data("STORY.DAT"))[choice->currentIndex()];
        normalized.replace("\r\n", "\n");
        return text->toPlainText() == normalized;
    };
    layout->addWidget(hint("Apply commits this page. Pages retain the engine's "
                           "fixed offsets and original "
                           "capacity. ASCII text only; intro index 6 is dynamic "
                           "and has no stored page."));
    return w;
}
#include "state_fields.h"
QWidget *WorkshopWindow::stateEditor() {
    require(project.data("INIT.GAM").size() == 4192, "Starting state must be 4192 bytes");
    auto tabs = new QTabWidget;
    auto numeric = [this](QFormLayout *form, QWidget *owner, const QString &label, int off,
                          int width, const QString &help) {
        auto sp = new QSpinBox;
        sp->setRange(0, width == 1 ? 255 : 65535);
        sp->setValue(width == 1 ? U5::byte(project.data("INIT.GAM"), off)
                                : U5::word(project.data("INIT.GAM"), off));
        sp->setToolTip(help + QString(" (offset 0x%1)").arg(off, 4, 16, QChar('0')));
        form->addRow(label, sp);
        if (off == 0x2b5) {
            sp->setReadOnly(true);
            sp->setButtonSymbols(QAbstractSpinBox::NoButtons);
        } else
            connect(sp, &QSpinBox::valueChanged, owner, [this, off, width, label](int value) {
                auto bytes = project.data("INIT.GAM");
                if (width == 1)
                    bytes[off] = char(value);
                else
                    U5::setWord(bytes, off, value);
                edit({{"INIT.GAM", bytes}}, "Set " + label);
            });
    };
    auto values = new QWidget;
    auto form = new QFormLayout(values);
    for (const auto &field : stateFields)
        numeric(form, values, QString::fromLatin1(field.name), field.offset, field.width,
                QString::fromLatin1(field.help));
    tabs->addTab(makeScroll(values, true), "Party / time / supplies");
    auto quests = new QWidget;
    auto qform = new QFormLayout(quests);
    QStringList virtues = {"Honesty",   "Compassion", "Valor",        "Justice",
                           "Sacrifice", "Honor",      "Spirituality", "Humility"};
    QStringList dungeons = {"Deceit",   "Despise", "Destard",  "Wrong",
                            "Covetous", "Shame",   "Hythloth", "Doom"};
    for (int i = 0; i < 8; i++) {
        numeric(qform, quests, QString("Moonstone %1 X").arg(i + 1), 0x28a + i, 1, {});
        numeric(qform, quests, QString("Moonstone %1 Y").arg(i + 1), 0x292 + i, 1, {});
        numeric(qform, quests, QString("Moonstone %1 inventory flag").arg(i + 1), 0x29a + i, 1,
                "0 = buried, 255 = carried");
        numeric(qform, quests, QString("Moonstone %1 world").arg(i + 1), 0x2a2 + i, 1,
                "0 = Britannia, 255 = Underworld");
    }
    QStringList reagents = {"Sulphurous ash", "Ginseng",     "Garlic",     "Spider silk",
                            "Blood moss",     "Black pearl", "Nightshade", "Mandrake root"};
    for (int i = 0; i < 8; i++)
        numeric(qform, quests, reagents[i], 0x2aa + i, 1, {});
    numeric(qform, quests, "Keys", 0x206, 1, {});
    numeric(qform, quests, "Gems", 0x207, 1, {});
    numeric(qform, quests, "Torches", 0x208, 1, {});
    for (int i = 0; i < 3; i++)
        numeric(qform, quests, QString("Prevent Shadowlord visits %1").arg(i + 1), 0x322 + i, 1,
                "255 prevents visits");
    auto flags = [this, quests, qform, virtues](const QString &label, int offset) {
        auto box = new QWidget;
        auto row = new QHBoxLayout(box);
        for (int bit = 0; bit < 8; bit++) {
            auto check = new QCheckBox(virtues[bit]);
            check->setChecked(U5::byte(project.data("INIT.GAM"), offset) & (1 << bit));
            row->addWidget(check);
            connect(check, &QCheckBox::toggled, quests, [this, bit, offset, label](bool on) {
                auto bytes = project.data("INIT.GAM");
                int value = U5::byte(bytes, offset);
                bytes[offset] = char(on ? value | (1 << bit) : value & ~(1 << bit));
                edit({{"INIT.GAM", bytes}}, label);
            });
        }
        qform->addRow(label, box);
    };
    flags("Ordained", 0x326);
    flags("Completed Codex pilgrimage", 0x328);
    for (int i = 0; i < 8; i++) {
        numeric(qform, quests, dungeons[i] + " open flags", 0x32a + i, 1, "Bit 7 = open");
        numeric(qform, quests, virtues[i] + " shrine status", 0x332 + i, 1, "Bit 7 = destroyed");
    }
    tabs->addTab(makeScroll(quests, true), "Moonstones / quests");
    QVBoxLayout *characterLayout;
    auto characters = panel(&characterLayout);
    auto selector = new QComboBox;
    for (int i = 0; i < 16; i++)
        selector->addItem(
            QString("%1 — %2%3")
                .arg(i)
                .arg(QString::fromLatin1(
                    project.data("INIT.GAM").mid(2 + i * 32, 9).split(char(0)).first()))
                .arg(i < int(U5::byte(project.data("INIT.GAM"), 0x2b5)) ? " (party)" : ""));
    characterLayout->addWidget(selector);
    auto detail = new QStackedWidget;
    characterLayout->addWidget(detail, 1);
    for (int i = 0; i < 16; i++) {
        auto card = new QWidget;
        auto cf = new QFormLayout(card);
        int base = 2 + i * 32;
        auto name = new QLineEdit(
            QString::fromLatin1(project.data("INIT.GAM").mid(base, 9).split(char(0)).first()));
        name->setMaxLength(8);
        cf->addRow("Name (8 ASCII characters)", name);
        connect(name, &QLineEdit::editingFinished, card, [this, name, base] {
            guard([&] {
                QByteArray encoded;
                for (QChar c : name->text()) {
                    require(c.unicode() >= 32 && c.unicode() <= 126, "Names require ASCII text");
                    encoded.append(char(c.unicode()));
                }
                encoded.append(QByteArray(9 - encoded.size(), 0));
                auto bytes = project.data("INIT.GAM");
                bytes.replace(base, 9, encoded);
                edit({{"INIT.GAM", bytes}}, "Rename character");
            });
        });
        const QStringList names = {"Gender (11 male / 12 female)",
                                   "Class (ASCII code)",
                                   "Status (ASCII code)",
                                   "Strength",
                                   "Dexterity",
                                   "Intelligence",
                                   "Magic"};
        for (int j = 0; j < names.size(); j++)
            numeric(cf, card, names[j], base + 9 + j, 1, {});
        numeric(cf, card, "HP", base + 16, 2, {});
        numeric(cf, card, "Maximum HP", base + 18, 2, {});
        numeric(cf, card, "Experience", base + 20, 2, {});
        numeric(cf, card, "Level", base + 22, 1, {});
        for (int j = 0; j < 6; j++)
            numeric(cf, card, QString("Equipment slot %1").arg(j + 1), base + 25 + j, 1, {});
        numeric(cf, card, "Home map ID", base + 31, 1,
                "Party membership is a contiguous prefix, independent of this field");
        auto membership = new QPushButton(i < int(U5::byte(project.data("INIT.GAM"), 0x2b5))
                                              ? "Remove from party…"
                                              : "Add to party");
        membership->setEnabled(i != 0);
        cf->addRow(membership);
        connect(membership, &QPushButton::clicked, card, [this, i] {
            guard([&] {
                auto b = project.data("INIT.GAM");
                int count = U5::byte(b, 0x2b5);
                require(count >= 1 && count <= 6, "Invalid party size");
                QVector<QByteArray> records;
                QVector<int> order;
                for (int r = 0; r < 16; r++) {
                    records << b.mid(2 + r * 32, 32);
                    order << r;
                }
                if (i < count) {
                    bool ok;
                    int home = QInputDialog::getInt(this, "Companion home",
                                                    "Settlement map ID (1–32)", 1, 1, 32, 1, &ok);
                    if (!ok)
                        return;
                    require(i != 0, "The Avatar cannot leave");
                    records[i][31] = char(home);
                    records[i][23] = 0;
                    records.append(records.takeAt(i));
                    order.append(order.takeAt(i));
                    --count;
                } else {
                    require(count < 6 && records[i][0] != 0,
                            "Party is full or selected record is empty");
                    records[i][31] = 0;
                    records.swapItemsAt(i, count);
                    order.swapItemsAt(i, count);
                    ++count;
                }
                int active = order.indexOf(U5::byte(b, 0x2d5));
                b[0x2d5] = char(active >= 0 && active < count ? active : 255);
                b[0x2b5] = char(count);
                for (int r = 0; r < 16; r++)
                    b.replace(2 + r * 32, 32, records[r]);
                edit({{"INIT.GAM", b}}, "Change party membership");
                rebuildEditor();
            });
        });
        detail->addWidget(makeScroll(card, true));
    }
    connect(selector, &QComboBox::currentIndexChanged, characters,
            [=](int i) { detail->setCurrentIndex(i); });
    tabs->addTab(characters, "Characters");
    return tabs;
}
QWidget *WorkshopWindow::npcEditor(const QString &name) {
    require(project.data(name).size() == 4608, "NPC schedules must be 4608 bytes");
    QVBoxLayout *layout;
    auto w = panel(&layout);
    auto row = new QHBoxLayout;
    auto settlement = new QComboBox;
    QString mapName = name.left(name.size() - 4) + ".DAT";
    QStringList labels;
    try {
        for (auto p : U5::mapPages(mapName, project.data(mapName))) {
            QString label = p.name.section(" — ", 0, 0);
            if (!labels.contains(label))
                labels << label;
        }
    } catch (...) {
        for (int i = 0; i < 8; i++)
            labels << QString("Settlement %1").arg(i + 1);
    }
    settlement->addItems(labels);
    settlement->setCurrentIndex(qBound(0, editorState.value(name + "/settlement"), 7));
    row->addWidget(settlement, 1);
    auto npc = new QSpinBox;
    npc->setRange(0, 31);
    npc->setValue(editorState.value(name + "/npc"));
    row->addWidget(new QLabel("NPC"));
    row->addWidget(npc);
    layout->addLayout(row);
    auto table = new QTableWidget(18, 3);
    table->setHorizontalHeaderLabels({"Field", "Offset", "Value"});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(table, 1);
    auto load = [=] {
        QSignalBlocker blocked(table);
        int base = settlement->currentIndex() * 576 + npc->value() * 16;
        QStringList fields = {"Behavior: change 1",
                              "Behavior: changes 2 and 4",
                              "Behavior: change 3",
                              "Destination X: change 1",
                              "Destination X: changes 2 and 4",
                              "Destination X: change 3",
                              "Destination Y: change 1",
                              "Destination Y: changes 2 and 4",
                              "Destination Y: change 3",
                              "Floor: change 1",
                              "Floor: changes 2 and 4",
                              "Floor: change 3",
                              "Start hour: change 1",
                              "Start hour: change 2",
                              "Start hour: change 3",
                              "Start hour: change 4",
                              "Appearance code",
                              "Conversation number"};
        for (int r = 0; r < 18; r++) {
            int off = r < 16
                          ? base + r
                          : settlement->currentIndex() * 576 + 512 + (r - 16) * 32 + npc->value();
            auto label = new QTableWidgetItem(fields[r]);
            label->setFlags(label->flags() & ~Qt::ItemIsEditable);
            table->setItem(r, 0, label);
            auto pos = new QTableWidgetItem(QString::number(off, 16));
            pos->setFlags(pos->flags() & ~Qt::ItemIsEditable);
            table->setItem(r, 1, pos);
            int v = U5::byte(project.data(name), off);
            if (r >= 9 && r < 12 && v >= 128)
                v -= 256;
            auto value = new QTableWidgetItem(QString::number(v));
            value->setData(Qt::UserRole, off);
            table->setItem(r, 2, value);
        }
    };
    load();
    connect(settlement, &QComboBox::currentIndexChanged, w, [=](int i) {
        editorState[name + "/settlement"] = i;
        load();
    });
    connect(npc, &QSpinBox::valueChanged, w, [=](int i) {
        editorState[name + "/npc"] = i;
        load();
    });
    connect(table, &QTableWidget::itemChanged, w, [=](QTableWidgetItem *item) {
        if (item->column() != 2)
            return;
        if (!guard([&] {
                bool ok;
                int v = item->text().toInt(&ok);
                bool z = item->row() >= 9 && item->row() < 12;
                require(ok && v >= (z ? -128 : 0) && v <= (z ? 127 : 255),
                        "NPC value is outside its byte range");
                auto b = project.data(name);
                b[item->data(Qt::UserRole).toInt()] = char(v);
                edit({{name, b}}, "Edit NPC schedule");
            }))
            load();
    });
    layout->addWidget(
        hint("Four schedule changes choose three destinations and behaviors. Changes 2 and 4 share "
             "a destination: editing either changes both. Start hours normally use 0–23; unusual "
             "values are preserved. Floor −1 is the basement. Behavior and appearance codes are "
             "advanced values; appearance code +256 selects the sprite. Conversation numbers link "
             "to this town's dialogue."));
    return w;
}
QWidget *WorkshopWindow::combatEditor(const QString &name) {
    auto pages = U5::mapPages(name, project.data(name));
    QVBoxLayout *layout;
    auto w = panel(&layout);
    auto choice = new QComboBox;
    for (auto page : pages)
        choice->addItem(page.name);
    layout->addWidget(choice);
    auto table = new QTableWidget(11, 21);
    QStringList headers;
    for (int i = 0; i < 21; i++)
        headers << QString::number(i);
    table->setHorizontalHeaderLabels(headers);
    table->setVerticalHeaderLabels({"New tiles (8)", "Party E X/Y (6+6)", "Party W X/Y",
                                    "Party S X/Y", "Party N X/Y", "Monster tiles (16)",
                                    "Monster X (16)", "Monster Y (16)", "Triggers X/Y (8+8)",
                                    "Change 1 X/Y", "Change 2 X/Y"});
    layout->addWidget(table, 1);
    auto load = [=] {
        QSignalBlocker blocked(table);
        int base = pages[choice->currentIndex()].offset;
        for (int y = 0; y < 11; y++)
            for (int x = 0; x < 21; x++) {
                int off = base + y * 32 + 11 + x;
                auto item =
                    new QTableWidgetItem(QString::number(U5::byte(project.data(name), off)));
                item->setData(Qt::UserRole, off);
                table->setItem(y, x, item);
            }
    };
    load();
    connect(choice, &QComboBox::currentIndexChanged, w, [=] { load(); });
    connect(table, &QTableWidget::itemChanged, w, [=](QTableWidgetItem *item) {
        if (!guard([&] {
                bool ok;
                int v = item->text().toInt(&ok);
                require(ok && v >= 0 && v <= 255, "Combat setup values must be 0–255");
                auto b = project.data(name);
                b[item->data(Qt::UserRole).toInt()] = char(v);
                edit({{name, b}}, "Edit combat setup");
            }))
            load();
    });
    layout->addWidget(hint("All 21 metadata bytes per row are preserved. Normal coordinates "
                           "are 0–10; sentinel and unknown "
                           "values remain editable as 0–255. Party rows: six X followed by six "
                           "Y; trigger/change rows: "
                           "eight X then eight Y. Remaining cells are unknown/padding."));
    return w;
}
QWidget *WorkshopWindow::bytesEditor(const QString &name) {
    QVBoxLayout *layout;
    auto w = panel(&layout);
    auto row = new QHBoxLayout;
    auto offset = new QSpinBox;
    offset->setObjectName("resourceByteOffset");
    offset->setRange(0, qMax(0, int(project.data(name).size()) - 1));
    offset->setDisplayIntegerBase(16);
    offset->setPrefix("0x");
    row->addWidget(new QLabel("Start offset"));
    row->addWidget(offset);
    row->addStretch();
    layout->addLayout(row);
    auto table = new QTableWidget(256, 16);
    table->setObjectName("byteInspector");
    table->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    QStringList headers;
    for (int i = 0; i < 16; i++)
        headers << QString::number(i, 16).toUpper();
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setDefaultSectionSize(42);
    layout->addWidget(table, 1);
    auto load = [=] {
        QSignalBlocker blocked(table);
        const auto &b = project.data(name);
        QStringList rows;
        for (int y = 0; y < 256; y++) {
            rows << QString("%1").arg(offset->value() + y * 16, 6, 16, QChar('0'));
            for (int x = 0; x < 16; x++) {
                int pos = offset->value() + y * 16 + x;
                auto item = new QTableWidgetItem(
                    pos < b.size()
                        ? QString("%1").arg(U5::byte(b, pos), 2, 16, QChar('0')).toUpper()
                        : "");
                item->setData(Qt::UserRole, pos);
                if (pos >= b.size())
                    item->setFlags(Qt::NoItemFlags);
                table->setItem(y, x, item);
            }
        }
        table->setVerticalHeaderLabels(rows);
    };
    load();
    connect(offset, &QSpinBox::valueChanged, w, [=] { load(); });
    connect(table, &QTableWidget::itemChanged, w, [=](QTableWidgetItem *item) {
        if (!guard([&] {
                bool ok;
                int value = item->text().toInt(&ok, 16);
                require(ok && value >= 0 && value <= 255 && item->text().size() <= 2,
                        "Enter one hexadecimal byte (00–FF)");
                auto b = project.data(name);
                int pos = item->data(Qt::UserRole).toInt();
                require(pos >= 0 && pos < b.size(), "Byte offset out of range");
                b[pos] = char(value);
                edit({{name, b}}, "Edit " + name + " byte");
            }))
            load();
    });
    auto actions = new QHBoxLayout;
    button(actions, "Import edited resource…", [=] {
        QString path = QFileDialog::getOpenFileName(
            this, "Import an existing editor's output as a project override");
        if (path.isEmpty())
            return;
        guard([&] {
            Project next = project;
            auto b = readFile(path);
            next.resources[name].edited = b;
            next.validate();
            edit({{name, b}}, "Import " + name);
            rebuildEditor();
        });
    });
    button(actions, "Revert resource", [=] {
        if (QMessageBox::question(this, "Revert resource", "Discard edits to " + name + "?",
                                  QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
            return;
        edit({{name, project.resources[name].original}}, "Revert " + name);
        rebuildEditor();
    });
    actions->addStretch();
    layout->addLayout(actions);
    layout->addWidget(hint("Advanced hexadecimal inspector. Each edit is "
                           "undoable. Import can bring existing editor "
                           "outputs into a mod without changing the original "
                           "files. Resource checks run on project save "
                           "and package export; unknown fields still require "
                           "knowledge of the DOS format."));
    return w;
}

bool WorkshopWindow::reviewPackage(bool launch) {
    if (flushDraft && !flushDraft())
        return false;
    if (project.resources.isEmpty())
        return false;
    const auto diagnostics = validateMod(project);
    bool errors = false;
    QDialog dialog(this);
    dialog.setWindowTitle(launch ? "Validate and test mod" : "Validation / package preview");
    dialog.resize(850, 500);
    auto layout = new QVBoxLayout(&dialog);
    auto explanation = new QLabel(
        "Double-click a resource to inspect it. Warnings preserve unusual values; errors block "
        "export/testing. Overlapping installed packages are rejected, not merged. Test Mod ignores "
        "installed packages and uses a separate runtime folder.");
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto report = new QTreeWidget;
    report->setObjectName("modValidationReport");
    report->setHeaderLabels({"Result", "Resource", "Details"});
    report->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    for (const auto &diagnostic : diagnostics) {
        errors |= diagnostic.severity == ModDiagnostic::Error;
        auto item =
            new QTreeWidgetItem(report, {diagnostic.severity == ModDiagnostic::Error     ? "Error"
                                         : diagnostic.severity == ModDiagnostic::Warning ? "Warning"
                                                                                         : "Info",
                                         diagnostic.resource, diagnostic.message});
        item->setToolTip(2, diagnostic.message);
        item->setData(0, Qt::UserRole, diagnostic.resource);
        item->setData(0, Qt::UserRole + 1, diagnostic.entryId);
    }
    report->setWordWrap(true);
    layout->addWidget(report, 1);
    connect(report, &QTreeWidget::itemDoubleClicked, &dialog,
            [this, &dialog](QTreeWidgetItem *item) {
                if (project.resources.contains(item->data(0, Qt::UserRole).toString())) {
                    if (item->data(0, Qt::UserRole).toString() == "DUNGEON.DAT") {
                        auto parts = item->data(0, Qt::UserRole + 1).toString().split('/');
                        if (parts.size() == 6 && parts[0] == "dungeon" && parts[2] == "level" &&
                            parts[4] == "cell") {
                            editorState["DUNGEON.DAT/dungeon"] = parts[1].toInt();
                            editorState["DUNGEON.DAT/level"] = parts[3].toInt();
                            editorState["DUNGEON.DAT/x"] = parts[5].toInt() % 8;
                            editorState["DUNGEON.DAT/y"] = parts[5].toInt() / 8;
                        }
                    }
                    selectResource(item->data(0, Qt::UserRole).toString());
                    dialog.reject();
                }
            });
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto proceed = buttons->addButton(launch ? "Launch isolated test" : "Continue",
                                      QDialogButtonBox::AcceptRole);
    proceed->setEnabled(!errors && !project.changed().isEmpty());
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    if (!launch)
        return true;
    return guard([&] {
        QString executable = QSettings().value("paths/engine").toString();
        if (!QFileInfo(executable).isFile() &&
            !(QFileInfo(executable).isDir() && executable.endsWith(".app", Qt::CaseInsensitive))) {
            executable = QFileDialog::getOpenFileName(this, "Choose Impera executable or AppImage",
                                                      executable);
            if (executable.isEmpty())
                return;
        }
        if (executable.endsWith(".app", Qt::CaseInsensitive))
            executable = QDir(executable).filePath("Contents/MacOS/impera-engine");
        require(QFileInfo(executable).isFile() && QFileInfo(executable).isExecutable(),
                "Choose an executable Impera file");
        QSettings().setValue("paths/engine", executable);
        QString root = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                           .filePath("test-sessions");
        QString session = prepareModTest(project, root);
        QSettings().setValue("paths/testSession", session);
        auto process = new QProcess(this);
        process->setProgram(QFileInfo(executable).absoluteFilePath());
        process->setWorkingDirectory(session);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("U5D_DATA_DIR", session);
        environment.insert("U5D_RUNTIME_DIR", session);
        // Existing Linux release launchers use XDG_DATA_HOME; isolate those too.
        environment.insert("XDG_DATA_HOME", session);
        environment.insert("XDG_CONFIG_HOME", QDir(session).filePath("config"));
        environment.insert("XDG_CACHE_HOME", QDir(session).filePath("cache"));
        process->setProcessEnvironment(environment);
        process->setStandardOutputFile(QDir(session).filePath("engine-output.log"));
        process->setStandardErrorFile(QDir(session).filePath("engine-errors.log"));
        connect(process, &QProcess::errorOccurred, this, [this, process, session] {
            if (process->error() == QProcess::FailedToStart)
                process->deleteLater();
            QMessageBox::warning(this, "Mod test launch failed",
                                 process->errorString() + "\nTest files/logs: " + session);
        });
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, process, session](int code, QProcess::ExitStatus status) {
                    statusBar()->showMessage(
                        QString("Test finished (%1). Saves and logs: %2")
                            .arg(status == QProcess::CrashExit ? "crashed" : QString::number(code))
                            .arg(session),
                        30000);
                    process->deleteLater();
                });
        process->start();
        statusBar()->showMessage("Launching an isolated test in " + session +
                                     ". Start a NEW character to exercise this mod's starting "
                                     "state. Saves/logs stay in this test folder.",
                                 30000);
    });
}
