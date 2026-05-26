#include "ui/WelcomeDialog.h"

#include "ui/Theme.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace quewi::ui {

namespace {
constexpr const char *kRecentKey   = "ui/recentFiles";
constexpr const char *kShowWelcome = "ui/showWelcome";
constexpr int         kRecentCap   = 12;   // a few more than the menu shows
} // namespace

bool WelcomeDialog::showOnLaunchEnabled()
{
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    return s.value(QString::fromLatin1(kShowWelcome), true).toBool();
}

WelcomeDialog::WelcomeDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Welcome to quewi"));
    setModal(true);
    // Fixed size — this is a launchpad, not a workspace. Resizing
    // would invite scaling pain on the recent-files list and the
    // brand panel; the design assumes a single layout.
    resize(720, 460);
    setMinimumSize(720, 460);
    buildLayout();
    populateRecents();
}

WelcomeDialog::~WelcomeDialog() = default;

void WelcomeDialog::buildLayout()
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Left brand panel ─────────────────────────────────────────
    // Darker tone than the main app background — reads as a poster
    // rather than a working surface. Kiwi icon centred, wordmark
    // and version below it.
    // Brand panel uses inkOnAccent as its background — a slightly
    // deeper warm tone than bgDeep, deliberately differentiating the
    // poster-feel left half from the working surface on the right.
    // Pulled from theme so dark / high-contrast / light all stay in
    // sync.
    const auto &tk = Theme::tokens();
    auto *left = new QWidget(this);
    left->setObjectName(QStringLiteral("welcomeBrand"));
    left->setFixedWidth(280);
    left->setStyleSheet(QStringLiteral(
        "QWidget#welcomeBrand { background: %1; }"
        "QLabel#welcomeWord { color: %2; font-size: 32px;"
        "                     font-weight: 300; letter-spacing: 2px; }"
        "QLabel#welcomeVer  { color: %3; font-size: 12px; }"
        "QLabel#welcomeTag  { color: %3; font-size: 11px;"
        "                     font-style: italic; }")
        .arg(tk.inkOnAccent.name(),
             tk.ink100.name(),
             tk.ink40.name()));
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(28, 36, 28, 28);
    leftLayout->setSpacing(8);
    leftLayout->addStretch(2);
    auto *iconLabel = new QLabel(left);
    QIcon icon(QStringLiteral(":/icons/quewi.png"));
    if (!icon.isNull()) {
        iconLabel->setPixmap(icon.pixmap(160, 160));
    }
    iconLabel->setAlignment(Qt::AlignCenter);
    leftLayout->addWidget(iconLabel);
    leftLayout->addSpacing(12);
    auto *word = new QLabel(QStringLiteral("quewi"), left);
    word->setObjectName(QStringLiteral("welcomeWord"));
    word->setAlignment(Qt::AlignCenter);
    leftLayout->addWidget(word);
    // Pull version from QCoreApplication (set in main.cpp). The
    // QUEWI_VERSION compile define is only on the app target, not
    // quewi_ui, so reading the macro here would always show 0.0.0.
    auto *ver = new QLabel(tr("Version %1").arg(
                               QCoreApplication::applicationVersion()),
                           left);
    ver->setObjectName(QStringLiteral("welcomeVer"));
    ver->setAlignment(Qt::AlignCenter);
    leftLayout->addWidget(ver);
    leftLayout->addStretch(1);
    auto *tag = new QLabel(tr("Theatre cueing software"), left);
    tag->setObjectName(QStringLiteral("welcomeTag"));
    tag->setAlignment(Qt::AlignCenter);
    leftLayout->addWidget(tag);

    root->addWidget(left);

    // ── Right action panel ───────────────────────────────────────
    auto *right = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(28, 28, 28, 20);
    rightLayout->setSpacing(10);

    auto *newBtn = new QPushButton(tr("Create new show"), right);
    newBtn->setMinimumHeight(40);
    newBtn->setDefault(true);
    newBtn->setStyleSheet(QStringLiteral(
        "QPushButton { font-size: 14px; padding: 8px 18px; "
        "              border-radius: 5px;"
        "              background: %1; color: %2;"
        "              border: none; font-weight: 600; }"
        "QPushButton:hover  { background: %3; }"
        "QPushButton:pressed { background: %4; }")
        .arg(tk.warn.name(),
             tk.inkOnAccent.name(),
             tk.accentHover.name(),
             tk.accentSoft.name()));
    connect(newBtn, &QPushButton::clicked, this, &WelcomeDialog::onNewClicked);
    rightLayout->addWidget(newBtn);

    auto *openBtn = new QPushButton(tr("Open existing…"), right);
    openBtn->setMinimumHeight(36);
    openBtn->setStyleSheet(QStringLiteral(
        "QPushButton { font-size: 13px; padding: 6px 18px; "
        "              border-radius: 5px;"
        "              background: transparent; color: %1;"
        "              border: 1px solid %2; }"
        "QPushButton:hover  { border-color: %3; }"
        "QPushButton:pressed { background: %4; }")
        .arg(tk.ink100.name(),
             tk.outline.name(),
             tk.ink60.name(),
             tk.bgRow.name()));
    connect(openBtn, &QPushButton::clicked, this, &WelcomeDialog::onOpenClicked);
    rightLayout->addWidget(openBtn);

    rightLayout->addSpacing(14);

    auto *recentLabel = new QLabel(tr("Recent"), right);
    recentLabel->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 11px;"
        "font-weight: 600; letter-spacing: 1px;").arg(tk.ink40.name()));
    rightLayout->addWidget(recentLabel);

    m_recentList = new QListWidget(right);
    m_recentList->setFrameShape(QFrame::NoFrame);
    m_recentList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_recentList->setStyleSheet(QStringLiteral(
        "QListWidget { background: transparent; border: none; }"
        "QListWidget::item { padding: 8px 10px; border-radius: 4px; }"
        "QListWidget::item:hover    { background: %1; }"
        "QListWidget::item:selected { background: %2;"
        "                              color: %3; }")
        .arg(tk.bgRow.name(),
             tk.bgRowSelected.name(),
             tk.ink100.name()));
    connect(m_recentList, &QListWidget::itemActivated,
            this, &WelcomeDialog::onRecentActivated);
    connect(m_recentList, &QListWidget::itemDoubleClicked,
            this, &WelcomeDialog::onRecentActivated);
    rightLayout->addWidget(m_recentList, 1);

    auto *showOnLaunch = new QCheckBox(tr("Show this window on launch"), right);
    showOnLaunch->setChecked(showOnLaunchEnabled());
    connect(showOnLaunch, &QCheckBox::toggled, this, [](bool v) {
        QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
        s.setValue(QString::fromLatin1(kShowWelcome), v);
    });
    rightLayout->addWidget(showOnLaunch);

    root->addWidget(right, 1);
}

