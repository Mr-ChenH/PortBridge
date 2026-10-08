#include <QVariant>
#include <QString>
#include <cstdint>
struct Pair { QString id; QString name; };
Q_DECLARE_METATYPE(Pair)
QVariant source(int);
#ifdef CONST_VALUE
Pair extract(int n) { const QVariant value=source(n); return value.value<Pair>(); }
#else
Pair extract(int n) { return source(n).value<Pair>(); }
#endif
int main() {
    for (int n=0;n<200000;++n) {
        auto value=extract(n);
        if(value.id!=QString::number(n)||value.name!=QStringLiteral("中文 / byte → connection"))return 1;
    }
    return 0;
}
