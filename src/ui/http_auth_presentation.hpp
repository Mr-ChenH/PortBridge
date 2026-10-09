#pragma once
#include <QString>
namespace portbridge {
inline QString httpAuthName(const QString &kind) {
    if (kind == "bearer")
        return QStringLiteral("Bearer Token（令牌）");
    if (kind == "basic")
        return QStringLiteral("Basic Auth（账号密码）");
    return QStringLiteral("无认证");
}
inline QString httpAuthDescription(const QString &kind) {
    if (kind == "bearer")
        return QStringLiteral("使用访问令牌。工具自动添加 Bearer 前缀，只需填写 Token 值或变量引用。");
    if (kind == "basic")
        return QStringLiteral("按 Basic 格式发送用户名和密码。可引用当前环境的账号变量。");
    return QStringLiteral("不生成认证请求头，适用于公开接口。登录请求通常单独选择“无认证”。");
}
inline QString httpAuthHeaderPreview(const QString &kind) {
    if (kind == "bearer")
        return QStringLiteral("Authorization: Bearer <Token>");
    if (kind == "basic")
        return QStringLiteral("Authorization: Basic <编码后的账号密码>");
    return QStringLiteral("不生成 Authorization 请求头");
}
} // namespace portbridge
