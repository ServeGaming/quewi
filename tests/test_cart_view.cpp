#include <QTest>
#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>
#include <QSignalSpy>
#include <QTimer>

#include <algorithm>

#include "core/CartGrid.h"
#include "core/CueList.h"
#include "core/Workspace.h"
#include "cues/MemoCue.h"
#include "ui/CartView.h"

using namespace quewi;

// The soundboard view against a real CartGrid. Resizing the board rebuilds
// every pad widget and every keybind shortcut; Matthew hit what looked like a
// crash changing rows/columns, so this drives exactly that — bound pads with
// keys, shrink past them, grow, flip layers — and then proves the board still
// fires the right cue.
class CartViewTests : public QObject {
    Q_OBJECT

    struct Board {
        core::Workspace   ws;
        core::CueList    *list = nullptr;
        QList<QUuid>      ids;
    };

    static void fill(Board &b, int n)
    {
        b.list = b.ws.addCueList(std::make_unique<core::CueList>(QStringLiteral("Board")));
        for (int i = 0; i < n; ++i) {
            auto c = std::make_unique<cues::MemoCue>();
            c->setField(QStringLiteral("name"), QStringLiteral("SFX %1").arg(i + 1));
            b.ids.append(c->id());
            b.list->insertCue(i, std::move(c));
        }
    }

    // Let deleteLater()'d pads actually die, as they would in the app.
    static void settle() { QTest::qWait(5); }

private slots:
    void resizeRebuildsSafely()
    {
        Board b;
        fill(b, 12);
        auto *cart = b.ws.cart();
        cart->setSize(4, 6);
        for (int i = 0; i < 12; ++i) {
            cart->setCell(i / 6, i % 6, b.ids[i]);
            cart->setCellHotkey(i / 6, i % 6, QString(QChar('A' + i)));
        }

        ui::CartView view;
        view.setWorkspace(&b.ws);
        view.resize(900, 600);
        view.show();
        settle();

        // Shrink past bound pads, grow, go to the extremes the Resize dialog
        // allows, and back. Each step deletes and recreates every pad.
        const QList<QPair<int,int>> sizes = { {2, 3}, {8, 12}, {1, 1}, {16, 12},
                                              {3, 2}, {4, 6} };
        for (const auto &[r, c] : sizes) {
            cart->setSize(r, c);
            settle();
            QCOMPARE(cart->rows(), r);
            QCOMPARE(cart->cols(), c);
        }

        // Layers + resize interleaved.
        cart->addLayer();
        cart->setSize(3, 3);
        settle();
        cart->setActiveLayer(0);
        settle();
        cart->setSize(5, 5);
        settle();

        // After all that, pad (0,0) still fires its own cue.
        QSignalSpy fired(&view, &ui::CartView::fireRequested);
        QVERIFY(view.firePadAt(0, 0));
        QCOMPARE(fired.count(), 1);
        auto *cue = fired.first().first().value<cues::Cue *>();
        QVERIFY(cue);
        QCOMPARE(cue->id(), b.ids[0]);
        // A pad dropped by an earlier shrink is gone, not dangling.
        QVERIFY(!view.firePadAt(3, 5));
    }

    void boardEditsMarkShowUnsaved()
    {
        Board b;
        fill(b, 2);
        b.ws.markClean();
        QVERIFY(!b.ws.isDirty());
        b.ws.cart()->setCellHotkey(0, 0, QStringLiteral("Q"));
        QVERIFY2(b.ws.isDirty(), "a keybind edit must raise the save prompt");

        b.ws.markClean();
        b.ws.cart()->setActiveLayer(0);   // flipping pages is not an edit
        b.ws.cart()->addLayer();          // ...but adding one is
        QVERIFY(b.ws.isDirty());
        b.ws.markClean();
        b.ws.cart()->setActiveLayer(0);
        QVERIFY2(!b.ws.isDirty(), "switching layers mid-show must not dirty the show");

        b.ws.cart()->setSize(2, 2);
        QVERIFY(b.ws.isDirty());
    }

    // The Resize dialog warns with this count before shrinking.
    void padsOutsideCountsEveryLayer()
    {
        Board b;
        fill(b, 3);
        auto *cart = b.ws.cart();
        cart->setSize(4, 4);
        cart->setCell(0, 0, b.ids[0]);
        cart->setCell(3, 3, b.ids[1]);
        cart->addLayer();
        cart->setCell(2, 3, b.ids[2]);
        QCOMPARE(cart->padsOutside(4, 4), 0);
        QCOMPARE(cart->padsOutside(2, 4), 2);   // (3,3) on layer 1, (2,3) on layer 2
        QCOMPARE(cart->padsOutside(1, 1), 2);
        QCOMPARE(cart->padsOutside(4, 3), 2);
    }

    // Right-click an EMPTY pad → "Import from URL…" asks the host to import
    // onto that pad; a bound pad's menu doesn't offer it.
    void emptyPadOffersImportFromUrl()
    {
        Board b;
        fill(b, 1);
        auto *cart = b.ws.cart();
        cart->setSize(1, 2);
        cart->setCell(0, 0, b.ids[0]);          // (0,0) bound, (0,1) empty

        ui::CartView view;
        view.setWorkspace(&b.ws);
        view.resize(600, 300);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        settle();

        QList<QWidget *> pads;
        for (auto *w : view.findChildren<QWidget *>())
            if (QByteArray(w->metaObject()->className()).endsWith("CartPad") && w->isVisible())
                pads.append(w);
        QCOMPARE(pads.size(), 2);
        std::sort(pads.begin(), pads.end(), [](QWidget *a, QWidget *b) {
            return a->mapTo(a->window(), QPoint()).x() < b->mapTo(b->window(), QPoint()).x();
        });

        // Open a pad's context menu and report the action texts it shows;
        // trigger `pick` if it's there.
        auto menuOf = [](QWidget *pad, const QString &pick) {
            QStringList texts;
            QTimer::singleShot(50, [&texts, pick] {
                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                if (!menu) return;
                QAction *hit = nullptr;
                for (auto *a : menu->actions()) {
                    texts << a->text();
                    if (a->text() == pick) hit = a;
                }
                if (hit) hit->trigger();
                menu->close();
            });
            QContextMenuEvent ev(QContextMenuEvent::Mouse, pad->rect().center(),
                                 pad->mapToGlobal(pad->rect().center()));
            QApplication::sendEvent(pad, &ev);
            return texts;
        };

        QSignalSpy spy(&view, &ui::CartView::importUrlRequested);
        const QStringList bound = menuOf(pads[0], QString());
        QVERIFY2(!bound.isEmpty(), "bound pad context menu didn't open");
        QVERIFY(!bound.contains(QStringLiteral("Import from URL…")));

        const QStringList empty = menuOf(pads[1], QStringLiteral("Import from URL…"));
        QVERIFY(empty.contains(QStringLiteral("Choose sound file…")));
        QVERIFY(empty.contains(QStringLiteral("Import from URL…")));
        QTRY_COMPARE(spy.count(), 1);           // queued
        QCOMPARE(spy.first().at(0).toInt(), 0);
        QCOMPARE(spy.first().at(1).toInt(), 1);
    }
};

QTEST_MAIN(CartViewTests)
#include "test_cart_view.moc"