void WelcomeDialog::populateRecents()
{
    m_recentList->clear();
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    auto list = s.value(QString::fromLatin1(kRecentKey)).toStringList();
    // Filter out paths the user has since deleted, but persist the
    // pruned list so the next launch is even cleaner.
    QStringList live;
    for (const auto &p : list) {
        if (QFileInfo::exists(p)) live.append(p);
    }
    if (live != list) s.setValue(QString::fromLatin1(kRecentKey), live);
    while (live.size() > kRecentCap) live.removeLast();

    if (live.isEmpty()) {
        auto *empty = new QListWidgetItem(tr("No recent shows yet"),
                                          m_recentList);
        empty->setFlags(Qt::NoItemFlags);
        empty->setForeground(QColor(0x6F, 0x66, 0x5D));
        return;
    }
    for (const auto &p : live) {
        const QFileInfo fi(p);
        auto *item = new QListWidgetItem(m_recentList);
        // Two-line label: filename on top, parent path below in
        // muted text. UserRole carries the absolute path for the
        // activate handler.
        item->setText(fi.fileName() + QStringLiteral("\n")
                      + fi.absolutePath());
        item->setData(Qt::UserRole, fi.absoluteFilePath());
        item->setToolTip(fi.absoluteFilePath());
    }
}

void WelcomeDialog::onNewClicked()
{
    m_action = Action::NewShow;
    accept();
}

void WelcomeDialog::onOpenClicked()
{
    const QString path = QFileDialog::getOpenFileName(this,
        tr("Open show"), QString(),
        tr("Quewi shows (*.quewi);;All files (*)"));
    if (path.isEmpty()) return;
    m_action = Action::OpenExisting;
    m_chosenPath = path;
    accept();
}

void WelcomeDialog::onRecentActivated()
{
    auto *item = m_recentList->currentItem();
    if (!item) return;
    const QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty()) return;
    m_action = Action::OpenRecent;
    m_chosenPath = path;
    accept();
}

} // namespace quewi::ui
