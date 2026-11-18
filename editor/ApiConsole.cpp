// The API console (ApiConsole.h): every call of the registry as a Python line with its answer, and a line
// to type calls into. A call typed is run through the same registry as the window's and the network's,
// with the source "console", and comes back into the log as every other call does.
#include "ApiConsole.h"

#include "EditorOps.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QCompleter>
#include <QCoreApplication>
#include <QDir>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QScrollBar>

namespace {

// Following the log is explicit; manual reading preserves the scroll position across every append.
void appendLog(QPlainTextEdit* log, const QString& text) {
    auto* follow = log->parentWidget()->findChild<QCheckBox*>("apiConsoleFollow");
    const int vertical = log->verticalScrollBar()->value(), horizontal = log->horizontalScrollBar()->value();
    log->appendHtml(text);
    log->verticalScrollBar()->setValue(follow && follow->isChecked() ? log->verticalScrollBar()->maximum() : vertical);
    log->horizontalScrollBar()->setValue(horizontal);
}

QString pyValue(const QJsonValue& v);

QString pyList(const QJsonArray& a) {
    QStringList items;
    for (const QJsonValue& x : a) items << pyValue(x);
    return "[" + items.join(", ") + "]";
}

QString pyValue(const QJsonValue& v) {
    switch (v.type()) {
    case QJsonValue::Bool: return v.toBool() ? "True" : "False";
    case QJsonValue::Null:
    case QJsonValue::Undefined: return "None";
    case QJsonValue::Double: return QString::number(v.toDouble(), 'g', 10);
    case QJsonValue::String: return QString::fromUtf8(QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
    case QJsonValue::Array: return pyList(v.toArray());
    case QJsonValue::Object: return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    }
    return "None";
}

// Python's argument list as JSON: key= becomes "key":, True/False/None their JSON words, a 'string' a
// "string"; what is inside strings is left as it is.
QString argumentsAsJson(const QString& in) {
    QString out;
    for (int i = 0; i < in.size();) {
        const QChar c = in[i];
        if (c == '"' || c == '\'') {
            int j = i + 1;
            while (j < in.size() && in[j] != c) j += in[j] == '\\' ? 2 : 1;
            QString body = in.mid(i + 1, j - i - 1);
            if (c == '\'') body.replace("\\'", "'").replace("\"", "\\\"");
            out += "\"" + body + "\"";
            i = j + 1;
            continue;
        }
        if (c.isLetter() || c == '_') {
            int j = i;
            while (j < in.size() && (in[j].isLetterOrNumber() || in[j] == '_')) ++j;
            const QString word = in.mid(i, j - i);
            int k = j;
            while (k < in.size() && in[k].isSpace()) ++k;
            if (k < in.size() && in[k] == '=' && (k + 1 >= in.size() || in[k + 1] != '=')) out += "\"" + word + "\":", i = k + 1;
            else out += word == "True" ? "true" : word == "False" ? "false" : word == "None" ? "null" : word, i = j;
            continue;
        }
        out += c;
        ++i;
    }
    return "{" + out + "}";
}

// A long answer in one line: its numbers and words as they are, a list as how many it holds, an
// object as {…} - t = 3.0, shapes [13], sensors [2] instead of every number of the state.
QString summary(const QJsonObject& answer) {
    QStringList parts;
    for (auto it = answer.begin(); it != answer.end(); ++it) {
        const QJsonValue& v = it.value();
        parts << it.key() + (v.isArray() ? QString(" [%1]").arg(v.toArray().size()) : v.isObject() ? QString(" {…}") : "=" + pyValue(v).left(60));
    }
    return parts.join(", ");
}

} // namespace

QString ApiConsole::pythonCall(const QString& name, const QJsonObject& args, int longest) {
    QStringList parts;
    for (auto it = args.begin(); it != args.end(); ++it) {
        QString v = pyValue(it.value());
        if (longest > 0 && it.value().isString() && v.size() > longest) v = v.left(longest) + "…\"";
        parts << it.key() + "=" + v;
    }
    return "ed." + name + "(" + parts.join(", ") + ")";
}

bool ApiConsole::parseCall(const QString& text, QString& name, QJsonObject& args, QString& error) {
    QString t = text.trimmed();
    if (t.startsWith("ed.")) t = t.mid(3);
    int i = 0;
    while (i < t.size() && (t[i].isLetterOrNumber() || t[i] == '_')) ++i;
    name = t.left(i);
    const QString rest = t.mid(i).trimmed();
    if (name.isEmpty()) return error = "нужно имя операции: ed.scene_list()", false;
    QString json = "{}";
    if (rest.startsWith('{')) json = rest;
    else if (rest.startsWith('(') && rest.endsWith(')')) json = argumentsAsJson(rest.mid(1, rest.size() - 2));
    else if (!rest.isEmpty()) return error = "аргументы — в скобках: " + name + "(key=value, ...)", false;
    QJsonParseError bad;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &bad);
    if (!doc.isObject()) return error = "аргументы не читаются: " + bad.errorString(), false;
    args = doc.object();
    return true;
}

