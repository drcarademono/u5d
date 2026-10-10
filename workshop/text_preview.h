#pragma once
#include <QImage>
#include <QStringList>
#include <QWidget>
class QPlainTextEdit;
class Project;
namespace Workshop {
enum class FontMode { Normal, Runic, Proportional };
struct TextPreview {
    QImage image;
    QStringList issues;
};
// Proportional input is the decoded DOS bit-image resource, not conversation tokens.
TextPreview previewText(const QByteArray &font, const QString &text, FontMode mode,
                        int width = 144);
QWidget *textPreviewPanel(const Project *project, QPlainTextEdit *text);
} // namespace Workshop
