#include "ui/AudioEditorWindow.h"

#include "audio/AudioCue.h"
#include "audio/AudioFile.h"
#include "ui/WaveformWidget.h"

#include <QDialogButtonBox>
#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

namespace quewi::ui {

AudioEditorWindow::AudioEditorWindow(audio::AudioCue *cue, QWidget *parent)
    : QDialog(parent)
    , m_cue(cue)
{
    setWindowTitle(cue ? tr("Audio Editor — %1").arg(cue->name()) : tr("Audio Editor"));
    resize(960, 540);
    setAttribute(Qt::WA_DeleteOnClose);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(20, 20, 20, 20);
    outer->setSpacing(12);

    // Header
    auto *title = new QLabel(tr("Audio Editor"), this);
    auto titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 6.0);
    titleFont.setBold(true);
    title->setFont(titleFont);
    outer->addWidget(title);

    auto *subtitle = new QLabel(
        tr("Coming in Phase 9 — full Audacity-like editing inside quewi."),
        this);
    subtitle->setStyleSheet(QStringLiteral("color:#A8AEBA;"));
    outer->addWidget(subtitle);

    // Waveform preview of the actual cue
    if (cue) {
        cue->prepare();
        auto *wave = new WaveformWidget(this);
        wave->setMinimumHeight(220);
        wave->setAudioFile(cue->audioFile());
        outer->addWidget(wave);

        if (auto file = cue->audioFile(); file
            && file->state() == audio::AudioFile::State::Loaded) {
            auto *meta = new QLabel(
                tr("%1   ·   %2 Hz   ·   %3 ch   ·   %4 s")
                    .arg(cue->filePath())
                    .arg(file->sampleRate())
                    .arg(file->channelCount())
                    .arg(QString::number(file->durationSeconds(), 'f', 2)),
                this);
            meta->setStyleSheet(QStringLiteral("color:#A8AEBA;"));
            outer->addWidget(meta);
        }
    }

    auto *divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet(QStringLiteral("color:#23262E;"));
    outer->addWidget(divider);

    // Feature list — what the real editor will do
    auto *featuresHeader = new QLabel(tr("Planned features"), this);
    auto fhFont = featuresHeader->font();
    fhFont.setBold(true);
    featuresHeader->setFont(fhFont);
    outer->addWidget(featuresHeader);

    auto *features = new QLabel(this);
    features->setTextFormat(Qt::RichText);
    features->setText(
        tr("<ul style='margin-top:0;'>"
           "<li><b>Multi-track timeline</b> — multiple regions on the "
                "same cue, freely arrangeable.</li>"
           "<li><b>Sample-buffer editing</b> with full undo: cut, copy, "
                "paste, delete, silence, gain envelope, fades with "
                "curve picker (linear, equal-power, S-curve, custom).</li>"
           "<li><b>Effects rack</b>: EQ, compressor, reverb, delay, "
                "limiter — applied per-region or per-track.</li>"
           "<li><b>Spectral view</b> for surgical noise removal and "
                "click repair.</li>"
           "<li><b>Render to file</b> — bounce all edits to a flat WAV "
                "so the live cue plays a deterministic file.</li>"
           "<li><b>Undo across the editor's session</b>, separate from "
                "the main show file's undo stack.</li>"
           "</ul>"));
    features->setWordWrap(true);
    outer->addWidget(features);

    auto *roadmap = new QLabel(
        tr("<i>Tracked in the QLab-parity roadmap as <b>Phase 9: "
           "Full Audio Editor</b>. Today the inspector pane already "
           "supports trim, pan, gain, fade in/out, normalize, and "
           "reverse — the full editor extends those primitives with a "
           "proper timeline UI.</i>"),
        this);
    roadmap->setStyleSheet(QStringLiteral("color:#A8AEBA;"));
    roadmap->setWordWrap(true);
    outer->addWidget(roadmap);

    outer->addStretch(1);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(bb);
}

AudioEditorWindow::~AudioEditorWindow() = default;

} // namespace quewi::ui
