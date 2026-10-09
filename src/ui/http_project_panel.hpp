#pragma once
#include "http_project_store.hpp"
#include <QWidget>
#include <functional>
class QComboBox;
class QLabel;
namespace portbridge {
class HttpProjectPanel final : public QWidget {
    Q_OBJECT
  public:
    explicit HttpProjectPanel(HttpProjectStore *store, QWidget *parent = nullptr);
    std::function<bool(bool discardDraft)> beforeContextChange;
    std::function<void()> contextChanged;
    std::function<void()> definitionsChanged;
    std::function<void()> exportRequested, importRequested;
    std::function<void(QString)> projectRemoved;
    void refresh();
    void setActive(bool active);
    bool matches(const QJsonObject &request, const QString &defaultProject) const;
    QString folderFilter() const;
    void editVariables(bool projectScope = false);
    void editAuthentication();
    HttpProjectStore *store() const { return store_; }

  private:
    HttpProjectStore *store_;
    QComboBox *projects_, *environments_, *folders_;
    QLabel *tokenStatus_;
    bool updating_ = false;
    void result(bool ok, const QString &error);
};
} // namespace portbridge
