#include "ui/CommandMenu.h"

#include "ui/CommandMatch.h"
#include "ui/Theme.h"

#include <QAbstractListModel>
#include <QAction>
#include <QApplication>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace quewi::ui {

// ── Settings ──────────────────────────────────────────────────────────
namespace {

QSettings settings()
{
    return QSettings(QStringLiteral("ServeGaming"), QStringLiteral("quewi"));
}

constexpr int kMaxRecent = 40;

QString stripAmp(QString s)
{
    s.remove(QStringLiteral("&&"));
    s.remove(QChar('&'));
    return s;
}

QChar acceleratorOf(const QString &menuText)
{
    for (int i = 0; i + 1 < menuText.size(); ++i) {
        if (menuText.at(i) != QChar('&')) continue;
        const QChar n = menuText.at(i + 1);
        if (n == QChar('&')) { ++i; continue; }
        return n.toUpper();
    }
    return {};
}

QString slug(const QString &label)
{
    QString out;
    for (const QChar c : label) {
        if (c.isLetterOrNumber()) out += c.toLower();
        else if (!out.isEmpty() && !out.endsWith(QChar('-'))) out += QChar('-');
    }
    while (out.endsWith(QChar('-'))) out.chop(1);
    return out;
}

QString portableShortcut(const QAction *a)
{
    if (!a || a->shortcut().isEmpty()) return {};
    return a->shortcut().toString(QKeySequence::NativeText);
}

QString cueBadge(const QString &type)
{
    const auto t = type.toLower();
    if (t == QLatin1String("audio"))      return QStringLiteral("♪");
    if (t == QLatin1String("video"))      return QStringLiteral("▶");
    if (t == QLatin1String("image"))      return QStringLiteral("▣");
    if (t == QLatin1String("text"))       return QStringLiteral("T");
    if (t == QLatin1String("light") || t == QLatin1String("light fade")) return QStringLiteral("☀");
    if (t == QLatin1String("fade"))       return QStringLiteral("◢");
    if (t == QLatin1String("memo"))       return QStringLiteral("✎");
    if (t == QLatin1String("wait"))       return QStringLiteral("◷");
    if (t == QLatin1String("group"))      return QStringLiteral("▤");
    if (t == QLatin1String("osc"))        return QStringLiteral("⇄");
    if (t == QLatin1String("midi") || t == QLatin1String("msc")) return QStringLiteral("♩");
    return QStringLiteral("•");
}

QString cueNumberText(double n)
{
    QString s = QString::number(n, 'f', 3);
    while (s.endsWith(QChar('0'))) s.chop(1);
    if (s.endsWith(QChar('.'))) s.chop(1);
    return s;
}

// The app verbs: menu actions that read better under their own name and
// group in a launcher. Keyed by the menu text without accelerators.
struct Alias { const char *menuText; const char *group; const char *checkedTitle; const char *uncheckedTitle; const char *description; };
const Alias kAliases[] = {
    { "Show Mode (locked)",        "Show",   "Exit Show Mode",             "Enter Show Mode",
      "Lock editing and bring up the stage manager screen. Asks for the PIN on the way out." },
    { "Lighting Triggers Armed",   "Lights", "Disarm lighting triggers",   "Arm lighting triggers",
      "Master switch: songs send their triggers to the desk only while armed." },
    { "Lighting Desk…",            "Lights", nullptr, nullptr,
      "Set up the console the lighting triggers talk to (Eos, grandMA…)." },
    { "Lighting panel",            "Lights", "Hide the Lighting panel",    "Show the Lighting panel",
      "The dockable panel showing the desk's state and the next hits." },
    { "Inspector panel",           "View",   "Hide the Inspector",         "Show the Inspector", nullptr },
    { "Pre-flight…",               "Show",   nullptr, nullptr,
      "Check the show before doors: missing files, levels, outputs, memory." },
    { "OSC Monitor…",              "Show",   nullptr, nullptr, "Watch OSC traffic in and out." },
    { "Script follower…",          "Show",   nullptr, nullptr, "The script window, following the running cue." },
    { "Check for updates…",        "File",   nullptr, nullptr, "Look for a newer quewi and offer to install it." },
    { "Preferences…",              "File",   nullptr, nullptr, "Every setting. Type a page name to jump straight to it." },
    { "Keyboard shortcuts…",       "Tools",  nullptr, nullptr, "See and rebind every shortcut, including the command menu leader." },
};

const Alias *aliasFor(const QString &text)
{
    for (const auto &a : kAliases)
        if (text == QString::fromUtf8(a.menuText)) return &a;
    return nullptr;
}

bool isEditingMenu(const QString &topLabel)
{
    return topLabel == QLatin1String("File") || topLabel == QLatin1String("Edit")
        || topLabel == QLatin1String("Cue")  || topLabel == QLatin1String("List");
}

bool isSelfAction(const QString &text)
{
    return text.startsWith(QLatin1String("Command menu")) || text.startsWith(QLatin1String("Command palette"));
}

} // namespace

bool CommandMenuSettings::chordsEnabled()
{
    return settings().value(QStringLiteral("commandmenu/chordsEnabled"), true).toBool();
}

void CommandMenuSettings::setChordsEnabled(bool on)
{
    settings().setValue(QStringLiteral("commandmenu/chordsEnabled"), on);
}

QList<CommandMenuSettings::Pin> CommandMenuSettings::pins()
{
    QList<Pin> out;
    auto s = settings();
    s.beginGroup(QStringLiteral("commandmenu/chords"));
    auto keys = s.childKeys();
    std::sort(keys.begin(), keys.end());
    for (const auto &k : keys) {
        if (k.size() != 1) continue;
        const auto v = s.value(k).toStringList();
        if (v.isEmpty()) continue;
        out.append({k.at(0).toUpper(), v.at(0), v.value(1)});
    }
    return out;
}

void CommandMenuSettings::setPin(QChar letter, const QString &itemId, const QString &title)
{
    if (!letter.isLetterOrNumber()) return;
    settings().setValue(QStringLiteral("commandmenu/chords/%1").arg(letter.toUpper()),
                        QStringList{itemId, title});
}

void CommandMenuSettings::removePin(QChar letter)
{
    settings().remove(QStringLiteral("commandmenu/chords/%1").arg(letter.toUpper()));
}

void CommandMenuSettings::clearPins()
{
    settings().remove(QStringLiteral("commandmenu/chords"));
}

QChar CommandMenuSettings::mnemonic(const QString &nodeId)
{
    const auto v = settings().value(QStringLiteral("commandmenu/mnemonics/%1").arg(nodeId)).toString();
    return v.isEmpty() ? QChar() : v.at(0).toUpper();
}

void CommandMenuSettings::setMnemonic(const QString &nodeId, QChar letter)
{
    settings().setValue(QStringLiteral("commandmenu/mnemonics/%1").arg(nodeId), QString(letter.toUpper()));
}

void CommandMenuSettings::resetMnemonics()
{
    settings().remove(QStringLiteral("commandmenu/mnemonics"));
}

