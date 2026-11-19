#pragma once
// Where the program's own folders are (examples, data, python): next to the program when installed, else the
// source tree the build came from (KE_SOURCE_DIR, set by CMakeLists.txt). One rule for all of them.
#include <QCoreApplication>
#include <QDir>
#include <QString>

inline QString resourceDir(const QString& name) {
    const QString nextToExe = QCoreApplication::applicationDirPath() + "/" + name;
    return QDir(nextToExe).exists() ? nextToExe : QStringLiteral(KE_SOURCE_DIR) + "/" + name;
}