ApiConsole::ApiConsole(EditorOps* ops, QWidget* parent) : QWidget(parent), ops_(ops) {
    setObjectName("apiConsole");
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(4, 4, 4, 4);
    log_ = new QPlainTextEdit;
    log_->setObjectName("apiConsoleLog");
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(4000);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    log_->setPlaceholderText("Здесь каждое действие — агента, скрипта, окна и ваше — появляется строкой Python с ответом");
    col->addWidget(log_, 1);
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(">>>"));
    input_ = new QLineEdit;
    input_->setObjectName("apiConsoleInput");
    input_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    input_->setPlaceholderText("scene.add(\"box\")   scene.objects[\"Куб 1\"].move((0, 0.1, 0))   ed.scene_list()   — Python; ↑↓ — прежние");
    QStringList names;
    for (const QJsonValue& op : ops_->list()) names << "ed." + op.toObject().value("name").toString() + "(";
    auto* complete = new QCompleter(names, input_);
    complete->setCaseSensitivity(Qt::CaseInsensitive);
    complete->setFilterMode(Qt::MatchContains);
    input_->setCompleter(complete);
    input_->installEventFilter(this);
    row->addWidget(input_, 1);
    queries_ = new QCheckBox("запросы");
    queries_->setToolTip("Показывать и запросы, которые ничего не меняют: списки, состояние, снимки, пиксели точек");
    row->addWidget(queries_);
    auto* follow = new QCheckBox("Следить");
    follow->setObjectName("apiConsoleFollow");
    follow->setChecked(true);
    follow->setToolTip("Показывать последние команды; отключите, чтобы спокойно читать предыдущие строки");
    connect(follow, &QCheckBox::toggled, log_, [this](bool enabled) {
        if (enabled) log_->verticalScrollBar()->setValue(log_->verticalScrollBar()->maximum());
    });
    row->addWidget(follow);
    auto* clear = new QPushButton("Очистить");
    connect(clear, &QPushButton::clicked, log_, &QPlainTextEdit::clear);
    row->addWidget(clear);
    col->addLayout(row);
    connect(input_, &QLineEdit::returnPressed, this, &ApiConsole::submit);
    ops_->addListener([this](const QString& source, const QString& name, const QJsonObject& args, const QJsonObject& answer, bool query) {
        show(source, name, args, answer, query);
    });
}

// One call: who made it, its Python line, and under it its answer (a long one cut short).
void ApiConsole::show(const QString& source, const QString& name, const QJsonObject& args, const QJsonObject& answer, bool query) {
    if (name == "console_note") { // an agent's reasoning between its calls: a comment, no answer under it
        appendLog(log_, QString("<span style='color:#c9a0ff'># %1</span>").arg(args.value("text").toString().toHtmlEscaped()));
        return;
    }
    if (query && source != "console" && !queries_->isChecked()) return;
    const QString who = source == "console" ? "вы" : source == "ui" ? "окно" : "агент";
    const QString colour = source == "console" ? "#7fb8ff" : source == "ui" ? "#9aa0a8" : "#ffb454";
    QJsonObject plain = args;
    const QString line = plain.take("_py").toString(); // the object model's own line (kinetica), when it sent one
    appendLog(log_, QString("<span style='color:%1'>[%2]</span> %3").arg(colour, who, (line.isEmpty() ? pythonCall(name, plain, 120) : line).toHtmlEscaped()));
    QJsonObject shown = answer;
    shown.remove("ok");
    QString text = answer.value("ok").toBool() ? QString::fromUtf8(QJsonDocument(shown).toJson(QJsonDocument::Compact)) : answer.value("error").toString();
    if (answer.value("ok").toBool() && text.size() > 160) text = summary(shown); // a state: its numbers and how many of each list
    appendLog(log_, QString("<span style='color:%1'>&nbsp;&nbsp;%2 %3</span>")
                         .arg(answer.value("ok").toBool() ? "#8fd18f" : "#ff7b72", answer.value("ok").toBool() ? "→" : "✗", text.toHtmlEscaped()));
}