QStringList CommandMenuSettings::recent()
{
    return settings().value(QStringLiteral("commandmenu/recent")).toStringList();
}

void CommandMenuSettings::noteRecent(const QString &itemId)
{
    if (itemId.isEmpty()) return;
    auto list = recent();
    list.removeAll(itemId);
    list.prepend(itemId);
    while (list.size() > kMaxRecent) list.removeLast();
    settings().setValue(QStringLiteral("commandmenu/recent"), list);
}

void CommandMenuSettings::clearRecent()
{
    settings().remove(QStringLiteral("commandmenu/recent"));
}

// ── Items ─────────────────────────────────────────────────────────────
QStringList commandMenuPreferencePages()
{
    return { QObject::tr("General"), QObject::tr("Audio"), QObject::tr("Cue List"), QObject::tr("OSC"),
             QObject::tr("MIDI"), QObject::tr("Lighting"), QObject::tr("Theme"), QObject::tr("Show Mode"),
             QObject::tr("Command menu") };
}

bool isRunSafe(const CommandItem &item)
{
    return item.enabled && !item.editing;
}

QList<CommandItem> buildCommandItems(const CommandContext &ctx)
{
    auto tr = [](const char *text) { return QObject::tr(text); };
    QList<CommandItem> items;
    QSet<QString> seenTitles;

    // Menu-bar actions. Walk every menu; leaves become items. The Open
    // Recent submenu is skipped — the recent shows below replace it.
    if (ctx.menuBar) {
        std::function<void(QMenu *, const QString &, const QStringList &)> walk;
        walk = [&](QMenu *menu, const QString &top, const QStringList &path) {
            if (!menu) return;
            for (QAction *a : menu->actions()) {
                if (a->isSeparator()) continue;
                const QString text = stripAmp(a->text());
                if (text.isEmpty()) continue;
                if (a->menu()) {
                    if (text == QLatin1String("Open Recent")) continue;
                    walk(a->menu(), top, path + QStringList{text});
                    continue;
                }
                if (isSelfAction(text)) continue;
                CommandItem it;
                it.id = QStringLiteral("action:%1/%2").arg(top, (path + QStringList{text}).join(QChar('/')));
                it.title = text;
                it.group = path.isEmpty() ? top : QStringLiteral("%1 › %2").arg(top, path.join(QStringLiteral(" › ")));
                it.keywords = (QStringList{top} + path).join(QChar(' '));
                it.badge = QString(top.at(0));
                it.shortcut = portableShortcut(a);
                it.enabled = a->isEnabled();
                it.editing = isEditingMenu(top);
                if (const Alias *al = aliasFor(text)) {
                    it.group = QString::fromUtf8(al->group);
                    if (al->checkedTitle && a->isCheckable())
                        it.title = QString::fromUtf8(a->isChecked() ? al->checkedTitle : al->uncheckedTitle);
                    if (al->description) it.description = QString::fromUtf8(al->description);
                    it.keywords += QChar(' ') + text;
                    it.badge = QString(it.group.at(0));
                }
                if (path.contains(QStringLiteral("Theme"))) {
                    it.title = tr("%1 theme").arg(text);
                    it.badge = QStringLiteral("◐");
                    it.description = tr("Switch the whole app to the %1 theme.").arg(text);
                }
                if (it.description.isEmpty())
                    it.description = tr("%1 → %2").arg(top, (path + QStringList{text}).join(QStringLiteral(" → ")));
                // The same action can sit in two menus (Keyboard shortcuts…).
                if (seenTitles.contains(it.title)) continue;
                seenTitles.insert(it.title);
                QPointer<QAction> act(a);
                it.run = [act] { if (act) act->trigger(); };
                items.append(it);
            }
        };
        for (QAction *a : ctx.menuBar->actions())
            if (a->menu()) walk(a->menu(), stripAmp(a->text()), {});
    }

    // Cue lists: switch tab. The tab strip is locked in Show Mode, so is this.
    for (const auto &l : ctx.lists) {
        CommandItem it;
        it.id = QStringLiteral("list:%1").arg(l.id.toString(QUuid::WithoutBraces));
        it.title = l.name;
        it.group = tr("Switch to list");
        it.keywords = tr("list tab %1").arg(l.kind);
        it.badge = l.kind == QLatin1String("soundboard") ? QStringLiteral("♪")
                 : l.kind == QLatin1String("mix")        ? QStringLiteral("▤")
                                                         : QStringLiteral("☰");
        it.description = l.current ? tr("This list is already on screen.")
                                   : tr("Show the %1 tab.").arg(l.name);
        it.enabled = !l.current;
        it.editing = true;
        const QUuid id = l.id;
        auto fn = ctx.switchList;
        it.run = [fn, id] { if (fn) fn(id); };
        items.append(it);
    }

    // Recent shows: open. Opening a show closes this one, so never in Show Mode.
    for (const auto &p : ctx.recentShows) {
        CommandItem it;
        it.id = QStringLiteral("recent:%1").arg(p);
        it.title = QFileInfo(p).completeBaseName();
        it.group = tr("Open recent show");
        it.keywords = p;
        it.badge = QStringLiteral("⌂");
        it.description = p;
        it.editing = true;
        auto fn = ctx.openRecent;
        it.run = [fn, p] { if (fn) fn(p); };
        items.append(it);
    }

    // Preferences pages.
    for (const auto &page : ctx.preferencePages) {
        CommandItem it;
        it.id = QStringLiteral("prefs:%1").arg(page);
        it.title = page;
        it.group = tr("Preferences");
        it.keywords = tr("preferences settings options");
        it.badge = QStringLiteral("⚙");
        it.description = tr("Open Preferences on the %1 page.").arg(page);
        it.editing = true;
        auto fn = ctx.openPreferences;
        it.run = [fn, page] { if (fn) fn(page); };
        items.append(it);
    }

    // Cues: stand by (select as next GO). Never fires anything. The editor
    // is the Ctrl+Enter secondary, and not in Show Mode.
    for (const auto &c : ctx.cues) {
        CommandItem it;
        it.id = QStringLiteral("cue:%1").arg(c.id.toString(QUuid::WithoutBraces));
        it.title = QStringLiteral("%1  %2").arg(cueNumberText(c.number), c.name);
        it.group = tr("Stand by cue");
        it.keywords = tr("cue %1 %2").arg(cueNumberText(c.number), c.type);
        it.badge = cueBadge(c.type);
        it.number = c.number;
        it.description = tr("Make cue %1 the next to GO — nothing fires.").arg(cueNumberText(c.number));
        const QUuid id = c.id;
        auto sel = ctx.standByCue;
        it.run = [sel, id] { if (sel) sel(id); };
        if (c.hasEditor && ctx.editCue && !ctx.showMode) {
            it.secondaryLabel = tr("Open in editor");
            auto ed = ctx.editCue;
            it.secondary = [ed, id] { ed(id); };
        }
        items.append(it);
    }

    if (ctx.showMode) {
        items.erase(std::remove_if(items.begin(), items.end(),
                                   [](const CommandItem &i) { return !isRunSafe(i); }),
                    items.end());
    }
    return items;
}

