#include "ui/CartView.h"

#include "app/GoEngine.h"
#include "audio/AudioCue.h"
#include "core/CartGrid.h"
#include "core/CueList.h"
#include "core/Workspace.h"
#include "cues/Cue.h"
#include "ui/Theme.h"

#include <QAudioDevice>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QEnterEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLinearGradient>
#include <QLineEdit>
#include <QMediaDevices>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QTabBar>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>

namespace quewi::ui {

namespace {

QColor textColorFor(const QColor &c)
{
    const double lum = 0.299 * c.redF() + 0.587 * c.greenF() + 0.114 * c.blueF();
    return lum > 0.62 ? QColor(0x16, 0x18, 0x1e) : QColor(0xf2, 0xf5, 0xf8);
}

// A curated, vibrant palette for colour-coding pads — the soundboard reads
// like a Launchpad rather than a wall of grey.
const QList<QColor> &padPalette()
{
    static const QList<QColor> p = {
        QColor("#E0564E"), QColor("#E08A3C"), QColor("#E8C24E"),
        QColor("#7DBE5A"), QColor("#46A99A"), QColor("#4F8FCB"),
        QColor("#7E6BD0"), QColor("#C264A6"), QColor("#5B6472"),
    };
    return p;
}

} // namespace

// ───────────────────────────────────────────────────────────────────────────
// CartPad — one tile. Custom-painted so we control the soundboard look.
// ───────────────────────────────────────────────────────────────────────────
class CartPad : public QWidget {
    Q_OBJECT
public:
    CartPad(int row, int col, QWidget *parent = nullptr)
        : QWidget(parent), m_row(row), m_col(col)
    {
        setAcceptDrops(true);
        setMinimumSize(112, 80);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
    }

    int row() const { return m_row; }
    int col() const { return m_col; }
    cues::Cue *cue() const { return m_cue.data(); }

