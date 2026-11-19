#pragma once
// A plugin's panel drawn from its description (docs/contracts/editor-plugin.md, «Словарь элементов панели»): the plugin
// sends data, never widgets - section, row, label, value (a live word the plugin sets), button (a command), and the
// fields number, slider, toggle, choice, vector (FieldForm draws them), text. The fields hold the panel's values; a
// button calls its command with those of them the command has (a panel is a form that stays). The plugin changes them
// while it runs (plugin_panel_set): its words, a measured number. Qt lives here only: a plugin does not know it.
#include <QJsonObject>
#include <QWidget>

#include <functional>
#include <vector>

class FieldForm;
class QBoxLayout;
class QLabel;
class QLineEdit;

class PluginPanel : public QWidget {
public:
    // `press`: a button's command and the panel's values (PluginHost picks the command's own of them).
    PluginPanel(const QJsonObject& description, std::function<void(const QString& command, const QJsonObject& values)> press, QWidget* parent = nullptr);
    QString title() const { return title_; }
    QString place() const { return place_; } // "right": a tab of the inspector; "bottom": a tab of the bottom panel
    QJsonObject values() const { return values_; }
    void setValues(const QJsonObject& values); // fields and words: what the plugin says, or the last session's

private:
    void build(const QJsonObject& node, QBoxLayout* into);
    FieldForm* form(QBoxLayout* into); // the fields in a row of the layout share one form
    void refresh();                    // every field and value from values_

    QString title_, place_;
    QJsonObject values_;
    std::function<void(const QString&, const QJsonObject&)> press_;
    std::vector<FieldForm*> forms_;
    std::vector<std::pair<QString, QLabel*>> words_;    // value(name)
    std::vector<std::pair<QString, QLineEdit*>> texts_; // text(name)
    FieldForm* open_ = nullptr;                         // the form fields go into until another kind of element
};
