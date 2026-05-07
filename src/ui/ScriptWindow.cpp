#include "ui/ScriptWindow.h"

#include "app/GoEngine.h"
#include "core/ScriptModel.h"
#include "core/Workspace.h"
#include "cues/Cue.h"
#include "ui/ScriptPdfView.h"
#include "ui/ScriptViewer.h"

#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace quewi::ui {

ScriptWindow::ScriptWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setObjectName(QStringLiteral("ScriptWindow"));
    setWindowTitle(tr("Script — quewi"));
    resize(700, 800);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Toolbar
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("scriptToolbar"));
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(8, 6, 8, 6);

    auto *openBtn = new QPushButton(tr("Open script…"), bar);
    auto *clearBtn = new QPushButton(tr("Clear"), bar);
    m_modeBtn = new QPushButton(tr("Edit mode"), bar);
    m_modeBtn->setCheckable(true);
    m_modeBtn->setToolTip(tr("Toggle between Edit (click line to bind cue) "
                              "and Follow (read-only, auto-scrolls during show)"));
    m_status = new QLabel(tr("No script loaded."), bar);
    m_status->setObjectName(QStringLiteral("scriptStatus"));

    barLayout->addWidget(openBtn);
    barLayout->addWidget(clearBtn);
    barLayout->addWidget(m_modeBtn);
    barLayout->addStretch(1);
    barLayout->addWidget(m_status);

    outer->addWidget(bar);

    m_stack  = new QStackedWidget(this);
    m_viewer = new ScriptViewer(this);
    m_pdfView = new ScriptPdfView(this);
    m_stack->addWidget(m_viewer);   // index 0: text
    m_stack->addWidget(m_pdfView);  // index 1: pdf
    outer->addWidget(m_stack, 1);

    connect(openBtn,  &QPushButton::clicked, this, &ScriptWindow::openScript);
    connect(clearBtn, &QPushButton::clicked, this, &ScriptWindow::clearScript);
    connect(m_modeBtn, &QPushButton::toggled, this, &ScriptWindow::toggleMode);
}

ScriptWindow::~ScriptWindow() = default;

void ScriptWindow::setWorkspace(core::Workspace *ws)
{
    m_workspace = ws;
    if (m_viewer)  m_viewer->setWorkspace(ws);
    if (m_pdfView) m_pdfView->setWorkspace(ws);
    switchToFormat();
    updateStatus();
    if (auto *m = ws ? ws->scriptModel() : nullptr) {
        connect(m, &core::ScriptModel::scriptChanged,
                this, &ScriptWindow::updateStatus);
        connect(m, &core::ScriptModel::annotationsChanged,
                this, &ScriptWindow::updateStatus);
        connect(m, &core::ScriptModel::scriptChanged,
                this, &ScriptWindow::switchToFormat);
    }
}

bool ScriptWindow::isPdfActive() const
{
    return m_workspace && m_workspace->scriptModel()
        && m_workspace->scriptModel()->format() == core::ScriptModel::Format::Pdf;
}

void ScriptWindow::switchToFormat()
{
    if (!m_stack) return;
    m_stack->setCurrentIndex(isPdfActive() ? 1 : 0);
}

void ScriptWindow::setGoEngine(GoEngine *engine)
{
    if (m_goEngine == engine) return;
    if (m_goEngine) disconnect(m_goEngine, nullptr, this, nullptr);
    m_goEngine = engine;
    if (engine) {
        connect(engine, &GoEngine::cueFired,
                this, &ScriptWindow::onCueFired);
    }
}

void ScriptWindow::setSelectedCue(const QUuid &cueId)
{
    if (m_viewer) {
        m_viewer->setSelectedCue(cueId);
        if (m_viewer->mode() == ScriptViewer::Mode::Follow)
            m_viewer->setNextCue(cueId);
    }
    if (m_pdfView) {
        m_pdfView->setSelectedCue(cueId);
        if (m_pdfView->mode() == ScriptPdfView::Mode::Follow)
            m_pdfView->setNextCue(cueId);
    }
    updateStatus();
}

void ScriptWindow::openScript()
{
    if (!m_workspace || !m_workspace->scriptModel()) return;
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    const QString lastDir = s.value(QStringLiteral("script/lastDir")).toString();
    const QString path = QFileDialog::getOpenFileName(this, tr("Open script"),
        lastDir,
        tr("Script files (*.txt *.md *.text *.pdf);;PDF (*.pdf);;Text (*.txt *.md *.text);;All files (*)"));
    if (path.isEmpty()) return;
    s.setValue(QStringLiteral("script/lastDir"), QFileInfo(path).absolutePath());

    QString err;
    if (!m_workspace->scriptModel()->loadFromFile(path, &err)) {
        QMessageBox::warning(this, tr("Open script"),
            tr("Couldn't load script:\n%1").arg(err));
        return;
    }
}

void ScriptWindow::clearScript()
{
    if (!m_workspace || !m_workspace->scriptModel()) return;
    if (QMessageBox::question(this, tr("Clear script"),
            tr("Remove the script and all cue annotations?"),
            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        m_workspace->scriptModel()->clear();
    }
}

void ScriptWindow::toggleMode()
{
    const bool follow = m_modeBtn->isChecked();
    if (m_viewer) m_viewer->setMode(follow ? ScriptViewer::Mode::Follow
                                            : ScriptViewer::Mode::Edit);
    if (m_pdfView) m_pdfView->setMode(follow ? ScriptPdfView::Mode::Follow
                                              : ScriptPdfView::Mode::Edit);
    m_modeBtn->setText(follow ? tr("Follow mode") : tr("Edit mode"));
    updateStatus();
}

void ScriptWindow::onCueFired(cues::Cue *cue)
{
    if (!cue) return;
    if (m_viewer)  m_viewer->scrollToCue(cue->id());
    if (m_pdfView) m_pdfView->scrollToCue(cue->id());
}

void ScriptWindow::updateStatus()
{
    if (!m_workspace || !m_workspace->scriptModel()) {
        m_status->setText(tr("No workspace."));
        return;
    }
    auto *m = m_workspace->scriptModel();
    if (!m->hasScript()) {
        m_status->setText(tr("No script loaded — drop a .txt / .pdf or click Open script…"));
        return;
    }
    // "Missing" is decided by the file system, not by the in-memory
    // text buffer — PDFs are rendered by QPdfView and never populate
    // m_text.
    if (!QFileInfo::exists(m->path())) {
        m_status->setText(tr("⚠ Script file missing on disk: %1").arg(m->path()));
        return;
    }
    if (m->format() == core::ScriptModel::Format::Pdf) {
        m_status->setText(tr("%1 · PDF · %2 cue%3 bound")
            .arg(m->fileName())
            .arg(m->annotations().size())
            .arg(m->annotations().size() == 1 ? QString() : QStringLiteral("s")));
        return;
    }
    m_status->setText(tr("%1 · %2 lines · %3 cue%4 bound")
        .arg(m->fileName())
        .arg(m->lineCount())
        .arg(m->annotations().size())
        .arg(m->annotations().size() == 1 ? QString() : QStringLiteral("s")));
}

void ScriptWindow::closeEvent(QCloseEvent *event)
{
    // The window is owned by MainWindow with WA_DeleteOnClose=false, so
    // closing simply hides; a re-show via the menu picks up where the
    // user left off (mode + scroll position).
    event->accept();
    hide();
}

} // namespace quewi::ui
