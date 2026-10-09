#pragma once
#include "http_project_store.hpp"
#include <QDialog>
#include <functional>
namespace portbridge {
// Edits a local draft; the store changes only after one validated atomic save.
class HttpConfigurationDialog final : public QDialog {
  public:
    HttpConfigurationDialog(HttpProjectStore *store, int tab, QWidget *parent = nullptr);
    std::function<bool()> beforeSave;
};
} // namespace portbridge
