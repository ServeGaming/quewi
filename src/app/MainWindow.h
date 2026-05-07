#pragma once

#include <QMainWindow>
#include <QString>
#include <memory>

class QAction;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QTabBar;
class QTimer;
class QUrl;

namespace quewi::core { class Workspace; class CueListModel; }
namespace quewi::cues { class Cue; }
namespace quewi::ui   { class ActiveCuesPanel; class CartView; class CueListView; class Inspector; class ShortcutManager; class TransportBar; class OscMonitor; class ScriptWindow; }
namespace quewi::osc  { class OscEngine; }
namespace quewi::audio { class AudioEngine; }
namespace quewi::lighting { class LightingEngine; }
namespace quewi::video { class VideoEngine; }
namespace quewi::midi  { class MidiEngine; class MidiInputEngine; }

namespace quewi {

class GoEngine;
class UpdateChecker;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void newShow();
    void openShow();
    bool saveShow();
    bool saveShowAs();
    void showPreferences();
    void showOscMonitor();
    void showScriptWindow();
    void insertMemoCue();
    void insertOscCue();
    void insertAudioCue();
    void insertFadeCue();
    void insertLightCue();
    void insertLightFadeCue();
    void insertVideoCue();
    void insertImageCue();
    void insertTextCue();
    void insertWaitCue();
    void insertStartCue();
    void insertStopCue();
    void insertGotoCue();
    void insertPauseCue();
    void insertLoadCue();
    void insertResetCue();
    void insertDevampCue();
    void insertGroupCue();
    void insertMidiCue();
    void insertMscCue();
    void deleteSelectedCue();
    void onSelectionChanged();
    void updateTitle();
    void onGoRequested();
    void showPreflight();
    void showCommandPalette();
    void toggleShowMode();
    void addCueListTab();
    void renameCueListTab();
    void removeCueListTab();
    void onTabSelected(int index);
    void showShortcutsDialog();
    void showPatchEditor();
    void showSpeakerPatch();
    void showProjectionMapping();
    void showAbout();
    void showNotifications();
    void openRecent(const QString &path);
    void rebuildRecentMenu();
    void onMidiTrigger(quint8 status, const QByteArray &bytes);
    void onCartFileDropped(int row, int col, const QString &path);

public:
    bool loadShowFromPath(const QString &path);
    // Manual = launched from File → Check for updates… (Verbose mode, so
    // "you're up to date" is also confirmed). Startup pass calls this
    // with manual=false so a flat line stays silent.
    void checkForUpdates(bool manual);
    void runInAppInstall(const QString &msiUrl);

private:
    void buildLayout();
    void buildMenus();
    void resetWorkspace();
    void rebindModel();
    bool maybeSaveChanges();
    bool saveTo(const QString &path);
    std::unique_ptr<cues::Cue> cueFromFile(const QString &path);
    int  insertCuesFromUrls(const QList<QUrl> &urls, int startRow = -1);

    void registerOscRemoteHandlers();
    void selectCueByNumber(double number);
    void fireCueByNumber(double number);

    std::unique_ptr<core::Workspace>    m_workspace;
    std::unique_ptr<core::CueListModel> m_model;
    std::unique_ptr<osc::OscEngine>     m_oscEngine;
    std::unique_ptr<audio::AudioEngine> m_audioEngine;
    std::unique_ptr<lighting::LightingEngine> m_lightingEngine;
    std::unique_ptr<video::VideoEngine>       m_videoEngine;
    std::unique_ptr<midi::MidiEngine>         m_midiEngine;
    std::unique_ptr<midi::MidiInputEngine>    m_midiInput;
    std::unique_ptr<GoEngine>                 m_goEngine;

    ui::CueListView *m_cueListView = nullptr;
    ui::CartView    *m_cartView    = nullptr;
    QStackedWidget  *m_centerStack = nullptr;
    ui::Inspector   *m_inspector   = nullptr;
    ui::TransportBar *m_transport  = nullptr;
    ui::ActiveCuesPanel *m_activePanel = nullptr;
    ui::OscMonitor   *m_oscMonitor = nullptr;
    ui::ScriptWindow *m_scriptWindow = nullptr;
    UpdateChecker    *m_updateChecker = nullptr;
    QSplitter       *m_mainSplitter = nullptr;

    QAction *m_actUndo = nullptr;
    QAction *m_actRedo = nullptr;
    QAction *m_actSave = nullptr;
    QAction *m_actShowMode = nullptr;

    // Recent-files menu — re-built whenever the MRU list mutates
    // (open/save) so missing files get pruned from the visible list.
    QMenu *m_recentMenu = nullptr;
    void noteRecentFile(const QString &path);

    // Notification badge in the status bar — a clickable QLabel that
    // reads "N alerts" and opens the inbox. Updates on every post().
    QPushButton *m_notifBadge = nullptr;
    int          m_unreadNotifs = 0;
    void refreshNotifBadge();

    // Transport actions — exposed as QActions so the shortcut manager
    // can rebind them. Triggering them runs the same code as the buttons.
    QAction *m_actGo       = nullptr;
    QAction *m_actPause    = nullptr;
    QAction *m_actFadeAll  = nullptr;
    QAction *m_actPanic    = nullptr;

    ui::ShortcutManager *m_shortcuts = nullptr;

    QTabBar *m_listTabs = nullptr;
    QWidget *m_showModeStrip = nullptr;
    bool     m_showMode = false;

    QString m_currentPath;
    QString m_journalPath;
    QTimer *m_journalTimer = nullptr;

    void rebuildListTabs();
    void applyShowMode();
    void scheduleJournal();
    void writeJournal();
    void clearJournal();
    void recoverFromJournalIfPresent();
};

} // namespace quewi
