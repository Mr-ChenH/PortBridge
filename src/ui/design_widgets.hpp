#pragma once
#include <algorithm>
#include <utility>
#include <QAbstractProxyModel>
#include <QApplication>
#include <QFontDatabase>
#include <QIcon>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QToolButton>
#include <QTemporaryDir>

namespace portbridge::design {
enum class Icon { Brand, Workspace, Folder, Terminal, Network, Chip, Sun, Moon, Plug,
                  Plus, Edit, Trash, Save, Import, Export, Search, Pause, Reset, Copy,
                  Right, Up, Down, Send, Record, Stop, Menu, Check, More };
inline QFont font(int pixels, bool mono = false, bool bold = false) {
    QFont f; f.setFamilies(mono ? QStringList{"Cascadia Code", "Consolas", "Microsoft YaHei UI"}
                              : QStringList{"Segoe UI", "Microsoft YaHei UI", "Microsoft YaHei"});
    f.setPixelSize(pixels); f.setWeight(bold ? QFont::DemiBold : QFont::Normal); return f;
}
inline void drawIcon(QPainter& p, const QRectF& rect, Icon icon, const QColor& color) {
    p.save(); p.setRenderHint(QPainter::Antialiasing); p.translate(rect.topLeft());
    p.scale(rect.width()/24., rect.height()/24.); p.setPen(QPen(color,1.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin)); p.setBrush(Qt::NoBrush);
    auto line=[&](double x,double y,double a,double b){p.drawLine(QPointF(x,y),QPointF(a,b));};
    auto box=[&](double x,double y,double w,double h){p.drawRoundedRect(QRectF(x,y,w,h),1.3,1.3);};
    switch(icon) {
    case Icon::Brand: line(3,18,3,6);line(3,6,9,6);line(9,6,9,18);line(9,11,15,11);line(15,18,15,6);line(15,6,21,6);line(21,6,21,18);p.drawEllipse(QPointF(3,20),1.6,1.6);p.drawEllipse(QPointF(21,4),1.6,1.6);break;
    case Icon::Workspace: box(3,4,18,16);line(3,9,21,9);line(9,9,9,20);break;
    case Icon::Folder: {QPainterPath path;path.moveTo(3,7);path.lineTo(9,7);path.lineTo(11,10);path.lineTo(21,10);path.lineTo(21,20);path.lineTo(3,20);path.closeSubpath();p.drawPath(path);break;}
    case Icon::Terminal: line(4,6,10,12);line(10,12,4,18);line(13,18,20,18);break;
    case Icon::Network: box(9,2,6,5);box(2,17,6,5);box(16,17,6,5);line(12,7,12,12);line(5,12,19,12);line(5,12,5,17);line(19,12,19,17);break;
    case Icon::Chip: box(6,6,12,12);for(int n=8;n<=16;n+=4){line(n,3,n,6);line(n,18,n,21);line(3,n,6,n);line(18,n,21,n);}break;
    case Icon::Sun: p.drawEllipse(QPointF(12,12),4,4);for(int n=0;n<8;++n){p.save();p.translate(12,12);p.rotate(n*45);line(0,7,0,10);p.restore();}break;
    case Icon::Moon: {QPainterPath path;path.moveTo(17,3);path.cubicTo(3,1,1,20,15,21);path.cubicTo(19,21,22,18,22,14);path.cubicTo(13,17,9,9,17,3);p.drawPath(path);break;}
    case Icon::Plug: line(8,3,8,8);line(16,3,16,8);box(5,8,14,7);line(12,15,12,21);break;
    case Icon::Plus: line(12,5,12,19);line(5,12,19,12);break;
    case Icon::Edit: {QPainterPath path;path.moveTo(4,16);path.lineTo(16,4);path.lineTo(20,8);path.lineTo(8,20);path.lineTo(3,21);path.closeSubpath();p.drawPath(path);line(13,7,17,11);break;}
    case Icon::Trash: line(3,6,21,6);line(9,3,15,3);line(5,6,7,21);line(7,21,17,21);line(17,21,19,6);line(10,10,10,17);line(14,10,14,17);break;
    case Icon::Save: box(4,3,16,18);box(8,3,8,6);box(8,14,8,7);break;
    case Icon::Import: case Icon::Export: box(3,14,18,7);line(7,18,17,18);line(12,3,12,14);if(icon==Icon::Import){line(8,10,12,14);line(12,14,16,10);}else{line(8,7,12,3);line(12,3,16,7);}break;
    case Icon::Search: p.drawEllipse(QPointF(10,10),6,6);line(15,15,21,21);break;
    case Icon::Pause: line(8,5,8,19);line(16,5,16,19);break;
    case Icon::Reset: {QPainterPath path;path.moveTo(5,8);path.cubicTo(9,-1,22,4,21,13);path.cubicTo(20,23,5,24,3,14);p.drawPath(path);line(5,3,5,8);line(5,8,10,8);break;}
    case Icon::Copy: box(8,7,13,15);line(4,17,3,17);line(3,17,3,2);line(3,2,16,2);line(16,2,16,3);break;
    case Icon::Right: line(9,6,15,12);line(15,12,9,18);break;
    case Icon::Up: line(6,15,12,9);line(12,9,18,15);break;
    case Icon::Down: line(6,9,12,15);line(12,15,18,9);break;
    case Icon::Send: line(4,12,20,12);line(14,6,20,12);line(20,12,14,18);break;
    case Icon::Record: p.drawEllipse(QPointF(12,12),5,5);break;
    case Icon::Stop: p.drawRect(QRectF(7,7,10,10));break;
    case Icon::Menu: line(4,6,20,6);line(4,12,20,12);line(4,18,20,18);break;
    case Icon::More: p.setPen(Qt::NoPen);p.setBrush(color);for(int x:{5,12,19})p.drawEllipse(QPointF(x,12),1.5,1.5);break;
    case Icon::Check: line(5,12,10,17);line(10,17,19,7);break;
    }
    p.restore();
}
inline QIcon icon(Icon value, const QColor& color, int size=18) {
    QIcon result;
    for(int scale: {1,2}) {QPixmap image(size*scale,size*scale);image.setDevicePixelRatio(scale);image.fill(Qt::transparent);QPainter p(&image);drawIcon(p,QRectF(0,0,size,size),value,color);p.end();result.addPixmap(image);}
    return result;
}
class Assets {
    QTemporaryDir directory;
public:
    QString file(Icon value, const QColor& color, bool dark) {
        const auto path=directory.filePath(QStringLiteral("%1-%2.png").arg(int(value)).arg(dark?"dark":"light"));
        QPixmap image(24,24);image.fill(Qt::transparent);QPainter p(&image);drawIcon(p,QRectF(0,0,24,24),value,color);p.end();image.save(path);return path;
    }
};
class ProfilePicker final : public QToolButton {
    QString name, detail;
    int kind = 3;
    bool active = false;
public:
    explicit ProfilePicker(QWidget* parent=nullptr):QToolButton(parent){setFixedHeight(64);setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);setPopupMode(QToolButton::InstantPopup);}
    void profile(const QString& title,const QString& subtitle,int protocol,bool running){
        name=title;detail=subtitle;kind=protocol;active=running;
        setText(title+'\n'+subtitle);setAccessibleName(QStringLiteral("当前连接方案：%1，点击切换").arg(title));
        setToolTip(QStringLiteral("%1\n%2\n点击搜索和切换方案；浏览不停止当前通信。").arg(title,subtitle));update();
    }
    QSize sizeHint() const override {return {190,64};}
protected:
    void paintEvent(QPaintEvent*) override {
        const bool dark=window()->property("darkTheme").toBool();
        const QColor ink(dark?"#e5edee":"#213336"),muted(dark?"#8b9a9f":"#5e7379"),accent(dark?"#85dec4":"#176e58");
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(underMouse()?(dark?"#24302f":"#edf5f1"):(dark?"#1d2428":"#ffffff")));
        p.setPen(QPen(hasFocus()?accent:QColor(dark?"#2b3438":"#d5dfe1"),1));p.drawRoundedRect(QRectF(rect()).adjusted(0.5,0.5,-0.5,-0.5),5,5);
        drawIcon(p,QRectF(12,19,22,22),kind==0?Icon::Chip:Icon::Network,active?accent:muted);
        if(active){p.setPen(Qt::NoPen);p.setBrush(accent);p.drawEllipse(QPointF(32,45),3,3);}
        const int textWidth=std::max(1,width()-70);p.setPen(ink);p.setFont(design::font(12,false,true));
        p.drawText(QRect(44,10,textWidth,22),Qt::AlignLeft|Qt::AlignVCenter,p.fontMetrics().elidedText(name,Qt::ElideRight,textWidth));
        p.setPen(muted);p.setFont(design::font(10,true));p.drawText(QRect(44,34,textWidth,20),Qt::AlignLeft|Qt::AlignVCenter,p.fontMetrics().elidedText(detail,Qt::ElideRight,textWidth));
        drawIcon(p,QRectF(width()-23,25,14,14),Icon::Down,muted);
    }
};
class ProfileList final : public QListWidget {
public:
    using QListWidget::QListWidget;
    QSize sizeHint() const override {auto result=QListWidget::sizeHint();result.setHeight(std::clamp(count()*64,130,260));return result;}
};
class LibraryList final : public QListWidget {
    Icon symbol; QString heading,description;
public:
    LibraryList(Icon value,QString title,QString note):symbol(value),heading(std::move(title)),description(std::move(note)){}
protected:
    void paintEvent(QPaintEvent* event) override {
        QListWidget::paintEvent(event);if(count())return;QPainter p(viewport());const auto center=viewport()->rect().center();const bool dark=window()->property("darkTheme").toBool();const QColor muted(dark?"#8b9a9f":"#5e7379");drawIcon(p,QRectF(center.x()-17,center.y()-60,34,34),symbol,muted);
        p.setPen(muted);p.setFont(design::font(14));p.drawText(QRect(0,center.y()-11,viewport()->width(),24),Qt::AlignCenter,heading);p.setFont(design::font(11));p.drawText(QRect(0,center.y()+19,viewport()->width(),24),Qt::AlignCenter,description);
    }
};
class LibraryDelegate final : public QStyledItemDelegate {
    Icon symbol;
public:
    LibraryDelegate(Icon value,QObject* owner):QStyledItemDelegate(owner),symbol(value){}
    QSize sizeHint(const QStyleOptionViewItem&,const QModelIndex&) const override {return {300,90};}
    void paint(QPainter* p,const QStyleOptionViewItem& option,const QModelIndex& index) const override {
        const bool dark=option.widget->window()->property("darkTheme").toBool(),selected=option.state&QStyle::State_Selected;const QColor muted(dark?"#8b9a9f":"#5e7379"),text(dark?"#e5edee":"#213336"),accent(dark?"#85dec4":"#176e58");const auto r=option.rect;const auto parts=index.data().toString().split('\n');p->save();p->fillRect(r,QColor(selected?(dark?"#21352f":"#e3f1eb"):(dark?"#171c1f":"#ffffff")));p->setPen(QColor(dark?"#2b3438":"#d5dfe1"));p->drawLine(r.bottomLeft(),r.bottomRight());if(index.data(Qt::UserRole+10).toBool()){p->restore();return;}drawIcon(*p,QRectF(r.left()+16,r.top()+19,24,24),symbol,accent);
        const int x=r.left()+56,width=std::max(1,r.width()-76);for(int n=0;n<std::min(3,int(parts.size()));++n){p->setFont(font(n?11:13,n==2,n==0));p->setPen(n?muted:text);p->drawText(QRect(x,r.top()+13+n*22,width,20),Qt::AlignLeft|Qt::AlignVCenter,p->fontMetrics().elidedText(parts[n],Qt::ElideRight,width));}p->restore();
    }
};
class ProfileDelegate final : public QStyledItemDelegate {
public:
    explicit ProfileDelegate(QObject* p):QStyledItemDelegate(p){}
    QSize sizeHint(const QStyleOptionViewItem&,const QModelIndex&) const override {return {190,64};}
    void paint(QPainter* p,const QStyleOptionViewItem& option,const QModelIndex& index) const override {
        const bool dark=option.widget->window()->property("darkTheme").toBool();
        const QColor text(dark?"#e5edee":"#213336"),muted(dark?"#8b9a9f":"#5e7379"),accent(dark?"#85dec4":"#176e58");
        const bool selected=option.state&QStyle::State_Selected;
        p->save();p->setRenderHint(QPainter::Antialiasing);const auto r=QRectF(option.rect).adjusted(0,3,-1,-3);
        if(selected||option.state&QStyle::State_MouseOver){p->setBrush(QColor(selected?(dark?"#21352f":"#e3f1eb"):(dark?"#1d2428":"#f3f6f6")));p->setPen(selected?QPen(QColor(dark?"#38584e":"#b0d1c7"),1):QPen(Qt::NoPen));p->drawRoundedRect(r,4,4);}
        drawIcon(*p,QRectF(r.left()+10,r.center().y()-9,18,18),index.data(Qt::UserRole).toInt()==0?Icon::Chip:Icon::Network,selected?accent:muted);
        const auto parts=index.data().toString().split('\n');const auto x=int(r.left()+36);const int width=int(r.width()-50);
        p->setFont(font(12,false,selected));p->setPen(selected?text:muted);p->drawText(QRect(x,int(r.top()+12),width,18),Qt::AlignLeft|Qt::AlignVCenter,p->fontMetrics().elidedText(parts.value(0),Qt::ElideRight,width));
        p->setFont(font(10,true));p->setPen(muted);p->drawText(QRect(x,int(r.top()+32),width,16),Qt::AlignLeft|Qt::AlignVCenter,p->fontMetrics().elidedText(parts.value(1),Qt::ElideRight,width));
        if(index.data(Qt::UserRole+1).toBool()){p->setPen(Qt::NoPen);p->setBrush(accent);p->drawEllipse(QPointF(r.right()-10,r.center().y()),3,3);}p->restore();
    }
};
class RecordDelegate final : public QStyledItemDelegate {
public:
    explicit RecordDelegate(QObject* p):QStyledItemDelegate(p){}
    void paint(QPainter* p,const QStyleOptionViewItem& option,const QModelIndex& index) const override {
        const bool dark=option.widget->window()->property("darkTheme").toBool(),selected=option.state&QStyle::State_Selected;
        const QColor text(dark?"#e5edee":"#213336"),muted(dark?"#8b9a9f":"#5e7379"),accent(dark?"#85dec4":"#176e58");
        p->save();p->fillRect(option.rect,QColor(selected?(dark?"#21352f":"#e3f1eb"):(option.state&QStyle::State_MouseOver)?(dark?"#1d2428":"#f3f6f6"):(dark?"#171c1f":"#ffffff")));
        p->setPen(QColor(dark?"#232b2f":"#e7eded"));p->drawLine(option.rect.bottomLeft(),option.rect.bottomRight());
        const auto r=option.rect.adjusted(10,0,-6,0);QString value=index.data().toString();p->setFont(font(11,true,selected));
        if(index.column()==2){const QColor color=value=="RX"?QColor(dark?"#68ED9D":"#116B35"):value=="TX"?QColor(dark?"#FFAD5C":"#9A4300"):muted;p->setFont(font(9,true));const int width=p->fontMetrics().horizontalAdvance(value)+8;const QRect badge(r.left(),r.center().y()-7,width,15);auto background=color;background.setAlpha(25);p->setPen(Qt::NoPen);p->setBrush(background);p->drawRoundedRect(badge,2,2);p->setPen(color);p->drawText(badge,Qt::AlignCenter,value);}
        else{if(index.column()==0)value=value.rightJustified(3,'0');p->setPen(selected||index.column()==5?text:muted);p->drawText(r,(index.column()==4?Qt::AlignRight:Qt::AlignLeft)|Qt::AlignVCenter,p->fontMetrics().elidedText(value,Qt::ElideRight,r.width()));}
        if(selected&&index.column()==0) p->fillRect(QRect(option.rect.left(),option.rect.top(),2,option.rect.height()),accent);
        p->restore();
    }
};
class RecordView final : public QTableView {
public:
    explicit RecordView(QWidget* p=nullptr):QTableView(p){}
protected:
    void paintEvent(QPaintEvent* event) override {
        QTableView::paintEvent(event);if(!model()||model()->rowCount())return;
        QPainter p(viewport());const bool dark=window()->property("darkTheme").toBool();const auto center=viewport()->rect().center();
        const QColor muted(dark?"#63757d":"#70868c");drawIcon(p,QRectF(center.x()-14,center.y()-45,28,28),Icon::Workspace,muted);
        const auto* proxy=qobject_cast<const QAbstractProxyModel*>(model());const bool filtered=proxy&&proxy->sourceModel()&&proxy->sourceModel()->rowCount()>0;
        p.setFont(design::font(12));p.setPen(QColor(dark?"#8b9a9f":"#5e7379"));p.drawText(QRect(0,center.y()-7,viewport()->width(),24),Qt::AlignCenter,filtered?QStringLiteral("没有匹配的数据样本"):QStringLiteral("等待通信数据"));
        p.setFont(design::font(10));p.setPen(muted);p.drawText(QRect(0,center.y()+20,viewport()->width(),24),Qt::AlignCenter,filtered?QStringLiteral("试试调整字节或来源搜索条件"):QStringLiteral("连接设备后，在这里查看收发记录"));
    }
};
} // namespace portbridge::design