    void setCue(cues::Cue *c)            { m_cue = c; update(); }
    void setCell(const core::CartCell &c){ m_cell = c; update(); }
    void setPlaying(bool p)              { if (m_playing == p) return; m_playing = p; update(); }
    void setEditMode(bool e)             { if (m_editMode == e) return; m_editMode = e; update(); }

signals:
    void clicked(int row, int col);
    void editRequested(int row, int col);
    void editCueRequested(int row, int col);
    void fileDropped(int row, int col, const QString &path);

protected:
    void enterEvent(QEnterEvent *) override { m_hover = true;  update(); }
    void leaveEvent(QEvent *)      override { m_hover = false; m_pressed = false; update(); }

    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) { m_pressed = true; update(); }
        QWidget::mousePressEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            const bool was = m_pressed;
            m_pressed = false; update();
            if (was && rect().contains(e->pos())) {
                if (m_editMode) emit editRequested(m_row, m_col);
                else            emit clicked(m_row, m_col);
            }
        }
        QWidget::mouseReleaseEvent(e);
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override {
        // Double-clicking an EMPTY pad opens a file picker to assign a sound.
        // Bound pads keep their click=fire behaviour (no dialog), so this only
        // ever triggers on a blank tile. Reuses the fileDropped pipeline so
        // the cue is created and bound exactly as a drag-drop would.
        if (e->button() == Qt::LeftButton && !m_cue) {
            m_pressed = false;
            const QString p = QFileDialog::getOpenFileName(this,
                tr("Pick a sound for this pad"), QString(),
                tr("Audio files (*.wav *.mp3 *.flac *.aiff *.aif *.ogg *.oga "
                   "*.opus *.m4a *.aac *.wma *.webm);;All files (*.*)"));
            if (!p.isEmpty()) emit fileDropped(m_row, m_col, p);
            return;
        }
        QWidget::mouseDoubleClickEvent(e);
    }

    void dragEnterEvent(QDragEnterEvent *e) override {
        if (e->mimeData()->hasUrls()) { m_dragHover = true; update(); e->acceptProposedAction(); }
    }
    void dragMoveEvent(QDragMoveEvent *e) override {
        if (e->mimeData()->hasUrls()) e->acceptProposedAction();
    }
    void contextMenuEvent(QContextMenuEvent *e) override {
        // Right-click an audio pad to open its cue in the audio editor
        // (EQ / trim / compress) without leaving the soundboard.
        if (!qobject_cast<audio::AudioCue *>(m_cue.data())) return;
        QMenu menu(this);
        menu.addAction(tr("Open in audio editor…"), this,
                       [this]{ emit editCueRequested(m_row, m_col); });
        menu.exec(e->globalPos());
    }
    void dragLeaveEvent(QDragLeaveEvent *) override { m_dragHover = false; update(); }
    void dropEvent(QDropEvent *e) override {
        m_dragHover = false; update();
        const auto urls = e->mimeData()->urls();
        if (urls.isEmpty()) return;
        const QString p = urls.first().toLocalFile();
        if (p.isEmpty()) return;
        emit fileDropped(m_row, m_col, p);
        e->acceptProposedAction();
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(rect()).adjusted(2, 2, -2, -2);
        const qreal radius = 11;

        if (!m_cue) {
            // Empty pad — quiet dashed slot inviting a drop. When a file is
            // being dragged over it, light up in the accent colour so the
            // operator sees exactly which pad will receive the drop.
            if (m_dragHover) {
                const QColor acc = Theme::tokens().accent;
                QColor fill = acc; fill.setAlpha(40);
                p.setPen(Qt::NoPen); p.setBrush(fill);
                p.drawRoundedRect(r, radius, radius);
                p.setPen(QPen(acc, 2.4)); p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(r.adjusted(3, 3, -3, -3), radius - 2, radius - 2);
                p.setPen(acc);
                p.drawText(r, Qt::AlignCenter, tr("drop\nhere"));
                return;
            }
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, m_hover ? 16 : 8));
            p.drawRoundedRect(r, radius, radius);
            QPen dash(QColor(255, 255, 255, m_hover ? 90 : 45), 1.2, Qt::DashLine);
            p.setPen(dash); p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(5, 5, -5, -5), radius - 3, radius - 3);
            p.setPen(QColor(255, 255, 255, m_hover ? 150 : 80));
            p.drawText(r, Qt::AlignCenter, tr("drop\nsound"));
            return;
        }

        const QColor base = effectiveColor();
        QColor top = base, bottom = base.darker(122);
        if (m_pressed)      { top = base.darker(118); bottom = base.darker(145); }
        else if (m_hover)   { top = base.lighter(110); bottom = base; }

        QLinearGradient grad(r.topLeft(), r.bottomLeft());
        grad.setColorAt(0.0, top);
        grad.setColorAt(1.0, bottom);
        p.setBrush(grad); p.setPen(Qt::NoPen);
        p.drawRoundedRect(r, radius, radius);

        if (m_playing) {
            // Glow ring while the cue plays.
            QColor glow = base.lighter(170); glow.setAlpha(235);
            p.setPen(QPen(glow, 3)); p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(1.5, 1.5, -1.5, -1.5), radius - 1, radius - 1);
        } else {
            p.setPen(QPen(base.lighter(138), 1)); p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r, radius, radius);
        }

        const QColor ink = textColorFor(base);
        QColor sub = ink; sub.setAlpha(165);

        // Cue number, top-left.
        QFont small = font(); small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.5));
        small.setStyleHint(QFont::Monospace);
        p.setFont(small); p.setPen(sub);
        p.drawText(r.adjusted(9, 6, -9, -9), Qt::AlignTop | Qt::AlignLeft, numberText());

        // Hotkey chip, top-right.
        if (!m_cell.hotkey.isEmpty())
            drawChip(p, r, m_cell.hotkey, Qt::AlignTop | Qt::AlignRight, base, ink);
        // MIDI chip, bottom-right.
        if (m_cell.midiNote >= 0)
            drawChip(p, r, QStringLiteral("♪ %1").arg(m_cell.midiNote),
                     Qt::AlignBottom | Qt::AlignRight, base, ink);

        // Label, centred.
        QFont big = font(); big.setBold(true); big.setPointSizeF(big.pointSizeF() + 1.0);
        p.setFont(big); p.setPen(ink);
        const QString label = m_cell.label.isEmpty()
            ? (m_cue->name().isEmpty() ? m_cue->typeName() : m_cue->name())
            : m_cell.label;
        p.drawText(r.adjusted(9, 17, -9, -16), Qt::AlignCenter | Qt::TextWordWrap, label);

        if (m_editMode) {
            p.setFont(small); p.setPen(sub);
            p.drawText(r.adjusted(9, 0, -9, -6), Qt::AlignBottom | Qt::AlignLeft, tr("edit"));
        }

        // Drop-target ring — a file dragged over a BOUND pad will replace it,
        // so show the same accent ring as an empty target.
        if (m_dragHover) {
            const QColor acc = Theme::tokens().accent;
            p.setPen(QPen(acc, 2.6)); p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r, radius, radius);
        }
    }

