#pragma once

#include "audio/AudioFile.h"

#include <QImage>
#include <memory>

namespace quewi::ui::spectro {

// Build a log-frequency heat-map spectrogram covering the ENTIRE file.
//
// Designed to run on a worker thread: it operates only on the immutable
// AudioBufferSnapshot (which shares the decoded buffer via shared_ptr), so
// concurrent edits or re-loads on the GUI thread can't pull the samples out
// from under it. The returned QImage is `cols × rows`, where each column is
// one FFT frame across the file and each row maps to a log-spaced frequency
// from ~20 Hz (bottom) to Nyquist (top). Returns a null QImage when the
// snapshot is empty or shorter than one FFT window.
//
// `rows`    — vertical resolution (frequency bins after log remap).
// `maxCols` — horizontal cap; the hop size is widened for long files so the
//             build stays bounded (a ~5 min song lands well under maxCols).
QImage buildFullFile(const std::shared_ptr<const audio::AudioBufferSnapshot> &snap,
                     int rows = 512, int maxCols = 6000);

} // namespace quewi::ui::spectro
