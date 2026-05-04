#include "ui/CommandPalette.h"

#include <QAction>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QVBoxLayout>

namespace quewi::ui {

CommandPalette::CommandPalette(QMenuBar *menuBar, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Command palette"));
    setModal(true);
    setWindowFlag(Qt::FramelessWindowHint, true);
    resize(520, 360);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("Type to search actions…"));
    m_input->setStyleSheet(QStringLiteral("font-size:14px; padding:8px;"));
    root->addWidget(m_input);

    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(true);
    root->addWidget(m_list, 1);

    collect(menuBar);
    filter({});

    connect(m_input, &QLineEdit::textChanged, this, &CommandPalette::filter);
    connect(m_list, &QListWidget::itemActivated, this, &CommandPalette::run);
    connect(m_list, &QListWidget::itemClicked, this, &CommandPalette::run);

    m_input->installEventFilter(this);
    m_input->setFocus();
}

CommandPalette::~CommandPalette() = default;

void CommandPalette::collect(QMenuBar *bar)
{
    if (!bar) return;
    std::function<void(QMenu *, const QString &)> walk;
    walk = [&](QMenu *menu, const QString &prefix) {
        if (!menu) return;
        for (QAction *a : menu->actions()) {
            if (a->isSeparator()) continue;
            const QString text = a->text().remove(QChar('&'));
            const QString label = prefix.isEmpty()
                ? text : QStringLiteral("%1 → %2").arg(prefix, text);
            if (a->menu()) {
                walk(a->menu(), label);
            } else if (a->isEnabled() || true) { // include disabled so users see them
                m_entries.append({a, label});
            }
        }
    };
    for (QAction *a : bar->actions()) {
        if (a->menu()) walk(a->menu(), a->text().remove(QChar('&')));
    }
}

void CommandPalette::filter(const QString &query)
{
    m_list->clear();
    const auto q = query.trimmed().toLower();
    for (const auto &e : m_entries) {
        if (q.isEmpty() || e.label.toLower().contains(q)) {
            auto *item = new QListWidgetItem(e.label);
            item->setData(Qt::UserRole, QVariant::fromValue(static_cast<void *>(e.action)));
            if (!e.action->isEnabled()) item->setForeground(QColor(0x4A, 0x4F, 0x5A));
            const auto sc = e.action->shortcut().toString();
            if (!sc.isEmpty()) {
                item->setText(QStringLiteral("%1     %2").arg(e.label, sc));
            }
            m_list->addItem(item);
        }
    }
    if (m_list->count() > 0) m_list->setCurrentRow(0);
}

void CommandPalette::run(QListWidgetItem *item)
{
    if (!item) return;
    auto *a = static_cast<QAction *>(item->data(Qt::UserRole).value<void *>());
    if (!a || !a->isEnabled()) return;
    accept();
    a->trigger();
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        switch (ke->key()) {
        case Qt::Key_Down:
            if (m_list->count() > 0) {
                const int next = std::min(m_list->currentRow() + 1, m_list->count() - 1);
                m_list->setCurrentRow(next);
            }
            return true;
        case Qt::Key_Up:
            if (m_list->count() > 0) {
                const int prev = std::max(m_list->currentRow() - 1, 0);
                m_list->setCurrentRow(prev);
            }
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (auto *cur = m_list->currentItem()) run(cur);
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

} // namespace quewi::ui
