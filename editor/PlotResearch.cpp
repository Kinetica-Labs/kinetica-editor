// The plots' research instruments (see PlotResearch.h): the object and quantity pickers, the window
// and bins of the histogram, the statistics line and the histogram's CSV.
#include "PlotResearch.h"

#include "core/Format.h"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

PlotResearch::PlotResearch(QWidget* parent) : QWidget(parent) {
    setObjectName("plotResearch");
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(4, 3, 4, 3);
    col->setSpacing(4);
    buildControls();
    buildDistribution();
}

void PlotResearch::buildControls() {
    auto* row = new QGridLayout;
    object_ = new QComboBox;
    object_->setObjectName("plotObject");
    object_->addItem("Вся сцена", -1);
    object_->addItem("Выбранный в сцене", -2);
    object_->setCurrentIndex(1);
    object_->setMinimumWidth(170);
    object_->setToolTip("Следовать выбору мышью или в инспекторе; отдельный объект в списке закрепляет объект исследования");
    quantity_ = new QComboBox;
    quantity_->setObjectName("plotQuantity");
    quantity_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    quantity_->setMinimumContentsLength(20);
    view_ = new QComboBox;
    view_->setObjectName("plotView");
    view_->addItems({"Графики во времени", "Гистограмма и статистика"});
    row->addWidget(new QLabel("Объект:"), 0, 0);
    row->addWidget(object_, 0, 1);
    row->addWidget(new QLabel("Величина:"), 0, 2);
    row->addWidget(quantity_, 0, 3);
    row->addWidget(view_, 0, 4);
    row->setColumnStretch(3, 1);
    qobject_cast<QVBoxLayout*>(layout())->addLayout(row);
    connect(object_, &QComboBox::currentIndexChanged, this, [this] { chooseBody(); });
    connect(quantity_, &QComboBox::currentIndexChanged, this, [this] {
        if (!channel().empty()) emit channelWanted(channel());
        emit analysisChanged();
    });
    connect(view_, &QComboBox::currentIndexChanged, this, [this] {
        distribution_->setVisible(histogram());
        emit analysisChanged();
    });
}

void PlotResearch::buildDistribution() {
    distribution_ = new QWidget;
    auto* col = new QVBoxLayout(distribution_);
    col->setContentsMargins(0, 0, 0, 0);
    auto* row = new QHBoxLayout;
    window_ = new QComboBox;
    window_->setObjectName("histogramWindow");
    for (int seconds : {1, 10, 60}) window_->addItem(QString("Последние %1 с").arg(seconds), seconds);
    window_->addItem("Вся несокращённая запись", 0);
    window_->setCurrentIndex(1);
    bins_ = new QComboBox;
    bins_->setObjectName("histogramBins");
    for (int count : {10, 20, 40, 80}) bins_->addItem(QString::number(count), count);
    bins_->setCurrentIndex(1);
    auto* csv = new QPushButton("CSV гистограммы");
    csv->setObjectName("histogramCsv");
    connect(csv, &QPushButton::clicked, this, &PlotResearch::exportCsv);
    row->addWidget(new QLabel("Интервал:"));
    row->addWidget(window_);
    row->addWidget(new QLabel("Столбцов:"));
    row->addWidget(bins_);
    row->addStretch();
    row->addWidget(csv);
    col->addLayout(row);
    chart_ = new HistogramChart;
    col->addWidget(chart_, 1);
    statistics_ = new QLabel;
    statistics_->setObjectName("histogramStatistics");
    statistics_->setWordWrap(true);
    statistics_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statistics_->setToolTip("Каждый записанный кадр имеет одинаковый вес. σ — стандартное отклонение с делением на N, не погрешность модели. NaN исключены. Старые сохранённые пики исключены.");
    col->addWidget(statistics_);
    qobject_cast<QVBoxLayout*>(layout())->addWidget(distribution_, 1);
    distribution_->hide();
    connect(window_, &QComboBox::currentIndexChanged, this, &PlotResearch::analysisChanged);
    connect(bins_, &QComboBox::currentIndexChanged, this, &PlotResearch::analysisChanged);
}

int PlotResearch::body() const {
    const int id = object_->currentData().toInt();
    return id == -2 ? selected_ : id;
}

QString PlotResearch::bodyLabel() const {
    for (const auto& [id, label] : objects_) if (id == body()) return label;
    return QString();
}

std::string PlotResearch::channel() const { return quantity_->currentData().toString().toStdString(); }
double PlotResearch::windowSeconds() const { return window_->currentData().toDouble(); }
int PlotResearch::binCount() const { return bins_->currentData().toInt(); }
bool PlotResearch::histogram() const { return view_->currentIndex() == 1; }

void PlotResearch::setObjects(const Objects& objects, int selected) {
    if (std::none_of(objects.begin(), objects.end(), [selected](const auto& o) { return o.first == selected; })) selected = -1;
    if (objects_ == objects && selected_ == selected) return;
    const int previous = body(), choice = object_->currentData().toInt();
    objects_ = objects;
    const auto selectedItem = std::find_if(objects_.begin(), objects_.end(), [selected](const auto& o) { return o.first == selected; });
    selected_ = selectedItem == objects_.end() ? -1 : selected;
    {
        QSignalBlocker block(object_);
        object_->clear();
        object_->addItem("Вся сцена", -1);
        const QString chosen = selectedItem == objects_.end() ? QString() : selectedItem->second;
        object_->addItem(selected_ < 0 ? "Выбранный в сцене — не выбран" : "Выбранный: " + chosen, -2);
        for (const auto& [id, label] : objects_) object_->addItem(label, id);
        const int at = object_->findData(choice);
        object_->setCurrentIndex(at < 0 ? 0 : at);
    }
    filterChannels();
    if (previous != body()) emit bodyWanted(body());
    emit analysisChanged();
}

