#include "ui/MediaImportDialog.h"

#include "ui/Theme.h"

#include <QAudioOutput>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>

namespace quewi::ui {

namespace {
QString fmtDuration(qint64 sec)
{
    if (sec <= 0) return QStringLiteral("—");
    const qint64 h = sec / 3600, m = (sec % 3600) / 60, s = sec % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h)
            .arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}
} // namespace

bool MediaImportDialog::confirmDisclaimer(QWidget *parent)
{
    QSettings s(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
    if (s.value(QStringLiteral("mediaImport/disclaimerAccepted"), false).toBool())
        return true;

    QMessageBox box(parent);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(QObject::tr("Import from URL"));
    box.setText(QObject::tr("Download media for your show"));
    box.setInformativeText(QObject::tr(
        "This tool downloads audio and video from YouTube and many other "
        "sites using yt-dlp. You are responsible for ensuring you have the "
        "rights to download and use any material — downloading copyrighted "
        "content without permission, or in violation of a site's terms of "
        "service, may be unlawful in your jurisdiction.\n\n"
        "Use it only for material you are licensed to perform with."));
    auto *agree = box.addButton(QObject::tr("I understand"), QMessageBox::AcceptRole);
    box.addButton(QObject::tr("Cancel"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == agree) {
        s.setValue(QStringLiteral("mediaImport/disclaimerAccepted"), true);
        return true;
    }
    return false;
}

MediaImportDialog::MediaImportDialog(const QString &destDir, QWidget *parent)
    : QDialog(parent)
    , m_svc(new MediaImportService(this))
    , m_destDir(destDir)
    , m_thumbNam(new QNetworkAccessManager(this))
{
    setWindowTitle(tr("Import from URL"));
    setModal(true);
    resize(680, 560);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    // ── Search row ────────────────────────────────────────────────
    auto *searchRow = new QHBoxLayout();
    searchRow->setSpacing(8);
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(
        tr("Search YouTube, or paste a video / playlist URL…"));
    m_searchBtn = new QPushButton(tr("Search"), this);
    m_searchBtn->setDefault(true);
    searchRow->addWidget(m_searchEdit, 1);
    searchRow->addWidget(m_searchBtn);
    root->addLayout(searchRow);

    // ── Results ───────────────────────────────────────────────────
    m_resultsList = new QListWidget(this);
    m_resultsList->setIconSize(QSize(120, 68));
    m_resultsList->setUniformItemSizes(false);
    m_resultsList->setAlternatingRowColors(false);
    root->addWidget(m_resultsList, 1);

    // ── Mode + actions ────────────────────────────────────────────
    auto *modeRow = new QHBoxLayout();
    modeRow->setSpacing(10);
    m_audioRadio = new QRadioButton(tr("Audio"), this);
    m_videoRadio = new QRadioButton(tr("Video"), this);
    m_audioRadio->setChecked(true);
    modeRow->addWidget(m_audioRadio);
    modeRow->addWidget(m_videoRadio);
    modeRow->addStretch(1);
    m_previewBtn  = new QPushButton(tr("Preview"), this);
    m_downloadBtn = new QPushButton(tr("Download + add cue"), this);
    m_previewBtn->setEnabled(false);
    m_downloadBtn->setEnabled(false);
    modeRow->addWidget(m_previewBtn);
    modeRow->addWidget(m_downloadBtn);
    root->addLayout(modeRow);

    // ── Progress + status ─────────────────────────────────────────
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setVisible(false);
    root->addWidget(m_progress);

    auto *bottomRow = new QHBoxLayout();
    m_status = new QLabel(this);
    m_status->setStyleSheet(QStringLiteral("color:%1; font-size:11px;")
                                .arg(Theme::tokens().ink60.name()));
    m_updateBtn = new QPushButton(tr("Update downloader"), this);
    m_updateBtn->setFlat(true);
    m_updateBtn->setStyleSheet(QStringLiteral("color:%1; font-size:11px;")
                                   .arg(Theme::tokens().ink40.name()));
    bottomRow->addWidget(m_status, 1);
    bottomRow->addWidget(m_updateBtn);
    root->addLayout(bottomRow);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(bb);

    // ── Wiring ────────────────────────────────────────────────────
    connect(m_searchBtn, &QPushButton::clicked, this, &MediaImportDialog::onSearchClicked);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &MediaImportDialog::onSearchClicked);
    connect(m_resultsList, &QListWidget::itemSelectionChanged,
            this, &MediaImportDialog::onResultSelectionChanged);
    connect(m_previewBtn, &QPushButton::clicked, this, &MediaImportDialog::onPreviewClicked);
    connect(m_downloadBtn, &QPushButton::clicked, this, &MediaImportDialog::onDownloadClicked);
    connect(m_audioRadio, &QRadioButton::toggled, this,
            [this](bool on) { if (on) m_audioMode = true; });
    connect(m_videoRadio, &QRadioButton::toggled, this,
            [this](bool on) { if (on) m_audioMode = false; });
    connect(m_updateBtn, &QPushButton::clicked, this, [this] {
        setBusy(tr("Updating yt-dlp…"), true);
        m_svc->updateTool();
    });

    connect(m_svc, &MediaImportService::toolReady, this, [this] {
        setBusy(QString(), false);
        m_status->setText(tr("Ready."));
        m_searchBtn->setEnabled(true);
    });
    connect(m_svc, &MediaImportService::toolError, this, [this](const QString &msg) {
        setBusy(QString(), false);
        m_status->setText(msg);
        QMessageBox::warning(this, tr("Downloader setup failed"), msg);
    });
    connect(m_svc, &MediaImportService::toolDownloadProgress, this,
        [this](qint64 rec, qint64 tot) {
            if (tot > 0) {
                m_progress->setVisible(true);
                m_progress->setValue(int(100 * rec / tot));
            }
        });
    connect(m_svc, &MediaImportService::searchResults, this,
        [this](const QList<MediaResult> &results) {
            setBusy(QString(), false);
            m_results = results;
            m_resultsList->clear();
            for (int i = 0; i < results.size(); ++i) {
                const auto &r = results[i];
                auto *item = new QListWidgetItem(m_resultsList);
                item->setText(QStringLiteral("%1\n%2  ·  %3")
                    .arg(r.title,
                         r.uploader.isEmpty() ? tr("Unknown") : r.uploader,
                         fmtDuration(r.durationSec)));
                m_resultsList->addItem(item);
                if (!r.thumbnailUrl.isEmpty()) loadThumbnail(i, r.thumbnailUrl);
            }
            m_status->setText(tr("%1 result%2").arg(results.size())
                .arg(results.size() == 1 ? QString() : QStringLiteral("s")));
        });
    connect(m_svc, &MediaImportService::searchFailed, this, [this](const QString &msg) {
        setBusy(QString(), false);
        m_status->setText(tr("Search failed: %1").arg(msg));
    });
    connect(m_svc, &MediaImportService::streamUrlReady, this, [this](const QString &url) {
        setBusy(QString(), false);
        if (!m_player) {
            m_player = new QMediaPlayer(this);
            m_audioOut = new QAudioOutput(this);
            m_player->setAudioOutput(m_audioOut);
        }
        m_player->setSource(QUrl(url));
        m_player->play();
        m_status->setText(tr("Previewing… (press Preview again to stop)"));
    });
    connect(m_svc, &MediaImportService::streamUrlFailed, this, [this](const QString &msg) {
        setBusy(QString(), false);
        m_status->setText(tr("Preview unavailable: %1").arg(msg));
    });
    connect(m_svc, &MediaImportService::downloadProgress, this, [this](int pct) {
        m_progress->setVisible(true);
        m_progress->setValue(pct);
        m_status->setText(tr("Downloading… %1%").arg(pct));
    });
    connect(m_svc, &MediaImportService::downloadFinished, this, [this](const QString &path) {
        m_progress->setVisible(false);
        m_importedPath = path;
        accept();   // host creates the cue
    });
    connect(m_svc, &MediaImportService::downloadFailed, this, [this](const QString &msg) {
        m_progress->setVisible(false);
        m_downloadBtn->setEnabled(true);
        m_status->setText(tr("Download failed: %1").arg(msg));
        QMessageBox::warning(this, tr("Download failed"), msg);
    });

    // Kick off tool setup.
    if (m_svc->isToolReady()) {
        m_status->setText(tr("Ready."));
    } else {
        setBusy(tr("Setting up the downloader (first run)…"), true);
        m_searchBtn->setEnabled(false);
        m_svc->ensureTool();
    }
}

MediaImportDialog::~MediaImportDialog()
{
    if (m_player) m_player->stop();
}

void MediaImportDialog::setBusy(const QString &what, bool busy)
{
    if (busy) {
        m_status->setText(what);
        m_progress->setVisible(true);
        m_progress->setRange(0, 0);   // indeterminate
    } else {
        m_progress->setRange(0, 100);
        if (m_progress->value() <= 0) m_progress->setVisible(false);
    }
}

void MediaImportDialog::loadThumbnail(int row, const QString &url)
{
    QNetworkRequest req((QUrl(url)));
    auto *reply = m_thumbNam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, row] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        QPixmap pm;
        if (pm.loadFromData(reply->readAll()) && row < m_resultsList->count()) {
            if (auto *item = m_resultsList->item(row))
                item->setIcon(QIcon(pm.scaled(120, 68, Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation)));
        }
    });
}

