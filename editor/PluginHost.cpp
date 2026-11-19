// Plugins as processes (see PluginHost.h): the folders scanned, a process a plugin, its commands as the editor's
// operations and buttons, its calls held for it and answered, a saved file restarting it, a fall said on a banner.
#include "PluginHost.h"

#include "PluginPanel.h"
#include "PythonRuntime.h"
#include "SceneBuilder.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QUrl>
#include <QDesktopServices>
#include <QFormLayout>
#include <QJsonArray>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QMainWindow>
#include <QMenu>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QUuid>

#include <cmath>

namespace {

// «cubes» + «cube_row» -> cubes_cube_row: an operation's name, as ops_list, the console and MCP spell it.
QString slug(const QString& s) {
    QString out = s.toLower();
    out.replace(QRegularExpression("[^a-z0-9_]"), "_");
    return out;
}

// The author's line in a traceback: «plugin.py:12: NameError: name 'x' is not defined».
QString lastError(const QStringList& tail) {
    static const QRegularExpression frame(R"re(File "([^"]+)", line (\d+))re");
    QString where, what;
    for (const QString& line : tail) {
        const QRegularExpressionMatch m = frame.match(line);
        if (m.hasMatch() && !m.captured(1).contains("kinetica")) where = QFileInfo(m.captured(1)).fileName() + ":" + m.captured(2) + ": ";
        else if (!line.trimmed().isEmpty() && !line.startsWith(' ')) what = line.trimmed();
    }
    return where + what;
}

// The command's fields as JSON Schema 2020-12: what MCP gives an agent as the tool's inputSchema.
QJsonObject jsonSchema(const QJsonObject& args) {
    QJsonObject properties;
    for (auto it = args.begin(); it != args.end(); ++it) {
        QJsonObject f = it.value().toObject(), p{{"type", f.value("type")}, {"default", f.value("default")}};
        QString about = f.value("label").toString();
        if (f.contains("unit")) about += ", " + f.value("unit").toString();
        if (f.contains("help")) about += ". " + f.value("help").toString();
        p.insert("description", about);
        for (const char* k : {"minimum", "maximum", "enum"})
            if (f.contains(k)) p.insert(k, f.value(k));
        properties.insert(it.key(), p);
    }
    return {{"type", "object"}, {"properties", properties}, {"additionalProperties", false}};
}

QString argsHelp(const QJsonObject& args, const QJsonArray& order) {
    QStringList parts;
    for (const QJsonValue& n : order) {
        const QJsonObject f = args.value(n.toString()).toObject();
        parts << n.toString() + "?: " + f.value("type").toString() + (f.contains("unit") ? " " + f.value("unit").toString() : QString());
    }
    return "{" + parts.join(", ") + "}";
}

} // namespace

PluginHost::PluginHost(EditorOps* ops, SceneBuilder* builder, QWidget* window, QMenu* menu, std::function<quint16()> apiPort,
                       std::function<void(PluginPanel*, bool)> place)
    : QObject(window), ops_(ops), builder_(builder), window_(window), menu_(menu), apiPort_(std::move(apiPort)), place_(std::move(place)),
      watcher_(new QFileSystemWatcher(this)) {
    ops_->setUnknownHint([this](const QString& op) -> QString { // a command of a plugin that is down: why, and what to do
        for (const auto& p : plugins_) {
            if (!op.startsWith(p->name + "_")) continue;
            if (p->state == "упал")
                return "«" + op + "» - команда плагина «" + p->name + "», а он упал: " + (p->error.isEmpty() ? QString("без сообщения") : p->error) +
                       ". Исправьте файл и сохраните - плагин перезапустится сам";
            if (p->state == "запускается") return "плагин «" + p->name + "» ещё запускается: «" + op + "» появится через секунду";
            if (p->state == "остановлен") return "плагин «" + p->name + "» остановлен: plugin_reload {name: \"" + p->name + "\"} запустит его";
        }
        return QString();
    });
    commandsEnd_ = menu_->addSeparator();
    connect(watcher_, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
        QTimer::singleShot(150, this, [this, path] { // an editor writes in steps: the file as it stands a moment later
            if (QFileInfo::exists(path)) watcher_->addPath(path); // a replaced file is a new one to watch
            for (auto& p : plugins_)
                if (p->file == path) {
                    p->reloaded = true;
                    p->since.start();
                    if (p->process) p->restarting = true, stop(*p);
                    else launch(*p);
                }
        });
    });
    addOps();
}

PluginHost::~PluginHost() {
    ops_->setUnknownHint(nullptr);
    for (auto& p : plugins_)
        if (p->process) p->process->disconnect(this), p->process->kill(), p->process->waitForFinished(500);
}

QString PluginHost::userFolder() const { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/plugins"; }

QStringList PluginHost::names() const {
    QStringList out;
    for (const auto& p : plugins_) out << p->name;
    return out;
}

PluginHost::Plugin* PluginHost::find(const QString& name) {
    for (auto& p : plugins_)
        if (p->name == name) return p.get();
    return nullptr;
}

PluginHost::Plugin* PluginHost::authorised(const QJsonObject& a, QJsonObject& refusal) {
    Plugin* p = find(a.value("name").toString());
    if (!p || p->token.isEmpty() || a.value("token").toString() != p->token)
        return refusal = EditorOps::error("a plugin speaks with the name and the token the editor started it with"), nullptr;
    return p;
}

void PluginHost::start(bool onlyGiven) {
    if (const QString given = qEnvironmentVariable("KE_PLUGINS"); !given.isEmpty()) scan(given);
    if (onlyGiven) return;
    for (const QString& folder : folders()) scan(folder); // a plugins' repository, kinetica-plugins (Плагины → Добавить папку…)
    scan(userFolder());
}

QStringList PluginHost::folders() const { return QSettings().value("plugins/folders").toStringList(); }

void PluginHost::addFolder(const QString& folder) {
    QStringList all = folders();
    if (!all.contains(folder)) all << folder, QSettings().setValue("plugins/folders", all);
    scan(folder);
}

void PluginHost::scan(const QString& folder) {
    for (const QFileInfo& d : QDir(folder).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString file = d.absoluteFilePath() + "/plugin.py", name = slug(d.fileName());
        if (!QFileInfo::exists(file) || find(name)) continue;
        auto p = std::make_unique<Plugin>();
        p->name = name, p->file = file;
        watcher_->addPath(file);
        plugins_.push_back(std::move(p));
        launch(*plugins_.back());
    }
}

QString PluginHost::create(const QString& name, QString& error) {
    const QString folder = userFolder() + "/" + slug(name);
    if (slug(name).isEmpty()) return error = "имя плагина: латиница, цифры, _", QString();
    if (QFileInfo::exists(folder)) return error = "плагин «" + slug(name) + "» уже есть: " + folder, QString();
    QFile templ(pythonPackage() + "/kinetica/plugin_template.py");
    if (!templ.open(QIODevice::ReadOnly)) return error = "нет заготовки плагина: " + templ.fileName(), QString();
    QDir().mkpath(folder);
    QFile out(folder + "/plugin.py");
    if (!out.open(QIODevice::WriteOnly)) return error = "не записать " + out.fileName(), QString();
    out.write(QString::fromUtf8(templ.readAll()).replace("PLUGIN_NAME", slug(name)).toUtf8());
    out.close();
    scan(userFolder());
    return out.fileName();
}

void PluginHost::launch(Plugin& p) {
    const quint16 port = apiPort_();
    if (!port) {
        p.state = "нет API";
        return;
    }
    p.token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p.process = new QProcess(this);
    QProcessEnvironment env = pythonEnvironment(port);
    env.insert("KE_PLUGIN_NAME", p.name);
    env.insert("KE_PLUGIN_TOKEN", p.token);
    p.process->setProcessEnvironment(env);
    p.process->setWorkingDirectory(QFileInfo(p.file).absolutePath());
    p.process->setProcessChannelMode(QProcess::MergedChannels); // its prints and its traceback, one tail
    const QString name = p.name;
    connect(p.process, &QProcess::readyReadStandardOutput, this, [this, name] {
        Plugin* q = find(name);
        if (!q || !q->process) return;
        for (const QByteArray& line : q->process->readAllStandardOutput().split('\n'))
            if (!line.trimmed().isEmpty()) q->stderrTail << QString::fromUtf8(line);
        while (q->stderrTail.size() > 40) q->stderrTail.removeFirst();
    });
    connect(p.process, &QProcess::finished, this, [this, name](int code, QProcess::ExitStatus status) {
        if (Plugin* q = find(name)) onExit(*q, status == QProcess::CrashExit ? -1 : code);
    });
    p.state = "запускается";
    p.stderrTail.clear();
    if (!p.since.isValid()) p.since.start();
    p.process->start(pythonInterpreter(), {"-u", p.file});
}

void PluginHost::stop(Plugin& p) {
    if (!p.process) return;
    p.process->terminate();
    if (!p.process->waitForFinished(700)) p.process->kill();
}

// The process ended: its commands go, its calls fail; restarted when its file was saved, said on a banner when it fell.
void PluginHost::onExit(Plugin& p, int code) {
    p.process->readAll();
    p.process->deleteLater();
    p.process = nullptr;
    unregister(p);
    for (auto& [id, reply] : p.open) reply(EditorOps::error("плагин «" + p.name + "» остановился"));
    p.open.clear(), p.queued.clear(), p.waiting = nullptr;
    if (p.restarting) return p.restarting = false, launch(p); // once: a plugin falling at its start falls, it does not loop
    p.state = code == 0 ? "остановлен" : "упал";
    p.error = code == 0 ? QString() : lastError(p.stderrTail);
    if (code != 0) notice(p.name, "Плагин «" + p.name + "» упал: " + (p.error.isEmpty() ? QString("код %1").arg(code) : p.error));
    emit changed();
}

void PluginHost::notice(const QString& plugin, const QString& text) {
    auto* w = qobject_cast<QMainWindow*>(window_);
    if (!w) return;
    if (!notice_) {
        notice_ = new QFrame;
        notice_->setObjectName("pluginNotice");
        notice_->setStyleSheet("QFrame#pluginNotice { background: #4a2323; border: 1px solid #ff6a5a; border-radius: 4px; }");
        auto* row = new QHBoxLayout(notice_);
        row->setContentsMargins(8, 1, 4, 1);
        noticeText_ = new QLabel;
        noticeText_->setObjectName("pluginNoticeText");
        auto* again = new QToolButton;
        again->setText("Перезапустить");
        auto* close = new QToolButton;
        close->setText("✕");
        row->addWidget(noticeText_), row->addWidget(again), row->addWidget(close);
        connect(again, &QToolButton::clicked, this, [this] {
            if (Plugin* q = find(noticeFor_); q && !q->process) q->since.start(), launch(*q);
        });
        connect(close, &QToolButton::clicked, notice_, &QFrame::hide);
        w->statusBar()->insertPermanentWidget(0, notice_, 1);
    }
    noticeFor_ = plugin;
    noticeText_->setText("⚠ " + text);
    notice_->setVisible(!text.isEmpty());
}

void PluginHost::unregister(Plugin& p) {
    if (p.panel) { // its values kept for its return; the panel out of the window
        p.panelValues = p.panel->values();
        place_(p.panel, false);
        p.panel->deleteLater();
        p.panel = nullptr;
    }
    for (Command& c : p.commands) {
        ops_->removePluginOp(c.op);
        delete c.action;
    }
    p.commands.clear();
    delete p.menu;
    p.menu = nullptr;
}

void PluginHost::call(Plugin& p, const QString& command, const QJsonObject& args, const EditorOps::Reply& reply) {
    const int id = ++nextCall_;
    builder_->beginUndoGroup(); // every edit the plugin makes for this call: one step of Ctrl+Z
    p.open[id] = [this, reply](QJsonObject r) { // a call that failed changes nothing: what it had built is taken back
        if (builder_->endUndoGroup(r.value("ok").toBool()))
            r.insert("error", r.value("error").toString() + ". Правки этого вызова отменены: сцена как до него");
        reply(r);
    };
    QJsonObject given;
    for (auto it = args.begin(); it != args.end(); ++it)
        if (!it.key().startsWith('_')) given.insert(it.key(), it.value());
    p.queued.push_back({id, {command, given}});
    deliver(p);
    QTimer::singleShot(30000, this, [this, name = p.name, id] {
        Plugin* q = find(name);
        const auto it = q ? q->open.find(id) : std::map<int, EditorOps::Reply>::iterator();
        if (!q || it == q->open.end()) return;
        const EditorOps::Reply r = it->second;
        q->open.erase(it);
        r(EditorOps::error("плагин «" + name + "» не ответил за 30 с"));
    });
}

void PluginHost::deliver(Plugin& p) {
    if (!p.waiting || p.queued.empty()) return;
    const auto [id, what] = p.queued.front();
    p.queued.erase(p.queued.begin());
    const EditorOps::Reply waiting = std::move(p.waiting);
    p.waiting = nullptr;
    waiting(EditorOps::ok({{"call", id}, {"command", what.first}, {"args", what.second}}));
}

// A button: its fields as a form (each starts from the last value given, else its default), then the call.
void PluginHost::invoke(const QString& plugin, const Command& c) {
    static std::map<QString, QJsonObject> last;
    QJsonObject args = last[c.op];
    const auto send = [this, op = c.op](const QJsonObject& given) {
        ops_->run(op, given, [this](const QJsonObject& r) {
            if (!r.value("ok").toBool())
                if (auto* w = qobject_cast<QMainWindow*>(window_)) w->statusBar()->showMessage(r.value("error").toString(), 10000);
        }, "ui");
    };
    if (c.args.isEmpty()) return send({});
    QDialog form(window_);
    form.setWindowTitle(c.label);
    auto* rows = new QFormLayout(&form);
    std::vector<std::function<void()>> read;
    QStringList fields;
    for (const QJsonValue& n : c.order) fields << n.toString();
    for (auto it = c.args.begin(); it != c.args.end(); ++it)
        if (!fields.contains(it.key())) fields << it.key();
    for (const QString& name : fields) {
        const QJsonObject f = c.args.value(name).toObject();
        const QJsonValue start = args.contains(name) ? args.value(name) : f.value("default");
        const QString type = f.value("type").toString(), unit = f.value("unit").toString();
        QWidget* w = nullptr;
        if (f.contains("enum")) {
            auto* box = new QComboBox;
            for (const QJsonValue& e : f.value("enum").toArray()) box->addItem(e.toVariant().toString(), e.toVariant());
            box->setCurrentIndex(std::max(0, box->findData(start.toVariant())));
            read.push_back([&args, name, box] { args.insert(name, QJsonValue::fromVariant(box->currentData())); });
            w = box;
        } else if (type == "boolean") {
            auto* box = new QCheckBox;
            box->setChecked(start.toBool());
            read.push_back([&args, name, box] { args.insert(name, box->isChecked()); });
            w = box;
        } else if (type == "integer") {
            auto* box = new QSpinBox;
            box->setRange(f.contains("minimum") ? f.value("minimum").toInt() : -1000000, f.contains("maximum") ? f.value("maximum").toInt() : 1000000);
            box->setValue(start.toInt());
            if (!unit.isEmpty()) box->setSuffix(" " + unit);
            read.push_back([&args, name, box] { args.insert(name, box->value()); });
            w = box;
        } else if (type == "string") {
            auto* line = new QLineEdit(start.toString());
            read.push_back([&args, name, line] { args.insert(name, line->text()); });
            w = line;
        } else {
            auto* box = new QDoubleSpinBox;
            box->setDecimals(3);
            box->setRange(f.contains("minimum") ? f.value("minimum").toDouble() : -1e6, f.contains("maximum") ? f.value("maximum").toDouble() : 1e6);
            box->setSingleStep(std::max(0.001, (box->maximum() - box->minimum()) / 100));
            box->setValue(start.toDouble());
            if (!unit.isEmpty()) box->setSuffix(" " + unit);
            read.push_back([&args, name, box] { args.insert(name, box->value()); });
            w = box;
        }
        w->setObjectName("pluginField_" + name);
        if (f.contains("help")) w->setToolTip(f.value("help").toString());
        rows->addRow(f.value("label").toString(name), w);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Выполнить");
    buttons->button(QDialogButtonBox::Cancel)->setText("Отмена"); // Qt's own word is English: one language in one dialog
    rows->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &form, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &form, &QDialog::reject);
    form.setObjectName("pluginForm");
    if (form.exec() != QDialog::Accepted) return;
    for (const auto& r : read) r();
    last[c.op] = args;
    Q_UNUSED(plugin);
    send(args);
}

void PluginHost::addOps() {
    ops_->addOp("plugin_register", "A plugin started by the editor: its commands made operations, buttons of «Плагины» and tools for agents "
                                   "(python/kinetica/plugin.py sends it)",
                "{name: str, token: str, file?: str, commands: [{name, label, key, help, args: {field: {type, default, label, unit, minimum, "
                "maximum, enum}}, order: [field]}], panel?: {…} (its panel: kinetica.plugin.ui)}",
                [this](const QJsonObject& a, const EditorOps::Reply& reply) {
        QJsonObject refusal;
        Plugin* p = authorised(a, refusal);
        if (!p) return reply(refusal);
        unregister(*p);
        QJsonArray made;
        for (const QJsonValue& v : a.value("commands").toArray()) {
            const QJsonObject c = v.toObject();
            Command cmd;
            cmd.name = c.value("name").toString(), cmd.label = c.value("label").toString(cmd.name), cmd.key = c.value("key").toString();
            cmd.help = c.value("help").toString(), cmd.args = c.value("args").toObject(), cmd.order = c.value("order").toArray();
            cmd.op = p->name + "_" + slug(cmd.name);
            const QString plugin = p->name, command = cmd.name;
            ops_->addPluginOp(cmd.op, QString("«%1» - %2%3").arg(cmd.label, cmd.help.isEmpty() ? QString() : cmd.help + " ",
                                                                QString("(plugin %1)").arg(plugin)),
                              argsHelp(cmd.args, c.value("order").toArray()),
                              [this, plugin, command](const QJsonObject& args, const EditorOps::Reply& r) {
                Plugin* q = find(plugin);
                if (!q || !q->process) return r(EditorOps::error("плагин «" + plugin + "» не запущен"));
                call(*q, command, args, r);
            }, jsonSchema(cmd.args));
            cmd.action = new QAction(cmd.label + (cmd.args.isEmpty() ? QString() : "…"), window_);
            cmd.action->setObjectName("plugin:" + cmd.op);
            cmd.action->setToolTip(cmd.help.isEmpty() ? cmd.op : cmd.help);
            if (!cmd.key.isEmpty()) cmd.action->setShortcut(QKeySequence(cmd.key));
            if (!p->menu) { // its own submenu: whose command it is, and its file a click away
                p->menu = new QMenu(p->name, window_);
                p->menu->setObjectName("pluginMenu:" + p->name);
                menu_->insertMenu(commandsEnd_, p->menu);
                p->menu->addSeparator();
                QAction* open = p->menu->addAction("Открыть plugin.py");
                open->setToolTip(p->file);
                connect(open, &QAction::triggered, this, [file = p->file] { QDesktopServices::openUrl(QUrl::fromLocalFile(file)); });
            }
            p->menu->insertAction(p->menu->actions().value(p->menu->actions().size() - 2), cmd.action); // before its separator
            connect(cmd.action, &QAction::triggered, this, [this, plugin, op = cmd.op] {
                if (Plugin* q = find(plugin))
                    for (const Command& x : q->commands)
                        if (x.op == op) return invoke(plugin, x);
            });
            p->commands.push_back(cmd);
            made.append(cmd.op);
        }
        if (a.contains("panel")) { // its panel: a button calls its command with the panel's values the command has
            p->panel = new PluginPanel(a.value("panel").toObject(), [this, plugin = p->name](const QString& command, const QJsonObject& values) {
                Plugin* q = find(plugin);
                for (const Command& c : q ? q->commands : std::vector<Command>()) {
                    if (c.name != command) continue;
                    QJsonObject args;
                    for (auto it = values.begin(); it != values.end(); ++it)
                        if (c.args.contains(it.key())) args.insert(it.key(), it.value());
                    ops_->run(c.op, args, [this](const QJsonObject& r) {
                        if (!r.value("ok").toBool())
                            if (auto* w = qobject_cast<QMainWindow*>(window_)) w->statusBar()->showMessage(r.value("error").toString(), 10000);
                    }, "ui");
                }
            });
            p->panel->setValues(p->panelValues);
            place_(p->panel, true);
        }
        p->state = "работает", p->error.clear();
        if (noticeFor_ == p->name) notice(p->name, {}); // back: its fall is no news any more
        if (p->reloaded) {
            p->reloaded = false;
            if (auto* w = qobject_cast<QMainWindow*>(window_))
                w->statusBar()->showMessage(QString("Плагин «%1» перезапущен за %2 мс").arg(p->name).arg(p->since.elapsed()), 5000);
        }
        emit changed();
        reply(EditorOps::ok({{"ops", made}}));
    });
    ops_->addOp("plugin_panel_set", "A plugin's panel shows these values: its fields' and its words' (a measured number, a state)",
                "{name: str, token: str, values: {name: value}}", [this](const QJsonObject& a, const EditorOps::Reply& reply) {
        QJsonObject refusal;
        Plugin* p = authorised(a, refusal);
        if (!p) return reply(refusal);
        if (!p->panel) return reply(EditorOps::error("the plugin has no panel (ui.panel(...) before run())"));
        p->panel->setValues(a.value("values").toObject());
        reply(EditorOps::ok());
    });
    ops_->addOp("plugin_wait", "A plugin's next call, held until there is one (its kinetica.plugin.run loop)", "{name: str, token: str}",
                [this](const QJsonObject& a, const EditorOps::Reply& reply) {
        QJsonObject refusal;
        Plugin* p = authorised(a, refusal);
        if (!p) return reply(refusal);
        if (p->waiting) p->waiting(EditorOps::error("a newer plugin_wait took its place"));
        p->waiting = reply;
        deliver(*p);
    });
    ops_->addOp("plugin_reply", "A plugin's answer to a call: its result, or the error with its file and line", "{name: str, token: str, call: int, result?: {}, error?: str}",
                [this](const QJsonObject& a, const EditorOps::Reply& reply) {
        QJsonObject refusal;
        Plugin* p = authorised(a, refusal);
        if (!p) return reply(refusal);
        const auto it = p->open.find(a.value("call").toInt());
        if (it == p->open.end()) return reply(EditorOps::error("no such call (answered, or timed out)"));
        const EditorOps::Reply answer = it->second;
        p->open.erase(it);
        if (a.contains("error")) answer(EditorOps::error("плагин «" + p->name + "»: " + a.value("error").toString()));
        else answer(EditorOps::ok(a.value("result").toObject()));
        reply(EditorOps::ok());
    });
    ops_->addOp("plugin_list", "The plugins: name, file, state (работает, запускается, упал, остановлен), last error, commands", "{}",
                [this](const QJsonObject&, const EditorOps::Reply& reply) {
        QJsonArray list;
        for (const auto& p : plugins_) {
            QJsonArray commands;
            for (const Command& c : p->commands) commands.append(QJsonObject{{"op", c.op}, {"label", c.label}, {"key", c.key}});
            list.append(QJsonObject{{"name", p->name}, {"file", p->file}, {"state", p->state}, {"error", p->error}, {"commands", commands}});
        }
        reply(EditorOps::ok({{"plugins", list}, {"folder", userFolder()}}));
    });
    ops_->addOp("plugin_new", "A new plugin from the template in the user's plugins' folder, started: a command with a field, a button in «Плагины»",
                "{name: str}", [this](const QJsonObject& a, const EditorOps::Reply& reply) {
        QString error;
        const QString file = create(a.value("name").toString(), error);
        reply(file.isEmpty() ? EditorOps::error(error) : EditorOps::ok({{"file", file}}));
    });
    ops_->addOp("plugin_reload", "A plugin started again (as a save of its file does)", "{name: str}", [this](const QJsonObject& a, const EditorOps::Reply& reply) {
        Plugin* p = find(a.value("name").toString());
        if (!p) return reply(EditorOps::error("no plugin " + a.value("name").toString()));
        p->reloaded = true;
        p->since.start();
        if (p->process) p->restarting = true, stop(*p);
        else launch(*p);
        reply(EditorOps::ok());
    });
}
