#pragma once

#include <QMainWindow>

namespace quewi {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
};

} // namespace quewi
