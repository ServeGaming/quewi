#pragma once

#include "ui/ShowSnapshot.h"

#include <QWidget>

#include <functional>
#include <vector>

class QBoxLayout;
class QLabel;
class QPushButton;
class QTimer;

namespace quewi::ui {

class ElideLabel;
class ThinProgressBar;

// The dockable "Lighting" panel on the main cue screen: the desk's link and
// its ACTIVE / PENDING cue, the Armed switch, and the lighting hits coming
// up in whatever's playing, with live countdowns. Same data path as
// ShowModeView — a ShowSnapshot pushed in or polled from a provider every
// 100 ms while visible — and the same rule: it never touches an engine.
//
// It can be docked at a side (narrow and tall) or the bottom (wide and
// short); the three blocks stack or sit side by side to suit.
class LightingPanel : public QWidget {
    Q_OBJECT
public:
    explicit LightingPanel(QWidget *parent = nullptr);

    void setSnapshotProvider(std::function<ShowSnapshot()> provider);
    void setSnapshot(const ShowSnapshot &s);
    const ShowSnapshot &snapshot() const { return m_snap; }

    // Wide and short (a bottom dock) puts the blocks in a row; otherwise
    // they stack. Decided from the size, so it's right before the first show.
    bool isWideLayout() const { return width() > height() * 2 && width() >= 700; }

signals:
    void armedToggled(bool on);
    void deskSettingsRequested();

protected:
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    void buildUi();
    void applyStyle();
    void relayout();
    void render();
    void renderDesk();
    void renderHits();

    struct HitRow {
        QWidget    *frame = nullptr;
        ElideLabel *when  = nullptr;
        ElideLabel *name  = nullptr;
        ElideLabel *does  = nullptr;
    };
    HitRow makeHitRow(QWidget *parent);

    ShowSnapshot m_snap;
    std::function<ShowSnapshot()> m_provider;
    QTimer *m_tick = nullptr;
    QBoxLayout *m_box = nullptr;
    bool m_wide = false;

    // Desk block
    QWidget     *m_deskBlock = nullptr;
    QLabel      *m_deskDot = nullptr;
    ElideLabel  *m_deskLink = nullptr;
    ElideLabel  *m_deskDetail = nullptr;
    QPushButton *m_settings = nullptr;

    // Desk cues block
    QWidget         *m_cueBlock = nullptr;
    QLabel          *m_activeCaps = nullptr;
    QLabel          *m_active = nullptr;
    QLabel          *m_activeTime = nullptr;
    ElideLabel      *m_activeLabel = nullptr;
    ThinProgressBar *m_activeBar = nullptr;
    QLabel          *m_pendingCaps = nullptr;
    ElideLabel      *m_pending = nullptr;
    QLabel          *m_blind = nullptr;
    QLabel          *m_cueEmpty = nullptr;

    // Hits block
    QWidget     *m_hitsBlock = nullptr;
    QPushButton *m_armed = nullptr;
    QLabel      *m_hitCountdown = nullptr;
    ElideLabel  *m_hitName = nullptr;
    ElideLabel  *m_hitDoes = nullptr;
    std::vector<HitRow> m_hitRows;
    QLabel      *m_hitsState = nullptr;
};

} // namespace quewi::ui
