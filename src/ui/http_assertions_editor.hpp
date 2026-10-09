#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>
#include <functional>
class QTableWidget;
class QPushButton;
namespace portbridge {
class HttpAssertionsEditor final : public QWidget {
  public:
    explicit HttpAssertionsEditor(QWidget *parent = nullptr);
    QJsonArray rules() const;
    void setRules(const QJsonArray &rules);
    std::function<void()> changed;

  private:
    void append(const QJsonObject &row);
    QTableWidget *table_;
    QPushButton *add_;
};
} // namespace portbridge
