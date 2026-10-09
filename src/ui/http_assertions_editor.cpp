#include "http_assertions_editor.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
namespace portbridge {
HttpAssertionsEditor::HttpAssertionsEditor(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    auto *hint = new QLabel(QStringLiteral("状态码、响应头或JSON字段断言；预期值填写JSON（文本须加双引"
                                           "号）。只报告条件是否通过，省略实际值。"));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    table_ = new QTableWidget(0, 6);
    table_->setObjectName("httpAssertions");
    table_->setHorizontalHeaderLabels({QStringLiteral("启用"), QStringLiteral("来源"),
                                       QStringLiteral("字段 / 响应头"), QStringLiteral("关系"),
                                       QStringLiteral("预期JSON值"), QStringLiteral("解析")});
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    table_->setColumnWidth(0, 42);
    table_->setColumnWidth(1, 112);
    table_->setColumnWidth(2, 150);
    table_->setColumnWidth(3, 106);
    table_->setColumnWidth(5, 60);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(table_, 1);
    auto *tools = new QHBoxLayout;
    add_ = new QPushButton(QStringLiteral("＋ 添加断言"));
    add_->setObjectName("httpAssertionAdd");
    auto *remove = new QPushButton(QStringLiteral("删除选中"));
    remove->setObjectName("httpAssertionRemove");
    tools->addWidget(add_);
    tools->addWidget(remove);
    tools->addStretch();
    layout->addLayout(tools);
    connect(add_, &QPushButton::clicked, this, [this] {
        if (table_->rowCount() < 32)
            append(
                {{"enabled", true}, {"source", "status"}, {"relation", "equals"}, {"expected", 200}});
        if (changed)
            changed();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        auto rows = table_->selectionModel()->selectedRows();
        std::sort(rows.begin(), rows.end(),
                  [](const auto &a, const auto &b) { return a.row() > b.row(); });
        for (const auto &row : rows)
            table_->removeRow(row.row());
        add_->setEnabled(table_->rowCount() < 32);
        if (changed)
            changed();
    });
}
void HttpAssertionsEditor::append(const QJsonObject &r) {
    const int row = table_->rowCount();
    table_->insertRow(row);
    auto *enabled = new QCheckBox;
    enabled->setChecked(r.value("enabled").toBool(true));
    table_->setCellWidget(row, 0, enabled);
    auto *source = new QComboBox;
    source->addItem(QStringLiteral("状态码"), "status");
    source->addItem(QStringLiteral("响应头"), "header");
    source->addItem("JSON", "json");
    source->setCurrentIndex(std::max(0, source->findData(r.value("source").toString("status"))));
    table_->setCellWidget(row, 1, source);
    auto *path = new QLineEdit(r.value("path").toString());
    path->setMaxLength(512);
    table_->setCellWidget(row, 2, path);
    auto *relation = new QComboBox;
    relation->addItem(QStringLiteral("等于"), "equals");
    relation->addItem(QStringLiteral("存在"), "exists");
    relation->addItem(QStringLiteral("包含"), "contains");
    relation->setCurrentIndex(std::max(0, relation->findData(r.value("relation").toString("equals"))));
    table_->setCellWidget(row, 3, relation);
    auto *expected = new QLineEdit;
    expected->setMaxLength(65536);
    const auto bytes = QJsonDocument(QJsonArray{r.value("expected")}).toJson(QJsonDocument::Compact);
    expected->setText(QString::fromUtf8(bytes.mid(1, bytes.size() - 2)));
    table_->setCellWidget(row, 4, expected);
    auto *parse = new QLabel;
    table_->setCellWidget(row, 5, parse);
    auto update = [this, source, path, relation, expected, parse] {
        path->setEnabled(source->currentData().toString() != "status");
        expected->setEnabled(relation->currentData().toString() != "exists");
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(("[" + expected->text() + "]").toUtf8(), &error);
        parse->setText(!expected->isEnabled() ||
                               (error.error == QJsonParseError::NoError && doc.array().size() == 1)
                           ? QStringLiteral("有效")
                           : QStringLiteral("无效"));
        if (changed)
            changed();
    };
    connect(enabled, &QCheckBox::toggled, this, [update] { update(); });
    connect(source, &QComboBox::currentIndexChanged, this, [update] { update(); });
    connect(path, &QLineEdit::textChanged, this, [update] { update(); });
    connect(relation, &QComboBox::currentIndexChanged, this, [update] { update(); });
    connect(expected, &QLineEdit::textChanged, this, [update] { update(); });
    update();
    add_->setEnabled(table_->rowCount() < 32);
}
QJsonArray HttpAssertionsEditor::rules() const {
    QJsonArray rules;
    for (int i = 0; i < table_->rowCount(); ++i) {
        auto *expected = qobject_cast<QLineEdit *>(table_->cellWidget(i, 4));
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(("[" + expected->text() + "]").toUtf8(), &error);
        QJsonObject r{
            {"enabled", qobject_cast<QCheckBox *>(table_->cellWidget(i, 0))->isChecked()},
            {"source", qobject_cast<QComboBox *>(table_->cellWidget(i, 1))->currentData().toString()},
            {"path", qobject_cast<QLineEdit *>(table_->cellWidget(i, 2))->text()},
            {"relation",
             qobject_cast<QComboBox *>(table_->cellWidget(i, 3))->currentData().toString()}};
        if (error.error == QJsonParseError::NoError && doc.array().size() == 1)
            r["expected"] = doc.array().first();
        rules.append(r);
    }
    return rules;
}
void HttpAssertionsEditor::setRules(const QJsonArray &rules) {
    table_->setRowCount(0);
    for (const auto &r : rules)
        append(r.toObject());
    add_->setEnabled(table_->rowCount() < 32);
}
} // namespace portbridge
