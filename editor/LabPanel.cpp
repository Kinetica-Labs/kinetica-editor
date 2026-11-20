// The Laboratory's panel (see LabPanel.h): quick presets, the layers by group, the card, the profiler.
#include "LabPanel.h"
#include "UiLanguage.h"

#include "LabCard.h"
#include "LabLayers.h"
#include "ProfilerBar.h"

#include <QAction>
#include <QCheckBox>
#include <QSignalBlocker>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

// The little colour sample at the end of a layer's row: one square per colour, or a gradient bar from
// the first colour to the last when the layer is coloured by a quantity.
class LayerSwatch : public QWidget {
public:
    LayerSwatch(const std::vector<QColor>& colors, bool gradient, QWidget* parent = nullptr)
        : QWidget(parent), colors_(colors), gradient_(gradient) {
        setFixedSize(gradient ? 34 : int(colors.size()) * 12, 12);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(0, 0, 0, 120), 1.0));
        if (gradient_) {
            QLinearGradient g(0, 0, width(), 0);
            for (size_t i = 0; i < colors_.size(); ++i) g.setColorAt(double(i) / double(colors_.size() - 1), colors_[i]);
            p.setBrush(g);
            p.drawRoundedRect(QRectF(0.5, 1.5, width() - 1, height() - 3), 2, 2);
            return;
        }
        for (size_t i = 0; i < colors_.size(); ++i) {
            p.setBrush(colors_[i]);
            p.drawRoundedRect(QRectF(12.0 * double(i) + 1.5, 1.5, 9, 9), 2, 2);
        }
    }

private:
    std::vector<QColor> colors_;
    bool gradient_;
};

// A section's heading («Быстрые наборы», «Что рисовать»), styled by main.cpp's labStyle().
QLabel* sectionTitle(const QString& text) {
    auto* l = new QLabel(text);
    l->setObjectName("labSection");
    return l;
}

} // namespace

// Top to bottom: two lines of what this is, the presets, the layers (they take the spare height), the
// card, the profiler. The boxes start as the Probe's layers are now.
LabPanel::LabPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("labPanel");
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(10, 8, 10, 10);
    col->setSpacing(8);
    auto* intro = new QLabel(uiText("Kinetica", "Что считает движок — прямо в виде. Щёлкните по контакту или телу: "
                             "его числа появятся в карточке."));
    intro->setObjectName("labIntro");
    intro->setWordWrap(true);
    col->addWidget(intro);
    buildPresets(col);
    col->addWidget(sectionTitle(uiText("Kinetica", "Что рисовать")));
    col->addWidget(buildLayerList(), 1);
    card_ = new LabCard;
    col->addWidget(card_);
    col->addWidget(sectionTitle(uiText("Kinetica", "Время кадра")));
    profiler_ = new ProfilerBar;
    col->addWidget(profiler_);
    showMask(ke::Probe::layers());
}

// «Быстрые наборы»: four buttons in two rows; the one whose set is on now is lit.
void LabPanel::buildPresets(QVBoxLayout* col) {
    col->addWidget(sectionTitle(uiText("Kinetica", "Быстрые наборы")));
    auto* grid = new QGridLayout;
    grid->setSpacing(6);
    int i = 0;
    for (const LabPreset& preset : labPresets()) {
        auto* b = new QPushButton(preset.name);
        b->setObjectName("labPreset");
        b->setCheckable(true);
        b->setToolTip(preset.tip);
        b->setCursor(Qt::PointingHandCursor);
        const QString name = preset.name;
        connect(b, &QPushButton::clicked, this, [this, name] { applyPreset(name); });
        grid->addWidget(b, i / 2, i % 2);
        presets_.push_back(b);
        ++i;
    }
    col->addLayout(grid);
}