// ── Mnemonics and the tree ────────────────────────────────────────────
QChar pickMnemonic(const QString &label, QChar accelerator, const QSet<QChar> &taken)
{
    auto free = [&](QChar c) { c = c.toUpper(); return c.isLetterOrNumber() && c.unicode() < 128 && !taken.contains(c); };
    if (!accelerator.isNull() && free(accelerator)) return accelerator.toUpper();
    for (int i : wordStarts(label))
        if (free(label.at(i))) return label.at(i).toUpper();
    for (const QChar c : label)
        if (free(c)) return c.toUpper();
    for (char d = '1'; d <= '9'; ++d)
        if (free(QChar::fromLatin1(d))) return QChar::fromLatin1(d);
    return {};
}

namespace {

struct TreeBuilder {
    const QList<CommandItem> &items;
    const CommandContext &ctx;
    QHash<QString, int> byId;

    TreeBuilder(const QList<CommandItem> &i, const CommandContext &c) : items(i), ctx(c)
    {
        for (int k = 0; k < items.size(); ++k) byId.insert(items[k].id, k);
    }

    int itemIndexFor(const QString &id) const { return byId.value(id, -1); }

    // A leaf for the item with this id, or nothing if it isn't in the list
    // (hidden by Show Mode, say).
    bool addLeaf(MenuNode &parent, const QString &itemId, QChar accel, const QString &labelOverride = {})
    {
        const int idx = itemIndexFor(itemId);
        if (idx < 0) return false;
        const auto &it = items[idx];
        MenuNode n;
        n.label = labelOverride.isEmpty() ? it.title : labelOverride;
        n.id = parent.id + QChar('/') + slug(n.label);
        n.key = accel;      // provisional; assignKeys settles it
        n.shortcut = it.shortcut;
        n.enabled = it.enabled;
        n.editing = it.editing;
        n.item = idx;
        parent.children.append(n);
        return true;
    }

    MenuNode &addSub(MenuNode &parent, const QString &slugId, const QString &label, QChar accel)
    {
        MenuNode n;
        n.id = parent.id.isEmpty() ? slugId : parent.id + QChar('/') + slugId;
        n.label = label;
        n.key = accel;
        parent.children.append(n);
        return parent.children.last();
    }

    QString actionId(const QString &top, const QStringList &path, const QString &text) const
    {
        return QStringLiteral("action:%1/%2").arg(top, (path + QStringList{text}).join(QChar('/')));
    }

    // Mirror a QMenu under `parent` (submenus become submenus).
    void addMenu(MenuNode &parent, QMenu *menu, const QString &top, const QStringList &path)
    {
        if (!menu) return;
        for (QAction *a : menu->actions()) {
            if (a->isSeparator()) continue;
            const QString text = stripAmp(a->text());
            if (text.isEmpty()) continue;
            if (a->menu()) {
                if (text == QLatin1String("Open Recent")) { addRecent(parent, acceleratorOf(a->text())); continue; }
                auto &sub = addSub(parent, slug(text), text, acceleratorOf(a->text()));
                addMenu(sub, a->menu(), top, path + QStringList{text});
                if (sub.children.isEmpty()) parent.children.removeLast();
                continue;
            }
            if (isSelfAction(text)) continue;
            addLeaf(parent, actionId(top, path, text), acceleratorOf(a->text()));
        }
    }

    void addRecent(MenuNode &parent, QChar accel)
    {
        auto &sub = addSub(parent, QStringLiteral("recent"), QObject::tr("Recent shows"), accel.isNull() ? QChar('R') : accel);
        for (const auto &p : ctx.recentShows) addLeaf(sub, QStringLiteral("recent:%1").arg(p), QChar());
        if (sub.children.isEmpty()) parent.children.removeLast();
    }

    QMenu *menuNamed(const QString &label) const
    {
        if (!ctx.menuBar) return nullptr;
        for (QAction *a : ctx.menuBar->actions())
            if (a->menu() && stripAmp(a->text()) == label) return a->menu();
        return nullptr;
    }

    QString anyActionId(const QString &menuText) const
    {
        for (int k = 0; k < items.size(); ++k)
            if (items[k].id.startsWith(QStringLiteral("action:")) && items[k].id.endsWith(QChar('/') + menuText))
                return items[k].id;
        return {};
    }

    // One letter per sibling. Remembered assignments first (if still free),
    // then the rest in order. Recurses.
    static void assignKeys(MenuNode &parent)
    {
        QSet<QChar> taken;
        QList<bool> done(parent.children.size(), false);
        for (int i = 0; i < parent.children.size(); ++i) {
            auto &c = parent.children[i];
            const QChar saved = CommandMenuSettings::mnemonic(c.id);
            if (!saved.isNull() && !taken.contains(saved)) { c.key = saved; taken.insert(saved); done[i] = true; }
        }
        for (int i = 0; i < parent.children.size(); ++i) {
            if (done[i]) continue;
            auto &c = parent.children[i];
            const QChar k = pickMnemonic(c.label, c.key, taken);
            c.key = k;
            if (!k.isNull()) { taken.insert(k); CommandMenuSettings::setMnemonic(c.id, k); }
        }
        for (auto &c : parent.children) if (c.isSubmenu()) assignKeys(c);
    }

