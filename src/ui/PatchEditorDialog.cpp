#include "ui/PatchEditorDialog.h"
#include "ui/Theme.h"

#include <QAudioDevice>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMediaDevices>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace quewi::ui {

using Category = core::PatchManager::Category;

PatchEditorDialog::PatchEditorDialog(core::PatchManager *manager, QWidget *parent)
    : QDialog(parent), m_manager(manager)
{
    setWindowTitle(tr("Patch Editor"));
    resize(820, 540);

    auto *root = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    // Deliberately NOT every Category: MixingConsole is omitted because it has
    // no field editor in the switch below yet, and listing it would open an
    // empty tab. Add it here at the same time as its editor, not before.
    for (auto cat : {Category::AudioOutput, Category::OscDestination,
                     Category::MidiPort,    Category::DmxUniverse,
                     Category::VideoSurface, Category::SpeakerArray})
    {
        m_tabs->addTab(buildTab(cat), core::PatchManager::categoryLabel(cat));
    }
    root->addWidget(m_tabs, 1);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(bb);
}

PatchEditorDialog::~PatchEditorDialog() = default;

// ── Tab construction ──────────────────────────────────────────────────────────

QWidget *PatchEditorDialog::buildTab(Category cat) {
    auto *page = new QWidget(this);
    auto *hl   = new QHBoxLayout(page);
    hl->setContentsMargins(8, 8, 8, 8);
    hl->setSpacing(8);

    // Left: list + add/remove
    auto *leftPanel = new QWidget(page);
    auto *lpl = new QVBoxLayout(leftPanel);
    lpl->setContentsMargins(0,0,0,0);
    lpl->setSpacing(4);

    auto *list = new QListWidget(leftPanel);
    list->setMinimumWidth(220);
    lpl->addWidget(list, 1);

    auto *btnRow = new QWidget(leftPanel);
    auto *brl = new QHBoxLayout(btnRow);
    brl->setContentsMargins(0,0,0,0);
    auto *addBtn = new QPushButton(tr("+ Add"),    btnRow);
    auto *delBtn = new QPushButton(tr("− Remove"), btnRow);
    auto *renBtn = new QPushButton(tr("Rename…"),  btnRow);
    brl->addWidget(addBtn); brl->addWidget(delBtn); brl->addWidget(renBtn);
    lpl->addWidget(btnRow);

    hl->addWidget(leftPanel);

    // Right: form for the selected patch
    auto *form = buildForm(cat, list);
    hl->addWidget(form, 1);

    refreshList(cat, list);

    // Wire add/remove
    connect(addBtn, &QPushButton::clicked, this, [this, cat, list]{
        if (!m_manager) return;
        bool ok = false;
        QString name = QInputDialog::getText(this, tr("New Patch"), tr("Name:"),
                                             QLineEdit::Normal, QString(), &ok);
        if (!ok || name.isEmpty()) return;
        m_manager->add(cat, name, {});
        refreshList(cat, list);
        list->setCurrentRow(list->count() - 1);
    });
    connect(delBtn, &QPushButton::clicked, this, [this, cat, list]{
        if (!m_manager) return;
        auto *item = list->currentItem();
        if (!item) return;
        QUuid id = item->data(Qt::UserRole).toUuid();
        m_manager->remove(id);
        refreshList(cat, list);
    });
    connect(renBtn, &QPushButton::clicked, this, [this, cat, list]{
        if (!m_manager) return;
        auto *item = list->currentItem();
        if (!item) return;
        QUuid id = item->data(Qt::UserRole).toUuid();
        bool ok = false;
        QString name = QInputDialog::getText(this, tr("Rename Patch"), tr("Name:"),
                                             QLineEdit::Normal, item->text(), &ok);
        if (!ok || name.isEmpty()) return;
        m_manager->rename(id, name);
        refreshList(cat, list);
    });

    if (m_manager) {
        connect(m_manager.data(), &core::PatchManager::patchesChanged,
                this, [this, cat, list](Category c){
            if (c == cat) refreshList(cat, list);
        });
    }

    return page;
}

