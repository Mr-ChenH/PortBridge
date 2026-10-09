#pragma once
#include "design_widgets.hpp"
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace portbridge::workflowUi {
inline QColor categoryColor(const QString& type, bool dark) {
    if (type == "http" || type == "wait") return QColor(dark ? "#91b8f1" : "#365f9d");
    if (type.startsWith("send") || type == "raw") return QColor(dark ? "#ffad5c" : "#9a4300");
    if (type == "ws" || type == "close" || type == "start" || type == "end")
        return QColor(dark ? "#85dec4" : "#176e58");
    return QColor(dark ? "#b9a4e9" : "#745399");
}
inline design::Icon nodeIcon(const QString& type) {
    if (type == "http" || type == "ws") return design::Icon::Network;
    if (type == "raw" || type == "close") return design::Icon::Plug;
    if (type.startsWith("send") || type == "start") return design::Icon::Send;
    if (type == "end") return design::Icon::Stop;
    if (type == "assert") return design::Icon::Check;
    if (type == "branch" || type == "loop") return design::Icon::Workflow;
    if (type == "delay" || type == "wait") return design::Icon::Record;
    return design::Icon::Terminal;
}
inline QString protocolTag(const QString& type) {
    if (type == "http") return "HTTP";
    if (type == "ws") return "WS";
    if (type == "raw") return "RAW";
    if (type == "extract") return "JSON";
    if (type == "assert") return "ASSERT";
    if (type == "sendWait" || type == "wait") return "WAIT";
    if (type == "send") return "SEND";
    return type.toUpper();
}
class PaletteDelegate final : public QStyledItemDelegate {
    const bool* dark;
public:
    PaletteDelegate(const bool* theme, QObject* parent) : QStyledItemDelegate(parent), dark(theme) {}
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex& index) const override {
        return {170, index.parent().isValid() ? 36 : 34};
    }
    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const auto rect=option.rect;
        const QColor muted(*dark ? "#b5c4cb" : "#435960"), ink(*dark ? "#e5edee" : "#213336");
        p->save();
        if (!index.parent().isValid()) {
            p->setPen(muted); p->setFont(design::font(11));
            p->drawText(rect.adjusted(0,5,-25,0), Qt::AlignVCenter, index.data().toString());
            p->drawText(rect.adjusted(0,5,-5,0), Qt::AlignRight|Qt::AlignVCenter,
                        QString::number(index.model()->rowCount(index)));
        } else {
            if (option.state & (QStyle::State_Selected | QStyle::State_MouseOver))
                p->fillRect(rect.adjusted(0,1,0,-1), QColor(*dark ? "#21352f" : "#e3f1eb"));
            const auto type=index.data(Qt::UserRole).toString();
            design::drawIcon(*p, QRectF(rect.left()+6,rect.center().y()-8,16,16), nodeIcon(type), categoryColor(type,*dark));
            p->setFont(design::font(13)); p->setPen(ink);
            p->drawText(rect.adjusted(32,0,-18,0), Qt::AlignVCenter,
                        p->fontMetrics().elidedText(index.data().toString(),Qt::ElideRight,rect.width()-50));
            p->setPen(QColor(*dark ? "#39484d" : "#bccccc"));
            for (int y : {-3,0,3}) { p->drawPoint(rect.right()-8,rect.center().y()+y);p->drawPoint(rect.right()-5,rect.center().y()+y); }
            if (option.state & QStyle::State_HasFocus) {
                p->setBrush(Qt::NoBrush);p->setPen(QColor(*dark ? "#85dec4" : "#176e58"));
                p->drawRoundedRect(rect.adjusted(1,1,-1,-1),3,3);
            }
        }
        p->restore();
    }
};
class FoldSection final : public QWidget {
    QPushButton* toggle;
    QWidget* body;
public:
    FoldSection(const QString& text, const QString& key, QWidget* parent=nullptr) : QWidget(parent) {
        setObjectName("workflowSection_"+key);
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(8);
        toggle=new QPushButton(QStringLiteral("▸ ")+text);toggle->setObjectName("workflowSectionToggle_"+key);
        toggle->setProperty("section",true);toggle->setAccessibleName(text);toggle->setCheckable(true);
        body=new QWidget;body->setObjectName("workflowSectionBody_"+key);body->hide();
        layout->addWidget(toggle);layout->addWidget(body);
        connect(toggle,&QPushButton::toggled,this,[this,text](bool open){
            body->setVisible(open);toggle->setText((open?QStringLiteral("▾ "):QStringLiteral("▸ "))+text);
        });
    }
    QWidget* contents() const { return body; }
    void expand() { toggle->setChecked(true); }
};
class EmptyTable final : public QTableView {
    const bool* dark;
    QString heading, detail;
public:
    EmptyTable(const bool* theme, QString title, QString description)
        : dark(theme), heading(std::move(title)), detail(std::move(description)) {}
protected:
    void paintEvent(QPaintEvent* event) override {
        QTableView::paintEvent(event);
        if (!model() || model()->rowCount()) return;
        QPainter p(viewport());const auto c=viewport()->rect().center();
        const QColor muted(*dark ? "#b5c4cb" : "#435960");
        p.setFont(design::font(12));p.setPen(muted);
        const auto textWidth=p.fontMetrics().horizontalAdvance(heading);
        design::drawIcon(p,QRectF(c.x()-textWidth/2-25,c.y()-8,16,16),design::Icon::Record,muted);
        p.drawText(QRect(12,c.y()-12,viewport()->width()-24,24),Qt::AlignCenter,heading);
        if (viewport()->height() >= 100) {
            p.setFont(design::font(10));
            p.drawText(QRect(12,c.y()+18,viewport()->width()-24,22),Qt::AlignCenter,detail);
        }
    }
};
} // namespace portbridge::workflowUi