// Every group with its layers, in a scroll area of its own: the card and the profiler stay in view.
QWidget* LabPanel::buildLayerList() {
    auto* host = new QWidget;
    host->setObjectName("labLayers");
    auto* col = new QVBoxLayout(host);
    col->setContentsMargins(0, 0, 4, 0);
    col->setSpacing(2);
    for (const QString& group : labGroups()) addGroup(col, group);
    col->addStretch(1);
    auto* scroll = new QScrollArea;
    scroll->setObjectName("labLayerScroll");
    scroll->setMinimumHeight(118); // five layers at least, however tall the card
    scroll->setWidget(host);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return scroll;
}

// One group: its name, then a row per layer - the checkbox with its words, and its colours at the end.
void LabPanel::addGroup(QVBoxLayout* col, const QString& group) {
    auto* caption = new QLabel(group);
    caption->setObjectName("labGroup");
    col->addWidget(caption);
    for (const LabLayer& layer : labLayers()) {
        if (layer.group != group) continue;
        auto* row = new QWidget;
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(6, 0, 2, 0);
        auto* box = new QCheckBox(layer.name);
        box->setObjectName("labLayer");
        box->setToolTip(layer.tip);
        row->setToolTip(layer.tip);
        line->addWidget(box, 1);
        line->addWidget(new LayerSwatch(layer.colors, layer.gradient));
        col->addWidget(row);
        const uint32_t bit = ke::Probe::bit(layer.layer);
        connect(box, &QCheckBox::toggled, this, [this, bit](bool on) {
            if (shownMask_ == ~0u) return; // being filled from the Probe
            setMask(on ? (ke::Probe::layers() | bit) : (ke::Probe::layers() & ~bit));
        });
        boxes_[int(layer.layer)] = box;
    }
}

void LabPanel::addActionRow(const QString& group, QAction* action, const QColor& colour) {
    for (QLabel* caption : findChildren<QLabel*>("labGroup")) {
        auto* col = caption->text() == group && caption->parentWidget() ? qobject_cast<QVBoxLayout*>(caption->parentWidget()->layout()) : nullptr;
        if (!col) continue;
        auto* row = new QWidget;
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(6, 0, 2, 0);
        auto* box = new QCheckBox(action->text());
        box->setObjectName("labActionRow");
        box->setToolTip(action->toolTip());
        box->setChecked(action->isChecked());
        line->addWidget(box, 1);
        line->addWidget(new LayerSwatch({colour}, false));
        col->insertWidget(col->indexOf(caption) + 1, row);
        connect(box, &QCheckBox::toggled, action, &QAction::setChecked);
        connect(action, &QAction::toggled, box, [box](bool on) {
            const QSignalBlocker quiet(box);
            box->setChecked(on);
        });
        return;
    }
}

// A preset replaces the whole set of layers (never adds to it): what it names is exactly what is drawn.
void LabPanel::applyPreset(const QString& name) {
    for (const LabPreset& p : labPresets())
        if (p.name == name) setMask(p.mask);
}

QCheckBox* LabPanel::layerBox(ke::DrawLayer l) const {
    const auto it = boxes_.find(int(l));
    return it != boxes_.end() ? it->second : nullptr;
}

// The one place the panel changes what the engine draws: the Probe, the boxes, the signal.
void LabPanel::setMask(uint32_t mask) {
    ke::Probe::setLayers(mask);
    showMask(mask);
}

// Someone else changed the layers (a scene, a test, «Следить за парой»): the boxes follow.
void LabPanel::syncFromProbe() {
    if (ke::Probe::layers() != shownMask_) showMask(ke::Probe::layers());
}

// The checkboxes and the presets show `mask`; while they are set their signals do not reach the Probe.
void LabPanel::showMask(uint32_t mask) {
    shownMask_ = ~0u;
    for (auto& [layer, box] : boxes_) box->setChecked((mask & ke::Probe::bit(ke::DrawLayer(layer))) != 0);
    for (size_t i = 0; i < presets_.size(); ++i) presets_[i]->setChecked(labPresets()[i].mask == mask);
    shownMask_ = mask;
}