    MenuNode build()
    {
        MenuNode root;
        root.label = QObject::tr("Command menu");

        // Cue — the Cue menu as-is.
        if (auto *m = menuNamed(QStringLiteral("Cue"))) {
            auto &cue = addSub(root, QStringLiteral("cue"), QObject::tr("Cue"), QChar('C'));
            addMenu(cue, m, QStringLiteral("Cue"), {});
            if (cue.children.isEmpty()) root.children.removeLast();
        }
        // Lights — the desk, the arm switch, the panel, the light cues.
        {
            auto &l = addSub(root, QStringLiteral("lights"), QObject::tr("Lights"), QChar('L'));
            addLeaf(l, anyActionId(QStringLiteral("Lighting Desk…")), QChar('D'));
            addLeaf(l, anyActionId(QStringLiteral("Lighting Triggers Armed")), QChar('A'));
            addLeaf(l, anyActionId(QStringLiteral("Lighting panel")), QChar('P'));
            addLeaf(l, anyActionId(QStringLiteral("New Light")), QChar('L'));
            addLeaf(l, anyActionId(QStringLiteral("New Light Fade")), QChar('F'));
            if (l.children.isEmpty()) root.children.removeLast();
        }
        // Show — running the show.
        {
            auto &s = addSub(root, QStringLiteral("show"), QObject::tr("Show"), QChar('S'));
            addLeaf(s, anyActionId(QStringLiteral("Show Mode (locked)")), QChar('M'));
            addLeaf(s, anyActionId(QStringLiteral("Pre-flight…")), QChar('P'));
            addLeaf(s, anyActionId(QStringLiteral("OSC Monitor…")), QChar('O'));
            addLeaf(s, anyActionId(QStringLiteral("Script follower…")), QChar('C'));
            addLeaf(s, anyActionId(QStringLiteral("Notifications…")), QChar('N'));
            if (s.children.isEmpty()) root.children.removeLast();
        }
        // Go to — lists, recent shows, and a cue by number or name.
        {
            auto &g = addSub(root, QStringLiteral("goto"), QObject::tr("Go to"), QChar('G'));
            MenuNode cueSearch;
            cueSearch.id = QStringLiteral("goto/cue");
            cueSearch.label = QObject::tr("Cue… (type a number or name)");
            cueSearch.key = QChar('C');
            cueSearch.opensSearch = true;
            g.children.append(cueSearch);
            if (!ctx.lists.isEmpty()) {
                auto &ls = addSub(g, QStringLiteral("lists"), QObject::tr("Lists"), QChar('L'));
                for (const auto &l : ctx.lists)
                    addLeaf(ls, QStringLiteral("list:%1").arg(l.id.toString(QUuid::WithoutBraces)), QChar());
                if (ls.children.isEmpty()) g.children.removeLast();
            }
            addRecent(g, QChar('R'));
        }
        // The rest of the menu bar, in its own order.
        if (ctx.menuBar) {
            for (QAction *a : ctx.menuBar->actions()) {
                if (!a->menu()) continue;
                const QString label = stripAmp(a->text());
                if (label == QLatin1String("Cue")) continue;
                auto &sub = addSub(root, slug(label), label, acceleratorOf(a->text()));
                addMenu(sub, a->menu(), label, {});
                if (sub.children.isEmpty()) root.children.removeLast();
            }
        }
        // Preferences — one leaf per page.
        if (!ctx.preferencePages.isEmpty()) {
            auto &p = addSub(root, QStringLiteral("prefs"), QObject::tr("Preferences"), QChar('P'));
            for (const auto &page : ctx.preferencePages)
                addLeaf(p, QStringLiteral("prefs:%1").arg(page), QChar());
            if (p.children.isEmpty()) root.children.removeLast();
        }
        assignKeys(root);
        return root;
    }
};

} // namespace

MenuNode buildMenuTree(const QList<CommandItem> &items, const CommandContext &ctx)
{
    TreeBuilder b(items, ctx);
    return b.build();
}

// ── Results model + delegate ──────────────────────────────────────────
class CommandMenu::ResultsModel : public QAbstractListModel {
public:
    struct Row {
        bool header = false;
        QString headerText;
        int item = -1;
        QList<int> positions;
    };
    enum { HeaderRole = Qt::UserRole + 1, ItemIndexRole, PositionsRole };

    explicit ResultsModel(const QList<CommandItem> *items, QObject *parent = nullptr)
        : QAbstractListModel(parent), m_items(items) {}

    void setRows(QList<Row> rows) { beginResetModel(); m_rows = std::move(rows); endResetModel(); }
    const QList<Row> &rows() const { return m_rows; }

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_rows.size(); }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_rows.size()) return {};
        const auto &r = m_rows[index.row()];
        switch (role) {
        case HeaderRole:     return r.header;
        case ItemIndexRole:  return r.item;
        case Qt::DisplayRole: return r.header ? r.headerText : (r.item >= 0 ? (*m_items)[r.item].title : QString());
        case PositionsRole:  return QVariant::fromValue(r.positions);
        default: return {};
        }
    }
    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        if (!index.isValid()) return Qt::NoItemFlags;
        return m_rows[index.row()].header ? Qt::NoItemFlags : (Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    }

private:
    const QList<CommandItem> *m_items;
    QList<Row> m_rows;
};

namespace {
constexpr int kRowH = 36;
constexpr int kHeaderH = 26;

// The theme sets fonts in pixels (QSS), so pointSizeF() is -1 there.
QFont adjusted(const QFont &base, qreal delta, bool bold = false)
{
    QFont f = base;
    if (f.pixelSize() > 0) f.setPixelSize(std::max(8, int(std::lround(f.pixelSize() + delta))));
    else                   f.setPointSizeF(std::max(7.0, f.pointSizeF() + delta));
    if (bold) f.setBold(true);
    return f;
}

// A keycap: the small rounded box a shortcut part or a mnemonic sits in.
void drawKeycap(QPainter &p, const QRect &r, const QString &text, const Theme::Tokens &tk,
                const QColor &ink, const QFont &font, bool accent = false)
{
    p.save();
    QPainterPath path;
    path.addRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    p.setPen(QPen(accent ? tk.accent : tk.outline, 1));
    p.setBrush(tk.bgInteractive);
    p.drawPath(path);
    p.setFont(font);
    p.setPen(ink);
    p.drawText(r, Qt::AlignCenter, text);
    p.restore();
}

// Right-aligned shortcut keycaps; returns the left edge used.
int drawShortcut(QPainter &p, const QRect &rowRect, const QString &shortcut, const Theme::Tokens &tk,
                 const QFont &base)
{
    if (shortcut.isEmpty()) return rowRect.right();
    const QFont f = adjusted(base, -2);
    const QFontMetrics fm(f);
    const auto parts = shortcut.split(QChar('+'), Qt::SkipEmptyParts);
    int x = rowRect.right() - 12;
    for (int i = parts.size() - 1; i >= 0; --i) {
        const QString t = parts[i].trimmed();
        const int w = std::max(22, fm.horizontalAdvance(t) + 10);
        const QRect cap(x - w, rowRect.center().y() - 10, w, 20);
        drawKeycap(p, cap, t, tk, tk.ink60, f);
        x = cap.left() - 4;
    }
    return x;
}
} // namespace