void PatchEditorDialog::refreshList(Category cat, QListWidget *list) {
    if (!m_manager) return;
    const auto patches = m_manager->patchesIn(cat);

    // Idempotency guard — CRITICAL. Editing a patch field (e.g. choosing a
    // device in the form's combo) calls setFields(), which emits
    // patchesChanged(), which lands here. If we blindly clear()+rebuild the
    // list, that fires currentRowChanged() and tears down the form — deleting
    // the very combo whose currentIndexChanged() is still on the stack, a
    // use-after-free that crashed the whole app when picking an audio device
    // or video display. A field edit changes no list entry, so when the list
    // already matches (same ids + names, in order) we simply return and leave
    // the live form untouched.
    if (list->count() == patches.size()) {
        bool same = true;
        for (int i = 0; i < patches.size(); ++i) {
            auto *it = list->item(i);
            if (!it || it->data(Qt::UserRole).toUuid() != patches[i].id
                    || it->text() != patches[i].name) { same = false; break; }
        }
        if (same) return;
    }

    QUuid kept = list->currentItem() ? list->currentItem()->data(Qt::UserRole).toUuid() : QUuid();
    list->clear();
    for (const auto &p : patches) {
        auto *item = new QListWidgetItem(p.name, list);
        item->setData(Qt::UserRole, p.id);
    }
    if (!kept.isNull()) {
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->data(Qt::UserRole).toUuid() == kept) {
                list->setCurrentRow(i); return;
            }
    }
    if (list->count() > 0) list->setCurrentRow(0);
}

// ── Form (right pane) ─────────────────────────────────────────────────────────

