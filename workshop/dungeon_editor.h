#pragma once
#include "map_editor.h"
namespace Workshop {
class DungeonDocument {
  public:
    explicit DungeonDocument(const Project *project);
    QByteArray level(int dungeon, int floor, bool original = false) const;
    QMap<QString, QByteArray> changes(int dungeon, int floor, const QByteArray &cells) const;
    static int offset(int dungeon, int floor);
    static int roomIndex(int dungeon, int cell);
    static QString feature(int cell);
    static QString variant(int cell);
    QStringList warnings(int dungeon, int floor, int x, int y) const;
    static void validate(const Project &project, bool allRoomReferences = false);

  private:
    const Project *project;
};
class DungeonWorkspace : public QWidget {
  public:
    DungeonWorkspace(Project *project, QMap<QString, int> *state,
                     std::function<bool(const QMap<QString, QByteArray> &, const QString &)> commit,
                     std::function<void(int)> room, QWidget *parent = nullptr);
    MapCanvas *canvas;
};
} // namespace Workshop
