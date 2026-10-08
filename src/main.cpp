#include "portbridge/session_controller.hpp"
#include "ui/main_window.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDir>
#include <QFont>
#include <QIcon>
#include <QSettings>
#include <QScreen>
#include <QPushButton>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QTimer>
#include <cstdio>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("PortBridge"));
    QApplication::setApplicationName(QStringLiteral("PortBridge"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/portbridge.ico")));
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("PortBridge 通信调试与原生工作流工作台"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption screenshot(QStringList{QStringLiteral("screenshot")}, QStringLiteral("保存原生界面截图并退出，不建立连接"), QStringLiteral("path"));
    QCommandLineOption quitAfter(QStringList{QStringLiteral("quit-after")}, QStringLiteral("指定毫秒后退出，用于启动验证"), QStringLiteral("milliseconds"));
    QCommandLineOption theme(QStringList{QStringLiteral("theme")}, QStringLiteral("指定初始主题：dark 或 light"), QStringLiteral("name"));
    QCommandLineOption size(QStringList{QStringLiteral("size")}, QStringLiteral("指定窗口尺寸，例如 1280x900"), QStringLiteral("widthxheight"));
    QCommandLineOption reviewSettings(QStringList{QStringLiteral("review-settings")}, QStringLiteral("使用独立配置目录进行界面验收"), QStringLiteral("directory"));
    QCommandLineOption page(QStringList{QStringLiteral("page")}, QStringLiteral("选择初始页面：workspace、captures、commands、workflow、http 或 websocket；不运行任务"), QStringLiteral("name"), QStringLiteral("workspace"));
    parser.addOptions({screenshot, quitAfter, theme, size, reviewSettings, page});
    parser.process(app);
    if (parser.isSet(theme) && parser.value(theme) != QStringLiteral("dark") && parser.value(theme) != QStringLiteral("light")) return 2;
    const QStringList pageNames{QStringLiteral("workspace"),QStringLiteral("captures"),QStringLiteral("commands"),QStringLiteral("workflow"),QStringLiteral("http"),QStringLiteral("websocket")};
    const int initialPage=pageNames.indexOf(parser.value(page));
    if(initialPage<0)return 2;
    if (parser.isSet(reviewSettings)) {
        const QString directory = QDir(parser.value(reviewSettings)).absolutePath();
        if (parser.value(reviewSettings).isEmpty() || !QDir().mkpath(directory)) return 2;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory);
        QStandardPaths::setTestModeEnabled(true);
        QSettings settings;
        settings.setValue(QStringLiteral("storage/directory"), QDir(directory).filePath(QStringLiteral("data")));
    }
    portbridge::SessionController controller;
    portbridge::MainWindow window(&controller);
    if(initialPage>=4){
        if(auto* mode=window.findChild<QPushButton*>(initialPage==4?QStringLiteral("httpWorkspaceMode"):QStringLiteral("webSocketWorkspaceMode")))mode->click();else return 2;
    }else if(initialPage>0){
        const QStringList navigationIds{QStringLiteral("workspaceNavigation"),QStringLiteral("capturesNavigation"),QStringLiteral("commandsNavigation"),QStringLiteral("workflowNavigation")};
        if(auto* navigation=window.findChild<QPushButton*>(navigationIds[initialPage]))navigation->click();
        else return 2;
    }
    if (parser.isSet(size)) {
        const auto match = QRegularExpression(QStringLiteral("^(\\d{3,4})x(\\d{3,4})$")).match(parser.value(size));
        if (!match.hasMatch()) return 2;
        window.resize(match.captured(1).toInt(), match.captured(2).toInt());
    } else if (auto* screen = window.screen()) {
        const QSize available = screen->availableGeometry().size() - QSize(24, 48);
        window.resize(window.size().boundedTo(available));
    }
    if (parser.isSet(theme)) {
        const bool wantDark = parser.value(theme) == QStringLiteral("dark");
        if (window.property("darkTheme").toBool() != wantDark)
            if (auto* button = window.findChild<QPushButton*>(QStringLiteral("themeButton"))) button->click();
    }
    window.show();
    if (parser.isSet(screenshot)) {
        const QString path = parser.value(screenshot);
        QTimer::singleShot(750, &window, [&window, &app, path] {
            const bool ok = window.grab().save(path);
            if (!ok) std::fprintf(stderr, "Unable to save screenshot\n");
            app.exit(ok ? 0 : 2);
        });
    }
    if (parser.isSet(quitAfter)) {
        bool ok = false;
        const int delay = parser.value(quitAfter).toInt(&ok);
        if (!ok || delay < 1 || delay > 3600000) return 2;
        QTimer::singleShot(delay, &app, &QApplication::quit);
    }
    return app.exec();
}