QWidget *PatchEditorDialog::buildForm(Category cat, QListWidget *list) {
    auto *holder = new QWidget(this);
    auto *vl = new QVBoxLayout(holder);
    vl->setContentsMargins(0,0,0,0);

    auto *form = new QWidget(holder);
    auto *fl   = new QFormLayout(form);
    fl->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Per-category fields. We re-use one form for each tab and rebuild its
    // contents whenever the selected list row changes.
    auto rebuild = [this, cat, fl, form, list]() {
        // Clear existing rows
        while (fl->rowCount() > 0) fl->removeRow(0);
        if (!m_manager) return;
        auto *item = list->currentItem();
        if (!item) return;
        QUuid id = item->data(Qt::UserRole).toUuid();
        auto patch = m_manager->patch(id);

        auto pushFields = [this, id]() {
            // Find current widget values is awkward without tracking them;
            // handled per-field below via setFields() calls on each editor.
            Q_UNUSED(id);
        };
        Q_UNUSED(pushFields);

        switch (cat) {
        case Category::AudioOutput: {
            auto *combo = new QComboBox(form);
            combo->addItem(tr("(default)"), QByteArray());
            for (const auto &dev : QMediaDevices::audioOutputs())
                combo->addItem(dev.description(), QVariant::fromValue(dev.id()));
            QByteArray cur = patch.fields.value(QStringLiteral("deviceId")).toByteArray();
            for (int i = 0; i < combo->count(); ++i)
                if (combo->itemData(i).toByteArray() == cur) { combo->setCurrentIndex(i); break; }
            connect(combo, &QComboBox::currentIndexChanged, form, [this, id, combo](int) {
                auto fields = m_manager->patch(id).fields;
                fields[QStringLiteral("deviceId")] = combo->currentData().toByteArray();
                m_manager->setFields(id, fields);
            });
            fl->addRow(tr("Device"), combo);
            break;
        }
        case Category::OscDestination: {
            auto *host = new QLineEdit(patch.fields.value(QStringLiteral("host"),
                                                          QStringLiteral("127.0.0.1")).toString(), form);
            auto *port = new QSpinBox(form); port->setRange(1, 65535);
            port->setValue(patch.fields.value(QStringLiteral("port"), 53000).toInt());
            auto *transport = new QComboBox(form);
            transport->addItem(tr("UDP"));
            transport->addItem(tr("TCP / SLIP"));
            transport->addItem(tr("WebSocket"));
            transport->setCurrentIndex(patch.fields.value(QStringLiteral("transport"), 0).toInt());

            auto save = [this, id, host, port, transport]() {
                QVariantMap f;
                f[QStringLiteral("host")]      = host->text();
                f[QStringLiteral("port")]      = port->value();
                f[QStringLiteral("transport")] = transport->currentIndex();
                m_manager->setFields(id, f);
            };
            connect(host, &QLineEdit::editingFinished,    form, save);
            connect(port, &QSpinBox::valueChanged,        form, [save](int){ save(); });
            connect(transport, &QComboBox::currentIndexChanged, form, [save](int){ save(); });

            fl->addRow(tr("Host"),      host);
            fl->addRow(tr("Port"),      port);
            fl->addRow(tr("Transport"), transport);
            break;
        }
        case Category::MidiPort: {
            auto *portName = new QLineEdit(patch.fields.value(QStringLiteral("portName")).toString(), form);
            portName->setPlaceholderText(tr("Output port name (substring match)"));
            connect(portName, &QLineEdit::editingFinished, form, [this, id, portName]{
                auto fields = m_manager->patch(id).fields;
                fields[QStringLiteral("portName")] = portName->text();
                m_manager->setFields(id, fields);
            });
            fl->addRow(tr("Port name"), portName);
            break;
        }
        case Category::DmxUniverse: {
            auto *universe = new QSpinBox(form);
            universe->setRange(1, 32768);
            universe->setValue(patch.fields.value(QStringLiteral("universe"), 1).toInt());
            auto *adapter = new QComboBox(form);
            adapter->addItem(tr("sACN (E1.31, multicast)"), QStringLiteral("sacn"));
            adapter->addItem(tr("Art-Net (broadcast UDP)"), QStringLiteral("artnet"));
            adapter->addItem(tr("DMX-USB (Enttec Open DMX)"), QStringLiteral("dmxusb"));
            const QString curAdapter = patch.fields.value(QStringLiteral("adapter"),
                                                          QStringLiteral("sacn")).toString();
            for (int i = 0; i < adapter->count(); ++i)
                if (adapter->itemData(i).toString() == curAdapter) { adapter->setCurrentIndex(i); break; }

            auto save = [this, id, universe, adapter]() {
                QVariantMap f;
                f[QStringLiteral("universe")] = universe->value();
                f[QStringLiteral("adapter")]  = adapter->currentData().toString();
                m_manager->setFields(id, f);
            };
            connect(universe, &QSpinBox::valueChanged, form, [save](int){ save(); });
            connect(adapter,  &QComboBox::currentIndexChanged, form, [save](int){ save(); });

            fl->addRow(tr("Universe"), universe);
            fl->addRow(tr("Adapter"),  adapter);
            break;
        }
        case Category::VideoSurface: {
            auto *display = new QComboBox(form);
            const auto screens = QGuiApplication::screens();
            for (int i = 0; i < screens.size(); ++i)
                display->addItem(tr("%1: %2 (%3×%4)")
                                     .arg(i)
                                     .arg(screens[i]->name(),
                                          QString::number(screens[i]->geometry().width()),
                                          QString::number(screens[i]->geometry().height())),
                                 i);
            int curScreen = patch.fields.value(QStringLiteral("screen"), 0).toInt();
            for (int i = 0; i < display->count(); ++i)
                if (display->itemData(i).toInt() == curScreen) { display->setCurrentIndex(i); break; }
            connect(display, &QComboBox::currentIndexChanged, form, [this, id, display](int) {
                auto fields = m_manager->patch(id).fields;
                fields[QStringLiteral("screen")] = display->currentData().toInt();
                m_manager->setFields(id, fields);
            });
            fl->addRow(tr("Display"), display);
            break;
        }
        case Category::SpeakerArray: {
            // Rich editor lives in SpeakerPatchDialog (Stage 4 of the
            // object-audio rollout). The patch fields model is fixed:
            // a "templateKey" string + a "speakers" array. For now show
            // a read-only summary; double-clicking the patch in Stage 4
            // will open the dedicated dialog.
            const auto tmpl = patch.fields.value(QStringLiteral("templateKey"),
                                                  tr("custom")).toString();
            const auto count = patch.fields.value(QStringLiteral("speakers"))
                                .toJsonArray().size();
            auto *summary = new QLabel(
                tr("Template: %1 · %2 speakers").arg(tmpl).arg(count), form);
            summary->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::tokens().ink60.name()));
            fl->addRow(summary);
            auto *hint = new QLabel(
                tr("Use Tools → Speaker Patch… to edit positions."), form);
            hint->setWordWrap(true);
            fl->addRow(hint);
            break;
        }
        }
    };

    connect(list, &QListWidget::currentRowChanged, form, [rebuild](int){ rebuild(); });
    rebuild();

    vl->addWidget(form, 1);
    return holder;
}

} // namespace quewi::ui
