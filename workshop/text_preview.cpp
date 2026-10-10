#include "text_preview.h"
#include "formats.h"
#include "project.h"
#include <QtWidgets>
namespace Workshop {
TextPreview previewText(const QByteArray &font, const QString &text, FontMode mode, int width) {
    U5::require(width > 0 && width <= 4096 && text.size() <= 65536,
                "Text preview exceeds supported bounds");
    struct Glyph {
        int width, height;
        QByteArray pixels;
    };
    QVector<Glyph> glyphs;
    if (mode == FontMode::Proportional) {
        int count = U5::word(font, 0);
        U5::require(count > 0 && count <= 256 && font.size() >= 2 + count * 2,
                    "Invalid proportional font directory");
        for (int i = 0; i < count; ++i) {
            int offset = U5::word(font, 2 + i * 2);
            U5::require(offset >= 2 + count * 2, "Glyph overlaps proportional font directory");
            int w = U5::word(font, offset), h = U5::word(font, offset + 2);
            U5::require(w >= 0 && w <= 64 && h > 0 && h <= 64,
                        "Invalid proportional glyph dimensions");
            int bytes = ((w + 7) / 8) * h;
            U5::require(offset <= font.size() - 4 && bytes <= font.size() - offset - 4,
                        "Truncated proportional glyph");
            glyphs.append({w, h, font.mid(offset + 4, bytes)});
        }
    } else {
        U5::require(font.size() == 1024, "Bitmap font must contain 128 eight-byte glyphs");
        for (int i = 0; i < 128; ++i)
            glyphs.append({8, 8, font.mid(i * 8, 8)});
    }
    TextPreview result;
    // Bound allocations even for pathological documents.
    result.image =
        QImage(width, qMin(1024, qMax(9, int(text.size() + 1) * 9)), QImage::Format_RGB32);
    result.image.fill(Qt::black);
    int x = 0, y = 0;
    for (auto ch : text) {
        if (ch == '\r')
            continue;
        if (ch == '\n') {
            x = 0;
            y += 9;
            continue;
        }
        if (mode == FontMode::Proportional && ch == '_')
            continue; // Discretionary hyphen.
        if (mode == FontMode::Proportional && ch == '{') {
            x += 15;
            continue;
        } // Paragraph indent.
        int index = int(ch.unicode()) - (mode == FontMode::Proportional ? 32 : 0);
        bool unsupported = index < 0 || index >= glyphs.size();
        Glyph g = unsupported ? Glyph{8, 8, QByteArray(8, char(0xff))} : glyphs[index];
        const int advance =
            mode == FontMode::Proportional ? (ch == ' ' ? 5 : g.width + 1) : g.width;
        if (x + advance > width && x) {
            x = 0;
            y += 9;
        }
        if (y + g.height >= result.image.height()) {
            result.issues.append("Preview truncated; document text is unchanged.");
            break;
        }
        if (unsupported &&
            !result.issues.contains(
                QString("Unsupported character U+%1").arg(ch.unicode(), 4, 16, QChar('0'))))
            result.issues.append(
                QString("Unsupported character U+%1").arg(ch.unicode(), 4, 16, QChar('0')));
        for (int row = 0; row < g.height; ++row)
            for (int col = 0; col < g.width && x + col < width; ++col)
                if (static_cast<unsigned char>(g.pixels[row * ((g.width + 7) / 8) + col / 8]) &
                    (128 >> (col % 8)))
                    result.image.setPixel(x + col, y + row,
                                          unsupported ? qRgb(255, 80, 80) : qRgb(255, 255, 255));
        x += advance;
    }
    result.image = result.image.copy(0, 0, width, qMin(result.image.height(), y + 9));
    return result;
}
} // namespace Workshop
namespace Workshop {
QWidget *textPreviewPanel(const Project *project, QPlainTextEdit *text) {
    auto panel = new QWidget;
    panel->setObjectName("textPreviewPanel");
    auto layout = new QVBoxLayout(panel);
    auto mode = new QComboBox;
    mode->addItems({"Normal font", "Runic font", "Proportional font"});
    mode->setToolTip(
        "Approximate game-font preview. Text encoding and scene layout remain format-specific.");
    auto image = new QLabel;
    image->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    auto scroll = new QScrollArea;
    scroll->setWidget(image);
    scroll->setWidgetResizable(true);
    scroll->setMinimumHeight(96);
    scroll->setMaximumHeight(240);
    scroll->setToolTip("Scroll to inspect the font preview; scene-specific layout is approximate.");
    auto issues = new QLabel;
    issues->setWordWrap(true);
    layout->addWidget(mode);
    layout->addWidget(scroll);
    layout->addWidget(issues);
    auto update = [=] {
        QList<QTextEdit::ExtraSelection> selections;
        const auto input = text->toPlainText();
        for (int i = 0; i < input.size(); ++i) {
            if (input[i].unicode() <= 127)
                continue;
            QTextEdit::ExtraSelection selection;
            selection.cursor = text->textCursor();
            selection.cursor.setPosition(i);
            selection.cursor.setPosition(i + 1, QTextCursor::KeepAnchor);
            selection.format.setBackground(QColor(100, 30, 30));
            selection.format.setToolTip(
                "This character is not representable by the DOS ASCII text format.");
            selections.append(selection);
        }
        text->setExtraSelections(selections);
        try {
            const auto kind = static_cast<FontMode>(mode->currentIndex());
            QByteArray font;
            if (kind == FontMode::Proportional) {
                QDir dir(project->sourceDirectory);
                QString path;
                for (auto name : dir.entryList(QDir::Files))
                    if (name.compare("PROPORT.PCS", Qt::CaseInsensitive) == 0)
                        path = dir.filePath(name);
                QFile f(path);
                U5::require(
                    !path.isEmpty() && f.open(QIODevice::ReadOnly) && f.size() <= 1024 * 1024,
                    "Proportional preview needs PROPORT.PCS in the game folder (read only).");
                font = U5::decompress(f.readAll());
            } else
                font = project->data(kind == FontMode::Runic ? "RUNES.CH" : "IBM.CH");
            auto result = previewText(font, text->toPlainText(), kind, 320);
            image->setPixmap(QPixmap::fromImage(
                result.image.scaled(result.image.width() * 2, result.image.height() * 2,
                                    Qt::IgnoreAspectRatio, Qt::FastTransformation)));
            issues->setText(result.issues.join("\n"));
            issues->setStyleSheet("color: #d65c5c");
        } catch (const std::exception &e) {
            image->clear();
            issues->setText(QString::fromUtf8(e.what()));
        }
    };
    QObject::connect(mode, qOverload<int>(&QComboBox::currentIndexChanged), panel,
                     [=](int) { update(); });
    QObject::connect(text, &QPlainTextEdit::textChanged, panel, update);
    update();
    return panel;
}
} // namespace Workshop
