#pragma once

#include "audio/AudioCue.h"
#include "audio/AudioEditorModel.h"
#include "audio/AudioEditorRenderer.h"
#include "audio/LightTrigger.h"
#include "ui/TimelineCanvas.h"
#include "ui/EffectsRackWidget.h"

#include <QMainWindow>
#include <QAudioSink>
#include <QElapsedTimer>
#include <QBuffer>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <functional>
#include <memory>
#include <vector>

class QTabWidget;
class QToolButton;
class QUndoStack;
namespace quewi::core { class Workspace; }

namespace quewi::ui {

class LightTriggersPanel;
class LiveAudioScope;
class LiveEffectDevice;

// Full audio editor window — Phase 9.
//
// Features:
//  • Multi-track timeline with waveform rendering (TimelineCanvas)
//  • Drag/trim regions; razor-cut; per-region gain
//  • Per-track effects rack: EQ, Compressor, Reverb, Delay
//  • FFT spectrogram view (tab alongside effects rack)
//  • Preview playback with transport controls
//  • Render / bounce to 24-bit WAV (updates the cue's file path)
//  • Separate undo stack from the main show undo (lighting-trigger edits
//    use the show's, via setTriggerSupport — they're cue fields)
//  • Lighting tab + marker lane: triggers that cue the desk from the song
//  • State persisted in the cue's editorModel payload key
class AudioEditorWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit AudioEditorWindow(audio::AudioCue *cue, QWidget *parent = nullptr);
    ~AudioEditorWindow() override;

    // Lighting triggers. ws feeds the Fire-cue picker, undo takes the edits
    // (null = edit the cue directly), midiPorts fills the MIDI / MSC port lists.
    void setTriggerSupport(core::Workspace *ws, QUndoStack *undo,
                           std::function<QStringList()> midiPorts);
    void showLightingTab();

signals:
    // Test buttons, and — with "Send while previewing" on — every trigger the
    // editor's own preview crosses. The owner sends it (GoEngine).
    void testTriggerRequested(const quewi::audio::TriggerAction &action);
    // The Lighting tab's "Change…" next to the desk line: open Preferences →
    // Lighting.
    void lightingDeskSettingsRequested();

public slots:
    // A trigger fired (live show or preview): flash its row and marker.
    void flashTrigger(const QUuid &id);

protected:
    void closeEvent(QCloseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    bool event(QEvent *e) override;

private slots:
    void onPlay();
    void onStop();
    void onLoopToggled(bool);
    void zoomIn();
    void zoomOut();
    void zoomFit();
    void addTrack();
    // Render: updates the cue's existing render in place (no dialog) when the
    // cue plays one, otherwise asks where to save. Returns false if nothing
    // was rendered (cancelled or failed).
    bool bounceToFile();
    bool renderAs();                 // always asks for a file
    void onPlaybackTick();
    void onRegionSelected(QUuid regionId);
    void onTrackSelected(int trackIndex);

private:
    void buildToolbar();
    void buildCentral();
    void buildBottomPanel();
    void startPlayback();
    void stopPlayback();
    bool promptSaveIfDirty();
    bool renderTo(const QString &path);
    // The cue plays the file this session was last rendered to.
    bool playsOwnRender() const;
    void updateRenderButton();
    // Write the session (tracks, regions, effects rack) into the cue.
    void syncSessionToCue();
    // Lighting triggers → the marker lane (on cue changes and rate changes).
    void syncTriggersToCanvas();
    double secondsPerFrame() const;
    // Preview tracker: sends what the preview crosses while the toggle is on.
    void beginPreviewTriggers(qint64 frame);
    void endPreviewTriggers();
    void sendPreviewEvents(const std::vector<audio::TriggerEvent> &events);
    QPushButton *m_renderBtn = nullptr;
    // Effect edits reach the cue shortly after you make them, so firing the
    // cue from the main window with the editor still open plays the new rack.
    QTimer       m_syncTimer;

    QPointer<audio::AudioCue>        m_cue;
    std::unique_ptr<audio::AudioEditorModel>    m_model;
    std::unique_ptr<audio::AudioEditorRenderer> m_renderer;

    // Central widgets
    TimelineCanvas   *m_timeline    = nullptr;
    QScrollBar       *m_hbar        = nullptr;
    QScrollBar       *m_vbar        = nullptr;

    // One-shot guard so the view fits to content exactly once, when the
    // async decode of the source file first completes. Keeps later edits
    // (reverse / normalise re-emit Loaded) from overriding a manual zoom.
    bool              m_initialFitDone = false;

    // Bottom panel
    EffectsRackWidget *m_effectsRack = nullptr;
    QTabWidget        *m_bottomTabs  = nullptr;
    LightTriggersPanel *m_triggersPanel = nullptr;
    QToolButton       *m_sendTriggersBtn = nullptr;   // "Send while previewing"
    audio::TriggerTracker m_previewTracker;
    bool               m_previewTracking = false;
    QElapsedTimer      m_previewClock;                 // wall time between ticks
    // Real-time analyzer fed from the preview playback; the open EQ /
    // Compressor editors read it to draw a live spectrum / level.
    LiveAudioScope    *m_scope = nullptr;

    // Header strip + status
    QLabel *m_headerNumber = nullptr;
    QLabel *m_headerName   = nullptr;
    QLabel *m_headerMeta   = nullptr;
    QLabel *m_statusLabel  = nullptr;
    void updateHeader();

    // Playback. m_renderedPcm holds the DRY mix; m_liveDevice streams it
    // through the active track's effects in real time so EQ/compressor edits
    // are heard live. m_activeTrack is whose chain the device applies.
    std::unique_ptr<QAudioSink>       m_sink;
    std::unique_ptr<LiveEffectDevice> m_liveDevice;
    std::vector<float>                m_renderedPcm;
    QPointer<audio::AudioEditorTrack> m_activeTrack; // nulls itself if removed
    QTimer                            m_playTimer;
    bool                        m_looping  = false;
    bool                        m_isPlaying = false;
    qint64                      m_playFrameOffset = 0; // first frame of current loop
    qint64                      m_sinkStartFrame  = 0;
};

} // namespace quewi::ui
