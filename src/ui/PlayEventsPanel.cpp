#include "ui/PlayEventsPanel.h"

#include "core/UndoCommands.h"
#include "ui/Theme.h"
#include "ui/TimelineCanvas.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QSpinBox>
#include <QTableWidget>
#include <QUndoStack>
#include <QVBoxLayout>

#include <cmath>

namespace quewi::ui {

using audio::PlayEvent;
using audio::PlayEvents;

namespace {

// Columns of the table.
enum Col { ColAt, ColKind, ColName, ColStart, ColEnd, ColRepeats, ColExit, kColumns };

// Every vamp edit is its own undo step: undoing a move shouldn't also undo
// the rename before it, so nothing merges.
class PlayEventsEditCommand : public core::EditCueFieldCommand {
public:
    PlayEventsEditCommand(cues::Cue *cue, QVariant oldValue, QVariant newValue, const QString &text)
        : core::EditCueFieldCommand(cue, QStringLiteral("playEvents"), std::move(oldValue),
                                    std::move(newValue))
    {
        setText(text);
    }
    int id() const override { return -1; }
};

QString timeText(double s)
{
    const qint64 ms = qint64(std::llround(std::max(0.0, s) * 1000.0));
    return QStringLiteral("%1:%2.%3")
        .arg(ms / 60000)
        .arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

// A time spin box that reads and writes m:ss.mmm (plain seconds typed in
// work too).
class TimeSpin : public QDoubleSpinBox {
public:
    explicit TimeSpin(QWidget *parent) : QDoubleSpinBox(parent)
    {
        setDecimals(3);
        setRange(0.0, 24 * 3600.0);
        setSingleStep(0.1);
        setKeyboardTracking(false);   // commit when typing is done, not per digit
        setButtonSymbols(QAbstractSpinBox::NoButtons);
        setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    QString textFromValue(double v) const override { return timeText(v); }
    double valueFromText(const QString &text) const override
    {
        const QString t = text.trimmed();
        const int colon = t.lastIndexOf(QLatin1Char(':'));
        if (colon < 0) return t.toDouble();
        return t.left(colon).toInt() * 60.0 + t.mid(colon + 1).toDouble();
    }
    QValidator::State validate(QString &text, int &) const override
    {
        static const QRegularExpression re(QStringLiteral("^\\s*(\\d+:)?\\d*(\\.\\d*)?\\s*$"));
        return re.match(text).hasMatch() ? QValidator::Acceptable : QValidator::Invalid;
    }
};

QLabel *helpLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color:%1; font-size:11px;").arg(Theme::tokens().ink40.name()));
    return l;
}

QPixmap swatch(const QColor &c)
{
    QPixmap px(10, 10);
    px.fill(c);
    return px;
}

} // namespace

PlayEventsPanel::PlayEventsPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("playEventsPanel"));
    const auto &tk = Theme::tokens();
    // The Lighting tab's buttons and table, so the two tabs look like one app.
    setStyleSheet(QStringLiteral(
        "QPushButton[peButton=\"true\"] { background:%2; color:%3;"
        "  border:none; border-radius:3px; padding:5px 12px; font-size:12px; font-weight:600; }"
        "QPushButton[peButton=\"true\"]:hover { background:%4; }"
        "QPushButton[peButton=\"true\"]:pressed { background:%5; }"
        "QPushButton[peButton=\"true\"]:disabled { color:%6; }"
        "QTableWidget#peTable { background:%1; border:none; border-radius:4px; }")
        .arg(tk.bgPanel.name(), tk.bgInteractive.name(), tk.ink100.name(), tk.bgRowHover.name(),
             tk.bgInteractive.darker(115).name(), tk.ink40.name()));

    auto *page = new QVBoxLayout(this);
    page->setContentsMargins(16, 10, 16, 16);
    page->setSpacing(8);

