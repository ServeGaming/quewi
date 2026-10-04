#pragma once

#include <QDialog>
#include <QKeySequence>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <functional>

class QLineEdit;
class QLabel;
class QListView;
class QMenuBar;
class QMenu;
class QAction;

namespace quewi::ui {

// ── Items ─────────────────────────────────────────────────────────────
// One thing the command menu can do. Everything is flattened into these:
// menu-bar actions, cues (stand by / open in editor), cue lists (switch),
// recent shows (open), Preferences pages (open on that page).
struct CommandItem {
    QString id;            // stable across runs: "action:Cue/New Audio", "cue:<uuid>", "list:<uuid>", "recent:<path>", "prefs:Audio"
    QString title;
    QString description;   // one line, shown at the bottom when selected
    QString group;         // "Cue", "Lights", "Show", "Cues", "Lists", "Recent shows", "Preferences", …
    QString keywords;      // matched at lower weight (menu path, cue number, type)
    QString badge;         // 1–2 characters drawn in the icon well
    QString shortcut;      // portable display text ("Ctrl+K"), empty if none
    bool    enabled = true;
    bool    editing = false;   // changes the show — hidden in Show Mode
    double  number = -1;       // cue number (for exact-number ranking), else -1
    std::function<void()> run;
    QString secondaryLabel;    // Ctrl+Enter, e.g. "Open in editor"; empty = none
    std::function<void()> secondary;
};

// What the owning window hands the menu. Everything is a plain value or a
// callback so the menu (and its tests) never touch the workspace directly.
struct CommandContext {
    QMenuBar *menuBar = nullptr;
    bool showMode = false;

    struct CueEntry { QUuid id; double number = 0; QString name; QString type; bool hasEditor = false; };
    QList<CueEntry> cues;                                   // the list on screen, in order
    std::function<void(const QUuid &)> standByCue;          // select it (next GO)
    std::function<void(const QUuid &)> editCue;             // open its editor

    struct ListEntry { QUuid id; QString name; QString kind; bool current = false; };
    QList<ListEntry> lists;
    std::function<void(const QUuid &)> switchList;

    QStringList recentShows;                                // paths, most recent first
    std::function<void(const QString &)> openRecent;

    QStringList preferencePages;                            // sidebar names
    std::function<void(const QString &)> openPreferences;
};

// The Preferences sidebar pages the menu can open (PreferencesDialog's
// names, in its order). Kept here so the menu's providers stay value-only.
QStringList commandMenuPreferencePages();

// The flat item list for a context. Transport (GO / Panic / Pause / Fade
// All) is never in it: those aren't menu-bar actions and the menu has no
// other way in. Show Mode keeps only run-safe items (see isRunSafe).
QList<CommandItem> buildCommandItems(const CommandContext &ctx);

// Show Mode's rule for the menu: enabled, and not an editing action.
bool isRunSafe(const CommandItem &item);

// ── Settings ──────────────────────────────────────────────────────────
// All of the menu's persisted state, in one place so Preferences and the
// menu agree on the keys.
struct CommandMenuSettings {
    static bool chordsEnabled();
    static void setChordsEnabled(bool on);

    // Leader+<letter> pins: letter → item id (+ title for display).
    struct Pin { QChar letter; QString itemId; QString title; };
    static QList<Pin> pins();
    static void setPin(QChar letter, const QString &itemId, const QString &title);
    static void removePin(QChar letter);
    static void clearPins();

    // Mnemonic overrides / remembered assignments: node id → letter.
    static QChar mnemonic(const QString &nodeId);
    static void setMnemonic(const QString &nodeId, QChar letter);
    static void resetMnemonics();