void PlotResearch::resetObjects() {
    setObjects({}, -1);
    { QSignalBlocker block(object_); object_->setCurrentIndex(1); }
    channels_.clear();
    filterChannels();
    setResult({});
}

void PlotResearch::chooseBody() {
    filterChannels();
    emit bodyWanted(body());
    emit analysisChanged();
}

void PlotResearch::setChannels(const std::vector<PlotChannel>& channels) {
    const bool same = channels.size() == channels_.size() && std::equal(channels.begin(), channels.end(), channels_.begin(),
        [](const PlotChannel& a, const PlotChannel& b) { return a.id == b.id && a.name == b.name && a.unit == b.unit; });
    if (same) return;
    channels_ = channels;
    filterChannels();
}

void PlotResearch::filterChannels() {
    const QString previous = quantity_->currentData().toString();
    QSignalBlocker block(quantity_);
    quantity_->clear();
    const std::string prefix = bodyLabel().toStdString() + "/";
    for (const PlotChannel& c : channels_) {
        if (body() >= 0 ? c.id.rfind(prefix, 0) != 0 : c.group == PlotGroup::Object) continue;
        quantity_->addItem(c.name + (c.unit.isEmpty() ? QString() : " (" + c.unit + ")"), QString::fromStdString(c.id));
    }
    int at = quantity_->findData(previous);
    if (at < 0) at = quantity_->findData(QString::fromStdString(body() < 0 ? "scene/mechanical energy" : prefix + "height"));
    if (at >= 0) quantity_->setCurrentIndex(at);
    quantity_->setEnabled(quantity_->count() > 0);
    if (!quantity_->count()) quantity_->addItem("Нет записи — нажмите ▶", QString());
}

void PlotResearch::setResult(const PlotStatistics& result) {
    PlotChannel card;
    for (const auto& c : channels_) if (c.id == channel()) { card = c; break; }
    chart_->setResult(result, card.name, card.unit);
    if (!result.count) { statistics_->setText("Измерения объекта записываются после выбора, пока симуляция идёт."); return; }
    statistics_->setText(QString("N = %1  ·  min = %2  ·  max = %3  ·  среднее = %4  ·  σ = %5 %6\n"
                                "Интервал: %7–%8 с. Вес: один кадр = одно измерение.%9%10")
        .arg(qulonglong(result.count)).arg(numberText(result.minimum), numberText(result.maximum), numberText(result.mean),
             numberText(result.deviation), card.unit, numberText(result.firstTime, 6), numberText(result.lastTime, 6))
        .arg(result.shortened ? " Сокращённая часть истории исключена." : "")
        .arg(result.gaps ? " Есть пропуски записи (NaN исключены)." : ""));
}

namespace {
QString num(double v) { return QString::fromStdString(ke::numberText(v)); } // the classic locale: '.' always
bool saveDistribution(const QString& path, const PlotStatistics& s, const std::string& id, const QString& unit) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream csv(&file);
    csv << "# channel=" << QString::fromStdString(id) << "; unit=" << unit << "; weighting=frame; standard_deviation=population\n";
    csv << "# t_start=" << num(s.firstTime) << "; t_end=" << num(s.lastTime)
        << "; N=" << qulonglong(s.count) << "; mean=" << num(s.mean)
        << "; sigma=" << num(s.deviation) << "; thinned_history_excluded=" << int(s.shortened) << '\n';
    csv << "lower,upper,count,fraction\n";
    const double step = (s.upperEdge - s.lowerEdge) / double(s.bins.size());
    for (size_t i = 0; i < s.bins.size(); ++i)
        csv << num(s.lowerEdge + i * step) << ',' << num(s.lowerEdge + (i + 1) * step)
            << ',' << qulonglong(s.bins[i]) << ',' << num(double(s.bins[i]) / double(s.count)) << '\n';
    csv.flush();
    return csv.status() == QTextStream::Ok;
}
} // namespace

bool PlotResearch::writeCsv(const QString& path) const {
    QString unit;
    for (const auto& card : channels_) if (card.id == channel()) unit = card.unit;
    return result().count > 0 && saveDistribution(path, result(), channel(), unit);
}

void PlotResearch::exportCsv() {
    const PlotStatistics s = result();
    const std::string id = channel();
    QString unit;
    for (const auto& card : channels_) if (card.id == id) unit = card.unit;
    if (!s.count) { QMessageBox::information(this, "Гистограмма", "Нет измерений для экспорта."); return; }
    const QString path = QFileDialog::getSaveFileName(this, "Сохранить гистограмму", "histogram.csv", "CSV (*.csv)");
    if (!path.isEmpty() && !saveDistribution(path, s, id, unit))
        QMessageBox::warning(this, "Гистограмма", "Не удалось полностью записать CSV.");
}
