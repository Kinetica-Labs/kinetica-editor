// The AI assistants on this computer that Kinetica connects itself to, as a table (data/assistants.json; KE_ASSISTANTS:
// another file): assistant_list says which are installed and which have Kinetica's MCP server (tools/ke_editor_mcp.py),
// assistant_connect writes the server into one - under mcpServers of its config file, or by the assistant's own
// command - and assistant_disconnect takes it out. A new assistant is a row of the table, not code. The window's
// «Файл → Подключить ИИ-ассистентов…» calls these (MainWindowPlugins.cpp).
#include "EditorOps.h"
#include "Resources.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QSysInfo>

namespace {

const QString kServer = "ke-editor"; // the server's name in every assistant, as the plugins name it
constexpr int kPort = 47801;         // the port the server asks for (KE_EDITOR_PORT): the window's --api by default

QString system() {
    const QString os = QSysInfo::productType();
    return os == "windows" ? "windows" : os == "macos" || os == "osx" ? "macos" : "linux";
}

QJsonArray table() {
    QString path = qEnvironmentVariable("KE_ASSISTANTS");
    if (path.isEmpty()) path = resourceDir("data") + "/assistants.json";
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object().value("assistants").toArray() : QJsonArray();
}

// The Python the server runs on: on Windows `python` (the Store's empty python3 alias is passed over), else `python3`.
QString python() {
    const QStringList names = system() == "windows" ? QStringList{"python", "py", "python3"} : QStringList{"python3", "python"};
    for (const QString& name : names)
        if (const QString path = QStandardPaths::findExecutable(name); !path.isEmpty() && !path.contains("WindowsApps")) return path;
    return names.front();
}

// The editor's plugins the server starts: KE_PLUGINS, else the folder the installers put them in beside the editor.
QString plugins() {
    if (const QString given = qEnvironmentVariable("KE_PLUGINS"); !given.isEmpty()) return given;
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    const QString folder = (local.isEmpty() ? QDir::homePath() : local) + "/Kinetica/plugins";
    return QDir(folder).exists() ? folder : QString();
}

QString expand(QString s) {
    const QString codexHome = qEnvironmentVariable("CODEX_HOME");
    s.replace("{home}", QDir::homePath()).replace("{appdata}", qEnvironmentVariable("APPDATA"));
    s.replace("{config}", QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
    s.replace("{codex_home}", codexHome.isEmpty() ? QDir::homePath() + "/.codex" : codexHome);
    s.replace("{python}", python()).replace("{server}", resourceDir("tools") + "/ke_editor_mcp.py");
    return s.replace("{port}", QString::number(kPort)).replace("{plugins}", plugins());
}

QString configFile(const QJsonObject& a) {
    const QJsonValue v = a.value("file");
    return expand(v.isObject() ? v.toObject().value(system()).toString() : v.toString());
}

// The assistant's command's arguments; one whose {plugins} is empty goes, with the flag before it.
QStringList arguments(const QJsonArray& words) {
    QStringList out;
    const bool none = plugins().isEmpty();
    for (const QJsonValue& w : words) {
        if (none && w.toString().contains("{plugins}")) {
            if (!out.isEmpty() && out.back().startsWith('-')) out.pop_back();
            continue;
        }
        out << expand(w.toString());
    }
    return out;
}

QJsonObject readJson(const QString& file, bool& readable) {
    QFile f(file);
    readable = true;
    if (!f.exists()) return {};
    QJsonParseError e;
    const QJsonDocument doc = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll(), &e) : QJsonDocument();
    readable = doc.isObject();
    return doc.object();
}

bool found(const QJsonObject& a) {
    if (a.value("how").toString() == "cli") return !QStandardPaths::findExecutable(a.value("command").toString()).isEmpty();
    return QFileInfo(configFile(a)).dir().exists(); // the app made its folder when it was installed and run
}

bool connected(const QJsonObject& a) {
    if (a.value("how").toString() == "json") {
        bool readable = true;
        return readJson(configFile(a), readable).value("mcpServers").toObject().contains(kServer);
    }
    for (const QJsonValue& check : a.value("connected").toArray()) {
        QFile f(expand(check.toObject().value("file").toString()));
        if (f.open(QIODevice::ReadOnly) && f.readAll().contains(check.toObject().value("contains").toString().toUtf8())) return true;
    }
    return false;
}

QJsonObject row(const QJsonObject& a) {
    return {{"app", a.value("app")}, {"name", a.value("name")}, {"about", a.value("about")}, {"found", found(a)}, {"connected", connected(a)}};
}

// The server under mcpServers of the assistant's config file - put there or taken out; the rest of the file as it was.
QString writeJson(const QJsonObject& a, bool on) {
    const QString file = configFile(a);
    bool readable = true;
    QJsonObject config = readJson(file, readable);
    if (!readable) return "the config " + file + " is not JSON: left as it is";
    QJsonObject servers = config.value("mcpServers").toObject();
    if (on) {
        QJsonObject env{{"KE_EDITOR_PORT", QString::number(kPort)}};
        if (!plugins().isEmpty()) env.insert("KE_PLUGINS", plugins());
        servers.insert(kServer, QJsonObject{{"command", python()}, {"args", QJsonArray{expand("{server}")}}, {"env", env}});
    } else {
        servers.remove(kServer);
    }
    config.insert("mcpServers", servers);
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return "cannot write " + file;
    f.write(QJsonDocument(config).toJson(QJsonDocument::Indented));
    return {};
}

} // namespace

