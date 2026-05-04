#include "ui/PreferencesDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace quewi::ui {

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    resize(640, 480);

    auto *root = new QVBoxLayout(this);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    auto *categories = new QListWidget(splitter);
    auto *pages = new QStackedWidget(splitter);

    const QStringList items = {
        tr("General"), tr("Audio"), tr("OSC"), tr("MIDI"),
        tr("Lighting"), tr("Theme"), tr("Show Mode")
    };
    for (const auto &name : items) {
        categories->addItem(name);
        auto *placeholder = new QLabel(
            tr("%1 settings will appear here as the subsystem lands.").arg(name));
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setWordWrap(true);
        pages->addWidget(placeholder);
    }
    categories->setCurrentRow(0);

    splitter->addWidget(categories);
    splitter->addWidget(pages);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({160, 480});
    root->addWidget(splitter, 1);

    connect(categories, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(bb);
}

PreferencesDialog::~PreferencesDialog() = default;

} // namespace quewi::ui
