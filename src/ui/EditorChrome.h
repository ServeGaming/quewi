#pragma once

#include <QIcon>
#include <QString>

#include <functional>

class QLabel;
class QToolBar;
class QWidget;

namespace quewi::ui {

// Shared look for the editor windows (audio editor, video editor): the line
// icons, toolbar section labels and dividers, the toolbar's style, and the
// cue header strip under it.

// 18×18 line icons in the theme's primary ink: play, pause, stop, loop,
// select, razor, zoomIn, zoomOut, zoomFit, addTrack, undo, redo, render,
// waveform, spectrogram, toIn, toOut, frameBack, frameForward, cut, keep.
QIcon makeEditorIcon(const QString &name);
QWidget *toolbarDivider(QWidget *parent);
QLabel  *sectionLabel(const QString &text, QWidget *parent);
void     styleEditorToolBar(QToolBar *tb);

// The header strip: cue number (accent), a small caps kind line ("AUDIO CUE")
// over the cue name, and a right-aligned meta readout. Returns the strip;
// the three labels come back through the out-parameters.
struct EditorHeader {
    QWidget *widget = nullptr;
    QLabel  *number = nullptr;
    QLabel  *name   = nullptr;
    QLabel  *meta   = nullptr;
};
EditorHeader makeEditorHeader(const QString &kindCaps, QWidget *parent);

// Space plays / pauses an editor window wherever the keyboard focus is inside
// it. A plain key handler loses Space to whatever has focus: a focused button
// pressed itself again, a number box (In, Level…) swallowed it. Only a real
// text field (a name, a desk command) keeps its spaces. Lives as long as
// `window`.
void installEditorSpaceKey(QWidget *window, std::function<void()> togglePlay);

} // namespace quewi::ui