    page->addWidget(helpLabel(tr("In the show, a vamp loops until you press Advance (→); a repeat plays "
                                 "its section a set number of times, then goes on; a hold stops the music "
                                 "until Advance; a skip is played unless you press Advance inside it; a "
                                 "marker is just a label. Drag across the timeline's VAMPS lane to make a "
                                 "vamp."), this));

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(6);
    struct B { const char *text; const char *kind; const char *tip; };
    const B buttons[] = {
        {QT_TR_NOOP("+ Vamp"),   "vamp",   QT_TR_NOOP("A section that loops until Advance")},
        {QT_TR_NOOP("+ Repeat"), "repeat", QT_TR_NOOP("A section played a set number of times")},
        {QT_TR_NOOP("+ Hold"),   "hold",   QT_TR_NOOP("The music stops here until Advance")},
        {QT_TR_NOOP("+ Skip"),   "skip",   QT_TR_NOOP("A section that Advance jumps over")},
        {QT_TR_NOOP("+ Marker"), "marker", QT_TR_NOOP("A labelled point, for finding your place")},
    };
    for (const auto &b : buttons) {
        auto *btn = new QPushButton(tr(b.text), this);
        btn->setProperty("peButton", true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(tr("%1. Added where the song is playing, or at the edit cursor; one bar "
                           "long (2 s without a beat grid).").arg(tr(b.tip)));
        const QString kind = QString::fromLatin1(b.kind);
        connect(btn, &QPushButton::clicked, this, [this, kind] { addHere(kind); });
        btnRow->addWidget(btn);
    }
    m_deleteBtn = new QPushButton(tr("Delete"), this);
    m_deleteBtn->setProperty("peButton", true);
    m_deleteBtn->setCursor(Qt::PointingHandCursor);
    m_deleteBtn->setToolTip(tr("Delete the selected one (Delete)"));
    connect(m_deleteBtn, &QPushButton::clicked, this, [this] { deleteEvent(m_selected); });
    btnRow->addWidget(m_deleteBtn);
    btnRow->addSpacing(8);
    m_problem = new QLabel(this);
    m_problem->setObjectName(QStringLiteral("peProblem"));
    m_problem->setStyleSheet(QStringLiteral("color:%1; font-size:11px;").arg(tk.err.name()));
    m_problem->setWordWrap(true);
    btnRow->addWidget(m_problem, 1);
    page->addLayout(btnRow);

    m_table = new QTableWidget(0, kColumns, this);
    m_table->setObjectName(QStringLiteral("peTable"));
    m_table->setHorizontalHeaderLabels({QString(), tr("Kind"), tr("Name"), tr("Start"), tr("End"),
                                        tr("Repeats"), tr("Exit")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);
    // Rows tall enough for the editors inside them (combos and time boxes
    // carry the theme's padding; 28 px cut their text in half).
    {
        QDoubleSpinBox probe;
        probe.ensurePolished();
        m_table->verticalHeader()->setDefaultSectionSize(std::max(36, probe.sizeHint().height() + 6));
    }
    auto *hh = m_table->horizontalHeader();
    hh->setSectionResizeMode(ColAt, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(ColKind, QHeaderView::Interactive);
    hh->setSectionResizeMode(ColName, QHeaderView::Stretch);
    hh->setSectionResizeMode(ColStart, QHeaderView::Interactive);
    hh->setSectionResizeMode(ColEnd, QHeaderView::Interactive);
    hh->setSectionResizeMode(ColRepeats, QHeaderView::Interactive);
    hh->setSectionResizeMode(ColExit, QHeaderView::Interactive);
    m_table->setColumnWidth(ColKind, 96);
    m_table->setColumnWidth(ColStart, 190);
    m_table->setColumnWidth(ColEnd, 190);
    m_table->setColumnWidth(ColRepeats, 76);
    m_table->setColumnWidth(ColExit, 150);
    page->addWidget(m_table, 1);

    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        if (m_selecting) return;
        const auto rows = m_table->selectionModel()->selectedRows();
        const QUuid id = rows.isEmpty() || rows.first().row() >= m_rowIds.size()
                             ? QUuid() : m_rowIds.at(rows.first().row());
        if (id == m_selected) return;
        m_selected = id;
        m_deleteBtn->setEnabled(!id.isNull());
        emit selectionChanged(id);
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row < 0 || row >= m_rowIds.size() || !m_cue) return;
        const int i = indexOf(m_rowIds.at(row));
        if (i >= 0) emit seekRequested(m_cue->playEvents()[size_t(i)].start);
    });
    auto *delKey = new QShortcut(QKeySequence::Delete, m_table);
    delKey->setContext(Qt::WidgetShortcut);
    connect(delKey, &QShortcut::activated, this, [this] { deleteEvent(m_selected); });
    auto *bsKey = new QShortcut(QKeySequence(Qt::Key_Backspace), m_table);
    bsKey->setContext(Qt::WidgetShortcut);
    connect(bsKey, &QShortcut::activated, this, [this] { deleteEvent(m_selected); });

