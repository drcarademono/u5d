#pragma once
#include "project.h"
#include <QWidget>
#include <functional>
namespace Workshop {
struct SignRecord {
    int map, floor, x, y;
    QByteArray text;
    bool operator==(const SignRecord &r) const {
        return map == r.map && floor == r.floor && x == r.x && y == r.y && text == r.text;
    }
};
QVector<SignRecord> readSigns(const QByteArray &bytes);
QByteArray writeSigns(const QByteArray &original, const QVector<SignRecord> &records);
QString signSource(const QByteArray &bytes);
QByteArray encodeSign(const QString &text);
QByteArray description(const QByteArray &bytes, int entry);
QByteArray changeDescription(const QByteArray &bytes, int entry, const QString &text, bool linked);
using TextCommit = std::function<bool(const QMap<QString, QByteArray> &, const QString &)>;
QWidget *locationTextEditor(Project *project, const QString &resource, TextCommit commit,
                            QMap<QString, int> *state = nullptr,
                            std::function<bool()> *flush = nullptr);
} // namespace Workshop