void ApiConsole::submit() {
    const QString line = input_->text().trimmed();
    if (line.isEmpty()) return;
    history_ << line;
    historyAt_ = int(history_.size());
    input_->clear();
    if (startPython()) { // the line runs in the console's Python; the calls it makes show under it as yours
        appendLog(log_, QString("<span style='color:#7fb8ff'>&gt;&gt;&gt;</span> %1").arg(line.toHtmlEscaped()));
        python_->write(line.toUtf8() + "\n");
        return;
    }
    QString name, error;
    QJsonObject args;
    if (!parseCall(line, name, args, error)) {
        appendLog(log_, QString("<span style='color:#ff7b72'>✗ %1</span>").arg(error.toHtmlEscaped()));
        return;
    }
    ops_->run(name, args, [](const QJsonObject&) {}, "console");
}

// Up and Down walk the lines typed before.
bool ApiConsole::eventFilter(QObject* watched, QEvent* event) {
    if (watched == input_ && event->type() == QEvent::KeyPress && !history_.isEmpty()) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Up || key == Qt::Key_Down) {
            historyAt_ = std::clamp(historyAt_ + (key == Qt::Key_Up ? -1 : 1), 0, int(history_.size()));
            input_->setText(historyAt_ < int(history_.size()) ? history_[historyAt_] : QString());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

bool ApiConsole::startPython() {
    if (python_ && python_->state() == QProcess::Running) return true;
    const quint16 port = noPython_ || !apiPort_ ? 0 : apiPort_();
    if (port == 0) return false;
    python_ = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString installed = QCoreApplication::applicationDirPath() + "/python"; // the kinetica package: next to the program, else the sources
    const QString package = QDir(installed).exists() ? installed : QString(KE_PYTHON);
    env.insert("PYTHONPATH", package + (env.contains("PYTHONPATH") ? QDir::listSeparator() + env.value("PYTHONPATH") : QString()));
    env.insert("KE_EDITOR_PORT", QString::number(port));
    python_->setProcessEnvironment(env);
    connect(python_, &QProcess::readyReadStandardOutput, this, &ApiConsole::readPython);
    connect(python_, &QProcess::readyReadStandardError, this, [this] {
        for (const QByteArray& l : python_->readAllStandardError().split('\n'))
            if (!l.trimmed().isEmpty()) appendLog(log_, QString("<span style='color:#ff7b72'>%1</span>").arg(QString::fromUtf8(l).toHtmlEscaped()));
    });
    python_->start("python3", {"-u", "-m", "kinetica.console"});
    if (python_->waitForStarted(3000)) return true;
    appendLog(log_, "<span style='color:#ff7b72'>✗ Python не запустился (python3): строка — один вызов, как ed.scene_list()</span>");
    python_->deleteLater();
    python_ = nullptr;
    noPython_ = true;
    return false;
}

void ApiConsole::readPython() {
    pending_ += python_->readAllStandardOutput();
    for (int at = pending_.indexOf('\n'); at >= 0; at = pending_.indexOf('\n')) {
        const QString line = QString::fromUtf8(pending_.left(at));
        pending_.remove(0, at + 1);
        if (line == "\x1e") continue; // the end of a line's answer
        appendLog(log_, QString("<span style='color:%1'>&nbsp;&nbsp;%2</span>").arg(line.startsWith("✗") ? "#ff7b72" : "#d0d4da", line.toHtmlEscaped()));
    }
}