    // The questions, as small dialogs unless a test answers them.
    m_askCount = [this](int current) -> std::optional<int> {
        bool ok = false;
        const int n = QInputDialog::getInt(this, tr("Repeat"), tr("Play the section how many times?"),
                                           std::max(2, current), 2, 999, 1, &ok);
        return ok ? std::optional<int>(n) : std::nullopt;
    };
    m_askName = [this](const QString &title, const QString &current) -> std::optional<QString> {
        bool ok = false;
        const QString n = QInputDialog::getText(this, title, tr("Name:"), QLineEdit::Normal, current, &ok);
        return ok ? std::optional<QString>(n.trimmed()) : std::nullopt;
    };

    refresh();
}

void PlayEventsPanel::setCue(audio::AudioCue *cue)
{
    if (m_cue == cue) return;
    if (m_cue) disconnect(m_cue, nullptr, this, nullptr);
    m_cue = cue;
    m_selected = QUuid();
    setProblem({});
    // The cue's changed() covers undo, remotes and our own commits alike.
    if (m_cue) connect(m_cue, &cues::Cue::changed, this, &PlayEventsPanel::scheduleRefresh);
    refresh();
}

void PlayEventsPanel::selectEvent(const QUuid &id)
{
    if (id == m_selected) return;
    m_selected = id;
    m_deleteBtn->setEnabled(!id.isNull());
    {
        m_selecting = true;
        const int row = m_rowIds.indexOf(id);
        if (row >= 0) m_table->selectRow(row);
        else          m_table->clearSelection();
        m_selecting = false;
    }
    emit selectionChanged(id);
}

double PlayEventsPanel::defaultLength() const
{
    const auto g = m_cue ? m_cue->beatGrid() : audio::BeatGrid{};
    return g.isSet() ? g.beatLength() * std::max(1, g.beatsPerBar) : 2.0;
}

QString PlayEventsPanel::barBeatText(double seconds) const
{
    if (!m_cue || !m_cue->beatGrid().isSet()) return {};
    const auto &g = m_cue->beatGrid();
    double beat = std::round(g.beatAt(seconds) * 100.0) / 100.0;
    if (beat < 0.0) return {};                     // before the first beat
    const int bpb = std::max(1, g.beatsPerBar);
    const long long whole = static_cast<long long>(std::floor(beat + 1e-9));
    const double frac = beat - double(whole);
    const double inBar = double(whole % bpb + 1) + (frac < 0.01 ? 0.0 : frac);
    return tr("bar %1 beat %2").arg(whole / bpb + 1).arg(QString::number(inBar, 'g', 4));
}

QString PlayEventsPanel::problem() const { return m_problem->text(); }

void PlayEventsPanel::setProblem(const QString &text) { m_problem->setText(text); }

int PlayEventsPanel::indexOf(const QUuid &id) const
{
    if (!m_cue || id.isNull()) return -1;
    const auto &ev = m_cue->playEvents();
    for (size_t i = 0; i < ev.size(); ++i)
        if (ev[i].id == id) return int(i);
    return -1;
}

PlayEvent::Kind PlayEventsPanel::kindOf(const QString &key)
{
    if (key == QLatin1String("repeat")) return PlayEvent::Kind::Vamp;
    return PlayEvent::kindFromKey(key);
}

QString PlayEventsPanel::keyOf(const PlayEvent &e)
{
    return e.isRepeat() ? QStringLiteral("repeat") : PlayEvent::kindKey(e.kind);
}

// ── Commit: the one write path ───────────────────────────────────────────────