private:
    QColor effectiveColor() const {
        if (m_cell.color.isValid()) return m_cell.color;
        if (m_cue && m_cue->color().isValid()) return m_cue->color();
        return Theme::tokens().accent;
    }
    QString numberText() const {
        const double n = m_cue ? m_cue->number() : 0.0;
        const bool whole = qFuzzyCompare(n, std::round(n));
        return QStringLiteral("Q%1").arg(n, 0, 'f', whole ? 0 : 2);
    }
    void drawChip(QPainter &p, const QRectF &r, const QString &text,
                  Qt::Alignment align, const QColor &base, const QColor &ink) const {
        QFont chipF = font(); chipF.setPointSizeF(std::max(7.0, chipF.pointSizeF() - 1.5));
        chipF.setBold(true);
        p.setFont(chipF);
        const QFontMetrics fm(chipF);
        const int tw = fm.horizontalAdvance(text);
        const QSizeF sz(tw + 12, fm.height() + 4);
        qreal x = (align & Qt::AlignRight) ? r.right() - sz.width() - 6 : r.left() + 6;
        qreal y = (align & Qt::AlignBottom) ? r.bottom() - sz.height() - 6 : r.top() + 6;
        const QRectF chip(x, y, sz.width(), sz.height());
        QColor chipBg = base.darker(150); chipBg.setAlpha(200);
        p.setPen(Qt::NoPen); p.setBrush(chipBg);
        p.drawRoundedRect(chip, 5, 5);
        p.setPen(ink);
        p.drawText(chip, Qt::AlignCenter, text);
    }

    int m_row, m_col;
    QPointer<cues::Cue> m_cue;
    core::CartCell m_cell;
    bool m_playing = false, m_editMode = false, m_hover = false, m_pressed = false;
    bool m_dragHover = false;
};

