#pragma once
#include <QMainWindow>
#include <memory>
class QCloseEvent;
namespace portbridge {
class SessionController;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(SessionController* controller, QWidget* parent = nullptr);
    ~MainWindow() override;
protected:
    void closeEvent(QCloseEvent*) override;
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
}
