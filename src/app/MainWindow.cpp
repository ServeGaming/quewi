#include "MainWindow.h"

#include <QLabel>
#include <QStatusBar>

namespace quewi {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("quewi");
    resize(1280, 800);

    auto *placeholder = new QLabel(
        "quewi — pre-alpha skeleton.\n\n"
        "See design.md and structure.md for the roadmap.",
        this);
    placeholder->setAlignment(Qt::AlignCenter);
    setCentralWidget(placeholder);

    statusBar()->showMessage("Ready");
}

MainWindow::~MainWindow() = default;

} // namespace quewi
