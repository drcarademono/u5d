#pragma once
#include "project.h"
namespace Workshop {
enum class EditorKind { Artwork, Conversation, Schedule, State, Story, Map, Dungeon, Inspector };
struct Capability {
    QString title, group, status;
    EditorKind editor = EditorKind::Inspector;
};
Capability capability(const QString &resource);
struct Entry {
    QString id, title;
    qsizetype offset = 0, length = 0;
};
// A live view of Project, never a second authoritative copy of resource bytes.
class ResourceDocument {
  public:
    ResourceDocument(const Project &project, QString resource);
    QString id() const { return resource; }
    QVector<Entry> entries() const;
    QByteArray read(const Entry &entry) const;
    QMap<QString, QByteArray> replace(const Entry &entry, const QByteArray &bytes) const;

  private:
    const Project &project;
    QString resource;
};
void validateResource(const Project &project, const QString &name);
} // namespace Workshop