    // Recently run item ids, most recent first (bounded).
    static QStringList recent();
    static void noteRecent(const QString &itemId);
    static void clearRecent();
};

// ── The key menu (which-key) tree ─────────────────────────────────────
struct MenuNode {
    QString id;          // stable: "cue", "cue/new-audio", "prefs/audio"
    QString label;
    QChar   key;         // the mnemonic
    QString shortcut;    // leaf: the action's shortcut, for the hint
    bool    enabled = true;
    bool    editing = false;
    int     item = -1;   // leaf: index into the item list
    bool    opensSearch = false;   // leaf that drops into the search view
    QList<MenuNode> children;   // non-empty = submenu
    bool isSubmenu() const { return !children.isEmpty(); }
};

// Builds the tree from the items + context, assigning one letter per
// sibling: the menu accelerator (&) when free, else a word-start letter,
// else any letter of the label, else a digit. Assignments are remembered
// in settings so they don't move between runs; the user can override.
MenuNode buildMenuTree(const QList<CommandItem> &items, const CommandContext &ctx);

// Picks a free letter for `label` given the siblings already keyed. Pure.
QChar pickMnemonic(const QString &label, QChar accelerator, const QSet<QChar> &taken);

// ── The widget ────────────────────────────────────────────────────────
// Spotlight-style floating panel over the main window: a search field,
// a result list, the selected result's description at the bottom. The
// same panel also shows the key menu (which-key): a grid of one-letter
// mnemonics by category; type letters to drill down, Backspace goes up,
// anything that isn't a mnemonic drops into the search, Esc closes.
//
// Modal, so none of the window's shortcuts (Space = GO, Esc = Panic) can
// reach it. Runs nothing itself: the chosen item's action is handed back
// through takePending() once the panel has closed.
class CommandMenu : public QDialog {
    Q_OBJECT
public:
    enum class View { Search, Keys };

    explicit CommandMenu(const CommandContext &ctx, QWidget *parent = nullptr);
    ~CommandMenu() override;

    void openSearch(const QString &query = {});
    // The key menu at the top level; `typed` letters are applied as if typed.
    void openKeys(const QString &typed = {});

    // Leader+<letter> while the leader is held. A pinned letter runs its
    // item (true, pending set); otherwise false and the caller opens the
    // key menu with the letter typed.
    bool runChord(QChar letter);

    // The action chosen (or none). Call after exec() returns. openKeys()
    // can choose before the panel is ever shown (typed letters reached a
    // leaf), so check hasPending() before exec().
    bool hasPending() const { return static_cast<bool>(m_pending); }
    std::function<void()> takePending();

    // ── Test / inspection hooks ──
    View view() const { return m_view; }
    QString breadcrumb() const;              // "Cue › New" in the key view
    QStringList visibleTitles() const;       // result rows in order (no headers)
    int currentRow() const;                  // index into visibleTitles()
    const CommandItem *currentItem() const;
    QList<MenuNode> visibleNodes() const;    // key view cells in order
    const QList<CommandItem> &items() const { return m_items; }
    const MenuNode &tree() const { return m_tree; }
    QString footerText() const;
    QLineEdit *input() const { return m_input; }
    // Feed a key as the user would type it (tests).
    void typeKey(int key, Qt::KeyboardModifiers mods = Qt::NoModifier, const QString &text = {});

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    class ResultsModel;
    class ResultDelegate;
    class KeyGrid;

    void refilter();
    void setView(View v);
    void enterNode(const MenuNode &node);
    void goUp();
    bool handleKeysViewKey(QKeyEvent *ke);
    bool handleSearchViewKey(QKeyEvent *ke);
    void chooseCurrent(bool secondary);
    void chooseItem(int index, bool secondary);
    void updateFooter();
    void updateHeader();
    void layoutPanel();
    const MenuNode *currentNode() const;

    CommandContext      m_ctx;
    QList<CommandItem>  m_items;
    MenuNode            m_tree;
    QList<int>          m_path;         // child indexes from the root
    View                m_view = View::Search;
    std::function<void()> m_pending;
    bool                m_pinning = false;   // Ctrl+P: waiting for the letter

    QWidget   *m_panel = nullptr;
    QLabel    *m_crumb = nullptr;
    QLineEdit *m_input = nullptr;
    QListView *m_list = nullptr;
    ResultsModel *m_model = nullptr;
    KeyGrid   *m_grid = nullptr;
    QLabel    *m_footerLeft = nullptr;
    QLabel    *m_footerRight = nullptr;
};

} // namespace quewi::ui