class CommandMenu::ResultDelegate : public QStyledItemDelegate {
public:
    ResultDelegate(const QList<CommandItem> *items, QObject *parent) : QStyledItemDelegate(parent), m_items(items) {}

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &index) const override
    {
        return QSize(100, index.data(ResultsModel::HeaderRole).toBool() ? kHeaderH : kRowH);
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override
    {
        const auto &tk = Theme::tokens();
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRect r = opt.rect;
        if (index.data(ResultsModel::HeaderRole).toBool()) {
            QFont f = adjusted(opt.font, -2.5, true);
            f.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
            p->setFont(f);
            p->setPen(tk.ink40);
            p->drawText(r.adjusted(14, 6, -12, 0), Qt::AlignLeft | Qt::AlignVCenter,
                        index.data(Qt::DisplayRole).toString().toUpper());
            p->restore();
            return;
        }
        const int idx = index.data(ResultsModel::ItemIndexRole).toInt();
        if (idx < 0 || idx >= m_items->size()) { p->restore(); return; }
        const auto &it = (*m_items)[idx];
        const bool selected = opt.state & QStyle::State_Selected;
        const bool hover = opt.state & QStyle::State_MouseOver;
        if (selected) {
            p->fillRect(r.adjusted(6, 1, -6, -1), tk.bgRowSelected);
            p->fillRect(QRect(r.left() + 6, r.top() + 1, 2, r.height() - 2), tk.accent);
        } else if (hover) {
            p->fillRect(r.adjusted(6, 1, -6, -1), tk.bgRowHover);
        }
        const QColor ink = it.enabled ? tk.ink100 : tk.ink40;

        // Badge.
        const QRect badge(r.left() + 16, r.center().y() - 11, 22, 22);
        QFont bf = opt.font;
        bf.setBold(true);
        drawKeycap(*p, badge, it.badge, tk, selected && it.enabled ? tk.accent : tk.ink60, bf, selected && it.enabled);

        // Shortcut on the right; the title gets what's left.
        const int rightEdge = drawShortcut(*p, r, it.shortcut, tk, opt.font);

        // Title with the matched letters in amber, then the group dimmed.
        const auto positions = index.data(ResultsModel::PositionsRole).value<QList<int>>();
        QFont tf = opt.font;
        QFont tb = opt.font; tb.setBold(true);
        const QFontMetrics fm(tf), fmb(tb);
        int x = badge.right() + 14;
        const int maxX = rightEdge - 8;
        const int baseY = r.center().y() + fm.ascent() / 2 - 1;
        for (int i = 0; i < it.title.size() && x < maxX; ++i) {
            const bool hit = positions.contains(i);
            p->setFont(hit ? tb : tf);
            p->setPen(hit && it.enabled ? tk.accent : ink);
            const QString ch = it.title.at(i);
            p->drawText(x, baseY, ch);
            x += (hit ? fmb : fm).horizontalAdvance(ch);
        }
        const QFont gf = adjusted(opt.font, -1.5);
        p->setFont(gf);
        p->setPen(tk.ink40);
        const QFontMetrics gfm(gf);
        const QString group = gfm.elidedText(it.group, Qt::ElideRight, std::max(0, maxX - x - 12));
        if (x + 12 < maxX) p->drawText(x + 12, baseY, group);
        p->restore();
    }

private:
    const QList<CommandItem> *m_items;
};

// ── Key grid (which-key) ──────────────────────────────────────────────
class CommandMenu::KeyGrid : public QWidget {
public:
    explicit KeyGrid(QWidget *parent) : QWidget(parent) { setMouseTracking(true); }

    void setNodes(const QList<MenuNode> &nodes) { m_nodes = nodes; m_hover = -1; updateGeometry(); update(); }
    const QList<MenuNode> &nodes() const { return m_nodes; }
    std::function<void(int)> onChosen;

    int columns() const { return std::max(1, std::min(3, width() / 210)); }
    int rowsNeeded() const { const int c = columns(); return (m_nodes.size() + c - 1) / c; }
    QSize sizeHint() const override { return QSize(600, std::max(1, rowsNeeded()) * kCellH + 16); }
    QSize minimumSizeHint() const override { return QSize(200, kCellH + 16); }

protected:
    static constexpr int kCellH = 34;

    QRect cellRect(int i) const
    {
        const int c = columns();
        const int cw = (width() - 16) / c;
        return QRect(8 + (i % c) * cw, 8 + (i / c) * kCellH, cw, kCellH);
    }
    int cellAt(const QPoint &pt) const
    {
        for (int i = 0; i < m_nodes.size(); ++i) if (cellRect(i).contains(pt)) return i;
        return -1;
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int h = cellAt(e->pos());
        if (h != m_hover) { m_hover = h; update(); }
    }
    void leaveEvent(QEvent *) override { m_hover = -1; update(); }
    void mousePressEvent(QMouseEvent *e) override
    {
        const int i = cellAt(e->pos());
        if (i >= 0 && onChosen) onChosen(i);
    }
    void resizeEvent(QResizeEvent *) override { updateGeometry(); }
    void paintEvent(QPaintEvent *) override
    {
        const auto &tk = Theme::tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QFont keyFont = font(); keyFont.setBold(true);
        const QFont hintFont = adjusted(font(), -2);
        const QFontMetrics fm(font()), hfm(hintFont);
        for (int i = 0; i < m_nodes.size(); ++i) {
            const auto &n = m_nodes[i];
            const QRect r = cellRect(i);
            if (i == m_hover) p.fillRect(r.adjusted(0, 1, -4, -1), tk.bgRowHover);
            const QColor ink = n.enabled ? tk.ink100 : tk.ink40;
            const QRect cap(r.left() + 6, r.center().y() - 11, 22, 22);
            drawKeycap(p, cap, n.key.isNull() ? QStringLiteral("·") : QString(n.key), tk,
                       n.enabled ? tk.accent : tk.ink40, keyFont, n.enabled);
            int right = r.right() - 10;
            if (n.isSubmenu()) {
                p.setFont(adjusted(font(), 2, true));
                p.setPen(tk.ink40);
                p.drawText(QRect(right - 12, r.top(), 12, r.height()), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("›"));
                right -= 18;
            } else if (!n.shortcut.isEmpty()) {
                p.setFont(hintFont);
                p.setPen(tk.ink40);
                const int w = hfm.horizontalAdvance(n.shortcut);
                p.drawText(QRect(right - w, r.top(), w, r.height()), Qt::AlignRight | Qt::AlignVCenter, n.shortcut);
                right -= w + 10;
            }
            p.setFont(font());
            p.setPen(ink);
            const int x = cap.right() + 10;
            p.drawText(QRect(x, r.top(), std::max(0, right - x), r.height()), Qt::AlignLeft | Qt::AlignVCenter,
                       fm.elidedText(n.label, Qt::ElideRight, std::max(0, right - x)));
        }
    }

private:
    QList<MenuNode> m_nodes;
    int m_hover = -1;
};

// ── The panel ─────────────────────────────────────────────────────────
namespace {
// The panel is a child that paints its own rounded card so the dialog's
// translucent backdrop shows around it.
class PanelWidget : public QWidget {
public:
    using QWidget::QWidget;
protected:
    void paintEvent(QPaintEvent *) override
    {
        const auto &tk = Theme::tokens();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        p.setPen(QPen(tk.outline, 1));
        p.setBrush(tk.bgPanel);
        p.drawPath(path);
    }
};

// The footer's description elides rather than clips.
class ElideLabel : public QLabel {
public:
    using QLabel::QLabel;
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setFont(font());
        p.setPen(palette().color(QPalette::WindowText));
        const QRect r = contentsRect();
        p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter,
                   fontMetrics().elidedText(text(), Qt::ElideRight, r.width()));
    }
    QSize minimumSizeHint() const override { return QSize(40, QLabel::minimumSizeHint().height()); }
};

QFrame *divider(QWidget *parent)
{
    auto *f = new QFrame(parent);
    f->setFixedHeight(1);
    f->setStyleSheet(QStringLiteral("background:%1;").arg(Theme::tokens().divider.name()));
    return f;
}

const char *kGroupOrder[] = {
    "Show", "Lights", "Cue", "View", "Tools", "File", "Edit", "List", "Help",
};
int groupRank(const QString &g)
{
    for (int i = 0; i < int(std::size(kGroupOrder)); ++i)
        if (g == QLatin1String(kGroupOrder[i]) || g.startsWith(QLatin1String(kGroupOrder[i]) + QStringLiteral(" ›"))) return i;
    return 50;
}
} // namespace

