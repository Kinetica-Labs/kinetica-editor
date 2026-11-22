#pragma once
// Where the program's own folders are (examples, data, python, plugins): next to the program when installed - in a
// macOS bundle in its Resources, beside the MacOS folder the program is in -, else the source tree the build came from
// (KE_SOURCE_DIR, set by CMakeLists.txt). One rule for all of them.
#include <QCoreApplication>
#include <QDir>
#include <QString>

inline QString resourceDir(const QString& name) {
    const QString exe = QCoreApplication::applicationDirPath();
    for (const QString& dir : {exe + "/" + name, exe + "/../Resources/" + name})
        if (QDir(dir).exists()) return QDir::cleanPath(dir);
    return QStringLiteral(KE_SOURCE_DIR) + "/" + name;
}