void MediaImportDialog::onSearchClicked()
{
    if (!m_svc->isToolReady()) {
        m_status->setText(tr("Still setting up the downloader…"));
        return;
    }
    setBusy(tr("Searching…"), true);
    m_resultsList->clear();
    m_results.clear();
    m_previewBtn->setEnabled(false);
    m_downloadBtn->setEnabled(false);
    m_svc->search(m_searchEdit->text());
}

void MediaImportDialog::onResultSelectionChanged()
{
    const bool has = m_resultsList->currentRow() >= 0;
    m_previewBtn->setEnabled(has);
    m_downloadBtn->setEnabled(has);
}

void MediaImportDialog::onPreviewClicked()
{
    const int row = m_resultsList->currentRow();
    if (row < 0 || row >= m_results.size()) return;

    // Toggle: if already previewing, stop.
    if (m_player && m_player->playbackState() == QMediaPlayer::PlayingState) {
        m_player->stop();
        m_status->setText(tr("Preview stopped."));
        return;
    }

    if (m_audioMode) {
        setBusy(tr("Resolving preview…"), true);
        m_svc->resolveStreamUrl(m_results[row].url, /*audioOnly=*/true);
    } else {
        // Video preview opens the source page in the browser to avoid
        // bundling a heavy embedded web view (per the design).
        QDesktopServices::openUrl(QUrl(m_results[row].url));
    }
}

void MediaImportDialog::onDownloadClicked()
{
    const int row = m_resultsList->currentRow();
    if (row < 0 || row >= m_results.size()) return;
    if (m_player) m_player->stop();
    m_downloadBtn->setEnabled(false);
    m_progress->setVisible(true);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_svc->download(m_results[row].url, m_audioMode, m_destDir);
}

} // namespace quewi::ui