CommandMenu::CommandMenu(const CommandContext &ctx, QWidget *parent)
    : QDialog(parent), m_ctx(ctx)
{
    setObjectName(QStringLiteral("commandMenu"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);

    m_items = buildCommandItems(m_ctx);
    m_tree = buildMenuTree(m_items, m_ctx);

    const auto &tk = Theme::tokens();
    // The theme paints every QWidget bgDeep; the panel paints its own card
    // and everything inside it must be see-through.
    setStyleSheet(QStringLiteral(
        "QWidget { background: transparent; }"
        "QLineEdit#commandMenuInput, QLineEdit#commandMenuInput:focus, QLineEdit#commandMenuInput:hover {"
        "  background: transparent; border: none; padding: 6px 4px; font-size: 18px; color: %1;"
        "  selection-background-color: %2; selection-color: %3; }"
        "QListView#commandMenuList { background: transparent; border: none; outline: 0; }"
        "QLabel#commandMenuCrumb { color: %3; background: %4; border: 1px solid %5; border-radius: 3px;"
        "  padding: 2px 8px; font-size: 12px; font-weight: 600; }"
        "QLabel#commandMenuFooter { color: %6; font-size: 12px; }"
        "QLabel#commandMenuHints { color: %7; font-size: 11px; }")
        .arg(tk.ink100.name(), tk.accent.name(), tk.inkOnAccent.name(), tk.accent.name(),
             tk.accent.name(), tk.ink60.name(), tk.ink40.name()));

    m_panel = new PanelWidget(this);
    m_panel->setObjectName(QStringLiteral("commandMenuPanel"));
    auto *v = new QVBoxLayout(m_panel);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto *header = new QHBoxLayout();
    header->setContentsMargins(14, 8, 14, 8);
    header->setSpacing(8);
    m_crumb = new QLabel(m_panel);
    m_crumb->setObjectName(QStringLiteral("commandMenuCrumb"));
    m_crumb->hide();
    header->addWidget(m_crumb, 0, Qt::AlignVCenter);
    m_input = new QLineEdit(m_panel);
    m_input->setObjectName(QStringLiteral("commandMenuInput"));
    m_input->setPlaceholderText(tr("Search cues, actions, lists…"));
    m_input->setClearButtonEnabled(false);
    m_input->setFrame(false);
    header->addWidget(m_input, 1);
    v->addLayout(header);
    v->addWidget(divider(m_panel));

    m_model = new ResultsModel(&m_items, this);
    m_list = new QListView(m_panel);
    m_list->setObjectName(QStringLiteral("commandMenuList"));
    m_list->setModel(m_model);
    m_list->setItemDelegate(new ResultDelegate(&m_items, m_list));
    m_list->setUniformItemSizes(false);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setMouseTracking(true);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setMinimumHeight(kRowH * 3);
    v->addWidget(m_list, 1);

    m_grid = new KeyGrid(m_panel);
    m_grid->onChosen = [this](int i) {
        const MenuNode *cur = currentNode();
        if (cur && i >= 0 && i < cur->children.size()) enterNode(cur->children[i]);
    };
    m_grid->hide();
    v->addWidget(m_grid, 0);

    v->addWidget(divider(m_panel));
    auto *footer = new QHBoxLayout();
    footer->setContentsMargins(14, 6, 14, 7);
    footer->setSpacing(12);
    m_footerLeft = new ElideLabel(m_panel);
    m_footerLeft->setObjectName(QStringLiteral("commandMenuFooter"));
    m_footerLeft->setTextFormat(Qt::PlainText);
    footer->addWidget(m_footerLeft, 1);
    m_footerRight = new QLabel(m_panel);
    m_footerRight->setObjectName(QStringLiteral("commandMenuHints"));
    m_footerRight->setTextFormat(Qt::PlainText);
    footer->addWidget(m_footerRight, 0, Qt::AlignRight);
    v->addLayout(footer);

    connect(m_input, &QLineEdit::textChanged, this, [this](const QString &) {
        if (m_view == View::Search) refilter();
    });
    connect(m_list, &QListView::clicked, this, [this](const QModelIndex &idx) {
        if (idx.isValid() && !idx.data(ResultsModel::HeaderRole).toBool()) {
            m_list->setCurrentIndex(idx);
            chooseItem(idx.data(ResultsModel::ItemIndexRole).toInt(), false);
        }
    });
    connect(m_list->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { updateFooter(); });
    m_input->installEventFilter(this);

    resize(960, 640);
    setView(View::Search);
}

CommandMenu::~CommandMenu() = default;

std::function<void()> CommandMenu::takePending()
{
    auto fn = std::move(m_pending);
    m_pending = nullptr;
    return fn;
}

// ── Opening ───────────────────────────────────────────────────────────
void CommandMenu::openSearch(const QString &query)
{
    m_path.clear();
    setView(View::Search);
    m_input->setText(query);
    m_input->deselect();
    m_input->end(false);
    refilter();
    m_input->setFocus();
}

void CommandMenu::openKeys(const QString &typed)
{
    m_path.clear();
    setView(View::Keys);
    m_input->clear();
    m_input->setFocus();
    for (const QChar c : typed) {
        if (m_pending) break;
        QKeyEvent ke(QEvent::KeyPress, c.toUpper().unicode(), Qt::NoModifier, QString(c));
        handleKeysViewKey(&ke);
    }
}

bool CommandMenu::runChord(QChar letter)
{
    const auto up = letter.toUpper();
    for (const auto &pin : CommandMenuSettings::pins()) {
        if (pin.letter != up) continue;
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items[i].id != pin.itemId) continue;
            if (!m_items[i].enabled || !m_items[i].run) return false;
            CommandMenuSettings::noteRecent(m_items[i].id);
            m_pending = m_items[i].run;
            return true;
        }
        return false;    // pinned to something that isn't here right now
    }
    return false;
}

// ── Views ─────────────────────────────────────────────────────────────
void CommandMenu::setView(View v)
{
    m_view = v;
    m_pinning = false;
    const bool keys = v == View::Keys;
    m_list->setVisible(!keys);
    m_grid->setVisible(keys);
    m_input->setPlaceholderText(keys ? tr("Press a letter — or type to search")
                                     : tr("Search cues, actions, lists…"));
    if (keys) {
        m_input->clear();
        if (const MenuNode *n = currentNode()) m_grid->setNodes(n->children);
    }
    updateHeader();
    updateFooter();
    layoutPanel();
}

const MenuNode *CommandMenu::currentNode() const
{
    const MenuNode *n = &m_tree;
    for (int i : m_path) {
        if (i < 0 || i >= n->children.size()) return nullptr;
        n = &n->children[i];
    }
    return n;
}

QString CommandMenu::breadcrumb() const
{
    QStringList parts;
    const MenuNode *n = &m_tree;
    for (int i : m_path) {
        if (i < 0 || i >= n->children.size()) break;
        n = &n->children[i];
        parts << n->label;
    }
    return parts.join(QStringLiteral(" › "));
}

