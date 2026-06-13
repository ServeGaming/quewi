#pragma once

#include "ui/MediaImportService.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QNetworkAccessManager;
class QMediaPlayer;
class QAudioOutput;

namespace quewi::ui {

// "Import from URL" — search YouTube and ~1800 other sites via yt-dlp,
// preview audio in-app, and download a clip straight into the show's
// media folder as a ready-to-use Audio or Video cue.
//
// On accept, importedPath()/importedIsAudio() carry the result; the
// host (MainWindow) creates the matching cue.
class MediaImportDialog : public QDialog {
    Q_OBJECT
public:
    explicit MediaImportDialog(const QString &destDir, QWidget *parent = nullptr);
    ~MediaImportDialog() override;

    QString importedPath()    const { return m_importedPath; }
    bool    importedIsAudio() const { return m_audioMode; }

    // One-time legal disclaimer gate. Returns true if the user has
    // accepted (or accepts now); false if they declined. Shown before
    // the dialog opens.
    static bool confirmDisclaimer(QWidget *parent);

private slots:
    void onSearchClicked();
    void onResultSelectionChanged();
    void onPreviewClicked();
    void onDownloadClicked();

private:
    void setBusy(const QString &what, bool busy);
    void loadThumbnail(int row, const QString &url);

    MediaImportService    *m_svc = nullptr;
    QString                m_destDir;
    QList<MediaResult>     m_results;
    QString                m_importedPath;
    bool                   m_audioMode = true;

    QLineEdit     *m_searchEdit  = nullptr;
    QPushButton   *m_searchBtn   = nullptr;
    QListWidget   *m_resultsList = nullptr;
    QRadioButton  *m_audioRadio  = nullptr;
    QRadioButton  *m_videoRadio  = nullptr;
    QPushButton   *m_previewBtn  = nullptr;
    QPushButton   *m_downloadBtn = nullptr;
    QProgressBar  *m_progress    = nullptr;
    QLabel        *m_status      = nullptr;
    QPushButton   *m_updateBtn   = nullptr;

    QNetworkAccessManager *m_thumbNam = nullptr;
    QMediaPlayer          *m_player   = nullptr;
    QAudioOutput          *m_audioOut = nullptr;
};

} // namespace quewi::ui
