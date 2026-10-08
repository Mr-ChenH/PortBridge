#pragma once
#include "design_widgets.hpp"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <array>
#include <memory>

namespace portbridge::design {
class ConnectionTypeCard final : public QPushButton {
    QString title_, detail_;
    Icon icon_;

  public:
    ConnectionTypeCard(QString title, QString detail, Icon symbol, QWidget *parent = nullptr)
        : QPushButton(parent), title_(std::move(title)), detail_(std::move(detail)), icon_(symbol) {
        setText(title_);
        setAccessibleName(title_ + QStringLiteral("，") + detail_);
        setToolTip(detail_);
        setCheckable(true);
        setAutoDefault(false);
        setStyleSheet("QPushButton{min-height:82px;max-height:82px;min-width:240px;padding:0;border:0;}");
        setFixedHeight(82);
        setMinimumWidth(240);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setCursor(Qt::PointingHandCursor);
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        const bool dark = window()->property("darkTheme").toBool();
        const QColor ink(dark ? "#e5edee" : "#213336"), muted(dark ? "#94a5a8" : "#60767b"),
            accent(dark ? "#85dec4" : "#176e58");
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(
            isChecked() ? (dark ? "#21352f" : "#e5f3ed")
                        : (underMouse() ? (dark ? "#243033" : "#f2f6f6") : (dark ? "#171f22" : "#ffffff"))));
        p.setPen(QPen(isChecked() || hasFocus() ? accent : QColor(dark ? "#344348" : "#d5dfe1"),
                      isChecked() || hasFocus() ? 1.5 : 1));
        p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 7, 7);
        drawIcon(p, QRectF(16, 17, 22, 22), icon_, isChecked() ? accent : muted);
        const int textWidth = width() - 76;
        p.setPen(ink);
        p.setFont(design::font(14, false, true));
        p.drawText(QRect(50, 14, textWidth, 24), Qt::AlignVCenter,
                   p.fontMetrics().elidedText(title_, Qt::ElideRight, textWidth));
        p.setFont(design::font(11));
        p.setPen(muted);
        p.drawText(QRect(16, 49, width() - 32, 20), Qt::AlignVCenter,
                   p.fontMetrics().elidedText(detail_, Qt::ElideRight, width() - 32));
        if (isChecked())
            drawIcon(p, QRectF(width() - 30, 18, 16, 16), Icon::Check, accent);
    }
};
class ConnectionDialog final : public QDialog {
  public:
    QLineEdit *name;
    QLineEdit *url;
    QComboBox *kind;
    QLabel *error;
    QDialogButtonBox *buttons;
    ConnectionDialog(QWidget *parent, bool add, int selected, const QString &initialName, bool dark)
        : QDialog(parent) {
        setObjectName("profileDialog");
        setProperty("darkTheme", dark);
        setWindowTitle(add ? QStringLiteral("新建连接方案") : QStringLiteral("编辑连接方案"));
        setMinimumWidth(560);
        resize(620, add ? 570 : 550);
        const QString ink = dark ? "#e5edee" : "#213336", muted = dark ? "#94a5a8" : "#60767b",
                      border = dark ? "#344348" : "#d5dfe1", accent = dark ? "#85dec4" : "#176e58";
        setStyleSheet(
            QString("QDialog#profileDialog{background:%1;color:%2;} QDialog#profileDialog "
                    "QLabel{color:%2;background:transparent;} "
                    "QLabel#profileDialogSubtitle,QLabel#profileDialogHint{color:%3;} "
                    "QLabel#profileDialogError{color:%4;} QLineEdit{border:1px solid "
                    "%5;border-radius:5px;padding:8px 10px;background:%6;color:%2;min-height:18px;} "
                    "QLineEdit:focus{border:1px solid %7;} QPushButton{padding:7px 16px;border:1px solid "
                    "%5;border-radius:5px;background:%6;color:%2;} "
                    "QPushButton#profileDialogSave{background:%7;color:%8;border-color:%7;}")
                .arg(dark ? "#101719" : "#f4f7f7", ink, muted, dark ? "#ffad5c" : "#a34b0f", border,
                     dark ? "#171f22" : "#ffffff", accent, dark ? "#10221c" : "#ffffff"));
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(24, 22, 24, 22);
        layout->setSpacing(14);
        auto *title = new QLabel(add ? QStringLiteral("选择连接方式") : QStringLiteral("编辑连接方案"));
        title->setFont(design::font(22, false, true));
        layout->addWidget(title);
        auto *subtitle = new QLabel(add ? QStringLiteral("为设备通信或接口调试创建一个可复用的方案。")
                                        : QStringLiteral("修改名称和通信方式，再回到工作台配置参数。"));
        subtitle->setObjectName("profileDialogSubtitle");
        subtitle->setWordWrap(true);
        layout->addWidget(subtitle);
        kind = new QComboBox(this);
        kind->setObjectName("profileKind");
        kind->addItems(
            {QStringLiteral("串口"), QStringLiteral("TCP 客户端"), QStringLiteral("TCP 服务端"), "UDP"});
        if (add)
            kind->addItems({"HTTP", "WebSocket"});
        kind->hide();
        const std::array<QString, 6> details = {
            QStringLiteral("本地串口 · 设备收发"),      QStringLiteral("连接远端 · 字节流收发"),
            QStringLiteral("本地监听 · 多客户端调试"),  QStringLiteral("本地绑定 · 独立发送目标"),
            QStringLiteral("请求 / 响应 · 状态与正文"), QStringLiteral("持久连接 · 文本与二进制消息")};
        auto *grid = new QGridLayout;
        grid->setSpacing(10);
        auto cards = std::make_shared<std::array<ConnectionTypeCard *, 6>>();
        cards->fill(nullptr);
        for (int i = 0; i < kind->count(); ++i) {
            auto *card =
                new ConnectionTypeCard(kind->itemText(i), details[i],
                                       i == 0 ? Icon::Chip : (i == 4 ? Icon::Send : Icon::Network), this);
            card->setObjectName(QString("profileType%1").arg(i));
            (*cards)[i] = card;
            grid->addWidget(card, i / 2, i % 2);
            connect(card, &QPushButton::clicked, this, [this, i, card] {
                kind->setCurrentIndex(i);
                card->setChecked(true);
            });
        }
        layout->addLayout(grid);
        auto *nameBlock = new QVBoxLayout;
        nameBlock->setSpacing(6);
        auto *nameCaption = new QLabel(QStringLiteral("方案名称"));
        name = new QLineEdit(initialName);
        name->setObjectName("profileName");
        name->setMaxLength(128);
        name->setPlaceholderText(QStringLiteral("例如：设备联调、健康检查、实时消息"));
        nameCaption->setBuddy(name);
        nameBlock->addWidget(nameCaption);
        nameBlock->addWidget(name);
        layout->addLayout(nameBlock);
        auto *urlBlock = new QWidget;
        auto *urlLayout = new QVBoxLayout(urlBlock);
        urlLayout->setContentsMargins(0, 0, 0, 0);
        urlLayout->setSpacing(6);
        auto *urlCaption = new QLabel(QStringLiteral("请求地址"));
        url = new QLineEdit;
        url->setObjectName("profileProtocolUrl");
        url->setMaxLength(4096);
        urlCaption->setBuddy(url);
        urlLayout->addWidget(urlCaption);
        urlLayout->addWidget(url);
        layout->addWidget(urlBlock);
        auto *hint = new QLabel;
        hint->setObjectName("profileDialogHint");
        hint->setWordWrap(true);
        layout->addWidget(hint);
        error = new QLabel;
        error->setObjectName("profileDialogError");
        error->setWordWrap(true);
        error->hide();
        layout->addWidget(error);
        layout->addStretch();
        buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
        auto *save = buttons->button(QDialogButtonBox::Save);
        save->setObjectName("profileDialogSave");
        save->setText(add ? QStringLiteral("创建方案") : QStringLiteral("保存修改"));
        layout->addWidget(buttons);
        auto update = [this, cards, urlBlock, hint, save](int index) {
            for (int i = 0; i < 6; ++i)
                if ((*cards)[i])
                    (*cards)[i]->setChecked(i == index);
            urlBlock->setVisible(index >= 4);
            url->setPlaceholderText(index == 4 ? "http://127.0.0.1:8080/health" : "ws://127.0.0.1:8081/echo");
            hint->setText(index >= 4 ? QStringLiteral("创建后加入对应方案库并打开工作台；不自动连接或发送。")
                                     : QStringLiteral("创建后在工作台配置端口与收发参数；不自动连接。"));
            save->setText(index >= 4 ? QStringLiteral("创建并打开") : QStringLiteral("保存方案"));
            error->clear();
            error->hide();
        };
        connect(kind, &QComboBox::currentIndexChanged, this, update);
        kind->setCurrentIndex(selected);
        update(selected);
        connect(name, &QLineEdit::textChanged, this, [this] { showError({}); });
        connect(url, &QLineEdit::textChanged, this, [this] { showError({}); });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        name->setFocus();
    }
    void showError(const QString &message) {
        error->setText(message);
        error->setVisible(!message.isEmpty());
    }
};
} // namespace portbridge::design
