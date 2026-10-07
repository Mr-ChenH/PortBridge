#pragma once
#include <QMainWindow>
#include <memory>
namespace portbridge {
class SessionController;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(SessionController* controller, QWidget* parent = nullptr);
    ~MainWindow() override;
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
}
