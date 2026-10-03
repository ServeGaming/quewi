#include <QTest>
#include <QApplication>
#include <QLabel>
#include <QPointer>
#include <QPushButton>

#include "audio/AudioEditorModel.h"
#include "audio/AudioEffect.h"
#include "ui/EffectsRackWidget.h"
#include "ui/ParametricEqDialog.h"

using namespace quewi;

// Audit A7: removing a track in the audio editor left the effects rack (and
// the window's active track) pointing at the freed track, so the next rack
// rebuild, effect edit or Play touched freed memory.
class AudioEditorTests : public QObject {
    Q_OBJECT

    static QString rackHeading(const ui::EffectsRackWidget &rack)
    {
        for (auto *l : rack.findChildren<QLabel *>())
            if (l->text().startsWith(QStringLiteral("TRACK")) || l->text().startsWith(QStringLiteral("NO TRACK")))
                return l->text();
        return {};
    }

private slots:
    void rackLetsGoOfARemovedTrack()
    {
        audio::AudioEditorModel model;
        model.addTrack(QStringLiteral("Keep"));
        auto *doomed = model.addTrack(QStringLiteral("Doomed"));
        doomed->addEffect(audio::AudioEffect::Type::Eq);
        doomed->addEffect(audio::AudioEffect::Type::Reverb);

        ui::EffectsRackWidget rack;
        rack.setTrack(doomed);
        QCOMPARE(rackHeading(rack), QStringLiteral("TRACK · Doomed"));

        model.removeTrack(1);
        QCOMPARE(rackHeading(rack), QStringLiteral("NO TRACK SELECTED"));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); // old cards go

        // Rebinding afterwards works and the rack is usable again.
        rack.setTrack(model.track(0));
        QCOMPARE(rackHeading(rack), QStringLiteral("TRACK · Keep"));
        model.track(0)->addEffect(audio::AudioEffect::Type::Delay);   // rebuilds from changed()
    }

    void effectEditorClosesWhenItsEffectGoes()
    {
        audio::AudioEditorModel model;
        model.addTrack(QStringLiteral("One"));
        auto *t = model.addTrack(QStringLiteral("Two"));
        t->addEffect(audio::AudioEffect::Type::Eq);

        ui::EffectsRackWidget rack;
        rack.setTrack(t);
        QPushButton *open = nullptr;
        for (auto *b : rack.findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("Open Editor")) open = b;
        QVERIFY(open);
        open->click();
        auto dialogs = rack.findChildren<ui::ParametricEqDialog *>();
        QCOMPARE(dialogs.size(), 1);
        QPointer<ui::ParametricEqDialog> dlg = dialogs.first();
        QVERIFY(dlg->isVisible());

        model.removeTrack(1);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY2(dlg.isNull() || !dlg->isVisible(), "editor closed with its effect");
    }
};

QTEST_MAIN(AudioEditorTests)
#include "test_audio_editor.moc"
