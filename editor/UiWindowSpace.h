#pragma once
// The composed screenshot and injected input share one logical position for a popup.
// Unattended windows are physically moved off screen; their last on-screen origin remains
// where a person would have seen and clicked them inside the editor's window image.
#include <QVariant>
#include <QWidget>
#include <QApplication>
#include <algorithm>

inline QPoint uiWindowOrigin(const QWidget* window) {
    const QVariant origin = window->property("keSeenOrigin");
    return origin.isValid() ? origin.toPoint() : window->mapToGlobal(QPoint());
}

inline QPoint uiWindowLocal(const QWidget* window, const QPoint& logicalGlobal) {
    return logicalGlobal - uiWindowOrigin(window);
}

inline int uiWindowLayer(const QWidget* window) {
    return window->windowType() == Qt::ToolTip ? 2 : window->windowType() == Qt::Popup ? 1 : 0;
}

// A single paint/input stack: Qt's activePopupWidget can be absent or refer to an older popup
// when a transparent unattended main window opens a menu from its menu bar.
inline QList<QWidget*> uiWindowLayers(QWidget* main) {
    QList<QWidget*> windows;
    for (QWidget* window : QApplication::topLevelWidgets())
        if (window != main && window->isVisible() && window->windowOpacity() > 0) windows.append(window);
    std::stable_sort(windows.begin(), windows.end(), [](const QWidget* a, const QWidget* b) {
        const int al = uiWindowLayer(a), bl = uiWindowLayer(b);
        return al != bl ? al < bl : a->property("keSeenOrder").toULongLong() < b->property("keSeenOrder").toULongLong();
    });
    return windows;
}

inline QWidget* uiPopupWindow(QWidget* main) {
    const auto windows = uiWindowLayers(main);
    for (auto it = windows.crbegin(); it != windows.crend(); ++it)
        if ((*it)->windowType() == Qt::Popup) return *it;
    return nullptr;
}

inline QWidget* uiInputWindow(QWidget* main, const QPoint& logicalGlobal) {
    if (QWidget* popup = uiPopupWindow(main)) return popup; // outside presses dismiss this popup first
    if (QWidget* modal = QApplication::activeModalWidget(); modal && modal->isVisible()) return modal;
    const auto windows = uiWindowLayers(main);
    for (auto it = windows.crbegin(); it != windows.crend(); ++it)
        if (uiWindowLayer(*it) != 2 && QRect(uiWindowOrigin(*it), (*it)->size()).contains(logicalGlobal)) return *it;
    return main;
}
