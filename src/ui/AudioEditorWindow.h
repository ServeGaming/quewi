#pragma once

#include "audio/AudioCue.h"
#include "audio/AudioEditorModel.h"
#include "audio/AudioEditorRenderer.h"
#include "ui/TimelineCanvas.h"
#include "ui/EffectsRackWidget.h"
#include "ui/SpectrogramWidget.h"

#include <QMainWindow>
#include <QAudioSink>
#include <QBuffer>
#include <QLabel>
#include <QPointer>
#include <QTimer>
#include <memory>
#include <vector>

namespace quewi::ui {

// Full audio editor window — Phase 9.
//
// Features:
//  • Multi-track timeline with waveform rendering (TimelineCanvas)
//  • Drag/trim regions; razor-cut; per-region gain
//  • Per-track effects rack: EQ, Compressor, Reverb, Delay
//  • FFT spectrogram view (tab alongside effects rack)
//  • Preview playback with transport controls
//  • Render / bounce to 24-bit WAV (updates the cue's file path)
//  • Separate undo stack from the main show undo
//  • State persisted in the cue's editorModel payload key
class AudioEditorWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit AudioEditorWindow(audio::AudioCue *cue, QWidget *parent = nullptr);
    ~AudioEditorWindow() override;

protected:
    void closeEvent(QCloseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

private slots:
    void onPlay();
    void onStop();
    void onLoopToggled(bool);
    void zoomIn();
    void zoomOut();
    void zoomFit();
    void addTrack();
    void bounceToFile();
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

    QPointer<audio::AudioCue>        m_cue;
    std::unique_ptr<audio::AudioEditorModel>    m_model;
    std::unique_ptr<audio::AudioEditorRenderer> m_renderer;

    // Central widgets
    TimelineCanvas   *m_timeline    = nullptr;
    QScrollBar       *m_hbar        = nullptr;
    QScrollBar       *m_vbar        = nullptr;

    // Bottom panel
    EffectsRackWidget *m_effectsRack = nullptr;
    SpectrogramWidget *m_spectrogram = nullptr;

    // Header strip + status
    QLabel *m_headerNumber = nullptr;
    QLabel *m_headerName   = nullptr;
    QLabel *m_headerMeta   = nullptr;
    QLabel *m_statusLabel  = nullptr;
    void updateHeader();

    // Playback
    std::unique_ptr<QAudioSink> m_sink;
    QBuffer                     m_playBuffer;
    std::vector<float>          m_renderedPcm;
    QTimer                      m_playTimer;
    bool                        m_looping  = false;
    bool                        m_isPlaying = false;
    qint64                      m_playFrameOffset = 0; // first frame of current loop
    qint64                      m_sinkStartFrame  = 0;
};

} // namespace quewi::ui