// ───────────────────────────────────────────────────────────────────────────
// CartPadEditDialog — customise one pad: label, colour, hotkey, MIDI note.
// ───────────────────────────────────────────────────────────────────────────
class CartPadEditDialog : public QDialog {
    Q_OBJECT
public:
    CartPadEditDialog(core::CartGrid *cart, CartView *view, int row, int col,
                      const QString &cueName, QWidget *parent)
        : QDialog(parent), m_cart(cart), m_view(view), m_row(row), m_col(col)
    {
        setWindowTitle(tr("Customise Pad"));
        setModal(true);
        const core::CartCell cell = cart->cell(row, col);
        m_color = cell.color;

        auto *form = new QFormLayout(this);

        m_label = new QLineEdit(cell.label, this);
        m_label->setPlaceholderText(cueName.isEmpty() ? tr("(cue name)") : cueName);
        form->addRow(tr("Label"), m_label);

        // Colour swatches + custom + default.
        auto *colorRow = new QWidget(this);
        auto *cl = new QHBoxLayout(colorRow);
        cl->setContentsMargins(0, 0, 0, 0); cl->setSpacing(4);
        for (const QColor &c : padPalette()) {
            auto *sw = new QPushButton(colorRow);
            sw->setFixedSize(22, 22);
            sw->setCheckable(true);
            sw->setStyleSheet(QStringLiteral(
                "QPushButton{background:%1;border:1px solid %2;border-radius:5px;}"
                "QPushButton:checked{border:2px solid %3;}")
                .arg(c.name(), Theme::tokens().bgDeep.name(),
                     Theme::tokens().accent.name()));
            connect(sw, &QPushButton::clicked, this, [this, c]{ setColor(c); });
            m_swatches.append(sw);
            cl->addWidget(sw);
        }
        auto *custom = new QPushButton(tr("Custom…"), colorRow);
        connect(custom, &QPushButton::clicked, this, [this]{
            const QColor c = QColorDialog::getColor(
                m_color.isValid() ? m_color : Qt::white, this, tr("Pad colour"));
            if (c.isValid()) setColor(c);
        });
        cl->addWidget(custom);
        auto *def = new QPushButton(tr("Default"), colorRow);
        connect(def, &QPushButton::clicked, this, [this]{ setColor(QColor()); });
        cl->addWidget(def);
        cl->addStretch(1);
        form->addRow(tr("Colour"), colorRow);
        refreshSwatchChecks();

        m_hotkey = new QKeySequenceEdit(this);
        m_hotkey->setMaximumSequenceLength(1);
        if (!cell.hotkey.isEmpty())
            m_hotkey->setKeySequence(QKeySequence(cell.hotkey));
        form->addRow(tr("Hotkey"), m_hotkey);

        auto *midiRow = new QWidget(this);
        auto *ml = new QHBoxLayout(midiRow);
        ml->setContentsMargins(0, 0, 0, 0); ml->setSpacing(6);
        m_midi = new QSpinBox(midiRow);
        m_midi->setRange(-1, 127);
        m_midi->setSpecialValueText(tr("None"));
        m_midi->setValue(cell.midiNote < 0 ? -1 : cell.midiNote);
        ml->addWidget(m_midi);
        m_learnBtn = new QPushButton(tr("Learn"), midiRow);
        m_learnBtn->setCheckable(true);
        m_learnBtn->setToolTip(tr("Press a pad on your MIDI controller to assign it"));
        connect(m_learnBtn, &QPushButton::toggled, this, [this](bool on){
            if (on) {
                m_learnBtn->setText(tr("Press a pad…"));
                m_view->beginMidiLearn(m_row, m_col, [this](int note){
                    m_midi->setValue(note);
                    m_learnBtn->setChecked(false);
                });
            } else {
                m_learnBtn->setText(tr("Learn"));
                m_view->cancelMidiLearn();
            }
        });
        ml->addWidget(m_learnBtn);
        ml->addStretch(1);
        form->addRow(tr("MIDI note"), midiRow);

        auto *bb = new QDialogButtonBox(this);
        auto *unbind = bb->addButton(tr("Unbind cue"), QDialogButtonBox::DestructiveRole);
        bb->addButton(QDialogButtonBox::Ok);
        bb->addButton(QDialogButtonBox::Cancel);
        connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(unbind, &QPushButton::clicked, this, [this]{ m_unbind = true; accept(); });
        form->addRow(bb);
    }

    void apply() {
        if (!m_cart) return;
        if (m_unbind) { m_cart->setCell(m_row, m_col, QUuid()); return; }
        m_cart->setCellLabel(m_row, m_col, m_label->text());
        m_cart->setCellColor(m_row, m_col, m_color);
        const auto seq = m_hotkey->keySequence();
        m_cart->setCellHotkey(m_row, m_col,
            seq.isEmpty() ? QString() : seq.toString(QKeySequence::PortableText));
        m_cart->setCellMidiNote(m_row, m_col, m_midi->value());
    }

protected:
    void done(int r) override { if (m_view) m_view->cancelMidiLearn(); QDialog::done(r); }

private:
    void setColor(const QColor &c) { m_color = c; refreshSwatchChecks(); }
    void refreshSwatchChecks() {
        for (int i = 0; i < m_swatches.size(); ++i)
            m_swatches[i]->setChecked(m_color.isValid()
                                      && padPalette()[i].name() == m_color.name());
    }

    core::CartGrid *m_cart;
    CartView       *m_view;
    int m_row, m_col;
    QColor m_color;
    bool   m_unbind = false;
    QLineEdit        *m_label = nullptr;
    QKeySequenceEdit *m_hotkey = nullptr;
    QSpinBox         *m_midi = nullptr;
    QPushButton      *m_learnBtn = nullptr;
    QList<QPushButton *> m_swatches;
};

