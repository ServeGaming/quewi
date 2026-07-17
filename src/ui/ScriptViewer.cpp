#include "ui/ScriptViewer.h"

#include "ui/Theme.h"

#include "core/CueList.h"
#include "core/ScriptModel.h"
#include "core/Workspace.h"
#include "cues/Cue.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextBlock>

namespace quewi::ui {

namespace {

// Lightweight gutter widget — paints the cue-number ribbon. We delegate
// painting back to the ScriptViewer so the cue-id → line lookup logic
// lives in one place.
class GutterRibbon : public QWidget {
public:
    explicit GutterRibbon(ScriptViewer *viewer) : QWidget(viewer), m_viewer(viewer) {}
    QSize sizeHint() const override { return { m_viewer->gutterWidth(), 0 }; }
protected:
    void paintEvent(QPaintEvent *e) override { m_viewer->paintGutter(e); }
private:
    ScriptViewer *m_viewer;
};

} // namespace

ScriptViewer::ScriptViewer(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setReadOnly(true);   // Edits flow through ScriptModel only.
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFrameShape(QFrame::NoFrame);
    document()->setDocumentMargin(8.0);
    QFont f = font();
    f.setFamily(QStringLiteral("Consolas"));
    f.setStyleHint(QFont::TypeWriter);
    f.setPointSize(11);
    setFont(f);

    m_gutter = new GutterRibbon(this);
    setViewportMargins(gutterWidth(), 0, 0, 0);

    connect(this, &QPlainTextEdit::updateRequest,
            this, &ScriptViewer::onUpdateRequest);
}

ScriptViewer::~ScriptViewer() = default;

void ScriptViewer::setWorkspace(core::Workspace *ws)
{
    if (m_workspace == ws) return;
    m_workspace = ws;
    if (auto *m = ws ? ws->scriptModel() : nullptr) {
        connect(m, &core::ScriptModel::scriptChanged,
                this, &ScriptViewer::onScriptChanged);
        connect(m, &core::ScriptModel::annotationsChanged,
                this, &ScriptViewer::onAnnotationsChanged);
        onScriptChanged();
    } else {
        setPlainText(QString());
    }
}

void ScriptViewer::setMode(Mode m)
{
    if (m_mode == m) return;
    m_mode = m;
    setCursor(m == Mode::Edit ? Qt::IBeamCursor : Qt::ArrowCursor);
    refreshHighlights();
}

void ScriptViewer::onScriptChanged()
{
    if (!m_workspace || !m_workspace->scriptModel()) {
        setPlainText(QString());
        return;
    }
    auto *m = m_workspace->scriptModel();
    // PDFs are rendered by ScriptPdfView; this widget is hidden behind
    // the QStackedWidget when a PDF is active, so don't paint a
    // misleading "missing" message for them.
    if (m->format() == core::ScriptModel::Format::Pdf) {
        setPlainText(QString());
    } else if (!m->hasScript()) {
        setPlainText(QString());
    } else if (m->text().isEmpty()) {
        setPlainText(tr("Script file not found:\n%1\n\n"
                        "Re-open the script via Script → Open script…")
                     .arg(m->path()));
    } else {
        setPlainText(m->text());
    }
    refreshHighlights();
    if (m_gutter) m_gutter->update();
}

void ScriptViewer::onAnnotationsChanged()
{
    refreshHighlights();
    if (m_gutter) m_gutter->update();
}

void ScriptViewer::scrollToCue(const QUuid &cueId)
{
    m_runningCue = cueId;
    if (!m_workspace || !m_workspace->scriptModel()) return;
    const auto *m = m_workspace->scriptModel();
    const int idx = m->annotationIndexForCue(cueId);
    if (idx < 0) { refreshHighlights(); return; }
    const int line = m->annotations()[idx].line;
    auto block = document()->findBlockByNumber(line - 1);
    if (block.isValid()) {
        QTextCursor c(block);
        setTextCursor(c);
        // Centre the line in the viewport.
        const int target = blockBoundingGeometry(block).translated(contentOffset()).y();
        verticalScrollBar()->setValue(verticalScrollBar()->value() + target - viewport()->height() / 2);
    }
    refreshHighlights();
}

void ScriptViewer::setNextCue(const QUuid &cueId)
{
    if (m_nextCue == cueId) return;
    m_nextCue = cueId;
    refreshHighlights();
    if (m_gutter) m_gutter->update();
}

int ScriptViewer::lineAtCursorPos(const QPoint &pos) const
{
    QTextCursor c = cursorForPosition(pos);
    return c.blockNumber() + 1;
}

void ScriptViewer::mousePressEvent(QMouseEvent *event)
{
    if (m_mode != Mode::Edit || !m_workspace || !m_workspace->scriptModel()) {
        QPlainTextEdit::mousePressEvent(event);
        return;
    }
    auto *m = m_workspace->scriptModel();
    const int line = lineAtCursorPos(event->pos());
    if (line <= 0 || line > m->lineCount()) {
        QPlainTextEdit::mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton && !m_selectedCue.isNull()) {
        // Toggle: if this cue already binds to this line, unbind. Else bind.
        const int existing = m->annotationIndexForCue(m_selectedCue);
        if (existing >= 0 && m->annotations()[existing].line == line) {
            m->removeAnnotation(m_selectedCue);
        } else {
            m->setAnnotation(m_selectedCue, line);
        }
    } else if (event->button() == Qt::RightButton) {
        m->clearLine(line);
    }
    QPlainTextEdit::mousePressEvent(event);
}

void ScriptViewer::keyPressEvent(QKeyEvent *event)
{
    // Read-only: pass arrow keys through for navigation, swallow text input.
    QPlainTextEdit::keyPressEvent(event);
}

void ScriptViewer::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    if (m_gutter) m_gutter->setGeometry(cr.left(), cr.top(), gutterWidth(), cr.height());
}

