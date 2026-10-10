#pragma once
#include "formats.h"
#include <QMap>
struct Resource {
    QByteArray original, edited;
};
class Project {
  public:
    QString sourceDirectory, title = "Untitled mod", projectPath;
    QMap<QString, Resource> resources;
    QMap<QString, QString> dialogueNames; // Project-only question annotations;
                                          // never exported as TLK data.
    void openGame(const QString &directory);
    void save(const QString &path);
    void load(const QString &path);
    QByteArray package() const;
    void importPackage(const QByteArray &bytes);
    QStringList changed() const;
    const QByteArray &data(const QString &name) const;
    void validate() const;
};

struct ModDiagnostic {
    enum Severity { Information, Warning, Error };
    Severity severity;
    QString resource, message;
    QString entryId = "resource"; // Stable document-relative ID; never a display label.
};
QVector<ModDiagnostic> validateMod(const Project &project);
// Produces a new, private runtime. No original data, Mods or saves are written.
QString prepareModTest(const Project &project, const QString &sessionsDirectory);
