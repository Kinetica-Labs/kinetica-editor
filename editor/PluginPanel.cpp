// A plugin's panel from its description (see PluginPanel.h).
#include "PluginPanel.h"

#include "InspectorWidgets.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

PluginPanel::PluginPanel(const QJsonObject& d, std::function<void(const QString&, const QJsonObject&)> press, QWidget* parent)
    : QWidget(parent), press_(std::move(press)) {
    setObjectName("pluginPanel");
    title_ = d.value("title").toString("Плагин"), place_ = d.value("place").toString("right");
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(8, 8, 8, 8);
    col->setSpacing(6);
    for (const QJsonValue& c : d.value("children").toArray()) build(c.toObject(), col);
    col->addStretch(1);
    refresh();
}

FieldForm* PluginPanel::form(QBoxLayout* into) {
    if (open_) return open_;
    open_ = new FieldForm([](bool) {}, this); // the values live in values_: nothing else to tell
    forms_.push_back(open_);
    into->addWidget(open_);
    return open_;
}

void PluginPanel::build(const QJsonObject& n, QBoxLayout* into) {
    const QString kind = n.value("kind").toString(), name = n.value("name").toString(), label = n.value("label").toString(name);
    if (n.contains("default") && !values_.contains(name)) values_.insert(name, n.value("default"));
    const double lo = n.value("minimum").toDouble(-1e6), hi = n.value("maximum").toDouble(1e6);
    const auto get = [this, name] { return values_.value(name).toDouble(); };
    const auto set = [this, name](double v) { values_.insert(name, v); };
    if (kind == "number" || kind == "slider" || kind == "toggle" || kind == "choice" || kind == "vector") {
        FieldForm* f = form(into);
        if (kind == "number") {
            const bool whole = n.value("type").toString() == "integer";
            f->number(label, lo, hi, n.value("step").toDouble(whole ? 1 : std::max(0.001, (hi - lo) / 100)), whole ? 0 : 3, get,
                      whole ? std::function<void(double)>([this, name](double v) { values_.insert(name, int(v)); }) : std::function<void(double)>(set));
        } else if (kind == "slider") {
            f->slider(label, int(lo), int(hi), get, [this, name](double v) { values_.insert(name, int(v)); });
        } else if (kind == "toggle") {
            f->toggle(label, [this, name] { return values_.value(name).toBool() ? 1.0 : 0.0; }, [this, name](double v) { values_.insert(name, v != 0); });
        } else if (kind == "choice") {
            QStringList items;
            for (const QJsonValue& e : n.value("enum").toArray()) items << e.toVariant().toString();
            f->choice(label, items, [this, name, items] { return double(std::max<qsizetype>(0, items.indexOf(values_.value(name).toVariant().toString()))); },
                      [this, name, items](double v) { values_.insert(name, items.value(int(v))); });
        } else {
            f->vector(label, lo, hi, 0.01, 3,
                      [this, name] {
                          const QJsonArray a = values_.value(name).toArray();
                          return ke::Vector3(ke::Real(a.at(0).toDouble()), ke::Real(a.at(1).toDouble()), ke::Real(a.at(2).toDouble()));
                      },
                      [this, name](const ke::Vector3& v) { values_.insert(name, QJsonArray{double(v.x), double(v.y), double(v.z)}); });
        }
        if (n.contains("unit")) f->unit(" " + n.value("unit").toString());
        if (n.contains("help")) f->tip(n.value("help").toString());
        return;
    }
    open_ = nullptr; // not a field: the next fields start a form of their own below it
    if (kind == "section") {
        auto* box = new QGroupBox(n.value("title").toString(), this);
        auto* inner = new QVBoxLayout(box);
        inner->setContentsMargins(6, 6, 6, 6);
        for (const QJsonValue& c : n.value("children").toArray()) build(c.toObject(), inner);
        open_ = nullptr;
        into->addWidget(box);
    } else if (kind == "row") {
        auto* row = new QHBoxLayout;
        for (const QJsonValue& c : n.value("children").toArray()) build(c.toObject(), row), open_ = nullptr;
        into->addLayout(row);
    } else if (kind == "label") {
        auto* l = new QLabel(n.value("text").toString(), this);
        l->setWordWrap(true);
        into->addWidget(l);
    } else if (kind == "value") {
        auto* l = new QLabel(this);
        l->setObjectName("pluginValue_" + name);
        l->setWordWrap(true);
        words_.push_back({name, l});
        into->addWidget(l);
        if (n.contains("label")) l->setProperty("caption", n.value("label").toString());
    } else if (kind == "text") {
        auto* line = new QLineEdit(this);
        line->setObjectName("pluginText_" + name);
        line->setPlaceholderText(label);
        texts_.push_back({name, line});
        connect(line, &QLineEdit::textEdited, this, [this, name](const QString& t) { values_.insert(name, t); });
        into->addWidget(line);
    } else if (kind == "button") {
        auto* b = new QPushButton(n.value("label").toString(n.value("command").toString()), this);
        b->setObjectName("pluginButton_" + n.value("command").toString());
        if (n.contains("help")) b->setToolTip(n.value("help").toString());
        connect(b, &QPushButton::clicked, this, [this, command = n.value("command").toString()] { press_(command, values_); });
        into->addWidget(b);
    }
}

void PluginPanel::setValues(const QJsonObject& values) {
    for (auto it = values.begin(); it != values.end(); ++it) values_.insert(it.key(), it.value());
    refresh();
}

void PluginPanel::refresh() {
    for (FieldForm* f : forms_) f->fill();
    for (auto& [name, l] : words_) {
        const QJsonValue v = values_.value(name);
        const QString text = v.isDouble() ? QString::number(v.toDouble(), 'g', 6) : v.toVariant().toString();
        const QString caption = l->property("caption").toString();
        l->setText(caption.isEmpty() ? text : caption + ": " + text);
    }
    for (auto& [name, line] : texts_)
        if (line->text() != values_.value(name).toString()) line->setText(values_.value(name).toString());
}