void CommandMenu::enterNode(const MenuNode &node)
{
    if (node.isSubmenu()) {
        const MenuNode *cur = currentNode();
        if (!cur) return;
        for (int i = 0; i < cur->children.size(); ++i) {
            if (cur->children[i].id == node.id) {
                m_path.append(i);
                m_grid->setNodes(cur->children[i].children);
                updateHeader();
                updateFooter();
                layoutPanel();
                return;
            }
        }
        return;
    }
    if (node.opensSearch) {
        m_path.clear();
        setView(View::Search);
        m_input->clear();
        refilter();
        return;
    }
    if (node.item >= 0) chooseItem(node.item, false);
}

void CommandMenu::goUp()
{
    if (m_path.isEmpty()) return;
    m_path.removeLast();
    if (const MenuNode *n = currentNode()) m_grid->setNodes(n->children);
    updateHeader();
    updateFooter();
    layoutPanel();
}

void CommandMenu::updateHeader()
{
    const QString crumb = breadcrumb();
    if (m_view == View::Keys) {
        m_crumb->setText(crumb.isEmpty() ? tr("Menu") : crumb);
        m_crumb->show();
    } else {
        m_crumb->hide();
    }
}

void CommandMenu::updateFooter()
{
    if (m_pinning) {
        const CommandItem *it = currentItem();
        m_footerLeft->setText(tr("Pin “%1” to Leader + ?  — press a letter. Backspace unpins, Esc cancels.")
                                  .arg(it ? it->title : QString()));
        m_footerRight->clear();
        return;
    }
    if (m_view == View::Keys) {
        QStringList pins;
        for (const auto &p : CommandMenuSettings::pins())
            pins << QStringLiteral("%1 %2").arg(p.letter, p.title);
        m_footerLeft->setText(pins.isEmpty()
            ? tr("Hold the leader and press a letter to jump here directly.")
            : tr("Pinned (hold leader): %1").arg(pins.join(QStringLiteral("  ·  "))));
        m_footerRight->setText(m_path.isEmpty() ? tr("letter  Choose   ·   type  Search   ·   Esc  Close")
                                                : tr("letter  Choose   ·   ⌫  Back   ·   Esc  Close"));
        return;
    }
    const CommandItem *it = currentItem();
    if (!it) {
        m_footerLeft->setText(m_model->rowCount() == 0 ? tr("Nothing matches.") : QString());
        m_footerRight->setText(tr("↵  Run   ·   Esc  Close"));
        return;
    }
    m_footerLeft->setText(it->enabled ? it->description : tr("%1 — not available right now").arg(it->description));
    QStringList hints;
    hints << tr("↵  Run");
    if (!it->secondaryLabel.isEmpty()) hints << tr("Ctrl+↵  %1").arg(it->secondaryLabel);
    hints << tr("Ctrl+P  Pin") << tr("Esc  Close");
    m_footerRight->setText(hints.join(QStringLiteral("   ·   ")));
}

QString CommandMenu::footerText() const { return m_footerLeft->text(); }

void CommandMenu::layoutPanel()
{
    const int w = std::min(680, width() - 48);
    int h;
    if (m_view == View::Keys) {
        h = 50 + 1 + m_grid->sizeHint().height() + 1 + 34;
    } else {
        int content = 0;
        for (const auto &r : m_model->rows()) content += r.header ? kHeaderH : kRowH;
        content = std::clamp(content + 8, kRowH * 3, kRowH * 10 + 8);
        h = 50 + 1 + content + 1 + 34;
    }
    h = std::min(h, height() - 48);
    const int top = std::max(16, int(height() * 0.14));
    m_panel->setGeometry((width() - w) / 2, top, w, h);
}

void CommandMenu::showEvent(QShowEvent *event)
{
    if (QWidget *p = parentWidget()) {
        QWidget *w = p->window();
        setGeometry(QRect(w->mapToGlobal(QPoint(0, 0)), w->size()));
    }
    QDialog::showEvent(event);
    layoutPanel();
    m_input->setFocus();
    m_input->deselect();
}

void CommandMenu::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    layoutPanel();
}

void CommandMenu::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    QColor dim = Theme::tokens().bgDeep;
    dim.setAlpha(90);      // a gentle dim: the cue list stays readable behind the panel
    p.fillRect(rect(), dim);
}

void CommandMenu::mousePressEvent(QMouseEvent *event)
{
    if (!m_panel->geometry().contains(event->pos())) reject();
    else QDialog::mousePressEvent(event);
}

void CommandMenu::keyPressEvent(QKeyEvent *event)
{
    // Keys that reach the dialog itself (focus somewhere odd) still work.
    if (m_view == View::Keys ? handleKeysViewKey(event) : handleSearchViewKey(event)) return;
    if (event->key() == Qt::Key_Escape) { reject(); return; }
    QDialog::keyPressEvent(event);
}

// ── Filtering ─────────────────────────────────────────────────────────
void CommandMenu::refilter()
{
    const QString q = m_input->text().trimmed();
    const QStringList recent = CommandMenuSettings::recent();
    auto recentRank = [&](const QString &id) { return recent.indexOf(id); };

    QList<ResultsModel::Row> rows;
    if (q.isEmpty()) {
        // Recent picks first, then everything by group.
        QList<int> order;
        for (const auto &id : recent)
            for (int i = 0; i < m_items.size(); ++i)
                if (m_items[i].id == id && m_items[i].enabled) { order << i; break; }
        if (!order.isEmpty()) {
            ResultsModel::Row h; h.header = true; h.headerText = tr("Recent");
            rows << h;
            for (int i : order.mid(0, 6)) { ResultsModel::Row r; r.item = i; rows << r; }
        }
        QList<int> rest;
        for (int i = 0; i < m_items.size(); ++i) rest << i;
        std::stable_sort(rest.begin(), rest.end(), [&](int a, int b) {
            return groupRank(m_items[a].group) < groupRank(m_items[b].group);
        });
        QString lastGroup;
        for (int i : rest) {
            if (m_items[i].group != lastGroup) {
                lastGroup = m_items[i].group;
                ResultsModel::Row h; h.header = true; h.headerText = lastGroup;
                rows << h;
            }
            ResultsModel::Row r; r.item = i; rows << r;
        }
    } else {
        bool numOk = false;
        const double qNum = q.toDouble(&numOk);
        struct Scored { int item; int score; QList<int> positions; };
        QList<Scored> scored;
        for (int i = 0; i < m_items.size(); ++i) {
            const auto &it = m_items[i];
            const auto t = fuzzyMatch(q, it.title);
            const auto k = fuzzyMatch(q, it.keywords);
            const auto g = fuzzyMatch(q, it.group);
            int best = -1;
            QList<int> pos;
            if (t.matched) { best = t.score; pos = t.positions; }
            if (k.matched && k.score * 6 / 10 > best) { best = k.score * 6 / 10; pos.clear(); }
            if (g.matched && g.score / 2 > best)      { best = g.score / 2; pos.clear(); }
            if (best < 0) continue;
            if (numOk && it.number >= 0 && std::abs(it.number - qNum) < 1e-9) best += 400;
            const int rr = recentRank(it.id);
            if (rr >= 0 && rr < 10) best += 120 - 10 * rr;
            if (!it.enabled) best -= 500;
            scored.append({i, best, pos});
        }
        std::stable_sort(scored.begin(), scored.end(), [&](const Scored &a, const Scored &b) {
            if (a.score != b.score) return a.score > b.score;
            return m_items[a.item].title.localeAwareCompare(m_items[b.item].title) < 0;
        });
        for (const auto &s : scored.mid(0, 200)) {
            ResultsModel::Row r; r.item = s.item; r.positions = s.positions; rows << r;
        }
    }
    m_model->setRows(rows);
    for (int i = 0; i < m_model->rowCount(); ++i) {
        if (!m_model->rows()[i].header) { m_list->setCurrentIndex(m_model->index(i)); break; }
    }
    m_list->scrollToTop();
    updateFooter();
    layoutPanel();
}

