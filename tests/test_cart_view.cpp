#include <QTest>
#include <QSignalSpy>

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
};

QTEST_MAIN(CartViewTests)
#include "test_cart_view.moc"
