#include "text_preview.h"
#include "dialogue_editor.h"
#include <memory>
using namespace Dialogue;
static QPushButton *button(QBoxLayout *layout, const QString &text,
                           const std::function<void()> &fn) {
    auto b = new QPushButton(text);
    layout->addWidget(b);
    QObject::connect(b, &QPushButton::clicked, b, fn);
    return b;
}
static QString caption(const QByteArray &bytes) {
    QString s;
    try {
        for (auto b : blocks(bytes))
            if (b.opcode < 0)
                s += plain(b.bytes);
            else if (b.opcode == 129)
                s += "{Avatar}";
    } catch (...) {
    }
    return s.simplified().left(65);
}
static QPixmap dosFontPreview(const QByteArray &font, const QString &text) {
    QStringList lines;
    QString line;
    for (auto paragraph : text.split('\n')) {
        for (auto word : paragraph.split(' ', Qt::SkipEmptyParts)) {
            while (word.size() > 18) {
                if (!line.isEmpty()) { lines << line; line.clear(); }
                lines << word.left(18);
                word = word.mid(18);
            }
            if (line.size() + (!line.isEmpty() ? 1 : 0) + word.size() > 18) {
                lines << line;
                line.clear();
            }
            if (!line.isEmpty()) line += ' ';
            line += word;
        }
        lines << line;
        line.clear();
    }
    auto preview = Workshop::previewText(font, lines.join("\n"), Workshop::FontMode::Normal);
    return QPixmap::fromImage(preview.image.scaled(preview.image.width()*3, preview.image.height()*3,
        Qt::IgnoreAspectRatio, Qt::FastTransformation));
}
ConversationEditor::ConversationEditor(
    Project *p, const QString &r, std::function<void(const QByteArray &, const QString &)> cb,
    std::function<void(const QString &, const QString &)> nameCallback, int npc, int entry,
    std::function<void(int, int)> choose, QWidget *parent)
    : QWidget(parent), project(p), resource(r), commit(cb), selection(choose),
      rename(nameCallback) {
    setObjectName("conversationEditor");
    conversations = U5::readDialogue(p->data(r));
    auto layout = new QVBoxLayout(this);
    auto heading = new QHBoxLayout;
    auto title = new QLabel("<b>Conversations</b>");
    heading->addWidget(title);
    search = new QLineEdit;
    search->setPlaceholderText("Search NPC names, topics and dialogue…");
    search->setObjectName("conversationSearch");
    heading->addWidget(search, 1);
    layout->addLayout(heading);
    auto split = new QSplitter;
    layout->addWidget(split, 1);
    npcs = new QTreeWidget;
    npcs->setObjectName("conversationNpcs");
    npcs->setHeaderLabels({"NPC / dialogue ID"});
    npcs->setMinimumWidth(140);
    split->addWidget(npcs);
    for (int i = 0; i < conversations.size(); ++i) {
        auto d = parse(conversations[i].bytes);
        QString name = d.entries.isEmpty() ? "Unnamed" : caption(d.entries[0].bytes);
        if (name.isEmpty())
            name = "Unnamed";
        auto item = new QTreeWidgetItem(npcs, {name + QString(" · %1").arg(conversations[i].id)});
        item->setData(0, Qt::UserRole, i);
        item->setToolTip(0, QString("%1 · dialogue ID %2").arg(r).arg(conversations[i].id));
    }
    auto navigation = new QWidget;
    auto nav = new QVBoxLayout(navigation);
    nav->setContentsMargins(0, 0, 0, 0);
    outline = new QTreeWidget;
    outline->setObjectName("conversationOutline");
    outline->setHeaderLabels({"Conversation"});
    nav->addWidget(outline, 1);
    auto add = new QHBoxLayout;
    button(add, "+ Topic", [this] { addTopic(false); });
    button(add, "+ Question", [this] {
        if (flush())
            attempt([&] { install(addQuestion(doc), "Add question"); });
    });
    nav->addLayout(add);
    auto ops = new QHBoxLayout;
    button(ops, "+ Reply", [this] { addTopic(true); });
    button(ops, "Delete", [this] { deleteSelected(); });
    nav->addLayout(ops);
    auto move = new QHBoxLayout;
    button(move, "↑ Topic", [this] { moveTopic(-1, false); });
    button(move, "↓ Topic", [this] { moveTopic(1, false); });
    button(move, "Duplicate", [this] { moveTopic(0, true); });
    nav->addLayout(move);
    button(nav, "Name question", [this] { renameQuestion(); });
    split->addWidget(navigation);
    auto right = new QWidget;
    auto detail = new QVBoxLayout(right);
    detail->setContentsMargins(0, 0, 0, 0);
    context = new QLabel;
    context->setWordWrap(true);
    detail->addWidget(context);
    aliases = new QLineEdit;
    aliases->setObjectName("conversationAliases");
    aliases->setPlaceholderText("Keywords, separated by commas");
    detail->addWidget(aliases);
    connect(aliases, &QLineEdit::textEdited, this, [this] {
        if (!loading) {
            aliasesDraft = true;
            changed();
        }
    });
    connect(aliases, &QLineEdit::editingFinished, this, [this] { aliasesChanged(); });
    tabs = new QTabWidget;
    detail->addWidget(tabs, 3);
    auto scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    blocksWidget = new QWidget;
    blocksLayout = new QVBoxLayout(blocksWidget);
    blocksLayout->setAlignment(Qt::AlignTop);
    scroll->setWidget(blocksWidget);
    tabs->addTab(scroll, "Text & Actions");
    auto advanced = new QWidget;
    auto advancedLayout = new QVBoxLayout(advanced);
    sourceScope = new QComboBox;
    sourceScope->setObjectName("conversationSourceScope");
    sourceScope->addItems(
        {"Selected entry (lossless tagged source)", "Whole conversation (advanced)"});
    advancedLayout->addWidget(sourceScope);
    source = new QPlainTextEdit;
    source->setObjectName("dialogueText");
    advancedLayout->addWidget(source, 1);
    tabs->addTab(advanced, "Advanced source");
    connect(sourceScope, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (loading)
            return;
        if (!flush()) {
            QSignalBlocker block(sourceScope);
            sourceScope->setCurrentIndex(wholeSource ? 1 : 0);
            return;
        }
        wholeSource = index == 1;
        refreshSource();
    });
    connect(source, &QPlainTextEdit::textChanged, this, [this] {
        if (!loading) {
            sourceDraft = true;
            changed();
        }
    });
    auto actions = new QHBoxLayout;
    button(actions, "+ Text", [this] { addBlock(true); });
    button(actions, "+ Action", [this] { addBlock(false); });
    button(actions, "Apply", [this] { applyDraft(); });
    detail->addLayout(actions);
    budget = new QLabel;
    detail->addWidget(budget);
    status = new QLabel;
    status->setWordWrap(true);
    detail->addWidget(status);
    previewTabs = new QTabWidget;
    readable = new QPlainTextEdit;
    readable->setReadOnly(true);
    previewTabs->addTab(readable, "Readable preview");
    auto dos = new QWidget;
    auto dosLayout = new QVBoxLayout(dos);
    auto caveat = new QLabel("Original IBM.CH font, 18-column word wrap. "
                             "Approximate text-only preview; automatic quotes, "
                             "pagination and rune spans are not reproduced.");
    caveat->setWordWrap(true);
    dosLayout->addWidget(caveat);
    dosImage = new QLabel;
    dosImage->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    auto dosScroll = new QScrollArea;
    dosScroll->setWidgetResizable(true);
    dosScroll->setWidget(dosImage);
    dosLayout->addWidget(dosScroll, 1);
    previewTabs->addTab(dos, "DOS font preview");
    diagnostics = new QListWidget;
    diagnostics->setObjectName("conversationDiagnostics");
    previewTabs->addTab(diagnostics, "Diagnostics");
    connect(diagnostics, &QListWidget::itemClicked, this, [this](QListWidgetItem *i) {
        int e = i->data(Qt::UserRole).toInt();
        if (e >= 0 && flush())
            loadEntry(e);
    });
    auto test = new QWidget;
    auto testLayout = new QVBoxLayout(test);
    auto stateRow = new QHBoxLayout;
    avatar = new QLineEdit("Avatar");
    avatar->setPlaceholderText("Party names, comma-separated");
    avatar->setToolTip("First name is the Avatar. Name recognition uses the "
                       "first four characters of any listed party member.");
    stateRow->addWidget(avatar);
    gold = new QSpinBox;
    gold->setRange(0, 9999);
    gold->setValue(100);
    gold->setPrefix("Gold ");
    stateRow->addWidget(gold);
    karma = new QSpinBox;
    karma->setRange(0, 99);
    karma->setValue(50);
    karma->setPrefix("Karma ");
    stateRow->addWidget(karma);
    testLayout->addLayout(stateRow);
    auto stateRow2 = new QHBoxLayout;
    introduced = new QCheckBox("Introduced");
    announce = new QCheckBox("Announce name on first encounter");
    announce->setChecked(true);
    stateRow2->addWidget(introduced);
    stateRow2->addWidget(announce);
    partySize = new QSpinBox;
    partySize->setRange(1, 6);
    partySize->setValue(3);
    partySize->setPrefix("Party ");
    stateRow2->addWidget(partySize);
    button(stateRow2, "Reset", [this] { resetSimulation(); });
    testLayout->addLayout(stateRow2);
    auto disclaimer =
        new QLabel("Sandbox preview; guards, recruitment and inventory/world effects are "
                   "traced, not executed. DOS font/rune/quote rendering is not simulated.");
    disclaimer->setWordWrap(true);
    testLayout->addWidget(disclaimer);
    transcript = new QTreeWidget;
    transcript->setHeaderLabels({"Conversation / execution trace"});
    transcript->setObjectName("conversationTranscript");
    testLayout->addWidget(transcript, 1);
    connect(transcript, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *i) {
        int e = i->data(0, Qt::UserRole).toInt();
        if (e >= 0 && flush())
            loadEntry(e);
    });
    auto answer = new QHBoxLayout;
    input = new QLineEdit;
    input->setObjectName("conversationInput");
    input->setMaxLength(15);
    input->setPlaceholderText("Type a reply (15 characters maximum)");
    answer->addWidget(input, 1);
    auto send = [this] {
        if (!simulation)
            resetSimulation();
        if (simulation) {
            appendTranscript(simulation->respond(input->text()));
            input->clear();
        }
    };
    button(answer, "Send", send);
    connect(input, &QLineEdit::returnPressed, this, send);
    testLayout->addLayout(answer);
    previewTabs->addTab(test, "Test Conversation");
    detail->addWidget(previewTabs, 2);
    split->addWidget(right);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 0);
    split->setStretchFactor(2, 1);
    split->setSizes({170, 235, 650});
    timer.setSingleShot(true);
    timer.setInterval(650);
    connect(&timer, &QTimer::timeout, this, [this] { applyDraft(); });
    connect(outline, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *previous) {
                if (loading || !item)
                    return;
                int e = item->data(0, Qt::UserRole).toInt();
                if (e < 0)
                    return;
                if (!flush()) {
                    QSignalBlocker block(outline);
                    outline->setCurrentItem(previous);
                    return;
                }
                loadEntry(e);
            });
    connect(npcs, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *previous) {
                if (loading || !item)
                    return;
                if (!flush()) {
                    QSignalBlocker block(npcs);
                    npcs->setCurrentItem(previous);
                    return;
                }
                loadNpc(item->data(0, Qt::UserRole).toInt());
            });
    connect(search, &QLineEdit::textChanged, this, [this](const QString &s) {
        for (int i = 0; i < npcs->topLevelItemCount(); ++i) {
            auto item = npcs->topLevelItem(i);
            auto e = conversations[item->data(0, Qt::UserRole).toInt()];
            bool found = item->text(0).contains(s, Qt::CaseInsensitive) ||
                         U5::decodeText(e.bytes).contains(s, Qt::CaseInsensitive) ||
                         caption(e.bytes).contains(s, Qt::CaseInsensitive);
            item->setHidden(!found);
        }
    });
    if (!conversations.isEmpty()) {
        loading = true;
        npcs->setCurrentItem(npcs->topLevelItem(qBound(0, npc, int(conversations.size() - 1))));
        loading = false;
        loadNpc(qBound(0, npc, int(conversations.size() - 1)), entry);
    }
}
bool ConversationEditor::attempt(const std::function<void()> &fn) {
    try {
        fn();
        return true;
    } catch (const std::exception &e) {
        status->setText(QString("Cannot apply: %1").arg(QString::fromUtf8(e.what())));
        return false;
    }
}
void ConversationEditor::loadNpc(int i, int entry) {
    npcIndex = i;
    doc = parse(conversations[i].bytes);
    simulation.reset();
    transcript->clear();
    buildOutline(entry);
    loadEntry(qBound(0, entry, int(doc.entries.size() - 1)));
}
void ConversationEditor::buildOutline(int selectEntry) {
    loading = true;
    outline->clear();
    auto root = [&](QString s) {
        auto i = new QTreeWidgetItem(outline, {s});
        i->setData(0, Qt::UserRole, -1);
        i->setExpanded(true);
        return i;
    };
    auto row = [&](QTreeWidgetItem *p, QString s, int e) {
        auto i = new QTreeWidgetItem(p, {s});
        i->setData(0, Qt::UserRole, e);
        if (e == selectEntry)
            outline->setCurrentItem(i);
        return i;
    };
    if (!doc.structuralError.isEmpty()) {
        row(root("Unrecognized structure"), "Whole conversation (Advanced)", 0);
        loading = false;
        return;
    }
    auto basics = root("Basics");
    QStringList names = {"Name", "Description", "Greeting (introduced)", "Job / Work", "Farewell"};
    for (int e = 0; e < 5; ++e)
        row(basics, names[e], e);
    auto label = [&](const Rule &r) {
        QStringList keys;
        for (int e : r.keywords)
            keys << keyword(doc.entries[e].bytes);
        return keys.join(" / ");
    };
    auto topics = root("Topics");
    for (auto r : doc.topics)
        row(topics, label(r), r.response);
    auto questions = root("Questions");
    for (auto q : doc.questions) {
        auto group = row(questions, questionName(q.label), q.prompt);
        group->setExpanded(true);
        if (q.fallback >= 0)
            row(group, "Otherwise / fallback", q.fallback);
        for (auto r : q.replies)
            row(group, label(r), r.response);
    }
    for (int e : doc.rawEntries)
        row(root("Unrecognized bytes"), QString("Raw entry %1 (Advanced)").arg(e), e);
    if (doc.tail >= 0) {
        auto t = row(root("Format"), "Structural tail (preserved)", doc.tail);
        t->setToolTip(0, "Not an editable question. Advanced source retains this "
                         "ending exactly.");
    }
    loading = false;
}
void ConversationEditor::loadEntry(int e) {
    if (e < 0 || e >= doc.entries.size())
        return;
    loading = true;
    selected = e;
    prefix = 0;
    for (auto q : doc.questions)
        if (q.prompt == e)
            prefix = 2;
    if (!doc.structuralError.isEmpty())
        wholeSource = true;
    {
        QSignalBlocker block(sourceScope);
        sourceScope->setCurrentIndex(wholeSource ? 1 : 0);
    }
    refreshSource();
    sourceDraft = false;
    aliasesDraft = false;
    pending = false;
    aliases->clear();
    aliases->hide();
    auto show = [&](const QVector<Rule> &rs) {
        for (auto r : rs)
            if (r.response == e) {
                QStringList keys;
                for (auto k : r.keywords)
                    keys << keyword(doc.entries[k].bytes);
                aliases->setText(keys.join(", "));
                aliases->show();
            }
    };
    show(doc.topics);
    for (auto q : doc.questions)
        show(q.replies);
    QString message = QString("Dialogue ID %1 · entry %2 · byte offset 0x%3")
                          .arg(conversations[npcIndex].id)
                          .arg(e)
                          .arg(doc.entries[e].offset, 0, 16);
    if (e == 0)
        message += "\nThe engine adds “My name is …”; this field may also contain "
                   "actions.";
    if (e == 1)
        message += "\nThe engine adds “You see …”.";
    if (e == 2)
        message += "\nUsed when the NPC has already been introduced.";
    if (prefix) {
        int label = U5::byte(doc.entries[e].bytes, 1) - 144;
        auto refs = references(doc, label);
        message += QString("\nQuestion %1 · %2 incoming reference(s). Fallback is "
                           "shown separately.")
                       .arg(label)
                       .arg(refs.size());
    }
    QString npcFile = resource.left(resource.size() - 4) + ".NPC";
    if (project->resources.contains(npcFile)) {
        auto data = project->data(npcFile);
        QStringList locations;
        auto pages = project->resources.contains(resource.left(resource.size() - 4) + ".DAT")
                         ? U5::mapPages(resource.left(resource.size() - 4) + ".DAT",
                                        project->data(resource.left(resource.size() - 4) + ".DAT"))
                         : QVector<U5::MapPage>{};
        for (int town = 0; town < 8; ++town)
            for (int n = 0; n < 32; ++n)
                if (town * 576 + 544 + n < data.size() &&
                    U5::byte(data, town * 576 + 544 + n) == conversations[npcIndex].id) {
                    QString location = QString("Settlement %1").arg(town + 1);
                    for (auto page : pages)
                        if (page.settlement == town) {
                            location = page.name;
                            break;
                        }
                    locations << QString("%1 · NPC %2").arg(location).arg(n);
                }
        if (!locations.isEmpty())
            message += "\nUsed by: " + locations.join("; ");
    }
    context->setTextFormat(Qt::PlainText);
    context->setText(message);
    status->clear();
    buildBlocks();
    selection(npcIndex, e);
    updateDiagnostics();
    loading = false;
}
void ConversationEditor::buildBlocks() {
    while (auto i = blocksLayout->takeAt(0)) {
        if (i->widget())
            i->widget()->deleteLater();
        delete i;
    }
    readers.clear();
    currentBlocks.clear();
    bool raw =
        !doc.structuralError.isEmpty() || selected == doc.tail || doc.rawEntries.contains(selected);
    if (raw) {
        blocksLayout->addWidget(new QLabel("This section is preserved byte-for-byte. Edit it "
                                           "explicitly in Advanced source."));
        tabs->setCurrentIndex(1);
        return;
    }
    currentBlocks = blocks(doc.entries[selected].bytes.mid(prefix));
    for (int index = 0; index < currentBlocks.size(); ++index) {
        auto b = currentBlocks[index];
        auto group = new QGroupBox(b.opcode < 0 ? "Text" : actionName(b.opcode));
        auto layout = new QVBoxLayout(group);
        auto tools = new QHBoxLayout;
        tools->addStretch();
        button(tools, "↑", [this, index] { mutateBlocks(index, -1, false); });
        button(tools, "↓", [this, index] { mutateBlocks(index, 1, false); });
        button(tools, "Remove", [this, index] { mutateBlocks(index, 0, true); });
        layout->addLayout(tools);
        if (b.opcode < 0) {
            auto edit = new QPlainTextEdit(plain(b.bytes));
            edit->setObjectName("conversationBlockText");
            edit->setMaximumHeight(110);
            layout->addWidget(edit);
            auto original = edit->toPlainText();
            readers.append([b, edit, original] {
                return edit->toPlainText() == original ? b.bytes : text(edit->toPlainText());
            });
            connect(edit, &QPlainTextEdit::textChanged, this, [this] { changed(); });
        } else if (b.opcode == 133 || b.opcode == 134 || b.opcode == 140 || b.opcode == 254 ||
                   (b.opcode >= 145 && b.opcode <= 159)) {
            auto row = new QHBoxLayout;
            auto value = new QSpinBox;
            int v = 0;
            if (b.opcode == 133) {
                for (int j = 1; j <= 3; ++j)
                    v = v * 10 + (U5::byte(b.bytes, j) & 127) - '0';
                value->setRange(0, 999);
                value->setPrefix("Gold ");
            } else if (b.opcode == 134) {
                v = U5::byte(b.bytes, 1) & 127;
                value->setRange(0, 127);
                value->setPrefix("Inventory code ");
            } else if (b.opcode == 254) {
                v = U5::byte(b.bytes, 1);
                value->setRange(1, 255);
                value->setPrefix("Karma ≥ ");
            }
            value->setValue(v);
            if (b.opcode == 133 || b.opcode == 254)
                row->addWidget(value);
            else
                value->hide();
            value->setParent(group);
            auto itemChoice = new QComboBox(group);
            if (b.opcode == 134) {
                auto options = inventoryOptions();
                for (auto it = options.begin(); it != options.end(); ++it)
                    itemChoice->addItem(it.value() + QString(" · %1").arg(it.key()), it.key());
                int item = itemChoice->findData(v);
                if (item < 0) {
                    itemChoice->addItem(QString("Raw operand %1 (advanced)").arg(v), v);
                    item = itemChoice->count() - 1;
                }
                itemChoice->setCurrentIndex(item);
                row->addWidget(itemChoice, 1);
            } else
                itemChoice->hide();
            auto dest = new QComboBox;
            dest->setParent(group);
            if (b.opcode == 140)
                dest->addItem("Return to ordinary topics", 0);
            for (auto q : doc.questions)
                dest->addItem(questionName(q.label), q.label);
            int target = b.opcode == 140                      ? int(U5::byte(b.bytes, 1)) - 144
                         : b.opcode == 254                    ? int(U5::byte(b.bytes, 2)) - 144
                         : b.opcode >= 145 && b.opcode <= 159 ? b.opcode - 144
                                                              : 0;
            if (b.opcode == 140 && U5::byte(b.bytes, 1) == 255)
                target = 0;
            int found = dest->findData(target);
            if (found < 0) {
                dest->addItem(QString("Missing Q%1").arg(target), target);
                found = dest->count() - 1;
            }
            dest->setCurrentIndex(found);
            if (b.opcode != 133 && b.opcode != 134)
                row->addWidget(dest, 1);
            else
                dest->hide();
            layout->addLayout(row);
            readers.append([b, value, itemChoice, dest, v, target] {
                int t = dest->currentData().toInt(),
                    n = b.opcode == 134 ? itemChoice->currentData().toInt() : value->value();
                if (n == v && t == target)
                    return b.bytes;
                return action(b.opcode >= 145 && b.opcode <= 159 ? 144 + t : b.opcode, n, t);
            });
            connect(itemChoice, &QComboBox::currentIndexChanged, this, [this] { changed(); });
            connect(value, &QSpinBox::valueChanged, this, [this] { changed(); });
            connect(dest, &QComboBox::currentIndexChanged, this, [this] { changed(); });
            auto note = new QLabel(
                b.opcode == 133   ? "Insufficient gold returns to ordinary topics. This attempts "
                                    "payment, rather than checking gold without spending it."
                : b.opcode == 140 ? "Runs only when this NPC has recognized a party member's name."
                : b.opcode == 134 ? "Equipment names come from the engine table. Supplies increase "
                                    "by one; special items are granted. Raw operands are retained "
                                    "for compatibility; codes 48–63 write outside the equipment "
                                    "array."
                                  : "Branching enters the question; it does not return to the "
                                    "remaining text.");
            note->setWordWrap(true);
            layout->addWidget(note);
        } else {
            auto note = new QLabel(U5::decodeText(b.bytes));
            note->setTextFormat(Qt::PlainText);
            layout->addWidget(note);
            readers.append([b] { return b.bytes; });
        }
        blocksLayout->addWidget(group);
    }
}
void ConversationEditor::changed() {
    if (loading)
        return;
    pending = true;
    status->setText("Draft pending — valid edits apply automatically after typing.");
    timer.start();
}
bool ConversationEditor::applyDraft() {
    timer.stop();
    if (!pending)
        return true;
    bool ok = attempt([&] {
        QByteArray bytes;
        if (sourceDraft) {
            auto encoded = U5::encodeText(source->toPlainText());
            if (wholeSource)
                bytes = encoded;
            else {
                auto e = doc.entries[selected];
                int length = e.bytes.size() + (e.terminated ? 1 : 0);
                bytes = doc.bytes().left(e.offset) + encoded + doc.bytes().mid(e.offset + length);
            }
        } else {
            auto next = doc;
            QByteArray b = next.entries[selected].bytes.left(prefix);
            for (auto read : readers)
                b += read();
            next.entries[selected].bytes = b;
            bytes = next.bytes();
        }
        bool wasSource = sourceDraft || aliasesDraft;
        int nextSelected = selected;
        auto next = parse(bytes);
        if (aliasesDraft) {
            QVector<Rule> rs = next.topics;
            for (auto q : next.questions)
                rs += q.replies;
            for (auto r : rs)
                if (r.response == selected) {
                    QStringList keys;
                    for (auto s : aliases->text().split(','))
                        keys << s.trimmed();
                    bytes = replaceAliases(next, r, keys);
                    nextSelected = r.keywords.first() + keys.size() * 2 - 1;
                    next = parse(bytes);
                    break;
                }
        }
        for (auto i : next.issues())
            U5::require(!i.error, i.message);
        auto entries = conversations;
        entries[npcIndex].bytes = bytes;
        auto encoded = U5::writeDialogue(entries);
        commit(encoded, "Edit conversation");
        conversations = entries;
        doc = next;
        selected = qBound(0, nextSelected, int(doc.entries.size() - 1));
        pending = false;
        sourceDraft = false;
        aliasesDraft = false;
        sourceBaseline = bytes;
        refreshSource();
        simulation.reset();
        status->setText("Applied to project; original game files unchanged.");
        buildOutline(selected);
        if (wasSource)
            loadEntry(qBound(0, selected, int(doc.entries.size() - 1)));
        else
            updateDiagnostics();
    });
    return ok;
}
bool ConversationEditor::flush() {
    if (applyDraft())
        return true;
    auto result =
        QMessageBox::question(this, "Invalid conversation draft",
                              "This draft cannot be applied. Correct it, or discard the draft to "
                              "continue.",
                              QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    if (result == QMessageBox::Discard) {
        pending = false;
        sourceDraft = false;
        loadEntry(selected);
        return true;
    }
    return false;
}
void ConversationEditor::install(const QByteArray &bytes, const QString &description, int entry) {
    auto next = parse(bytes);
    for (auto i : next.issues())
        U5::require(!i.error, i.message);
    auto entries = conversations;
    entries[npcIndex].bytes = bytes;
    auto encoded = U5::writeDialogue(entries);
    commit(encoded, description);
    conversations = entries;
    doc = next;
    simulation.reset();
    pending = false;
    buildOutline(entry < 0 ? selected : entry);
    loadEntry(qBound(0, entry < 0 ? selected : entry, int(doc.entries.size() - 1)));
}
void ConversationEditor::updateDiagnostics() {
    issues = doc.issues();
    diagnostics->clear();
    for (auto i : issues) {
        auto item =
            new QListWidgetItem((i.error ? "Error: " : "Warning: ") + i.message, diagnostics);
        item->setData(Qt::UserRole, i.entry);
        item->setToolTip(QString("Entry %1").arg(i.entry));
    }
    budget->setText(QString("%1 / 1024 encoded bytes · %2 / 15 question labels · %3 diagnostics")
                        .arg(doc.bytes().size())
                        .arg(doc.questions.size() + (doc.tail >= 0 ? 1 : 0))
                        .arg(issues.size()));
    if (selected >= 0 && selected < doc.entries.size()) {
        QString s;
        for (auto b : blocks(doc.entries[selected].bytes.mid(prefix))) {
            if (b.opcode < 0)
                s += plain(b.bytes);
            else
                s += "\n[" + actionName(b.opcode) + "]\n";
        }
        readable->setPlainText(s);
        QString onlyText;
        for (auto b : blocks(doc.entries[selected].bytes.mid(prefix)))
            if (b.opcode < 0)
                onlyText += plain(b.bytes);
            else if (b.opcode == 129)
                onlyText += avatar->text().section(',', 0, 0);
        if (project->resources.contains("IBM.CH") && project->data("IBM.CH").size() == 1024)
            dosImage->setPixmap(dosFontPreview(project->data("IBM.CH"), onlyText));
        else
            dosImage->setText("IBM.CH unavailable; DOS font preview requires the "
                              "original 1024-byte font.");
    }
}
void ConversationEditor::mutateBlocks(int block, int delta, bool remove) {
    if (!flush() || !doc.structuralError.isEmpty() ||
        (selected == doc.tail || doc.rawEntries.contains(selected)))
        return;
    attempt([&] {
        auto bs = blocks(doc.entries[selected].bytes.mid(prefix));
        if (remove)
            bs.removeAt(block);
        else {
            int n = block + delta;
            if (n < 0 || n >= bs.size())
                return;
            bs.swapItemsAt(block, n);
        }
        auto next = doc;
        QByteArray b = next.entries[selected].bytes.left(prefix);
        for (auto x : bs)
            b += x.bytes;
        next.entries[selected].bytes = b;
        install(next.bytes(), remove ? "Remove response block" : "Move response block");
    });
}
void ConversationEditor::addBlock(bool isText) {
    if (!flush() || !doc.structuralError.isEmpty() ||
        (selected == doc.tail || doc.rawEntries.contains(selected)))
        return;
    attempt([&] {
        auto next = doc;
        QByteArray b;
        if (isText)
            b = text("New text.");
        else {
            QDialog dlg(this);
            dlg.setWindowTitle("Add conversation action");
            auto layout = new QVBoxLayout(&dlg);
            auto choice = new QComboBox;
            for (int c :
                 {129, 130, 131, 132, 133, 134, 136, 137, 138, 139, 140, 142, 143, 254, 255})
                choice->addItem(actionName(c), c);
            if (!doc.questions.isEmpty())
                choice->addItem("Ask a question", 145);
            layout->addWidget(choice);
            auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            layout->addWidget(buttons);
            connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
            if (dlg.exec() != QDialog::Accepted)
                return;
            int c = choice->currentData().toInt(),
                l = doc.questions.isEmpty() ? 0 : doc.questions.first().label;
            U5::require(c != 254 || l > 0, "Create a question before adding a karma branch");
            b = action(c == 145 ? 144 + l : c, c == 134 ? 65 : c == 254 ? 50 : 0, l);
        }
        next.entries[selected].bytes += b;
        install(next.bytes(), "Add response block");
    });
}
void ConversationEditor::aliasesChanged() {
    if (loading || !aliases->isVisible())
        return;
    QVector<Rule> rs = doc.topics;
    for (auto q : doc.questions)
        rs += q.replies;
    for (auto r : rs)
        if (r.response == selected) {
            QStringList keys;
            for (auto k : r.keywords)
                keys << keyword(doc.entries[k].bytes);
            if (aliases->text() != keys.join(", ")) {
                aliasesDraft = true;
                pending = true;
                applyDraft();
            }
            break;
        }
}

void ConversationEditor::addTopic(bool reply) {
    if (!flush() || !doc.structuralError.isEmpty())
        return;
    attempt([&] {
        int before = doc.questions.isEmpty() ? (doc.tail >= 0 ? doc.tail : doc.entries.size())
                                             : doc.questions.first().prompt;
        if (reply) {
            int index = -1;
            for (int i = 0; i < doc.questions.size(); ++i) {
                auto q = doc.questions[i];
                int end = i + 1 < doc.questions.size() ? doc.questions[i + 1].prompt
                          : doc.tail >= 0              ? doc.tail
                                                       : doc.entries.size();
                if (selected >= q.prompt && selected < end)
                    index = i;
            }
            U5::require(index >= 0, "Select a question or one of its replies first");
            before = index + 1 < doc.questions.size() ? doc.questions[index + 1].prompt
                     : doc.tail >= 0                  ? doc.tail
                                                      : doc.entries.size();
        }
        bool ok;
        QString key = QInputDialog::getText(this, reply ? "Add reply" : "Add topic",
                                            "Keyword prefix", QLineEdit::Normal, {}, &ok);
        if (!ok)
            return;
        install(insertRule(doc, before, key), reply ? "Add question reply" : "Add topic",
                before + 1);
    });
}
void ConversationEditor::deleteSelected() {
    if (!flush() || !doc.structuralError.isEmpty())
        return;
    attempt([&] {
        for (auto q : doc.questions)
            if (q.prompt == selected) {
                auto refs = references(doc, q.label);
                int target = 0;
                if (!refs.isEmpty()) {
                    QStringList options;
                    QVector<int> labels;
                    for (auto x : doc.questions)
                        if (x.label != q.label) {
                            options << questionName(x.label);
                            labels << x.label;
                        }
                    U5::require(!labels.isEmpty(),
                                "This question has callers. Remove their branch actions "
                                "before deleting the last question.");
                    bool ok;
                    auto answer = QInputDialog::getItem(
                        this, "Referenced question",
                        QString("%1 callers must be retargeted before deletion.").arg(refs.size()),
                        options, 0, false, &ok);
                    if (!ok)
                        return;
                    target = labels[options.indexOf(answer)];
                }
                if (QMessageBox::question(this, "Delete question",
                                          "Delete the prompt, fallback and all replies?",
                                          QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
                    return;
                install(removeQuestion(doc, q.label, target), "Delete question", 0);
                return;
            }
        QVector<Rule> rs = doc.topics;
        for (auto q : doc.questions)
            rs += q.replies;
        for (auto r : rs)
            if (r.response == selected) {
                auto n = doc;
                for (int i = r.keywords.first(); i <= r.response; ++i)
                    n.entries.removeAt(r.keywords.first());
                install(n.bytes(), "Delete topic/reply", 0);
                return;
            }
        U5::require(false, "Fixed fields, fallbacks and structural tails cannot be deleted");
    });
}
void ConversationEditor::resetSimulation() {
    if (!flush())
        return;
    for (auto i : doc.issues())
        if (i.error) {
            status->setText("Resolve conversation errors before testing.");
            return;
        }
    Sandbox s;
    s.partyNames = avatar->text().split(',', Qt::SkipEmptyParts);
    for (auto &name : s.partyNames)
        name = name.trimmed();
    s.avatar = s.partyNames.isEmpty() ? "Avatar" : s.partyNames.first();
    s.introduced = introduced->isChecked();
    s.announceName = announce->isChecked();
    s.gold = gold->value();
    s.karma = karma->value();
    s.partySize = partySize->value();
    simulation = std::make_unique<Simulator>(doc, s);
    transcript->clear();
    appendTranscript(simulation->start());
}
void ConversationEditor::appendTranscript(const QVector<Line> &lines) {
    for (auto l : lines) {
        auto item = new QTreeWidgetItem(transcript, {(l.trace ? "↳ " : "") + l.text});
        item->setData(0, Qt::UserRole, l.entry);
        if (l.trace)
            item->setForeground(0, QBrush(QColor("#94b9e7")));
    }
    transcript->scrollToBottom();
    if (simulation)
        status->setText(QString("Sandbox: gold %1 · karma %2 · %3 · %4")
                            .arg(simulation->state.gold)
                            .arg(simulation->state.karma)
                            .arg(simulation->state.introduced ? "introduced" : "not introduced")
                            .arg(simulation->ended ? "ended" : "awaiting input"));
}

QString ConversationEditor::questionName(int label) const {
    QString key =
        resource + "/" + QString::number(conversations[npcIndex].id) + "/" + QString::number(label);
    QString name = project->dialogueNames.value(key);
    if (name.isEmpty()) {
        int i = doc.question(label);
        if (i >= 0)
            name = caption(doc.entries[doc.questions[i].prompt].bytes.mid(2));
    }
    return QString("Q%1 · %2").arg(label).arg(name);
}
void ConversationEditor::renameQuestion() {
    if (!flush())
        return;
    int label = 0;
    for (int i = 0; i < doc.questions.size(); ++i) {
        auto q = doc.questions[i];
        int end = i + 1 < doc.questions.size() ? doc.questions[i + 1].prompt
                  : doc.tail >= 0              ? doc.tail
                                               : doc.entries.size();
        if (selected >= q.prompt && selected < end)
            label = q.label;
    }
    if (!label) {
        status->setText("Select a question or one of its replies first.");
        return;
    }
    bool ok;
    QString name = QInputDialog::getText(
        this, "Name question", "Project-only name (does not change game dialogue)",
        QLineEdit::Normal, questionName(label).section(" · ", 1), &ok);
    if (!ok)
        return;
    if (name.size() > 120) {
        status->setText("Question names are limited to 120 characters.");
        return;
    }
    rename(resource + "/" + QString::number(conversations[npcIndex].id) + "/" +
               QString::number(label),
           name);
    buildOutline(selected);
    loadEntry(selected);
}
void ConversationEditor::moveTopic(int delta, bool duplicate) {
    if (!flush())
        return;
    attempt([&] {
        QVector<Rule> rules = doc.topics;
        for (auto q : doc.questions)
            for (auto r : q.replies)
                if (r.response == selected)
                    rules = q.replies;
        int index = -1;
        for (int i = 0; i < rules.size(); ++i)
            if (rules[i].response == selected)
                index = i;
        U5::require(index >= 0, "Select a topic or question reply");
        auto r = rules[index];
        auto next = doc;
        int begin = r.keywords.first(), end = r.response + 1;
        auto part = doc.entries.mid(begin, end - begin);
        if (duplicate) {
            for (int i = 0; i < part.size(); ++i)
                next.entries.insert(end + i, part[i]);
            install(next.bytes(), "Duplicate topic/reply", end + part.size() - 1);
            return;
        }
        int other = index + delta;
        if (other < 0 || other >= rules.size())
            return;
        auto neighbor = rules[other];
        int at = delta < 0 ? neighbor.keywords.first() : neighbor.response + 1 - (end - begin);
        for (int i = begin; i < end; ++i)
            next.entries.removeAt(begin);
        for (int i = 0; i < part.size(); ++i)
            next.entries.insert(at + i, part[i]);
        install(next.bytes(), "Move topic/reply", at + part.size() - 1);
    });
}

void ConversationEditor::refreshSource() {
    QSignalBlocker blocked(source);
    sourceBaseline = wholeSource ? conversations[npcIndex].bytes
                                 : doc.entries[selected].bytes + (doc.entries[selected].terminated
                                                                      ? QByteArray(1, char(0))
                                                                      : QByteArray());
    source->setPlainText(U5::decodeText(sourceBaseline));
}
