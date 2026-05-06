#include "ui/ProjectionMappingDialog.h"

#include "ui/CornerPinEditor.h"
#include "video/VideoEngine.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QTimer>
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

    auto *btnRow = new QHBoxLayout();
    m_resetBtn = new QPushButton(tr("Reset"), this);
    m_saveBtn  = new QPushButton(tr("Save"), this);
    m_saveBtn->setToolTip(tr(
        "Persist the warp so it applies on the next quewi launch. Live "
        "preview is on while the dialog is open."));
    btnRow->addStretch(1);
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
    connect(m_resetBtn,  &QPushButton::clicked, this,
            &ProjectionMappingDialog::onResetClicked);
    connect(m_saveBtn,   &QPushButton::clicked, this,
            &ProjectionMappingDialog::onSaveClicked);

    if (m_screen->count() > 0) {
        m_currentScreen = m_screen->currentData().toInt();
        loadForScreen(m_currentScreen);
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
    m_currentScreen = m_screen->itemData(comboIndex).toInt();
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

} // namespace quewi::ui
