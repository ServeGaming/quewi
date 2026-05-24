#pragma once

#include <QDialog>
#include <QString>

class QListWidget;

namespace quewi::ui {

// Xcode-style launchpad shown at startup when no show was passed on
// the command line. Lets the user create a new show, open an
// existing one, or jump straight to a recent file — without staring
// at an empty Untitled cue list as the first surface.
class WelcomeDialog : public QDialog {
    Q_OBJECT
public:
    enum class Action {
        None,           // user closed the window without choosing
        NewShow,        // create a new Untitled show
        OpenExisting,   // open file picker
        OpenRecent      // open chosenPath()
    };

    explicit WelcomeDialog(QWidget *parent = nullptr);
    ~WelcomeDialog() override;

    Action  action() const     { return m_action; }
    QString chosenPath() const { return m_chosenPath; }

    // Persisted in QSettings as ui/showWelcome (default true).
    static bool showOnLaunchEnabled();

private slots:
    void onNewClicked();
    void onOpenClicked();
    void onRecentActivated();

private:
    void buildLayout();
    void populateRecents();

    Action       m_action       = Action::None;
    QString      m_chosenPath;
    QListWidget *m_recentList   = nullptr;
};

} // namespace quewi::ui
