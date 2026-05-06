#include "ui/ProjectionMappingDialog.h"

#include "ui/CornerPinEditor.h"
#include "video/VideoEngine.h"

#include "video/TestPatternLayer.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QTransform>
#include <QVBoxLayout>

namespace quewi::ui {

namespace {
QPolygonF identityQuad() {
    return QPolygonF{ {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0} };
}

QPolygonF parseQuad(const QString &json) {
    if (json.isEmpty()) return identityQuad();
    const auto doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isArray()) return identityQuad();
    QPolygonF q;
    for (const auto &v : doc.array()) {
        const auto o = v.toObject();
        q << QPointF(o.value(QStringLiteral("x")).toDouble(),
                     o.value(QStringLiteral("y")).toDouble());
    }
    if (q.size() != 4) return identityQuad();
    return q;
}

QString serialiseQuad(const QPolygonF &q) {
    QJsonArray arr;
    for (const auto &p : q) {
        QJsonObject o;
        o.insert(QStringLiteral("x"), p.x());
        o.insert(QStringLiteral("y"), p.y());
        arr.append(o);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}
} // namespace

QString ProjectionMappingDialog::settingsKey(int screenIndex)
{
    return QStringLiteral("video/cornerPin/screen%1").arg(screenIndex);
}

void ProjectionMappingDialog::applyPersistedQuads(video::VideoEngine *engine)
{
    if (!engine) return;
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    const auto screens = QGuiApplication::screens();
    for (int i = 0; i < screens.size(); ++i) {
        const auto json = s.value(settingsKey(i)).toString();
        if (json.isEmpty()) continue;
        const auto quad = parseQuad(json);
        engine->setCornerPin(i, quad);
    }
}

ProjectionMappingDialog::ProjectionMappingDialog(video::VideoEngine *engine,
                                                  QWidget *parent)
    : QDialog(parent), m_engine(engine)
{
    setWindowTitle(tr("Projection Mapping"));
    resize(540, 460);

    auto *root = new QVBoxLayout(this);

    auto *header = new QHBoxLayout();
    header->addWidget(new QLabel(tr("Screen:"), this));
    m_screen = new QComboBox(this);
    const auto screens = QGuiApplication::screens();
    for (int i = 0; i < screens.size(); ++i) {
        m_screen->addItem(tr("%1: %2 (%3×%4)")
            .arg(i)
            .arg(screens[i]->name(),
                 QString::number(screens[i]->geometry().width()),
                 QString::number(screens[i]->geometry().height())), i);
    }
    header->addWidget(m_screen, 1);
    root->addLayout(header);

    m_editor = new CornerPinEditor(this);
    root->addWidget(m_editor, 1);

    m_patternToggle = new QCheckBox(tr("Show test pattern on output"), this);
    m_patternToggle->setChecked(true);
    m_patternToggle->setToolTip(tr(
        "Project a calibration grid + corner markers + circle on the "
        "selected screen so you can see what you're warping. Removed "
        "automatically when the dialog closes."));
    root->addWidget(m_patternToggle);

    auto *btnRow = new QHBoxLayout();
    m_resetBtn  = new QPushButton(tr("Reset"), this);
    m_exportBtn = new QPushButton(tr("Export Pattern…"), this);
    m_exportBtn->setToolTip(tr(
        "Save the warped test pattern as a PNG so you can use it as a "
        "template in other tools (Photoshop, After Effects, etc.)."));
    m_saveBtn   = new QPushButton(tr("Save"), this);
    m_saveBtn->setToolTip(tr(
        "Persist the warp so it applies on the next quewi launch. Live "
        "preview is on while the dialog is open."));
    btnRow->addStretch(1);
    btnRow->addWidget(m_exportBtn);
    btnRow->addWidget(m_resetBtn);
    btnRow->addWidget(m_saveBtn);
    root->addLayout(btnRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    root->addWidget(buttons);

    connect(m_screen,    qOverload<int>(&QComboBox::activated),
            this,        &ProjectionMappingDialog::onScreenChanged);
    connect(m_editor,    &CornerPinEditor::cornersChanged,
            this,        &ProjectionMappingDialog::onCornersChanged);
    connect(m_resetBtn,   &QPushButton::clicked, this, &ProjectionMappingDialog::onResetClicked);
    connect(m_saveBtn,    &QPushButton::clicked, this, &ProjectionMappingDialog::onSaveClicked);
    connect(m_exportBtn,  &QPushButton::clicked, this, &ProjectionMappingDialog::onExportClicked);
    connect(m_patternToggle, &QCheckBox::toggled, this,
            &ProjectionMappingDialog::onTestPatternToggled);

    if (m_screen->count() > 0) {
        m_currentScreen = m_screen->currentData().toInt();
        loadForScreen(m_currentScreen);
    }

    // Show the pattern by default so the user has feedback the moment
    // they open the dialog.
    if (m_engine) m_engine->showTestPattern(m_currentScreen);
}

ProjectionMappingDialog::~ProjectionMappingDialog()
{
    // Drop the test pattern from every screen we might have shown it on.
    if (m_engine) {
        const auto screens = QGuiApplication::screens();
        for (int i = 0; i < screens.size(); ++i)
            if (m_engine->hasTestPattern(i)) m_engine->hideTestPattern(i);
    }
}

void ProjectionMappingDialog::loadForScreen(int screenIndex)
{
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    const auto json = s.value(settingsKey(screenIndex)).toString();
    m_editor->setCorners(parseQuad(json));
}

void ProjectionMappingDialog::onScreenChanged(int comboIndex)
{
    const int oldScreen = m_currentScreen;
    m_currentScreen = m_screen->itemData(comboIndex).toInt();
    // Move the test pattern with the user — if they have it on for
    // the previous screen, drop it there and re-add on the new one.
    if (m_engine && m_patternToggle->isChecked()) {
        m_engine->hideTestPattern(oldScreen);
        m_engine->showTestPattern(m_currentScreen);
    }
    loadForScreen(m_currentScreen);
}

void ProjectionMappingDialog::onCornersChanged(const QPolygonF &q)
{
    if (m_engine) m_engine->setCornerPin(m_currentScreen, q);
}

void ProjectionMappingDialog::onResetClicked()
{
    m_editor->reset();
    if (m_engine) m_engine->setCornerPin(m_currentScreen, identityQuad());
}

void ProjectionMappingDialog::onSaveClicked()
{
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    s.setValue(settingsKey(m_currentScreen), serialiseQuad(m_editor->corners()));
    m_saveBtn->setText(tr("Saved ✓"));
    m_saveBtn->setEnabled(false);
    QTimer::singleShot(1500, this, [this]{
        m_saveBtn->setText(tr("Save"));
        m_saveBtn->setEnabled(true);
    });
}

void ProjectionMappingDialog::onTestPatternToggled(bool on)
{
    if (!m_engine) return;
    if (on) m_engine->showTestPattern(m_currentScreen);
    else    m_engine->hideTestPattern(m_currentScreen);
}

void ProjectionMappingDialog::onExportClicked()
{
    // Render the test pattern with the current warp applied to a flat
    // PNG so the user can drop it into Photoshop / After Effects /
    // a projection-template tool. The output is full-resolution: the
    // pattern is 1920×1080, sized up to the screen's pixel dimensions
    // so it matches what the projector sees.
    const auto screens = QGuiApplication::screens();
    if (m_currentScreen < 0 || m_currentScreen >= screens.size()) return;
    const QSize screenPx = screens[m_currentScreen]->size();
    if (screenPx.isEmpty()) return;

    // Default to ~/Pictures with a sensible filename.
    const QString defaultName = QStringLiteral("quewi-projection-screen%1-%2x%3.png")
        .arg(m_currentScreen)
        .arg(screenPx.width()).arg(screenPx.height());
    const QString path = QFileDialog::getSaveFileName(this,
        tr("Export warped test pattern"),
        QDir::homePath() + QStringLiteral("/") + defaultName,
        tr("PNG image (*.png);;All files (*.*)"));
    if (path.isEmpty()) return;

    // Build the pattern (same one the live layer uses) and render it
    // with the warp applied. We use a temporary Layer because its
    // currentFrame() is the source of truth for the pattern bitmap;
    // duplicating the painter code here would drift over time.
    video::TestPatternLayer src;
    const QImage base = src.currentFrame();
    if (base.isNull()) return;

    QImage out(screenPx, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::black);

    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QPolygonF q = m_editor->corners();
    const bool isIdentity = q.size() == 4
        && qFuzzyCompare(q[0].x(), 0.0) && qFuzzyCompare(q[0].y(), 0.0)
        && qFuzzyCompare(q[1].x(), 1.0) && qFuzzyCompare(q[1].y(), 0.0)
        && qFuzzyCompare(q[2].x(), 1.0) && qFuzzyCompare(q[2].y(), 1.0)
        && qFuzzyCompare(q[3].x(), 0.0) && qFuzzyCompare(q[3].y(), 1.0);

    if (!isIdentity && q.size() == 4) {
        const qreal W = screenPx.width();
        const qreal H = screenPx.height();
        QPolygonF dst;
        for (const auto &c : q) dst << QPointF(c.x() * W, c.y() * H);
        QTransform t;
        if (QTransform::quadToQuad(QPolygonF{ {0, 0}, {W, 0}, {W, H}, {0, H} },
                                   dst, t)) {
            p.setTransform(t);
        }
    }
    p.drawImage(QRectF(0, 0, screenPx.width(), screenPx.height()), base);
    p.end();

    if (!out.save(path, "PNG")) {
        QMessageBox::warning(this, tr("Export failed"),
            tr("Could not write %1.").arg(path));
        return;
    }
    m_exportBtn->setText(tr("Exported ✓"));
    m_exportBtn->setEnabled(false);
    QTimer::singleShot(1500, this, [this]{
        m_exportBtn->setText(tr("Export Pattern…"));
        m_exportBtn->setEnabled(true);
    });
}

} // namespace quewi::ui
