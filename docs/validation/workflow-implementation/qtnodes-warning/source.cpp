#include <QVariant>
#include <QString>
struct Pair { QString id; QString name; };
Q_DECLARE_METATYPE(Pair)
QVariant source(int n) {
    return QVariant::fromValue(Pair{QString::number(n),QStringLiteral("中文 / byte → connection")});
}