bool PlayEventsPanel::commit(PlayEvents next, const QString &undoText)
{
    if (!m_cue) return false;
    const size_t wanted = next.size();
    const PlayEvents norm = audio::normalizePlayEvents(next);
    if (norm.size() != wanted) {
        // normalizePlayEvents drops what playback can't do; say so instead
        // of letting an event vanish.
        const bool tooShort = std::any_of(next.begin(), next.end(), [](const PlayEvent &e) {
            return e.isRange() && std::abs(e.end - e.start) < audio::kMinPlayEventLength;
        });
        setProblem(tooShort ? tr("Too short: a vamp or skip needs at least %1 s.")
                                  .arg(audio::kMinPlayEventLength)
                            : tr("Vamps can't overlap: the song can only loop one section at a time."));
        return false;
    }
    setProblem({});
    const QJsonArray oldJson = audio::playEventsToJson(m_cue->playEvents());
    const QJsonArray newJson = audio::playEventsToJson(norm);
    if (oldJson == newJson) return true;
    if (m_undo)
        m_undo->push(new PlayEventsEditCommand(m_cue, QVariant(oldJson), QVariant(newJson), undoText));
    else
        m_cue->setField(QStringLiteral("playEvents"), QVariant(newJson));
    emit edited();
    return true;
}

bool PlayEventsPanel::editOne(const QUuid &id, const QString &undoText,
                              const std::function<void(PlayEvent &)> &f)
{
    const int i = indexOf(id);
    if (i < 0) return false;
    PlayEvents next = m_cue->playEvents();
    f(next[size_t(i)]);
    return commit(std::move(next), undoText);
}

// ── Edits ────────────────────────────────────────────────────────────────────

QUuid PlayEventsPanel::addEvent(const QString &kind, double start, double end, int count,
                                const QString &name)
{
    if (!m_cue) return {};
    PlayEvent e;
    e.id    = QUuid::createUuid();
    e.kind  = kindOf(kind);
    e.name  = name;
    e.start = std::max(0.0, start);
    e.end   = e.start;
    if (e.isRange()) e.end = end > e.start ? end : e.start + defaultLength();
    if (kind == QLatin1String("repeat")) e.count = std::clamp(count < 2 ? 2 : count, 2, 999);
    PlayEvents next = m_cue->playEvents();
    next.push_back(e);
    if (!commit(std::move(next), tr("Add %1").arg(e.kindName().toLower()))) return {};
    scheduleRefresh();
    selectEvent(e.id);
    return e.id;
}

QUuid PlayEventsPanel::addFromLane(const QString &kind, double start, double end)
{
    int count = 0;
    QString name;
    if (kind == QLatin1String("repeat")) {
        const auto n = m_askCount ? m_askCount(2) : std::optional<int>(2);
        if (!n) return {};
        count = *n;
    } else if (kind == QLatin1String("marker")) {
        const auto n = m_askName ? m_askName(tr("Marker"), QString()) : std::optional<QString>(QString());
        if (!n) return {};
        name = *n;
    }
    return addEvent(kind, start, end, count, name);
}

double PlayEventsPanel::hereSeconds() const
{
    double s = m_cursor;
    if (m_playhead) {
        const double p = m_playhead();
        if (p >= 0.0) s = p;
    }
    if (m_snap && m_cue && m_cue->beatGrid().isSet()) s = std::max(0.0, m_cue->beatGrid().snap(s));
    return s;
}

QUuid PlayEventsPanel::addHere(const QString &kind)
{
    double a = 0.0, b = 0.0;
    if (m_span && m_span(a, b) && b > a) {
        const bool range = kind == QLatin1String("vamp") || kind == QLatin1String("repeat")
                        || kind == QLatin1String("skip");
        return addFromLane(kind, a, range ? b : -1.0);
    }
    return addFromLane(kind, hereSeconds(), -1.0);
}

bool PlayEventsPanel::moveEvent(const QUuid &id, double start, double end)
{
    return editOne(id, tr("Move vamp"), [start, end](PlayEvent &e) {
        e.start = std::max(0.0, start);
        e.end   = e.isRange() ? std::max(0.0, end) : e.start;
    });
}

