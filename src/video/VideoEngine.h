#pragma once

#include <QObject>

namespace quewi::video {

// FFmpeg decode → Qt RHI texture → multi-output windows.
// Phase 5; see UX.md §18 and structure.md §4.
class VideoEngine : public QObject {
    Q_OBJECT
public:
    explicit VideoEngine(QObject *parent = nullptr);
    ~VideoEngine() override;
};

} // namespace quewi::video
