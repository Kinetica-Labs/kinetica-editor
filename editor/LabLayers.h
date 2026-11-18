#pragma once
// The Laboratory's catalogue of what can be drawn for research (ke::DrawLayer in core/Probe.h): each
// layer's group, its name in plain words, how to read it, the colours it is drawn with, and the quick
// presets («Контакты», «Всё о телах», «Ничего»).
//
// The colours next to a layer are the ones it is drawn with - the contact dots of the view
// (ViewportLab.cpp), the dimmed sleeping bodies (Viewport.cpp), and whatever draws the rigid layers
// into the Probe: a legend that lied would be worse than none, so a drawing of a layer takes its
// colours from here.
#include "core/Probe.h"

#include <QColor>
#include <QString>

#include <cstdint>
#include <vector>

// One checkbox of the Laboratory.
struct LabLayer {
    ke::DrawLayer layer;
    QString group;              // «Контакты», «Тела», ... (labGroups() gives their order)
    QString name;               // what the checkbox says
    QString tip;                // what the layer shows, and how to read its colours
    std::vector<QColor> colors; // the swatch: one little square per colour, or one gradient
    bool gradient = false;      // the colours are the ends of a scale (blue = little, red = much)
};

// Every layer there is, grouped, in the order the panel shows them.
const std::vector<LabLayer>& labLayers();
// The groups in the panel's order.
const std::vector<QString>& labGroups();

// A quick preset: a set of layers switched on together with one click (everything else off).
struct LabPreset {
    QString name;
    QString tip;
    uint32_t mask = 0; // Probe::bit of every layer it turns on
};
const std::vector<LabPreset>& labPresets();
