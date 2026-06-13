#pragma once

#include "osc/OscMessage.h"

#include <QMainWindow>
#include <QString>
#include <memory>
#include <vector>

class QAction;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QDockWidget;
class QSplitter;
class QStackedWidget;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QTabBar;
class QTimer;
class QUrl;

namespace quewi::core { class Workspace; class CueList; class CueListModel; }
namespace quewi::cues { class Cue; }
namespace quewi::ui   { class ActiveCuesPanel; class CartView; class CueListView; class Inspector; class ShortcutManager; class TransportBar; class OscMonitor; class ScriptWindow; }
namespace quewi::osc  { class OscEngine; }
namespace quewi::audio { class AudioEngine; class AudioCue; }
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
    void renumberSelection();
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
    void showMediaImport();
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
    void applyTheme(const QString &name);
    void closeShow();
    void revealShowInFolder();
    void resetWorkspace();
    void rebindModel();
    bool maybeSaveChanges();
    bool saveTo(const QString &path);
    std::unique_ptr<cues::Cue> cueFromFile(const QString &path);
    int  insertCuesFromUrls(const QList<QUrl> &urls, int startRow = -1);
    // Shared body for every New-<type> menu action: inserts `cue`
    // after the current selection, names + numbers it, pushes the
    // undo command, and selects the new row. Each insertXCue() slot
    // is a one-liner over this.
    void insertCueOfType(std::unique_ptr<cues::Cue> cue, const QString &name);
    // Walks every cue list and kicks off prepare() on each AudioCue so
    // QAudioDecoder runs in the background. Without this the first GO
    // after opening a show is the one that starts decoding, which
    // surfaces as "audio still decoding" until the decoder finishes.
    void prewarmAudioCues();
    // Returns total bytes pre-decoded across every AudioCue in the
    // current workspace. Walks all cue lists; cheap (~hundreds of cues
    // takes microseconds). Used by the status-bar mem readout and the
    // pre-flight check.
    qint64 currentAudioMemoryBytes() const;
    // The user-configured cap from Preferences → Audio → Memory budget,
    // in bytes. Default 512 MB.
    qint64 audioMemoryBudgetBytes() const;
    // Refresh the "Audio: X / Y MB" status-bar label. Polled at 0.5 Hz
    // by m_memTimer.
    void   refreshMemReadout();
    // Help → Report a bug. Opens the user's browser at a pre-filled
    // GitHub Issues "new" page with version, OS, Qt, and a template
    // body. We don't post via the API — keeps the app credential-free.
    void reportBug();

    // Serialize a cue to the self-contained JSON record the OSC API
    // hands out (toPayload() plus the common id/number/name/type/
    // wait/notes/armed fields). One definition so the query reply and
    // the change-notification push can't drift apart.
    static QString cueToJsonString(const cues::Cue *c);

    // Find the AudioCue in `list` that currently owns voice `id`, or
    // nullptr. Centralises the voice→cue lookup duplicated across the
    // OSC playback handlers.
    static audio::AudioCue *audioCueForVoice(core::CueList *list,
                                             quint64 voiceId);

    void registerOscRemoteHandlers();
    // Wire workspace + cue-list + cue signals into OSC push
    // notifications. Reattaches after every resetWorkspace.
    void wireOscNotifications();
    // Push an OSC notification to every subscribed peer whose pattern
    // matches the address. No-op when there are no subscribers.
    void pushOscNotify(const QString &address,
                       std::vector<quewi::osc::Argument> args = {});

    // Peers that asked to receive notifications. Keyed by host:port so
    // a single peer subscribing twice doesn't get duplicates. Pattern
    // is the OSC pattern they want to match (default "/quewi/notify/*").
    struct OscSubscriberRec {
        QString host;
        quint16 port = 0;
        QString pattern;
    };
    std::vector<OscSubscriberRec> m_oscSubscribers;
    // Per-cue and per-list signal connections we own so that
    // resetWorkspace can disconnect them cleanly before rebinding.
    QList<QMetaObject::Connection> m_oscNotifyConnections;
    // 4 Hz heartbeat that pushes /quewi/notify/cue/playback to OSC
    // subscribers while any audio voice is alive. Lazy: only ticks
    // when m_oscSubscribers is non-empty AND there's something to
    // report; otherwise stays idle so quewi sits at 0 % CPU when
    // no remote is watching. Started/stopped in maybeStartPlaybackPush.
    QTimer *m_oscPlaybackTimer = nullptr;
    void   maybeStartPlaybackPush();
    void   pushPlaybackHeartbeat();
    // Single source of truth for "the cue list every OSC handler
    // should operate on." Prefers the list the GUI's QTreeView is
    // currently rendering (m_model->cueList()); falls back to the
    // workspace's active list pointer if the model hasn't been
    // bound yet. Returns nullptr only when there's no workspace.
    // Fixes the HeliOSC-reported bug where /quewi/query/cues
    // returned [] while /quewi/cue/add and /quewi/go saw cues —
    // those code paths were using m_workspace->activeCueList()
    // directly, which can drift out of sync with the model.
    core::CueList *activeOscList() const;
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
    // Inspector lives in a tearable dock so the user can move it
    // onto a second monitor. Persisted via QMainWindow::saveState().
    QDockWidget      *m_inspectorDock = nullptr;
    void resetLayout();

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

    QLabel  *m_memLabel = nullptr;
    QTimer  *m_memTimer = nullptr;
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