// ───────────────────────────────────────────────────────────────────────────
// CartView
// ───────────────────────────────────────────────────────────────────────────

CartView::CartView(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("cartView"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 8, 10, 10);
    outer->setSpacing(8);

    // ── Toolbar ────────────────────────────────────────────────────────
    const auto &tk = Theme::tokens();
    auto *bar = new QHBoxLayout();
    bar->setSpacing(8);
    auto *title = new QLabel(tr("SOUNDBOARD"), this);
    title->setStyleSheet(QStringLiteral(
        "color:%1; font-size:12px; font-weight:800; letter-spacing:0.18em;")
        .arg(tk.ink60.name()));
    bar->addWidget(title);
    bar->addStretch(1);

    // Output-device picker — route the whole board to a chosen device
    // (e.g. a virtual cable) independent of the main playback device.
    m_outputCombo = new QComboBox(this);
    m_outputCombo->setCursor(Qt::PointingHandCursor);
    m_outputCombo->setToolTip(tr("Route the whole soundboard to this output device"));
    m_outputCombo->addItem(tr("Output: (default)"), QByteArray());
    for (const auto &dev : QMediaDevices::audioOutputs())
        m_outputCombo->addItem(tr("Output: %1").arg(dev.description()), dev.id());
    connect(m_outputCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_workspace && m_workspace->cart())
            m_workspace->cart()->setOutputDeviceId(
                m_outputCombo->currentData().toByteArray());
    });
    bar->addWidget(m_outputCombo);

    m_editBtn = new QPushButton(tr("Edit Layout"), this);
    m_editBtn->setCheckable(true);
    m_editBtn->setCursor(Qt::PointingHandCursor);
    m_editBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:%1;color:%2;border:1px solid %3;border-radius:6px;"
        "  padding:6px 14px;font-weight:600;}"
        "QPushButton:checked{background:%4;color:%5;border-color:%4;}")
        .arg(tk.bgInteractive.name(), tk.ink100.name(), tk.outline.name(),
             tk.accent.name(), tk.bgDeep.name()));
    connect(m_editBtn, &QPushButton::clicked, this, &CartView::toggleEditMode);
    bar->addWidget(m_editBtn);

    auto *resizeBtn = new QPushButton(tr("Resize…"), this);
    resizeBtn->setCursor(Qt::PointingHandCursor);
    resizeBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:%1;color:%2;border:1px solid %3;border-radius:6px;"
        "  padding:6px 14px;font-weight:600;}")
        .arg(tk.bgInteractive.name(), tk.ink100.name(), tk.outline.name()));
    connect(resizeBtn, &QPushButton::clicked, this, &CartView::resizeBoard);
    bar->addWidget(resizeBtn);

    auto *stopBtn = new QPushButton(tr("Stop All"), this);
    stopBtn->setCursor(Qt::PointingHandCursor);
    stopBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:%1;color:white;border:none;border-radius:6px;"
        "  padding:6px 16px;font-weight:700;}"
        "QPushButton:hover{background:%2;}")
        .arg(tk.err.name(), tk.err.lighter(115).name()));
    connect(stopBtn, &QPushButton::clicked, this, &CartView::stopAllRequested);
    bar->addWidget(stopBtn);
    outer->addLayout(bar);

    // ── Layer switcher ─────────────────────────────────────────────────
    // Stack several boards behind one soundboard tab (Act 1 / Act 2 / spot
    // FX). Only the active layer is shown and fires. Double-click a tab to
    // rename it; right-click for rename/delete; "+" adds a layer.
    auto *layerRow = new QHBoxLayout();
    layerRow->setSpacing(6);
    m_layerTabs = new QTabBar(this);
    m_layerTabs->setExpanding(false);
    m_layerTabs->setDrawBase(false);
    m_layerTabs->setFocusPolicy(Qt::NoFocus);
    m_layerTabs->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_layerTabs, &QTabBar::currentChanged, this, [this](int i) {
        if (m_syncingLayers) return;        // programmatic sync, not a user click
        if (m_workspace && m_workspace->cart()) m_workspace->cart()->setActiveLayer(i);
    });
    connect(m_layerTabs, &QTabBar::tabBarDoubleClicked, this, &CartView::renameLayer);
    connect(m_layerTabs, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
        const int idx = m_layerTabs->tabAt(pos);
        if (idx < 0) return;
        QMenu menu(this);
        menu.addAction(tr("Rename layer…"), this, [this, idx]{ renameLayer(idx); });
        QAction *del = menu.addAction(tr("Delete layer"), this,
                                      [this, idx]{ deleteLayer(idx); });
        del->setEnabled(m_workspace && m_workspace->cart()
                        && m_workspace->cart()->layerCount() > 1);
        menu.exec(m_layerTabs->mapToGlobal(pos));
    });
    layerRow->addWidget(m_layerTabs);

    auto *addLayerBtn = new QPushButton(QStringLiteral("+"), this);
    addLayerBtn->setCursor(Qt::PointingHandCursor);
    addLayerBtn->setToolTip(tr("Add a layer"));
    addLayerBtn->setFixedSize(26, 24);
    addLayerBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:%1;color:%2;border:1px solid %3;border-radius:6px;"
        "  font-size:15px;font-weight:700;}"
        "QPushButton:hover{border-color:%4;color:%4;}")
        .arg(tk.bgInteractive.name(), tk.ink100.name(), tk.outline.name(),
             tk.accent.name()));
    connect(addLayerBtn, &QPushButton::clicked, this, &CartView::addLayer);
    layerRow->addWidget(addLayerBtn);
    layerRow->addStretch(1);
    outer->addLayout(layerRow);

    // ── Pad grid ───────────────────────────────────────────────────────
    m_gridHost = new QWidget(this);
    m_grid = new QGridLayout(m_gridHost);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(8);
    outer->addWidget(m_gridHost, 1);

    // Poll playing state so pads glow while their cue runs.
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(120);
    connect(m_pollTimer, &QTimer::timeout, this, &CartView::onPollPlaying);
}

