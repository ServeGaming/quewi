#pragma once

#include <QObject>

namespace quewi::lighting {

// Hand-written sACN (E1.31), Art-Net, and DMX-USB outputs.
// Tick rate 44 Hz to match DMX refresh.
class LightingEngine : public QObject {
    Q_OBJECT
public:
    explicit LightingEngine(QObject *parent = nullptr);
    ~LightingEngine() override;
};

} // namespace quewi::lighting
