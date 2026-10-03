#include <QTest>

#include "osc/OscPattern.h"

using quewi::osc::Pattern;

class OscPatternTests : public QObject {
    Q_OBJECT
private slots:
    void exactLiteral()
    {
        QVERIFY( Pattern::matches(u"/foo/bar",   u"/foo/bar"));
        QVERIFY(!Pattern::matches(u"/foo/bar",   u"/foo/baz"));
        QVERIFY(!Pattern::matches(u"/foo",       u"/foo/bar"));
        QVERIFY(!Pattern::matches(u"/foo/bar",   u"/foo"));
    }

    void questionWildcard()
    {
        QVERIFY( Pattern::matches(u"/foo/?ar",   u"/foo/bar"));
        QVERIFY( Pattern::matches(u"/foo/?ar",   u"/foo/car"));
        QVERIFY(!Pattern::matches(u"/foo/?ar",   u"/foo/bbar"));
    }

    void starWildcard()
    {
        QVERIFY( Pattern::matches(u"/foo/*",     u"/foo/anything"));
        QVERIFY( Pattern::matches(u"/foo/b*r",   u"/foo/bar"));
        QVERIFY( Pattern::matches(u"/foo/b*r",   u"/foo/booster"));
        QVERIFY(!Pattern::matches(u"/foo/*",     u"/foo/bar/baz")); // * doesn't cross /
    }

    void characterClass()
    {
        QVERIFY( Pattern::matches(u"/cue/[abc]", u"/cue/a"));
        QVERIFY( Pattern::matches(u"/cue/[abc]", u"/cue/c"));
        QVERIFY(!Pattern::matches(u"/cue/[abc]", u"/cue/d"));
        QVERIFY( Pattern::matches(u"/cue/[a-z]", u"/cue/m"));
        QVERIFY(!Pattern::matches(u"/cue/[a-z]", u"/cue/M"));
        QVERIFY( Pattern::matches(u"/cue/[!a-z]", u"/cue/M"));
        QVERIFY(!Pattern::matches(u"/cue/[!a-z]", u"/cue/m"));
    }

    void alternatives()
    {
        QVERIFY( Pattern::matches(u"/cue/{go,stop}", u"/cue/go"));
        QVERIFY( Pattern::matches(u"/cue/{go,stop}", u"/cue/stop"));
        QVERIFY(!Pattern::matches(u"/cue/{go,stop}", u"/cue/pause"));
        QVERIFY( Pattern::matches(u"/{a,b}/{1,2}",   u"/a/2"));
    }

    void descendantDoubleSlash()
    {
        QVERIFY( Pattern::matches(u"/foo//bar",   u"/foo/bar"));
        QVERIFY( Pattern::matches(u"/foo//bar",   u"/foo/x/bar"));
        QVERIFY( Pattern::matches(u"/foo//bar",   u"/foo/a/b/c/bar"));
        QVERIFY(!Pattern::matches(u"/foo//bar",   u"/foo/baz"));
    }

    // What a "/quewi/notify/*" subscription is widened to (MainWindow::
    // pushOscNotify): this level and every level below it.
    void notifySubscriptionsReachDeeperAddresses()
    {
        QVERIFY( Pattern::matches(u"/quewi/notify//*", u"/quewi/notify/cue/state"));
        QVERIFY( Pattern::matches(u"/quewi/notify//*", u"/quewi/notify/trigger/fired"));
        QVERIFY( Pattern::matches(u"/quewi/notify//*", u"/quewi/notify/heartbeat"));
        QVERIFY(!Pattern::matches(u"/quewi/notify//*", u"/quewi/reply/cue"));
    }
};

QTEST_MAIN(OscPatternTests)
#include "test_osc_pattern.moc"
