#pragma once

#include <QString>

enum class AppMode {
    WebUnderlay,
};

inline QString appModeName(AppMode)
{
    return QStringLiteral("webunderlay");
}
