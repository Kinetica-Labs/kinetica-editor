// A plugin's panel from its description (see PluginPanel.h).
#include "PluginPanel.h"
#include "UiLanguage.h"

#include "InspectorWidgets.h"
#include "TrackChart.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr double kLiveSeconds = 60.0; // a chart shows the run's last minute, sliding
}

SvgView::SvgView(int maxHeight, QWidget* parent) : QWidget(parent), maxHeight_(maxHeight > 0 ? maxHeight : 240) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::OpenHandCursor);
    setToolTip(uiText("Kinetica", "Колесо — приблизить, зажатое колесо или левая кнопка — сдвинуть, двойной щелчок — целиком"));
}

void SvgView::wheelEvent(QWheelEvent* e) {
    const double to = std::clamp(zoom_ * std::pow(1.0015, e->angleDelta().y()), 1.0, 30.0);
    const QPointF at = e->position();
    pan_ = at - (at - pan_) * (to / zoom_); // the point under the mouse stays under it
    if (to == 1.0) pan_ = {};
    zoom_ = to;
    update();
    e->accept(); // the panel does not scroll under a picture being zoomed
}

void SvgView::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton && e->button() != Qt::MiddleButton) return QWidget::mousePressEvent(e); // the middle: as in the 3D view
    dragging_ = true, dragFrom_ = e->position();
    setCursor(Qt::ClosedHandCursor);
}

void SvgView::mouseMoveEvent(QMouseEvent* e) {
    if (!dragging_) return QWidget::mouseMoveEvent(e);
    pan_ += e->position() - dragFrom_, dragFrom_ = e->position();
    update();
}

void SvgView::mouseReleaseEvent(QMouseEvent* e) {
    dragging_ = false;
    setCursor(Qt::OpenHandCursor);
    QWidget::mouseReleaseEvent(e);
}

void SvgView::mouseDoubleClickEvent(QMouseEvent*) {
    zoom_ = 1.0, pan_ = {};
    update();
}

void SvgView::setSvg(const QByteArray& data) {
    if (data == data_) return;
    data_ = data;
    renderer_.load(data); // an invalid one leaves the renderer invalid: the placeholder
    fit();
    update();
}

// Its height from its width, set: a layout in a group box or a scrolled panel asks the size hint, not heightForWidth,
// and would keep the picture small in a wide panel.
void SvgView::fit() {
    const int h = heightForWidth(width());
    if (h != height()) setFixedHeight(h);
}

void SvgView::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    fit();
}

int SvgView::heightForWidth(int w) const {
    const QSizeF s = renderer_.isValid() ? QSizeF(renderer_.defaultSize()) : QSizeF();
    if (s.width() <= 0 || s.height() <= 0) return std::min(maxHeight_, 60);
    return std::clamp(int(std::lround(w * s.height() / s.width())), 1, maxHeight_);
}

void SvgView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QSizeF s = renderer_.isValid() ? QSizeF(renderer_.defaultSize()) : QSizeF();
    if (s.width() <= 0 || s.height() <= 0) {
        p.setPen(palette().color(QPalette::Disabled, QPalette::WindowText));
        p.drawText(rect(), Qt::AlignCenter, uiText("Kinetica", "нет картинки"));
        return;
    }
    const double k = std::min(width() / s.width(), height() / s.height());
    const QSizeF to(s.width() * k, s.height() * k);
    const QRectF whole((width() - to.width()) / 2, (height() - to.height()) / 2, to.width(), to.height());
    // the zoomed frame itself to the renderer: it clips to the frame it is given, so a frame scaled by the painter would
    // cut the picture at the unzoomed one's edges
    renderer_.render(&p, QRectF(pan_ + whole.topLeft() * zoom_, whole.size() * zoom_));
}

