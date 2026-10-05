#pragma once

#include "audio/DeskRecording.h"
#include "audio/LightTrigger.h"
#include "osc/OscMessage.h"

#include <QJsonObject>
#include <QMainWindow>
#include <QPointer>
#include "core/LightingDesk.h"
#include "ui/ShowSnapshot.h"
#include <QSet>
#include <QString>
#include <QUuid>
#include <memory>
#include <vector>

class QAction;
class QHBoxLayout;
class QLabel;
class QLockFile;
class QPushButton;
class QDockWidget;
class QSplitter;
class QStackedWidget;
class QMenu;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QTabBar;
class QTimer;
class QUrl;

namespace quewi::core { class Workspace; class CueList; class CueListModel; }
namespace quewi::cues { class Cue; }
namespace quewi::ui   { class DeskTakeRecorder; class AudioEditorWindow; class VideoEditorWindow; class ActiveCuesPanel; class CartView; class MixView; class CueListView; class Inspector; class ShortcutManager; class TransportBar; class OscMonitor; class ScriptWindow; class ShowModeView; class LightingPanel; class MatrixView; }
namespace quewi::osc  { class OscEngine; class EosFeedback; class EosCueLists; }
namespace quewi::audio { class AudioEngine; class AudioCue; }
namespace quewi::lighting { class LightingEngine; }
namespace quewi::video { class VideoEngine; class VideoCue; }
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
    // Double-click on the Inspector's divider → fit it to its content.
    bool event(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void newShow();
    void openShow();
    bool saveShow();
    bool saveShowAs();
    void showPreferences() { showPreferencesPage({}); }
    // Preferences opened on a page ("Lighting" for the lighting desk).
    void showPreferencesPage(const QString &page);
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
    void toggleArmSelectedCue();
    void renumberSelection();
    void onSelectionChanged();
    void updateTitle();
    void onGoRequested();
    // GO from this computer (not OSC): checks the safe key first.
    void requestLocalGo();
    void showPreflight();
    void showCommandPalette();
    // The command menu. keys=false: the search view (Ctrl+K); keys=true: the
    // key menu (leader tap), with `typed` letters applied — a leader chord.
    void showCommandMenu(bool keys, const QString &typed = QString());
    void toggleShowMode();
    void addCueListTab();
    void addSoundboardTab();
    void addMixListTab();
    void addMatrixListTab();
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
    // Right-click on empty space in the cue list — pop a menu to create a
    // new cue (any type), paste, import from URL, or open preferences.
    void showCueListContextMenu(const QPoint &globalPos);
    void openRecent(const QString &path);
    void rebuildRecentMenu();
    void onMidiTrigger(quint8 status, const QByteArray &bytes);
    // Video cue ↔ audio cue, undoably, keeping the cue's identity.
    void convertCue(cues::Cue *cue);
    // A soundboard pad fired: board output + "send to mic" routing.
    void fireSoundboardCue(cues::Cue *c);
    // Start / stop / retarget the live mic passthrough from MicRouting.
    void applyMicPassthrough();
    // Returns the cue now on the pad (nullptr if the file couldn't be added).
    cues::Cue *onCartFileDropped(int row, int col, const QString &path);
    // Right-click an empty pad → Import from URL…: the Ctrl+U importer in
    // soundboard mode, downloading onto that pad.
    void importToPad(int row, int col);

public:
    bool loadShowFromPath(const QString &path);
    // --screenshot-tour <dir>: a demo show, a picture of each main screen,
    // and fit.txt (text that doesn't fit), then quit. For looking at the
    // macOS / Linux builds from CI. See MainWindowTour.cpp.
    void runScreenshotTour(const QString &dir);
    // Offer to recover unsaved work left by a crash. main() calls this once,
    // before the Welcome dialog. Returns true if a show was recovered.
    bool recoverFromJournalIfPresent();
    // Manual = launched from File → Check for updates… (Verbose mode, so
    // "you're up to date" is also confirmed). Startup pass calls this
    // with manual=false so a flat line stays silent.
    void checkForUpdates(bool manual);
    void runInAppInstall(const QString &msiUrl);
    // Startup pop-ups (silent update check, "last update failed", What's new).
    // main() calls this once the window is shown and the Welcome dialog has
    // closed — scheduled from the constructor, they fired inside the Welcome
    // dialog's event loop, before app.exec(), where quit() is a no-op.
    void runStartupChecks();
    // True once an update installer has been launched and quewi is closing
    // so it can install. main() checks this after the Welcome dialog.
    bool isQuittingForUpdate() const { return m_quittingForUpdate; }

private:
    void buildLayout();
    void buildMenus();
    void applyTheme(const QString &name);
    void closeShow();
    void revealShowInFolder();
    void resetWorkspace();
    void rebindModel();
    bool maybeSaveChanges();
    // Where Import from URL downloads land: media/ beside a saved show, else
    // ~/Music/quewi-imports.
    QString mediaImportDir() const;
    // Opens the audio editor on an audio cue (or a video's soundtrack), wired
    // for lighting triggers: Test sends, live sends flash their marker.
    ui::AudioEditorWindow *openAudioEditor(cues::Cue *cue);
    // The video editor for a video cue — raises the one already open for it.
    ui::VideoEditorWindow *openVideoEditor(video::VideoCue *cue);
    // Double-click / Enter on a cue: its editor (audio or video), if it has one.
    void openEditorFor(cues::Cue *cue);
    // Close quewi so a launched update installer (which waits for this
    // process to exit) can run. Never prompts; the save question has
    // already been asked.
    void quitForUpdate();
    bool m_quittingForUpdate = false;
    bool saveTo(const QString &path);
    // Build a cue from a media file, classified by extension. When audioOnly
    // is true (soundboard pads), audio-/video-container files both become an
    // AudioCue so a pad plays just the sound — a .webm on a pad shouldn't open
    // a (black) video surface.
    std::unique_ptr<cues::Cue> cueFromFile(const QString &path, bool audioOnly = false);
    int  insertCuesFromUrls(const QList<QUrl> &urls, int startRow = -1,
                            core::CueList *targetList = nullptr);
    // The one soundboard cue list (its tab shows the pad grid). Created on
    // first use. Pad cues are inserted here so they stay out of the set list.
    core::CueList *getOrCreateSoundboardList();
    // Open the cue list at tab `idx` in its own floating window (multi-monitor).
    void detachCueListTab(int idx);
    // Delete the cue list shown on tab `idx`, with confirmation. Guards against
    // removing the last Normal list (the soundboard isn't a valid set-list
    // home). Shared by the menu-bar action and the tab right-click → Delete.
    void removeCueListAt(int idx);
    // Switch to `list`'s tab: page, model and GO context together.
    void selectListTab(core::CueList *list);
    // Point the Inspector and NEXT label at the cue view's current cue.
    void syncSelectionUi();
    void closeDetachedWindows();
    // Shared body for every New-<type> menu action: inserts `cue`
    // after the current selection, names + numbers it, pushes the
    // undo command, and selects the new row. Each insertXCue() slot
    // is a one-liner over this.
    void insertCueOfType(std::unique_ptr<cues::Cue> cue, const QString &name);
    // Fills `menu` with one action per cue type, each wired to append a new
    // cue at the end of the active list. Used by the empty-area right-click
    // menu. Kept separate from the menu-bar Cue menu so the two don't fight
    // over the type keyboard shortcuts.
    void populateNewCueMenu(QMenu *menu);
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
    // The cue a playing audio voice belongs to: an audio cue, or the video
    // cue whose soundtrack it is.
    static cues::Cue *audioCueForVoice(core::CueList *list,
                                             quint64 voiceId);

    void registerOscRemoteHandlers();
    // v5: lighting triggers, video conversion, soundboard → mic
    // (MainWindowOscV5.cpp). Needs the GoEngine, so it runs after it exists.
    void registerOscApiV5();
    // The Matrix List's remote queries (MainWindowMatrix.cpp).
    void registerOscMatrix();
    // Recording lighting triggers from the desk while a cue plays for real,
    // for remotes: /quewi/cue/<n>/triggers/record start|stop|keep|discard.
    void registerOscRecord();
    void stopLiveRecord(bool notify);
    ui::DeskTakeRecorder *m_liveRecorder = nullptr;
    QPointer<cues::Cue>   m_liveRecordCue;
    bool                  m_liveRecordHeard = false;   // its song has played since start
    std::vector<audio::RecordedCue> m_liveTake;        // stopped, waiting for keep / discard
    QTimer               *m_liveRecordWatch = nullptr;
    cues::Cue  *oscCueByNumber(double num) const;
    QJsonObject triggersReplyJson(cues::Cue *c) const;
    void        commitTriggers(cues::Cue *c, const audio::LightTriggers &next);
    // (Re)bind the OSC UDP listener to the configured port (osc/udpPort,
    // default 53535), falling back to a few stable ports if it's taken or in a
    // Windows-reserved range, and surfacing the actual port (or a clear
    // failure). Called at startup and again when the port changes in
    // Preferences, so a port change applies without a restart.
    void bindOscListener();
    // Wire workspace + cue-list + cue signals into OSC push
    // notifications. Reattaches after every resetWorkspace.
    void wireOscNotifications();
    // Row notifications for the ACTIVE list (re-run whenever it changes) and
    // the workspace-level ones; split out of wireOscNotifications.
    void wireOscListNotifications();
    void wireOscWorkspaceNotifications();
    QList<QMetaObject::Connection> m_oscListConnections;
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

    // Cross-list cue links. When `source` fires, fire the cue it's linked to —
    // the forward link it stores, plus any cue that links to it (links are
    // stored single-sided but fire both ways). m_pendingLinkFires marks the
    // partners we scheduled so a link-fire can't bounce back and loop, even
    // when the partner has a pre-wait and fires asynchronously.
    void fireLinkedFor(cues::Cue *source);
    QSet<QUuid> m_pendingLinkFires;

    // Move the playhead to where it belongs after GO fired `fired` (past its
    // auto-continue chain and a group's children), or past the end.
    void advancePlayheadAfter(cues::Cue *fired);
    // Set by the Goto handler: a Goto fired during onGoRequested already put
    // the playhead where it wants it, so the normal advance must not undo it.
    bool m_playheadJumped = false;
    // Soundboard "Stop All": stop only what the board's pads are playing —
    // not the main list, not the lights (it used to be a full panic).
    void stopSoundboard();

    std::unique_ptr<core::Workspace>    m_workspace;
    std::unique_ptr<core::CueListModel> m_model;
    // Models behind detached cue-list windows — fed the same running/peak
    // state as the main view so their live dots/VU update too.
    QList<QPointer<core::CueListModel>> m_detachedModels;
    QList<QPointer<QWidget>>            m_detachedWindows;
    QList<QPointer<ui::CueListView>>    m_detachedCueViews;
    QHash<video::VideoCue *, QPointer<ui::VideoEditorWindow>> m_videoEditors;
    std::unique_ptr<osc::OscEngine>     m_oscEngine;
    std::unique_ptr<audio::AudioEngine> m_audioEngine;
    std::unique_ptr<lighting::LightingEngine> m_lightingEngine;
    std::unique_ptr<video::VideoEngine>       m_videoEngine;
    std::unique_ptr<midi::MidiEngine>         m_midiEngine;
    std::unique_ptr<midi::MidiInputEngine>    m_midiInput;
    std::unique_ptr<GoEngine>                 m_goEngine;

    ui::CueListView *m_cueListView = nullptr;
    ui::CartView    *m_cartView    = nullptr;
    ui::MixView     *m_mixView     = nullptr;
    // The Matrix List page (quewi + desk cues merged) and the desk cue-list
    // reader it uses, which shares m_eosFeedback's TCP connection.
    ui::MatrixView  *m_matrixView  = nullptr;
    osc::EosCueLists *m_eosCueLists = nullptr;
    QSet<QUuid>      m_runningCueIds;            // from the active-cues panel
    bool matrixShowing() const;                  // the Matrix List page is up
    core::CueList *firstMatrixList() const;
    // Keep every Matrix List's desk cue list read (not just the one on
    // screen — remotes query them too), and remember what was read.
    void syncMatrixWatch();
    // GO Lights: fire the Matrix List's next desk cue (button, Show Mode,
    // Ctrl+Shift+G). Only does anything while the Matrix List is up.
    void goLights();
    // GO Both: quewi's GO and GO Lights in one press (transport bar, Matrix
    // List only). The safe key is checked once for the pair.
    void goBoth();
    void updateGoBoth();        // show/enable the transport's GO Both
    void lightsBack();          // the cue before the desk's LIVE one, in the matrix's list
    void lightsStop();          // the desk's Stop key
    QAction *m_actGoLights = nullptr;
    QAction *m_actGoBoth = nullptr;
    QAction *m_actLightsBack = nullptr;
    QAction *m_actLightsStop = nullptr;
    // Wire a Matrix List view (the page, or a detached one) to the window.
    void wireMatrixView(ui::MatrixView *view);
    void onDeskCuesRead(const QString &deskList);
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
    // Actions Show Mode disabled, with the enabled state to restore on exit.
    QList<QPair<QPointer<QAction>, bool>> m_showModeLocked;

    // Stage-manager Show Mode and the Lighting panel. Both poll
    // buildShowSnapshot(); the desk's own state comes from m_eosFeedback.
    QStackedWidget     *m_modeStack = nullptr;      // 0 = the normal screen, 1 = Show Mode
    ui::ShowModeView   *m_showModeView = nullptr;
    QDockWidget        *m_lightingDock = nullptr;
    ui::LightingPanel  *m_lightingPanel = nullptr;
    osc::EosFeedback   *m_eosFeedback = nullptr;
    core::LightingDesk  m_deskSeen;                 // as last read from Preferences
    QTimer             *m_deskWatch = nullptr;
    QPointer<cues::Cue> m_lastFiredCue;
    // Docks Show Mode hid, to bring back on the way out.
    QList<QPointer<QDockWidget>> m_showModeHiddenDocks;
    ui::ShowSnapshot buildShowSnapshot() const;
    // Starts / stops reading the desk back when the desk setting changes.
    void syncDeskFeedback(bool force = false);

    QString m_currentPath;
    QString m_journalPath;
    // Held for as long as m_journalPath exists, so another quewi starting up
    // knows this journal is live and leaves it alone (show/JournalLock.h).
    std::unique_ptr<QLockFile> m_journalLock;
    QTimer *m_journalTimer = nullptr;

    void rebuildListTabs();
    void applyShowMode();
    void scheduleJournal();
    void writeJournal();
    void clearJournal();
    void forgetRecentFile(const QString &path);
};

} // namespace quewi
