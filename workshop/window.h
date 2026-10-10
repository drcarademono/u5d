#pragma once
#include "map_editor.h"
#include "project.h"
#include <QLabel>
#include <QMainWindow>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QUndoStack>
#include <functional>
class WorkshopWindow : public QMainWindow {
  public:
    WorkshopWindow();
    bool openGame(const QString &path);
    bool restoreGameDirectory();
    bool openProject(const QString &path);
    Project &projectForTests() { return project; }
    void selectResource(const QString &name);

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    Project project;
    QUndoStack history;
    QTreeWidget *assets;
    QStackedWidget *content;
    QLabel *summary;
    QString current, savedTitle, lastMap;
    bool rebuildPending = false;
    bool dungeonRoomNavigation = false;
    std::function<bool()> flushDraft;
    QMap<QString, int> editorState;
    QMap<QString, MapViewState> mapViewStates;
    MapBrushState mapBrushes;
    bool guard(const std::function<void()> &operation);
    bool canLeave();
    bool reviewPackage(bool launch);
    bool saveProject();
    void rebuildTree();
    void updateSummary();
    void edit(const QMap<QString, QByteArray> &changes, const QString &description);
    QWidget *resourceEditor(const QString &name);
    QWidget *mapEditor(const QString &name);
    QWidget *graphicsEditor(const QString &name);
    QWidget *dialogueEditor(const QString &name);
    QWidget *storyEditor();
    QWidget *stateEditor();
    QWidget *npcEditor(const QString &name);
    QWidget *combatEditor(const QString &name);
    QWidget *bytesEditor(const QString &name);
    void rebuildEditor();
};