CartView::~CartView() = default;

void CartView::setWorkspace(core::Workspace *ws)
{
    if (m_workspace) {
        if (auto *cart = m_workspace->cart())
            disconnect(cart, nullptr, this, nullptr);
    }
    m_workspace = ws;
    if (auto *cart = ws ? ws->cart() : nullptr) {
        connect(cart, &core::CartGrid::layoutChanged, this, &CartView::onLayoutChanged);
        connect(cart, &core::CartGrid::layersChanged, this, &CartView::rebuildLayerBar);
    }

    // Reflect the saved board output device in the picker. Block signals so
    // restoring doesn't write back (and an unplugged saved device falls back
    // to "default" visually while the cart keeps its id for when it returns).
    if (m_outputCombo) {
        const QByteArray dev = (ws && ws->cart()) ? ws->cart()->outputDeviceId()
                                                  : QByteArray();
        m_outputCombo->blockSignals(true);
        const int idx = m_outputCombo->findData(dev);
        m_outputCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        m_outputCombo->blockSignals(false);
    }
    rebuildLayerBar();
    rebuildGrid();
}

void CartView::setGoEngine(GoEngine *engine) { m_goEngine = engine; }

void CartView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_pollTimer->start();
    setFocus();
}

void CartView::onLayoutChanged() { rebuildGrid(); }

void CartView::rebuildLayerBar()
{
    if (!m_layerTabs) return;
    auto *cart = m_workspace ? m_workspace->cart() : nullptr;
    // Block the currentChanged echo while we mirror the model into the tabs.
    m_syncingLayers = true;
    while (m_layerTabs->count() > 0) m_layerTabs->removeTab(0);
    if (cart) {
        for (int i = 0; i < cart->layerCount(); ++i)
            m_layerTabs->addTab(cart->layerName(i));
        const int a = cart->activeLayer();
        if (a >= 0 && a < m_layerTabs->count())
            m_layerTabs->setCurrentIndex(a);
    }
    m_syncingLayers = false;
}

void CartView::addLayer()
{
    if (m_workspace && m_workspace->cart())
        m_workspace->cart()->addLayer();   // becomes active; signals refresh the UI
}

