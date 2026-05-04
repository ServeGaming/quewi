#include "show/ShowFile.h"

namespace quewi::show {

ShowFile::ShowFile(QObject *parent) : QObject(parent) {}
ShowFile::~ShowFile() = default;

bool ShowFile::open(const QString &) { return false; }
bool ShowFile::save() { return false; }

} // namespace quewi::show
