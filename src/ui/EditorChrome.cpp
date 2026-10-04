#include "ui/EditorChrome.h"

#include "ui/Theme.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QHash>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QToolBar>
#include <QVBoxLayout>

namespace quewi::ui {

// 18×18 line icons painted into transparent QPixmaps. Cached on first
// use. Drawn in the theme's primary ink so the toolbar glyphs match the
// button text they sit beside.
QIcon makeEditorIcon(const QString &name) {
    static QHash<QString, QIcon> cache;
    if (auto it = cache.constFind(name); it != cache.constEnd()) return it.value();

    const int sz = 18;
    QPixmap pm(sz, sz);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor ink = Theme::tokens().ink100;
    QPen pen(ink); pen.setWidthF(1.5); pen.setCapStyle(Qt::RoundCap); pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    if (name == QLatin1String("play")) {
        QPainterPath path;
        path.moveTo(5, 4); path.lineTo(14, 9); path.lineTo(5, 14); path.closeSubpath();
        p.fillPath(path, ink);
    } else if (name == QLatin1String("stop")) {
        p.fillRect(QRectF(5, 5, 8, 8), ink);
    } else if (name == QLatin1String("loop")) {
        p.drawArc(3, 4, 12, 10, 30 * 16, 300 * 16);
        QPainterPath arrow;
        arrow.moveTo(13.5, 3); arrow.lineTo(15.5, 5.5); arrow.lineTo(11.5, 6); arrow.closeSubpath();
        p.fillPath(arrow, ink);
    } else if (name == QLatin1String("select")) {
        // Arrow / pointer
        QPainterPath path;
        path.moveTo(4, 3); path.lineTo(4, 14); path.lineTo(7, 11);
        path.lineTo(9.5, 15); path.lineTo(11, 14.2);
        path.lineTo(8.6, 10.4); path.lineTo(13, 10); path.closeSubpath();
        p.fillPath(path, ink);
    } else if (name == QLatin1String("razor")) {
        // Razor blade
        p.drawLine(3, 14, 13, 4);
        p.drawRect(QRectF(11, 3, 4, 4));
    } else if (name == QLatin1String("zoomIn")) {
        p.drawEllipse(2, 2, 9, 9);
        p.drawLine(11, 11, 16, 16);
        p.drawLine(6.5, 4, 6.5, 9); // +
        p.drawLine(4, 6.5, 9, 6.5);
    } else if (name == QLatin1String("zoomOut")) {
        p.drawEllipse(2, 2, 9, 9);
        p.drawLine(11, 11, 16, 16);
        p.drawLine(4, 6.5, 9, 6.5); // -
    } else if (name == QLatin1String("zoomFit")) {
        // Brackets
        p.drawLine(3, 5, 3, 3); p.drawLine(3, 3, 5, 3);
        p.drawLine(13, 3, 15, 3); p.drawLine(15, 3, 15, 5);
        p.drawLine(3, 13, 3, 15); p.drawLine(3, 15, 5, 15);
        p.drawLine(13, 15, 15, 15); p.drawLine(15, 15, 15, 13);
        p.drawLine(6, 9, 12, 9);
    } else if (name == QLatin1String("addTrack")) {
        p.drawLine(3, 5, 15, 5);
        p.drawLine(3, 9, 9, 9);
        p.drawLine(3, 13, 9, 13);
        p.drawLine(13, 11, 13, 15); // +
        p.drawLine(11, 13, 15, 13);
    } else if (name == QLatin1String("undo")) {
        p.drawArc(3, 4, 12, 10, 60 * 16, -180 * 16);
        QPainterPath arrow;
        arrow.moveTo(3, 5); arrow.lineTo(5, 3); arrow.lineTo(7, 6); arrow.closeSubpath();
        p.fillPath(arrow, ink);
    } else if (name == QLatin1String("redo")) {
        p.drawArc(3, 4, 12, 10, 120 * 16, 180 * 16);
        QPainterPath arrow;
        arrow.moveTo(15, 5); arrow.lineTo(13, 3); arrow.lineTo(11, 6); arrow.closeSubpath();
        p.fillPath(arrow, ink);
    } else if (name == QLatin1String("render")) {
        // Down-arrow into tray
        p.drawLine(9, 3, 9, 11);
        QPainterPath arrow;
        arrow.moveTo(5, 8); arrow.lineTo(9, 13); arrow.lineTo(13, 8); arrow.closeSubpath();
        p.fillPath(arrow, ink);
        p.drawLine(3, 15, 15, 15);
    } else if (name == QLatin1String("waveform")) {
        // Symmetric amplitude bars around a centre line.
        static const int hgt[] = {3, 6, 9, 5, 8, 4, 7};
        for (int i = 0; i < 7; ++i) {
            const int x = 3 + i * 2;
            p.drawLine(x, 9 - hgt[i] / 2, x, 9 + hgt[i] / 2);
        }
    } else if (name == QLatin1String("spectrogram")) {
        // Stacked frequency bands.
        for (int i = 0; i < 4; ++i) {
            const int y = 4 + i * 3;
            QColor c = ink; c.setAlpha(80 + i * 50);
            p.setPen(QPen(c, 2, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(3, y, 15, y);
        }
    }
    if (name == QLatin1String("toIn")) {
        // |◀  jump to the In point
        p.drawLine(4, 4, 4, 14);
        QPainterPath path;
        path.moveTo(14, 4); path.lineTo(6, 9); path.lineTo(14, 14); path.closeSubpath();
        p.fillPath(path, ink);
    } else if (name == QLatin1String("toOut")) {
        // ▶|  jump to the Out point
        p.drawLine(14, 4, 14, 14);
        QPainterPath path;
        path.moveTo(4, 4); path.lineTo(12, 9); path.lineTo(4, 14); path.closeSubpath();
        p.fillPath(path, ink);
    } else if (name == QLatin1String("pause")) {
        p.fillRect(QRectF(5, 4, 3, 10), ink);
        p.fillRect(QRectF(10, 4, 3, 10), ink);
    } else if (name == QLatin1String("frameBack")) {
        // ◁‖  one frame back: an outlined triangle against a bar
        p.drawLine(12.5, 5, 12.5, 13);
        QPainterPath path;
        path.moveTo(10, 5); path.lineTo(4, 9); path.lineTo(10, 13); path.closeSubpath();
        p.drawPath(path);
    } else if (name == QLatin1String("frameForward")) {
        // ‖▷  one frame forward
        p.drawLine(5.5, 5, 5.5, 13);
        QPainterPath path;
        path.moveTo(8, 5); path.lineTo(14, 9); path.lineTo(8, 13); path.closeSubpath();
        p.drawPath(path);
    } else if (name == QLatin1String("cut")) {
        // ✂ as two blades: a section going away
        p.drawLine(4, 4, 14, 14);
        p.drawLine(14, 4, 4, 14);
        p.drawEllipse(QRectF(2.5, 11.5, 4, 4));
        p.drawEllipse(QRectF(11.5, 11.5, 4, 4));
    } else if (name == QLatin1String("keep")) {
        // [ ]  keep only what's between the brackets
        p.drawLine(5, 4, 3, 4); p.drawLine(3, 4, 3, 14); p.drawLine(3, 14, 5, 14);
        p.drawLine(13, 4, 15, 4); p.drawLine(15, 4, 15, 14); p.drawLine(15, 14, 13, 14);
        p.fillRect(QRectF(6.5, 7, 5, 4), ink);
    }
    p.end();
    QIcon icon(pm);
    cache.insert(name, icon);
    return cache.value(name);
}

// Vertical 1 px divider for the toolbar — a styled QFrame is heavier
// and renders blurry, so paint it as a thin widget. Colour pulled
// from the central theme so the toolbar divider matches every other
// divider in the app instead of drifting toward the old cool-grey
// palette this file used to ship with.
QWidget *toolbarDivider(QWidget *parent) {
    auto *w = new QFrame(parent);
    w->setFrameShape(QFrame::VLine);
    w->setFixedWidth(1);
    const auto &tk = Theme::tokens();
    w->setStyleSheet(QStringLiteral("color:%1; background:%1;")
                         .arg(tk.outline.name()));
    return w;
}

QLabel *sectionLabel(const QString &text, QWidget *parent) {
    auto *l = new QLabel(text, parent);
    const auto &tk = Theme::tokens();
    l->setStyleSheet(QStringLiteral(
        "color:%1; font-size:10px; font-weight:700; letter-spacing:0.15em;"
        "padding:0 6px;")
        .arg(tk.ink40.name()));
    return l;
}

void styleEditorToolBar(QToolBar *tb)
{
    tb->setMovable(false);
    tb->setIconSize(QSize(18, 18));
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    const auto &tk = Theme::tokens();
    tb->setStyleSheet(QStringLiteral(
        "QToolBar { background:%1; border:none; padding:4px 8px; spacing:2px; }"
        "QToolButton { color:%2; padding:6px 10px; background:transparent;"
                     " border:1px solid transparent; }"
        "QToolButton:hover { background:%3; border:1px solid %4; }"
        "QToolButton:checked { background:%3; border:1px solid %5; color:%5; }"
        "QToolButton:pressed { background:%6; }")
        .arg(tk.bgDeep.name(),
             tk.ink100.name(),
             tk.bgRowHover.name(),
             tk.outline.name(),
             tk.accent.name(),
             tk.bgPanel.name()));
}

EditorHeader makeEditorHeader(const QString &kindCaps, QWidget *parent)
{
    // Cue identity left, format readout right. Sits between toolbar and
    // timeline so the operator always knows what they're editing.
    const auto &tkH = Theme::tokens();
    EditorHeader h;
    auto *header = new QWidget(parent);
    h.widget = header;
    header->setObjectName(QStringLiteral("editorHeader"));
    header->setStyleSheet(QStringLiteral(
        "QWidget#editorHeader { background:%1; border-bottom:1px solid %2; }")
        .arg(tkH.bgDeep.name(), tkH.divider.name()));
    header->setFixedHeight(56);
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(20, 8, 20, 8);
    hl->setSpacing(14);

    h.number = new QLabel(QStringLiteral("—"), header);
    h.number->setStyleSheet(QStringLiteral(
        "color:%1; font-family:'Space Grotesk','JetBrains Mono',monospace;"
        "font-size:24px; font-weight:700; letter-spacing:-0.01em;")
        .arg(tkH.accent.name()));
    h.number->setMinimumWidth(72);

    auto *nameStack = new QVBoxLayout();
    nameStack->setSpacing(0);
    nameStack->setContentsMargins(0, 0, 0, 0);
    auto *caps = new QLabel(kindCaps, header);
    caps->setStyleSheet(QStringLiteral(
        "color:%1; font-size:10px; font-weight:700; letter-spacing:0.18em;")
        .arg(tkH.ink40.name()));
    h.name = new QLabel(QStringLiteral("—"), header);
    h.name->setStyleSheet(QStringLiteral(
        "color:%1; font-size:18px; font-weight:600; letter-spacing:-0.005em;")
        .arg(tkH.ink100.name()));
    nameStack->addWidget(caps);
    nameStack->addWidget(h.name);

    h.meta = new QLabel(QString(), header);
    h.meta->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    h.meta->setStyleSheet(QStringLiteral(
        "color:%1; font-family:'Space Grotesk','JetBrains Mono',monospace;"
        "font-size:12px; letter-spacing:0.04em;")
        .arg(tkH.ink60.name()));

    hl->addWidget(h.number, 0, Qt::AlignVCenter);
    hl->addLayout(nameStack, 1);
    hl->addWidget(h.meta, 0, Qt::AlignVCenter);
    return h;
}

namespace {

// A widget you type words into. A spin box's inner line edit doesn't count:
// Space means nothing in a number.
bool isTextEntry(QWidget *w)
{
    if (qobject_cast<QTextEdit *>(w) || qobject_cast<QPlainTextEdit *>(w)) return true;
    if (auto *le = qobject_cast<QLineEdit *>(w))
        return !qobject_cast<QAbstractSpinBox *>(le->parentWidget());
    return false;
}

class SpaceKeyFilter : public QObject {
public:
    SpaceKeyFilter(QWidget *window, std::function<void()> toggle)
        : QObject(window), m_window(window), m_toggle(std::move(toggle)) {}

    bool eventFilter(QObject *, QEvent *e) override
    {
        // The application sees the key before the focused widget does.
        if (e->type() != QEvent::KeyPress && e->type() != QEvent::ShortcutOverride) return false;
        auto *k = static_cast<QKeyEvent *>(e);
        if (k->key() != Qt::Key_Space || k->modifiers() != Qt::NoModifier) return false;
        QWidget *fw = QApplication::focusWidget();
        if (!m_window->isActiveWindow() || !fw || fw->window() != m_window || isTextEntry(fw))
            return false;
        if (e->type() == QEvent::ShortcutOverride) {
            e->accept();            // ours: no shortcut, and the KeyPress comes here
            return true;
        }
        if (!k->isAutoRepeat()) m_toggle();
        return true;                // never reaches the focused button / spin box
    }

private:
    QWidget *m_window;
    std::function<void()> m_toggle;
};

} // namespace

void installEditorSpaceKey(QWidget *window, std::function<void()> togglePlay)
{
    qApp->installEventFilter(new SpaceKeyFilter(window, std::move(togglePlay)));
}

} // namespace quewi::ui