void CartView::renameLayer(int index)
{
    auto *cart = m_workspace ? m_workspace->cart() : nullptr;
    if (!cart || index < 0 || index >= cart->layerCount()) return;
    bool ok = false;
    const auto name = QInputDialog::getText(this, tr("Rename layer"),
        tr("Name:"), QLineEdit::Normal, cart->layerName(index), &ok);
    if (ok && !name.isEmpty()) cart->setLayerName(index, name);
}

void CartView::deleteLayer(int index)
{
    auto *cart = m_workspace ? m_workspace->cart() : nullptr;
    if (!cart || cart->layerCount() <= 1) return;     // keep at least one layer
    if (index < 0 || index >= cart->layerCount()) return;
    if (QMessageBox::question(this, tr("Delete layer"),
            tr("Delete layer \"%1\"? Its pad layout is removed — the cues "
               "themselves stay in the show.").arg(cart->layerName(index)))
        != QMessageBox::Yes) return;
    cart->removeLayer(index);
}

void CartView::onCueChanged() { for (auto *pad : m_pads) pad->update(); }

cues::Cue *CartView::cueForCellId(const QUuid &id) const
{
    if (id.isNull() || !m_workspace) return nullptr;
    for (const auto &list : m_workspace->cueLists())
        for (int row = 0; row < list->cueCount(); ++row)
            if (auto *c = list->cueAt(row); c && c->id() == id) return c;
    return nullptr;
}

void CartView::rebuildGrid()
{
    while (m_grid->count() > 0) {
        if (auto *w = m_grid->itemAt(0)->widget()) {
            m_grid->removeWidget(w);
            w->deleteLater();
        }
    }
    m_pads.clear();
    if (!m_workspace || !m_workspace->cart()) return;

    auto *cart = m_workspace->cart();
    for (int r = 0; r < cart->rows(); ++r) {
        for (int c = 0; c < cart->cols(); ++c) {
            auto *pad = new CartPad(r, c, m_gridHost);
            const core::CartCell cell = cart->cell(r, c);
            pad->setCell(cell);
            pad->setCue(cueForCellId(cell.cueId));
            pad->setEditMode(m_editMode);

            connect(pad, &CartPad::clicked,      this, &CartView::onPadClicked);
            connect(pad, &CartPad::editRequested, this, &CartView::onPadEdit);
            connect(pad, &CartPad::fileDropped,  this, &CartView::fileDropped);
            connect(pad, &CartPad::editCueRequested, this, [this](int rr, int cc) {
                if (!m_workspace || !m_workspace->cart()) return;
                if (auto *cue = cueForCellId(m_workspace->cart()->cueAt(rr, cc)))
                    emit editCueRequested(cue);
            });
            if (auto *cue = pad->cue())
                // UniqueConnection: rebuildGrid re-runs on every layout change
                // but the Cue and CartView both outlive the rebuild, so without
                // this the slot accumulates a duplicate per rebuild.
                connect(cue, &cues::Cue::changed, this, &CartView::onCueChanged,
                        Qt::UniqueConnection);

            m_grid->addWidget(pad, r, c);
            m_pads.append(pad);
        }
    }
    rebuildHotkeys();
}

void CartView::rebuildHotkeys()
{
    qDeleteAll(m_shortcuts);
    m_shortcuts.clear();
    if (!m_workspace || !m_workspace->cart()) return;
    auto *cart = m_workspace->cart();
    for (int r = 0; r < cart->rows(); ++r) {
        for (int c = 0; c < cart->cols(); ++c) {
            const auto cell = cart->cell(r, c);
            if (cell.hotkey.isEmpty() || cell.cueId.isNull()) continue;
            auto *sc = new QShortcut(QKeySequence(cell.hotkey), this);
            sc->setContext(Qt::WidgetWithChildrenShortcut);
            connect(sc, &QShortcut::activated, this, [this, r, c]{ firePadAt(r, c); });
            m_shortcuts.append(sc);
        }
    }
}

void CartView::onPadClicked(int row, int col) { firePadAt(row, col); }