PluginPanel::PluginPanel(const QJsonObject& d, std::function<void(const QString&, const QJsonObject&)> press, QWidget* parent)
    : QWidget(parent), press_(std::move(press)) {
    setObjectName("pluginPanel");
    title_ = d.value("title").toString(uiText("Kinetica", "Плагин")), place_ = d.value("place").toString("right");
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(8, 8, 8, 8);
    col->setSpacing(6);
    soon_ = new QTimer(this);
    soon_->setSingleShot(true);
    soon_->setInterval(40);
    connect(soon_, &QTimer::timeout, this, [this] { refresh(); });
    for (const QJsonValue& c : d.value("children").toArray()) build(c.toObject(), col);
    if (charts_.empty()) col->addStretch(1); // a chart takes the room there is
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
    } else if (kind == "svg") {
        auto* v = new SvgView(n.value("height").toInt(), this);
        v->setObjectName("pluginSvg_" + name);
        svgs_.push_back({name, v});
        into->addWidget(v);
    } else if (kind == "chart") {
        auto* view = new TrackChart(this);
        view->setObjectName("pluginChart");
        const QString unit = n.value("unit").toString();
        Chart chart;
        chart.view = view;
        chart.title = n.value("title").toString() + (unit.isEmpty() ? QString() : ", " + unit); // «Ошибка привода, рад»
        chart.x = n.value("x").toString();
        chart.xWords = chart.x.isEmpty() ? QString() : n.value("x_label").toString(chart.x);
        view->setTitle(chart.title, false);
        view->setAxisWords(chart.xWords);
        view->setEmptyText(chart.x.isEmpty() ? uiText("Kinetica", "линии появятся после ▶") : uiText("Kinetica", "точки появятся по ходу обучения"));
        std::vector<PlotTrack> tracks;
        for (const QJsonValue& v : n.value("lines").toArray()) {
            const QString line = v.toObject().value("name").toString();
            const int slot = int(tracks.size());
            auto series = std::make_shared<PlotSeries>();
            series->setKeep(!chart.x.isEmpty()); // a step chart keeps its whole history, not the last minute
            chart.lines.push_back({line, series});
            tracks.push_back({line.toStdString(), v.toObject().value("label").toString(line), unit, plotSlotColor(slot), plotSlotStyle(slot),
                              series.get(), false});
        }
        view->setTracks(std::move(tracks));
        connect(view, &TrackChart::hovered, this, [this, view](double t, QPoint at, int track) { hover(view, t, at, track); });
        connect(view, &TrackChart::hoverLeft, this, [this] { hover(nullptr, std::nan(""), {}, -1); });
        charts_.push_back(std::move(chart));
        into->addWidget(view, 1);
    } else if (kind == "button") {
        auto* b = new QPushButton(n.value("label").toString(n.value("command").toString()), this);
        b->setObjectName("pluginButton_" + n.value("command").toString());
        if (n.contains("help")) b->setToolTip(n.value("help").toString());
        connect(b, &QPushButton::clicked, this, [this, command = n.value("command").toString()] { press_(command, values_); });
        into->addWidget(b);
    }
}

void PluginPanel::setValues(const QJsonObject& values, double time) {
    for (auto it = values.begin(); it != values.end(); ++it) values_.insert(it.key(), it.value());
    for (Chart& c : charts_) {
        if (c.x.isEmpty() || !values.value(c.x).isDouble()) continue;
        const double x = values.value(c.x).toDouble();
        // The step did not advance: a new training (it starts again at the generation the last one ended on), a new
        // history - unless the same x only brings the other lines' values of one step, in several show() calls.
        bool again = x < c.lastX;
        for (auto& [name, line] : c.lines)
            if (x == c.lastX && values.value(name).isDouble() && line->size() > 0 && line->time(line->size() - 1) == x) again = true;
        if (again)
            for (auto& [name, line] : c.lines) line->clear();
        c.lastX = x;
        for (auto& [name, line] : c.lines)
            if (const QJsonValue v = values.value(name); v.isDouble()) line->add(x, v.toDouble());
    }
    if (!std::isfinite(time)) return refresh();
    if (time < now_) // the time went back: a new run, a new history
        for (Chart& c : charts_)
            if (c.x.isEmpty())
                for (auto& [name, line] : c.lines) line->clear();
    now_ = time;
    for (Chart& c : charts_)
        if (c.x.isEmpty())
            for (auto& [name, line] : c.lines)
                if (const QJsonValue v = values.value(name); v.isDouble()) line->add(time, v.toDouble());
    if (!soon_->isActive()) soon_->start();
}

std::vector<SharedChart> PluginPanel::sharedCharts() const {
    std::vector<SharedChart> out;
    for (const Chart& c : charts_) {
        SharedChart s{c.title, c.xWords, c.view->tracks(), {}};
        for (const auto& [name, line] : c.lines) s.keep.push_back(line);
        out.push_back(std::move(s));
    }
    return out;
}

// The mouse over a chart: the dashed line and the dots on every chart of the panel, the line under it in words -
// «Ошибка: 0,02 рад в 2,40 с», or «лучший: 849 м, поколение 12». Off the charts (no chart): gone.
void PluginPanel::hover(TrackChart* chart, double t, QPoint at, int track) {
    for (Chart& c : charts_) c.view->setHoverTime(chart && c.view->axisWords() == chart->axisWords() ? t : std::nan("")); // one axis, one cursor
    const PlotTrack* line = chart && track >= 0 && track < int(chart->tracks().size()) ? &chart->tracks()[size_t(track)] : nullptr;
    const QString text = line ? plotHoverText(*line, t, chart->axisWords()) : QString();
    if (text.isEmpty()) return QToolTip::hideText();
    QToolTip::showText(at + QPoint(14, 12), text, chart);
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
    for (auto& [name, v] : svgs_) v->setSvg(values_.value(name).toString().toUtf8());
    const double from = std::max(0.0, now_ - kLiveSeconds);
    for (Chart& c : charts_) {
        const SharedChart own{c.title, c.xWords, c.view->tracks(), {}};
        if (c.x.isEmpty()) c.view->setWindow(from, std::max(now_, from + 1e-3));
        else c.view->setWindow(own.span().first, own.span().second);
        c.view->refresh();
    }
}
