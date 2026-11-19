#pragma once
// The Laboratory's panel (the «Лаборатория» dock): everything the engine can show about itself, in one
// calm column, the way PhysX Visual Debugger lays it out.
//   1. «Быстрые наборы» - four buttons that switch on a whole set of layers at once.
//   2. «Что рисовать» - every debug layer (LabLayers.h), grouped: a checkbox, its colours, and a
//      tooltip saying what it shows and how to read it.
//   3. The card of what was clicked in the view (LabCard.h).
//   4. «Время кадра» - the live profiler (ProfilerBar.h).
// A checkbox sets ke::Probe::setLayers at once; the layer is drawn from the next step on and costs
// nothing while it is off (ke::Probe stores nothing for a layer that is off). Nothing of this is seen
// until the Laboratory is opened: a beginner's screen stays clean.
#include "core/Probe.h"

#include <QWidget>

#include <cstdint>
#include <map>
#include <vector>

class LabCard;
class ProfilerBar;
class QCheckBox;
class QPushButton;
class QVBoxLayout;

class LabPanel : public QWidget {
    Q_OBJECT
public:
    explicit LabPanel(QWidget* parent = nullptr);

    LabCard* card() const { return card_; }
    ProfilerBar* profiler() const { return profiler_; }

    // The checkboxes follow ke::Probe::layers() when someone else changed them (an API call, a test). Cheap: nothing happens when the mask did not change.
    void syncFromProbe();
    // Switch on exactly the layers of the preset with this name («Контакты», ...).
    void applyPreset(const QString& name);
    // Switch these layers on (the others keep their state).
    QCheckBox* layerBox(ke::DrawLayer l) const;

signals:

private:
    void buildPresets(QVBoxLayout* col);
    QWidget* buildLayerList();
    void addGroup(QVBoxLayout* col, const QString& group);
    void setMask(uint32_t mask);
    void showMask(uint32_t mask); // checkboxes and the lit preset, without touching the Probe

    std::map<int, QCheckBox*> boxes_;  // by ke::DrawLayer
    std::vector<QPushButton*> presets_;
    LabCard* card_ = nullptr;
    ProfilerBar* profiler_ = nullptr;
    uint32_t shownMask_ = ~0u;         // what the checkboxes show now
};