bool PlayEventsPanel::deleteEvent(const QUuid &id)
{
    const int i = indexOf(id);
    if (i < 0) return false;
    PlayEvents next = m_cue->playEvents();
    const QString what = next[size_t(i)].kindName().toLower();
    next.erase(next.begin() + i);
    if (!commit(std::move(next), tr("Delete %1").arg(what))) return false;
    if (m_selected == id) selectEvent({});
    return true;
}

bool PlayEventsPanel::setKind(const QUuid &id, const QString &kind, int count)
{
    const double len = defaultLength();
    return editOne(id, tr("Change vamp kind"), [&kind, count, len](PlayEvent &e) {
        const bool wasRange = e.isRange();
        e.kind = kindOf(kind);
        if (kind == QLatin1String("repeat"))
            e.count = std::clamp(count >= 2 ? count : (e.count >= 2 ? e.count : 2), 2, 999);
        else
            e.count = 0;
        // A point turned into a band needs a length; a band turned into a
        // point keeps its start.
        if (e.isRange() && !wasRange) e.end = e.start + len;
        if (!e.isRange()) e.end = e.start;
        if (e.kind != PlayEvent::Kind::Vamp) e.exit = PlayEvent::Exit::EndOfPass;
    });
}

bool PlayEventsPanel::setName(const QUuid &id, const QString &name)
{
    return editOne(id, tr("Rename vamp"), [&name](PlayEvent &e) { e.name = name.trimmed(); });
}

bool PlayEventsPanel::setCount(const QUuid &id, int count)
{
    return editOne(id, tr("Change repeat count"), [count](PlayEvent &e) {
        if (e.kind == PlayEvent::Kind::Vamp) e.count = std::clamp(count, 2, 999);
    });
}

bool PlayEventsPanel::setExit(const QUuid &id, PlayEvent::Exit exit)
{
    return editOne(id, tr("Change vamp exit"), [exit](PlayEvent &e) { e.exit = exit; });
}

bool PlayEventsPanel::applyEventAction(const QUuid &id, const QString &action)
{
    const int i = indexOf(id);
    if (i < 0) return false;
    const PlayEvent e = m_cue->playEvents()[size_t(i)];
    if (action == QLatin1String("delete"))     return deleteEvent(id);
    if (action == QLatin1String("makeVamp"))   return setKind(id, QStringLiteral("vamp"));
    if (action == QLatin1String("makeSkip"))   return setKind(id, QStringLiteral("skip"));
    if (action == QLatin1String("makeHold"))   return setKind(id, QStringLiteral("hold"));
    if (action == QLatin1String("makeMarker")) return setKind(id, QStringLiteral("marker"));
    if (action == QLatin1String("makeRepeat")) {
        const auto n = m_askCount ? m_askCount(e.isRepeat() ? e.count : 2) : std::optional<int>(2);
        if (!n) return false;
        return setKind(id, QStringLiteral("repeat"), *n);
    }
    if (action == QLatin1String("exitEndOfPass")) return setExit(id, PlayEvent::Exit::EndOfPass);
    if (action == QLatin1String("exitNextBar"))   return setExit(id, PlayEvent::Exit::NextBar);
    if (action == QLatin1String("exitNow"))       return setExit(id, PlayEvent::Exit::Now);
    if (action == QLatin1String("rename")) {
        selectEvent(id);
        const auto n = m_askName ? m_askName(tr("Rename %1").arg(e.kindName()), e.name) : std::nullopt;
        if (!n) return false;
        return setName(id, *n);
    }
    return false;
}

// ── Table ────────────────────────────────────────────────────────────────────

void PlayEventsPanel::scheduleRefresh()
{
    // Deferred: an edit made in a cell widget comes back here through the
    // cue's changed(), and rebuilding the row then would delete the widget
    // still inside its own signal.
    if (m_refreshQueued) return;
    m_refreshQueued = true;
    QMetaObject::invokeMethod(this, [this] { refresh(); }, Qt::QueuedConnection);
}