void CartView::onPadEdit(int row, int col)
{
    if (cueForCellId(m_workspace && m_workspace->cart()
                     ? m_workspace->cart()->cueAt(row, col) : QUuid()))
        editPad(row, col);
    // Clicking an EMPTY pad in edit mode does nothing yet — drop a sound on it.
}

bool CartView::firePadAt(int row, int col)
{
    if (!m_workspace || !m_workspace->cart()) return false;
    const QUuid id = m_workspace->cart()->cueAt(row, col);
    auto *cue = cueForCellId(id);
    if (!cue) return false;
    // Flash the pad immediately for tactile feedback.
    for (auto *pad : m_pads)
        if (pad->row() == row && pad->col() == col) { pad->setPlaying(true); break; }
    emit fireRequested(cue);
    return true;
}

bool CartView::firePadIndex(int index)
{
    if (!m_workspace || !m_workspace->cart()) return false;
    auto *cart = m_workspace->cart();
    if (index < 0 || index >= cart->rows() * cart->cols()) return false;
    return firePadAt(index / cart->cols(), index % cart->cols());
}

bool CartView::handleMidiNote(int note)
{
    // MIDI learn in progress — capture this note for the pad being edited.
    if (m_learnRow >= 0 && m_learnCol >= 0) {
        // Move the callback into a local and clear the parked state BEFORE
        // invoking it: the learn lambda unchecks the Learn button, which
        // synchronously emits toggled() -> cancelMidiLearn(), which sets
        // m_learnCallback = nullptr — i.e. it would free the std::function
        // whose operator() is still on the stack (a self-referential free).
        // Owning it locally keeps the live invocation alive until it returns.
        auto cb = std::move(m_learnCallback);
        m_learnRow = m_learnCol = -1;
        m_learnCallback = nullptr;
        if (cb) cb(note);
        return true;
    }
    if (!m_workspace || !m_workspace->cart()) return false;
    const auto [r, c] = m_workspace->cart()->cellOfMidiNote(note);
    if (r < 0) return false;
    return firePadAt(r, c);
}

void CartView::beginMidiLearn(int row, int col, std::function<void(int)> onLearned)
{
    m_learnRow = row; m_learnCol = col; m_learnCallback = std::move(onLearned);
}

void CartView::cancelMidiLearn()
{
    m_learnRow = m_learnCol = -1;
    m_learnCallback = nullptr;
}

void CartView::editPad(int row, int col)
{
    if (!m_workspace || !m_workspace->cart()) return;
    auto *cart = m_workspace->cart();
    auto *cue = cueForCellId(cart->cueAt(row, col));
    const QString name = cue ? (cue->name().isEmpty() ? cue->typeName() : cue->name()) : QString();
    CartPadEditDialog dlg(cart, this, row, col, name, this);
    if (dlg.exec() == QDialog::Accepted) dlg.apply(); // apply mutates the cart → rebuild
}

void CartView::toggleEditMode()
{
    m_editMode = m_editBtn->isChecked();
    for (auto *pad : m_pads) pad->setEditMode(m_editMode);
}

void CartView::resizeBoard()
{
    if (!m_workspace || !m_workspace->cart()) return;
    auto *cart = m_workspace->cart();
    bool ok = false;
    const int rows = QInputDialog::getInt(this, tr("Resize soundboard"),
        tr("Rows:"), cart->rows(), 1, 16, 1, &ok);
    if (!ok) return;
    const int cols = QInputDialog::getInt(this, tr("Resize soundboard"),
        tr("Columns:"), cart->cols(), 1, 12, 1, &ok);
    if (!ok) return;
    cart->setSize(rows, cols);
}

void CartView::onPollPlaying()
{
    if (!isVisible()) { m_pollTimer->stop(); return; }
    QSet<quint64> playing;
    if (m_goEngine) playing = m_goEngine->activeAudioVoiceIds();
    for (auto *pad : m_pads) {
        bool on = false;
        if (auto *ac = qobject_cast<audio::AudioCue *>(pad->cue()))
            on = ac->currentVoiceId() != 0 && playing.contains(ac->currentVoiceId());
        pad->setPlaying(on);
    }
}

} // namespace quewi::ui

#include "CartView.moc"
