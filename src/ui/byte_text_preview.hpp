#pragma once
#include <QByteArrayView>
#include <QString>
#include <QStringDecoder>
#include <algorithm>

namespace portbridge {
struct ByteTextPage {
    QString text;
    bool invalidUtf8 = false;
    qsizetype begin = 0, end = 0;
};
// Validate a single scalar before adjusting page boundaries. Invalid bytes
// stay in their original page and are reported by the stateless decoder.
inline int utf8ScalarBytes(QByteArrayView raw, qsizetype at) {
    if (at < 0 || at >= raw.size()) return 0;
    const auto first = static_cast<unsigned char>(raw[at]);
    const int count = first < 0x80 ? 1 : first >= 0xC2 && first <= 0xDF ? 2 :
                      first >= 0xE0 && first <= 0xEF ? 3 : first >= 0xF0 && first <= 0xF4 ? 4 : 0;
    if (!count || at + count > raw.size()) return 0;
    for (int i = 1; i < count; ++i) {
        const auto ch = static_cast<unsigned char>(raw[at + i]);
        if (ch < 0x80 || ch > 0xBF) return 0;
    }
    if (count >= 3) {
        const auto second = static_cast<unsigned char>(raw[at + 1]);
        if ((first == 0xE0 && second < 0xA0) || (first == 0xED && second >= 0xA0) ||
            (first == 0xF0 && second < 0x90) || (first == 0xF4 && second >= 0x90)) return 0;
    }
    return count;
}
inline ByteTextPage byteTextPage(QByteArrayView raw, qsizetype offset, qsizetype length) {
    ByteTextPage page;
    page.begin = std::clamp<qsizetype>(offset, 0, raw.size());
    page.end = page.begin + std::clamp<qsizetype>(length, 0, raw.size() - page.begin);
    // A character belongs to the page containing its first byte. Read at most
    // three extra bytes at the end; omit those continuation bytes on the next page.
    for (int back = 1; back <= 3 && page.begin - back >= 0; ++back) {
        const auto lead = page.begin - back;
        const int count = utf8ScalarBytes(raw, lead);
        if (count > back) { page.begin = lead + count; break; }
    }
    for (int back = 1; back <= 3 && page.end - back >= 0; ++back) {
        const auto lead = page.end - back;
        const int count = utf8ScalarBytes(raw, lead);
        if (count > back) { page.end = lead + count; break; }
    }
    if (page.begin > page.end) page.begin = page.end;
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString decoded = decoder(raw.sliced(page.begin, page.end - page.begin));
    page.invalidUtf8 = decoder.hasError();
    for (QChar ch : decoded) {
        const auto value = ch.unicode();
        if ((value < 32 && ch != '\n' && ch != '\t') || value == 127)
            page.text += QStringLiteral("\\x%1").arg(QString::number(value,16).rightJustified(2,QChar('0')).toUpper());
        else page.text += ch;
    }
    return page;
}
}