void ScriptViewer::onUpdateRequest(const QRect &rect, int dy)
{
    if (!m_gutter) return;
    if (dy != 0) m_gutter->scroll(0, dy);
    else         m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
    if (rect.contains(viewport()->rect()))
        setViewportMargins(gutterWidth(), 0, 0, 0);
}

int ScriptViewer::gutterWidth() const
{
    // Wide enough for "Q123.45" plus a little air.
    return fontMetrics().horizontalAdvance(QStringLiteral("Q123.45")) + 18;
}

QString ScriptViewer::cueLabelForLine(int line) const
{
    if (!m_workspace || !m_workspace->scriptModel()) return {};
    auto *m = m_workspace->scriptModel();
    const int idx = m->annotationIndexForLine(line);
    if (idx < 0) return {};
    const auto cueId = m->annotations()[idx].cueId;
    if (auto *list = m_workspace->activeCueList()) {
        for (int row = 0; row < list->cueCount(); ++row) {
            auto *c = list->cueAt(row);
            if (c && c->id() == cueId) {
                return QStringLiteral("Q%1").arg(c->number(), 0, 'f', c->number() == int(c->number()) ? 0 : 2);
            }
        }
    }
    return QStringLiteral("Q?");
}

void ScriptViewer::paintGutter(QPaintEvent *event)
{
    QPainter p(m_gutter);
    p.fillRect(event->rect(), palette().color(QPalette::AlternateBase));

    // 1px right divider so the ribbon visually separates from the text.
    p.setPen(palette().color(QPalette::Mid));
    p.drawLine(m_gutter->width() - 1, event->rect().top(),
               m_gutter->width() - 1, event->rect().bottom());

    if (!m_workspace || !m_workspace->scriptModel()) return;

    // State colours from the tokens, not hardcoded hex — the previous
    // literals were the warm-dark values baked in, so the other four
    // palettes (and light) never reached this gutter.
    const QColor accent  = palette().color(QPalette::Highlight);
    const QColor running = Theme::tokens().running;
    const QColor next    = Theme::tokens().warn;

    QTextBlock block = firstVisibleBlock();
    int top = blockBoundingGeometry(block).translated(contentOffset()).top();
    int bottom = top + qRound(blockBoundingRect(block).height());
    QFont f = font();
    f.setBold(true);
    p.setFont(f);
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const int line = block.blockNumber() + 1;
            const QString label = cueLabelForLine(line);
            if (!label.isEmpty()) {
                const auto *m = m_workspace->scriptModel();
                const int idx = m->annotationIndexForLine(line);
                const auto cueId = (idx >= 0) ? m->annotations()[idx].cueId : QUuid();
                QColor c = accent;
                if      (cueId == m_runningCue) c = running;
                else if (cueId == m_nextCue)    c = next;
                p.setPen(c);
                p.drawText(QRect(0, top, m_gutter->width() - 6, qRound(blockBoundingRect(block).height())),
                           Qt::AlignRight | Qt::AlignVCenter, label);
            }
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
    }
}

void ScriptViewer::refreshHighlights()
{
    QList<QTextEdit::ExtraSelection> sels;
    if (m_workspace && m_workspace->scriptModel()) {
        const auto *m = m_workspace->scriptModel();

        auto highlightLine = [&](int line, QColor bg) {
            if (line <= 0) return;
            auto block = document()->findBlockByNumber(line - 1);
            if (!block.isValid()) return;
            QTextEdit::ExtraSelection sel;
            QTextCursor c(block);
            c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            sel.cursor = c;
            sel.format.setBackground(bg);
            sel.format.setProperty(QTextFormat::FullWidthSelection, true);
            sels.append(sel);
        };

        // Faint highlight for every annotated line. Token-derived (with
        // per-use alpha) so the highlights track the active palette.
        const auto &tk = Theme::tokens();
        QColor faint = tk.accent;   faint.setAlpha(40);
        for (const auto &a : m->annotations()) {
            highlightLine(a.line, faint);
        }
        // Stronger for next + running.
        if (!m_nextCue.isNull()) {
            const int idx = m->annotationIndexForCue(m_nextCue);
            QColor c = tk.warn; c.setAlpha(90);
            if (idx >= 0) highlightLine(m->annotations()[idx].line, c);
        }
        if (!m_runningCue.isNull()) {
            const int idx = m->annotationIndexForCue(m_runningCue);
            QColor c = tk.running; c.setAlpha(110);
            if (idx >= 0) highlightLine(m->annotations()[idx].line, c);
        }
    }
    setExtraSelections(sels);
}

} // namespace quewi::ui