QStringList CommandMenu::visibleTitles() const
{
    QStringList out;
    for (const auto &r : m_model->rows())
        if (!r.header && r.item >= 0) out << m_items[r.item].title;
    return out;
}

int CommandMenu::currentRow() const
{
    const auto cur = m_list->currentIndex();
    if (!cur.isValid()) return -1;
    int n = -1;
    for (int i = 0; i <= cur.row() && i < m_model->rowCount(); ++i)
        if (!m_model->rows()[i].header) ++n;
    return n;
}

const CommandItem *CommandMenu::currentItem() const
{
    if (m_view != View::Search) return nullptr;
    const auto cur = m_list->currentIndex();
    if (!cur.isValid() || cur.data(ResultsModel::HeaderRole).toBool()) return nullptr;
    const int idx = cur.data(ResultsModel::ItemIndexRole).toInt();
    return idx >= 0 && idx < m_items.size() ? &m_items[idx] : nullptr;
}

QList<MenuNode> CommandMenu::visibleNodes() const
{
    return m_grid->nodes();
}

// ── Keys ──────────────────────────────────────────────────────────────
void CommandMenu::typeKey(int key, Qt::KeyboardModifiers mods, const QString &text)
{
    QKeyEvent ke(QEvent::KeyPress, key, mods, text);
    QApplication::sendEvent(m_input, &ke);
}

bool CommandMenu::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (m_view == View::Keys ? handleKeysViewKey(ke) : handleSearchViewKey(ke)) return true;
    }
    // A dialog hands its first focus out as a Tab, which makes QLineEdit
    // select everything; the query should stay as typed, caret at the end.
    if (watched == m_input && event->type() == QEvent::FocusIn) {
        QMetaObject::invokeMethod(m_input, [this] { m_input->deselect(); m_input->end(false); }, Qt::QueuedConnection);
    }
    return QDialog::eventFilter(watched, event);
}

bool CommandMenu::handleKeysViewKey(QKeyEvent *ke)
{
    const int key = ke->key();
    const auto mods = ke->modifiers() & ~Qt::KeypadModifier;
    if (key == Qt::Key_Escape) { reject(); return true; }
    if (key == Qt::Key_Backspace) { goUp(); return true; }
    if (mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) return false;
    const QString text = ke->text();
    if (text.isEmpty() || !text.at(0).isPrint()) return key == Qt::Key_Return || key == Qt::Key_Enter;
    const QChar typed = text.at(0).toUpper();
    if (const MenuNode *cur = currentNode()) {
        for (const auto &child : cur->children) {
            if (!child.key.isNull() && child.key == typed) {
                enterNode(child);
                return true;
            }
        }
    }
    if (typed == QChar(' ')) return true;      // Space is GO everywhere else; it does nothing here
    // Not a mnemonic: drop into the search with it.
    m_path.clear();
    setView(View::Search);
    m_input->setText(text);
    m_input->deselect();
    m_input->end(false);
    refilter();
    return true;
}

bool CommandMenu::handleSearchViewKey(QKeyEvent *ke)
{
    const int key = ke->key();
    const auto mods = ke->modifiers() & ~Qt::KeypadModifier;

    if (m_pinning) {
        if (key == Qt::Key_Escape) { m_pinning = false; updateFooter(); return true; }
        const CommandItem *it = currentItem();
        if (key == Qt::Key_Backspace) {
            if (it) for (const auto &p : CommandMenuSettings::pins()) if (p.itemId == it->id) CommandMenuSettings::removePin(p.letter);
            m_pinning = false;
            updateFooter();
            m_footerLeft->setText(tr("Unpinned."));
            return true;
        }
        const QString text = ke->text();
        if (it && !text.isEmpty() && text.at(0).isLetterOrNumber()) {
            CommandMenuSettings::setPin(text.at(0).toUpper(), it->id, it->title);
            m_pinning = false;
            updateFooter();
            m_footerLeft->setText(tr("Pinned “%1” to Leader + %2.").arg(it->title, text.at(0).toUpper()));
        }
        return true;
    }

    auto moveBy = [this](int delta) {
        const int n = m_model->rowCount();
        if (n == 0) return;
        int row = m_list->currentIndex().isValid() ? m_list->currentIndex().row() : -1;
        for (int step = 0; step < n; ++step) {
            row += delta;
            if (row < 0) row = 0;
            if (row >= n) row = n - 1;
            if (!m_model->rows()[row].header) break;
            if ((delta < 0 && row == 0) || (delta > 0 && row == n - 1)) break;
        }
        if (m_model->rows()[row].header) return;
        m_list->setCurrentIndex(m_model->index(row));
        m_list->scrollTo(m_model->index(row));
    };
    switch (key) {
    case Qt::Key_Down:     moveBy(1);  return true;
    case Qt::Key_Up:       moveBy(-1); return true;
    case Qt::Key_PageDown: moveBy(8);  return true;
    case Qt::Key_PageUp:   moveBy(-8); return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        chooseCurrent(mods & Qt::ControlModifier);
        return true;
    case Qt::Key_Escape:
        reject();
        return true;
    case Qt::Key_Backspace:
        if (m_input->text().isEmpty()) { m_path.clear(); setView(View::Keys); return true; }
        return false;
    case Qt::Key_P:
        if (mods == Qt::ControlModifier) {
            if (currentItem()) { m_pinning = true; updateFooter(); }
            return true;
        }
        return false;
    default:
        return false;
    }
}

void CommandMenu::chooseCurrent(bool secondary)
{
    const auto cur = m_list->currentIndex();
    if (!cur.isValid() || cur.data(ResultsModel::HeaderRole).toBool()) return;
    chooseItem(cur.data(ResultsModel::ItemIndexRole).toInt(), secondary);
}

void CommandMenu::chooseItem(int index, bool secondary)
{
    if (index < 0 || index >= m_items.size()) return;
    const auto &it = m_items[index];
    if (!it.enabled) {
        m_footerLeft->setText(tr("“%1” isn't available right now.").arg(it.title));
        return;
    }
    auto fn = secondary ? it.secondary : it.run;
    if (!fn) {
        if (secondary) fn = it.run;
        if (!fn) return;
    }
    CommandMenuSettings::noteRecent(it.id);
    m_pending = fn;
    accept();
}

} // namespace quewi::ui
