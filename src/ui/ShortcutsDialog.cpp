#include "ui/ShortcutsDialog.h"

#include "ui/ShortcutManager.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace quewi::ui {

ShortcutsDialog::ShortcutsDialog(ShortcutManager *mgr, QWidget *parent)
    : QDialog(parent), m_mgr(mgr)
{
    setWindowTitle(tr("Keyboard shortcuts"));
    resize(620, 520);

    auto *root = new QVBoxLayout(this);
    auto *hint = new QLabel(tr("Click a shortcut cell, press the new key combination, "
                               "then click Apply. Empty clears the binding."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#A8AEBA; font-size:11px;"));
    root->addWidget(hint);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("Action"), tr("Default"), tr("Shortcut")});
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(m_table, 1);

    auto *btnRow = new QHBoxLayout();
    auto *resetBtn = new QPushButton(tr("Reset all to defaults"), this);
    btnRow->addWidget(resetBtn);
    btnRow->addStretch(1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    btnRow->addWidget(bb);
    root->addLayout(btnRow);

    connect(resetBtn, &QPushButton::clicked, this, &ShortcutsDialog::resetAll);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    if (m_mgr) connect(m_mgr, &ShortcutManager::bindingsChanged, this, &ShortcutsDialog::rebuild);

    rebuild();
}

ShortcutsDialog::~ShortcutsDialog() = default;

void ShortcutsDialog::rebuild()
{
    if (!m_mgr) return;
    const auto bindings = m_mgr->bindings();
    m_table->setRowCount(bindings.size());
    for (int row = 0; row < bindings.size(); ++row) {
        const auto &b = bindings[row];
        auto *labelItem = new QTableWidgetItem(b.label);
        labelItem->setFlags(labelItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 0, labelItem);

        auto *defaultItem = new QTableWidgetItem(b.defaultSeq.toString());
        defaultItem->setFlags(defaultItem->flags() & ~Qt::ItemIsEditable);
        defaultItem->setForeground(QColor(0xA8, 0xAE, 0xBA));
        m_table->setItem(row, 1, defaultItem);

        auto *editor = new QKeySequenceEdit(b.currentSeq, m_table);
        const QString id = b.id;
        connect(editor, &QKeySequenceEdit::editingFinished, this,
            [this, id, editor] {
                if (m_mgr) m_mgr->setBinding(id, editor->keySequence());
            });
        m_table->setCellWidget(row, 2, editor);
    }
    m_table->setColumnWidth(2, 200);
}

void ShortcutsDialog::resetAll()
{
    if (m_mgr) m_mgr->resetAll();
}

} // namespace quewi::ui
