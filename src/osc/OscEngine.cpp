#include "osc/OscEngine.h"

namespace quewi::osc {

OscEngine::OscEngine(QObject *parent) : QObject(parent) {}
OscEngine::~OscEngine() = default;

QByteArray Codec::encode(const Message &) { return {}; }
QByteArray Codec::encode(const Bundle &)  { return {}; }
std::optional<Element> Codec::decode(const QByteArray &) { return std::nullopt; }

} // namespace quewi::osc
