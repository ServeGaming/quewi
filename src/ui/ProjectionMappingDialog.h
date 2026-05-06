#pragma once

#include <QDialog>
#include <QPolygonF>

class QCheckBox;
class QComboBox;
class QPushButton;

namespace quewi::video { class VideoEngine; }

namespace quewi::ui {

class CornerPinEditor;

// Tools → Projection Mapping. Pick a target screen, drag the four
// corner handles, and see the live compositor output warp in real
// time. The dialog persists per-screen quads via QSettings so the
// mapping survives quewi restarts; the same store is consulted on
// startup to apply the saved warp before the first cue fires.
//
// The dialog talks to VideoEngine::setCornerPin so the warp shows up
// on the live output as the user drags. If no compositor window is
// active for the chosen screen yet (no visual cue running), the
// preview is still saved to QSettings — the warp activates the
// instant a cue lands on that screen.
class ProjectionMappingDialog : public QDialog {
    Q_OBJECT
public:
    ProjectionMappingDialog(video::VideoEngine *engine, QWidget *parent = nullptr);
    ~ProjectionMappingDialog() override;

    // Helper for app startup: applies every persisted per-screen quad
    // to the engine. Call once VideoEngine is constructed.
    static void applyPersistedQuads(video::VideoEngine *engine);

private slots:
    void onScreenChanged(int index);
    void onCornersChanged(const QPolygonF &normalised);
    void onResetClicked();
    void onSaveClicked();
    void onTestPatternToggled(bool on);
    void onExportClicked();

private:
    static QString settingsKey(int screenIndex);
    void loadForScreen(int screenIndex);

    video::VideoEngine *m_engine;
    QComboBox          *m_screen        = nullptr;
    CornerPinEditor    *m_editor        = nullptr;
    QPushButton        *m_resetBtn      = nullptr;
    QPushButton        *m_saveBtn       = nullptr;
    QPushButton        *m_exportBtn     = nullptr;
    QCheckBox          *m_patternToggle = nullptr;
    int                 m_currentScreen = 0;
};

} // namespace quewi::ui