void EditorOps::addAssistantOps() {
    const auto assistant = [](const QString& app, QJsonObject& out) {
        QStringList apps;
        for (const QJsonValue& v : table()) {
            if (v.toObject().value("app").toString() == app) return out = v.toObject(), true;
            apps << v.toObject().value("app").toString();
        }
        return out = QJsonObject{{"error", "no assistant «" + app + "»: one of " + apps.join(", ")}}, false;
    };
    addSync("assistant_list", "The AI assistants Kinetica connects itself to, as on this computer: app - its word for assistant_connect, "
                              "name, about, found - installed here, connected - Kinetica's MCP server (ke-editor) is in it. The table: "
                              "data/assistants.json - a new assistant is a row",
            "{}", [](const QJsonObject&) {
        QJsonArray rows;
        for (const QJsonValue& v : table()) rows.append(row(v.toObject()));
        return ok({{"assistants", rows}});
    });
    // Connect or disconnect: the config file at once, the assistant's own command when it ends; its answer says it.
    const auto change = [this, assistant](bool on) {
        return [this, assistant, on](const QJsonObject& args, const Reply& reply) {
            QJsonObject a;
            const QString app = args.value("app").toString();
            if (!assistant(app, a)) return reply(error(a.value("error").toString()));
            if (!found(a)) return reply(error("«" + a.value("name").toString() + "» is not installed on this computer"));
            const QString restart = a.value("restart").toString();
            if (connected(a) == on) return reply(ok({{"app", app}, {"connected", on}, {"note", on ? "already connected" : "not connected"}}));
            if (a.value("how").toString() == "json") {
                if (const QString why = writeJson(a, on); !why.isEmpty()) return reply(error(why));
                return reply(ok({{"app", app}, {"connected", connected(a)}, {"note", restart}}));
            }
            auto* process = new QProcess(this);
            process->setProcessChannelMode(QProcess::MergedChannels);
            connect(process, &QProcess::finished, this, [process, reply, a, app, restart, on](int code) {
                const QString said = QString::fromUtf8(process->readAll()).trimmed().right(400);
                process->deleteLater();
                if (code != 0) return reply(error(a.value("command").toString() + ": " + said));
                const bool now = connected(a);
                // still there after a disconnect: the assistant's plugin «kinetica» holds it - taken off in its own settings
                const QString note = now != on && !on ? "connected by the plugin kinetica: remove it in the assistant's plugins" : restart;
                reply(ok({{"app", app}, {"connected", now}, {"note", note}}));
            });
            process->start(QStandardPaths::findExecutable(a.value("command").toString()), arguments(a.value(on ? "connect" : "disconnect").toArray()));
        };
    };
    add("assistant_connect", "Kinetica's MCP server (ke-editor: tools/ke_editor_mcp.py on this Python, the port 47801, the editor's "
                             "plugins) written into an AI assistant of this computer - app as assistant_list names it: claude, "
                             "claude_code, codex. Answers connected and what to restart",
        "{app: str}", change(true));
    add("assistant_disconnect", "Kinetica's MCP server taken out of an AI assistant (app as assistant_list names it); the rest of "
                                "its settings stay", "{app: str}", change(false));
}
