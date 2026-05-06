#include "ui/NotificationsDialog.h"

#include "ui/Notifications.h"

#include <QDialogButtonBox>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

namespace quewi::ui {

namespace {
QString levelTag(Notifications::Level l) {
    switch (l) {
    case Notifications::Level::Info:  return QStringLiteral("INFO");
    case Notifications::Level::Warn:  return QStringLiteral("WARN");
    case Notifications::Level::Error: return QStringLiteral("ERR ");
    }
    return {};
}
QColor levelColor(Notifications::Level l) {
    switch (l) {
    case Notifications::Level::Info:  return QColor(0xB5, 0xAC, 0x9C);
    case Notifications::Level::Warn:  return QColor(0xD7, 0xA2, 0x4E);
    case Notifications::Level::Error: return QColor(0xC2, 0x6A, 0x55);
    }
    return Qt::white;
}
} // namespace

NotificationsDialog::NotificationsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Notifications"));
    resize(640, 420);

    auto *root = new QVBoxLayout(this);
    m_list = new QListWidget(this);
    m_list->setStyleSheet(QStringLiteral(
        "QListWidget { font-family: 'Space Grotesk', monospace; font-size: 12px; }"));
    root->addWidget(m_list, 1);

    auto *buttons = new QDialogButtonBox(this);
    m_clear = buttons->addButton(tr("Clear"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_clear, &QPushButton::clicked, this, &NotificationsDialog::onClearClicked);
    root->addWidget(buttons);

    rebuild();
    connect(&Notifications::instance(), &Notifications::posted,
            this, [this](const Notifications::Entry &) { rebuild(); });
    connect(&Notifications::instance(), &Notifications::cleared,
            this, &NotificationsDialog::rebuild);
}

void NotificationsDialog::rebuild()
{
    m_list->clear();
    const auto entries = Notifications::instance().recent(0);
    // Newest first.
    for (int i = entries.size() - 1; i >= 0; --i) {
        const auto &e = entries[i];
        const auto line = QStringLiteral("%1  %2  [%3]  %4")
            .arg(e.when.toString(QStringLiteral("HH:mm:ss")),
                 levelTag(e.level),
                 e.source,
                 e.message);
        auto *item = new QListWidgetItem(line, m_list);
        item->setForeground(levelColor(e.level));
    }
    m_clear->setEnabled(!entries.isEmpty());
}

void NotificationsDialog::onClearClicked()
{
    Notifications::instance().clear();
}

} // namespace quewi::ui
