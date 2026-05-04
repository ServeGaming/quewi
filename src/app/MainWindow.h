#pragma once

#include <QMainWindow>
#include <QString>
#include <memory>

class QAction;
class QSplitter;

namespace quewi::core { class Workspace; class CueListModel; }
namespace quewi::ui   { class CueListView; class Inspector; class TransportBar; class OscMonitor; }
namespace quewi::osc  { class OscEngine; }
namespace quewi::audio { class AudioEngine; }

namespace quewi {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void newShow();
    void openShow();
    bool saveShow();
    bool saveShowAs();
    void showPreferences();
    void showOscMonitor();
    void insertMemoCue();
    void insertOscCue();
    void insertAudioCue();
    void insertFadeCue();
    void deleteSelectedCue();
    void onSelectionChanged();
    void updateTitle();
    void onGoRequested();

private:
    void buildLayout();
    void buildMenus();
    void resetWorkspace();
    void rebindModel();
    bool maybeSaveChanges();
    bool saveTo(const QString &path);

    std::unique_ptr<core::Workspace>    m_workspace;
    std::unique_ptr<core::CueListModel> m_model;
    std::unique_ptr<osc::OscEngine>     m_oscEngine;
    std::unique_ptr<audio::AudioEngine> m_audioEngine;

    ui::CueListView *m_cueListView = nullptr;
    ui::Inspector   *m_inspector   = nullptr;
    ui::TransportBar *m_transport  = nullptr;
    ui::OscMonitor   *m_oscMonitor = nullptr;
    QSplitter       *m_mainSplitter = nullptr;

    QAction *m_actUndo = nullptr;
    QAction *m_actRedo = nullptr;
    QAction *m_actSave = nullptr;

    QString m_currentPath;
};

} // namespace quewi