bool PlayEventsPanel::eventFilter(QObject *watched, QEvent *event)
{
    // A cell widget took focus or a click: that row is the selection. A
    // double-click on the bar / beat line goes there, like the swatch cell.
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress
        || event->type() == QEvent::MouseButtonDblClick) {
        const QUuid id = watched->property("peId").toUuid();
        if (!id.isNull() && id != m_selected) selectEvent(id);
        if (event->type() == QEvent::MouseButtonDblClick && watched->property("peSeek").toBool()) {
            const int i = indexOf(id);
            if (i >= 0) emit seekRequested(m_cue->playEvents()[size_t(i)].start);
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PlayEventsPanel::refresh()
{
    m_refreshQueued = false;
    const PlayEvents events = m_cue ? m_cue->playEvents() : PlayEvents{};

    // Keep the keyboard where it was across the rebuild.
    QUuid focusId;
    int focusCol = -1;
    if (QWidget *fw = QApplication::focusWidget(); fw && m_table->isAncestorOf(fw)) {
        for (QWidget *w = fw; w && w != m_table; w = w->parentWidget())
            if (w->property("peCol").isValid()) {
                focusId = w->property("peId").toUuid();
                focusCol = w->property("peCol").toInt();
                break;
            }
    }

    m_selecting = true;
    m_table->clearContents();
    m_table->setRowCount(int(events.size()));
    m_rowIds.clear();
    const auto &tk = Theme::tokens();
    auto tag = [this](QWidget *w, const QUuid &id, int col) {
        w->setProperty("peId", id);
        w->setProperty("peCol", col);
        w->installEventFilter(this);
    };
    auto dash = [&tk](const QString &tip = {}) {
        auto *it = new QTableWidgetItem(QStringLiteral("—"));
        it->setForeground(tk.ink40);
        it->setTextAlignment(Qt::AlignCenter);
        if (!tip.isEmpty()) it->setToolTip(tip);
        return it;
    };

    for (int row = 0; row < int(events.size()); ++row) {
        const PlayEvent e = events[size_t(row)];
        const QUuid id = e.id;
        m_rowIds << id;

        // Swatch + number: the plain cell a double-click goes to.
        auto *at = new QTableWidgetItem(QIcon(swatch(TimelineCanvas::playEventColor(e))),
                                        QString::number(row + 1));
        at->setToolTip(tr("Double-click to put the editor's cursor here"));
        m_table->setItem(row, ColAt, at);

        auto *kind = new QComboBox(m_table);
        kind->setObjectName(QStringLiteral("peKind"));
        const std::pair<QString, QString> kinds[] = {
            {QStringLiteral("vamp"), tr("Vamp")},   {QStringLiteral("repeat"), tr("Repeat")},
            {QStringLiteral("hold"), tr("Hold")},   {QStringLiteral("skip"), tr("Skip")},
            {QStringLiteral("marker"), tr("Marker")},
        };
        for (const auto &[key, text] : kinds) kind->addItem(text, key);
        kind->setCurrentIndex(kind->findData(keyOf(e)));
        connect(kind, &QComboBox::activated, this, [this, id, kind](int) {
            setKind(id, kind->currentData().toString());
        });
        tag(kind, id, ColKind);
        m_table->setCellWidget(row, ColKind, kind);

        auto *name = new QLineEdit(e.name, m_table);
        name->setObjectName(QStringLiteral("peName"));
        name->setPlaceholderText(e.kindName());
        name->setFrame(false);
        connect(name, &QLineEdit::editingFinished, this, [this, id, name] {
            const int i = indexOf(id);
            if (i >= 0 && m_cue->playEvents()[size_t(i)].name != name->text().trimmed())
                setName(id, name->text());
        });
        tag(name, id, ColName);
        m_table->setCellWidget(row, ColName, name);

        // Start / End: m:ss.mmm, with the bar and beat beside when there's a grid.
        auto timeCell = [&](bool isStart) {
            auto *box = new QWidget(m_table);
            auto *h = new QHBoxLayout(box);
            h->setContentsMargins(2, 0, 4, 0);
            h->setSpacing(6);
            auto *spin = new TimeSpin(box);
            spin->setObjectName(isStart ? QStringLiteral("peStart") : QStringLiteral("peEnd"));
            spin->setFrame(false);
            spin->setValue(isStart ? e.start : e.end);
            if (!isStart) spin->setMinimum(e.start + audio::kMinPlayEventLength);
            h->addWidget(spin);
            auto *bb = new QLabel(barBeatText(isStart ? e.start : e.end), box);
            bb->setStyleSheet(QStringLiteral("color:%1; font-size:11px;").arg(tk.ink40.name()));
            bb->setProperty("peSeek", true);
            bb->setToolTip(tr("Double-click to put the editor's cursor here"));
            h->addWidget(bb, 1);
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this, id, isStart](double v) {
                const int i = indexOf(id);
                if (i < 0) return;
                const PlayEvent cur = m_cue->playEvents()[size_t(i)];
                if (isStart) {
                    // Moving the start past the end carries the band along.
                    const double len = cur.end - cur.start;
                    const double end = cur.isRange() && v + audio::kMinPlayEventLength > cur.end
                                           ? v + len : cur.end;
                    moveEvent(id, v, end);
                } else {
                    moveEvent(id, cur.start, v);
                }
            });
            tag(box, id, isStart ? ColStart : ColEnd);
            tag(spin, id, isStart ? ColStart : ColEnd);
            bb->setProperty("peId", id);
            bb->installEventFilter(this);
            return box;
        };
        m_table->setCellWidget(row, ColStart, timeCell(true));
        if (e.isRange()) m_table->setCellWidget(row, ColEnd, timeCell(false));
        else             m_table->setItem(row, ColEnd, dash(tr("A hold or a marker is a point")));

        if (e.isRepeat()) {
            auto *count = new QSpinBox(m_table);
            count->setObjectName(QStringLiteral("peCount"));
            count->setRange(2, 999);
            count->setSuffix(tr(" times"));
            count->setKeyboardTracking(false);
            count->setFrame(false);
            count->setValue(e.count);
            connect(count, &QSpinBox::valueChanged, this, [this, id](int n) { setCount(id, n); });
            tag(count, id, ColRepeats);
            m_table->setCellWidget(row, ColRepeats, count);
        } else {
            m_table->setItem(row, ColRepeats, dash(tr("Only a repeat plays a set number of times")));
        }

        if (e.kind == PlayEvent::Kind::Vamp && !e.isRepeat()) {
            auto *exit = new QComboBox(m_table);
            exit->setObjectName(QStringLiteral("peExit"));
            exit->addItem(tr("End of the pass"), int(PlayEvent::Exit::EndOfPass));
            exit->addItem(m_cue->beatGrid().isSet() ? tr("Next bar") : tr("Next bar (no grid: end of pass)"),
                          int(PlayEvent::Exit::NextBar));
            exit->addItem(tr("Now"), int(PlayEvent::Exit::Now));
            exit->setCurrentIndex(exit->findData(int(e.exit)));
            exit->setToolTip(tr("When Advance is pressed, the vamp lets go here"));
            connect(exit, &QComboBox::activated, this, [this, id, exit](int) {
                setExit(id, PlayEvent::Exit(exit->currentData().toInt()));
            });
            tag(exit, id, ColExit);
            m_table->setCellWidget(row, ColExit, exit);
        } else {
            m_table->setItem(row, ColExit, dash(tr("Only a vamp waits for Advance")));
        }
    }

    m_table->resizeRowsToContents();   // never shorter than an editor in the row

    if (!m_selected.isNull() && !m_rowIds.contains(m_selected)) {
        m_selected = QUuid();
        m_selecting = false;
        emit selectionChanged(QUuid());
        m_selecting = true;
    }
    const int selRow = m_rowIds.indexOf(m_selected);
    if (selRow >= 0) m_table->selectRow(selRow);
    else             m_table->clearSelection();
    m_selecting = false;
    m_deleteBtn->setEnabled(!m_selected.isNull());

    if (focusCol >= 0) {
        const int row = m_rowIds.indexOf(focusId);
        if (QWidget *w = row >= 0 ? m_table->cellWidget(row, focusCol) : nullptr) {
            QWidget *target = w;
            for (auto *child : w->findChildren<QAbstractSpinBox *>()) { target = child; break; }
            target->setFocus();
        }
    }
}

} // namespace quewi::ui
