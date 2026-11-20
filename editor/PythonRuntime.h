#pragma once
// The Python a plugin or the console runs on: python3, or python as Windows' installer names it - not the Microsoft
// Store's stub in WindowsApps, which only opens the Store - and the kinetica package (python/: next to the program,
// else the sources), put on PYTHONPATH with the editor's port. ApiConsole and PluginHost start theirs alike. Its text is
// UTF-8 on every system (PYTHONUTF8): Windows' Python writes print() in the console's code page otherwise, and a
// plugin's Russian words came to the API console as noise. Its lines there end in \r\n: readers drop the \r.
#include "Resources.h"
#include "UiLanguage.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>

inline QString pythonInterpreter() {
    for (const char* name : {"python3", "python"}) {
        const QString path = QStandardPaths::findExecutable(name);
        if (!path.isEmpty() && !path.contains("WindowsApps")) return path;
    }
    return "python3";
}

inline QString pythonPackage() { return resourceDir("python"); }

// Where a Python process fell, from the last lines it wrote: «plugin.py:12: ZeroDivisionError: division by zero» - the
// author's file and line (the deepest frame that is not the kinetica package's own), then the error's own words.
inline QString pythonError(const QStringList& tail) {
    static const QRegularExpression frame(R"re(File "([^"]+)", line (\d+))re");
    const QString package = QDir::cleanPath(pythonPackage());
    QString where, what;
    for (const QString& line : tail) {
        const QRegularExpressionMatch m = frame.match(line);
        if (m.hasMatch() && !QDir::cleanPath(m.captured(1)).startsWith(package, Qt::CaseInsensitive)) where = QFileInfo(m.captured(1)).fileName() + ":" + m.captured(2) + ": ";
        else if (!m.hasMatch() && !line.trimmed().isEmpty() && !line.startsWith(' ')) what = line.trimmed();
    }
    return where + what;
}

inline QProcessEnvironment pythonEnvironment(quint16 port) {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString package = pythonPackage();
    env.insert("PYTHONPATH", package + (env.contains("PYTHONPATH") ? QDir::listSeparator() + env.value("PYTHONPATH") : QString()));
    env.insert("KE_EDITOR_PORT", QString::number(port));
    env.insert("KE_UI_LANGUAGE", UiLanguage::active());
    env.insert("PYTHONUNBUFFERED", "1");
    env.insert("PYTHONUTF8", "1");
    return env;
}
