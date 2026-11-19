#pragma once
// Small building blocks of the inspector, shared by every kind of thing it shows (a shape today, a
// camera or a light later): three numbers in a row (x, y, z), a spin box that reports only real
// edits, a section with a header that folds open and shut, a small-caps section title, and a form of
// fields declared once (FieldForm).
#include "math/Vector3.h"

#include <QDoubleSpinBox>
#include <QFrame>
#include <QIcon>
#include <QWidget>

#include <functional>
#include <vector>

class QFormLayout;
class QLabel;
class QPushButton;
class QToolButton;
class QVBoxLayout;

// A number field for the inspector: fixed range, step and decimals; typing counts only on Enter.
QDoubleSpinBox* makeSpin(double min, double max, double step, int decimals, const QString& suffix = {});

// Several objects selected and this number differs between them: the field shows "—" until it is
// given a value (then that value goes to all of them). clearMixed() on a panel gives every field of
// it its own range back (and a checkbox marked mixed its own text) before the panel is filled again.
void showMixed(QDoubleSpinBox* s);
void clearMixed(QWidget* panel);

// A small-caps title that starts a group of fields ("ОБЪЕКТ", "ФОРМА").
QLabel* sectionTitle(const QString& text);

// Three numbers in one row: position, size, rotation, velocity, gravity.
class Vec3Row : public QWidget {
    Q_OBJECT
public:
    Vec3Row(double min, double max, double step, int decimals, QWidget* parent = nullptr);
    void setValue(const ke::Vector3& v); // does not report a change
    ke::Vector3 value() const;
    QDoubleSpinBox* spin(int axis) const { return spins_[axis]; } // to show one of them as mixed

signals:
    void edited(); // the user changed one of the three numbers

private:
    QDoubleSpinBox* spins_[3];
};

// A section whose details fold away: a header button with an arrow and a title, and a body that
// shows only when opened. Starts closed, so a beginner sees titles, not forty fields.
class CollapsibleSection : public QWidget {
    Q_OBJECT
public:
    CollapsibleSection(const QString& title, const QIcon& icon = {}, QWidget* parent = nullptr);
    QVBoxLayout* body() { return bodyLayout_; }
    void setOpen(bool open);
    bool isOpen() const { return open_; }

private:
    QPushButton* header_ = nullptr;
    QWidget* bodyWidget_ = nullptr;
    QVBoxLayout* bodyLayout_ = nullptr;
    bool open_ = false;
};

// A component of an object as a card, as in Unity's inspector: a header with the component's icon
// and title that folds the card open and shut, and an ✕ that removes the component. Opens when
// added, so the numbers of what was just added are in view.
class ComponentCard : public QFrame {
    Q_OBJECT
public:
    ComponentCard(const QString& title, const QIcon& icon, QWidget* parent = nullptr);
    QVBoxLayout* body() { return bodyLayout_; }
    void setOpen(bool open);
    bool isOpen() const { return open_; }
    const QString& title() const { return title_; }
    void setIcon(const QIcon& icon);

signals:
    void removeClicked();

private:
    QPushButton* header_ = nullptr;
    QToolButton* remove_ = nullptr;
    QWidget* bodyWidget_ = nullptr;
    QVBoxLayout* bodyLayout_ = nullptr;
    QString title_;
    bool open_ = true;
};

// One sentence saying why something does not work (yet) and, where possible, a button that fixes
// it: "Дыму нужен воздух — [Включить газ]". No dialog to click away: the banner sits in the panel
// until the cause is gone.
class InlineBanner : public QFrame {
    Q_OBJECT
public:
    explicit InlineBanner(QWidget* parent = nullptr);
    void showMessage(const QString& text, const QString& buttonText = {}, std::function<void()> fix = {});
    void clearMessage();
    QString message() const; // what it says now (empty: hidden)

private:
    QLabel* text_ = nullptr;
    QPushButton* button_ = nullptr;
    std::function<void()> fix_;
};

// A card's fields declared once, as the plugin SDK's number, toggle, choice and label (docs/contracts/editor-plugin.md):
// each says how it reads itself from the thing the card shows and writes itself into it, its range, its unit and when
// it shows. The form builds the rows, fills them (fill), and on a person's edit writes the field and calls `edited` -
// `click` for a toggle or a choice (other fields may show now), not for a typed number (the next one joins its undo);
// its notes and labels follow every edit. A card is a list of fields, not hand-made widgets and their wiring.
// Several things selected: fill shows "—" where they differ, and repeat() writes the one field just edited into the
// next of them - the rest of each stays its own. Modifiers apply to the row added last.
class FieldForm : public QWidget {
public:
    struct Unit { QString suffix; double scale = 1, step = 0; }; // shown = stored * scale; step 0: the field's own
    struct Option { QString text; double value; };
    struct Tile { QIcon icon; QString text, tip = {}; }; // a tile without words of its own: no tip
    using Get = std::function<double()>;
    using Set = std::function<void(double)>;
    using GetVector = std::function<ke::Vector3()>;
    using SetVector = std::function<void(const ke::Vector3&)>;
    using Text = std::function<QString()>;
    explicit FieldForm(std::function<void(bool click)> edited, QWidget* parent = nullptr);
    FieldForm& number(const QString& label, double min, double max, double step, int decimals, Get get, Set set);
    FieldForm& vector(const QString& label, double min, double max, double step, int decimals, GetVector get, SetVector set);
    FieldForm& toggle(const QString& label, Get get, Set set); // get and set: 0 or 1
    FieldForm& choice(const QString& label, const QStringList& items, Get get, Set set); // the item's index
    FieldForm& choice(const QString& label, std::function<std::vector<Option>()> options, Get get, Set set); // its value
    FieldForm& tiles(const std::vector<Tile>& tiles, int iconSize, Get get, Set set); // icon buttons, one pressed: its index
    // The same over a number, flag, vector or kind the fields read and write in place (a card's draft).
    template <class N> FieldForm& number(const QString& label, double min, double max, double step, int decimals, N& value) {
        return number(label, min, max, step, decimals, [p = &value] { return double(*p); }, [p = &value](double v) { *p = N(v); });
    }
    FieldForm& vector(const QString& label, double min, double max, double step, int decimals, ke::Vector3& value) {
        return vector(label, min, max, step, decimals, [p = &value] { return *p; }, [p = &value](const ke::Vector3& v) { *p = v; });
    }
    FieldForm& toggle(const QString& label, bool& value) {
        return toggle(label, [p = &value] { return *p ? 1.0 : 0.0; }, [p = &value](double v) { *p = v != 0; });
    }
    template <class E> FieldForm& tiles(const std::vector<Tile>& tiles, int iconSize, E& kind) {
        return this->tiles(tiles, iconSize, [p = &kind] { return double(int(*p)); }, [p = &kind](double v) { *p = E(int(v)); });
    }
    // A slider of whole steps, its value in words beside it; let go within `detent` of 0, it is 0.
    FieldForm& slider(const QString& label, int min, int max, Get get, Set set, int detent = 0);
    FieldForm& note(Text text); // a line of words, word-wrapped
    FieldForm& unit(std::function<Unit()> unit);
    FieldForm& unit(const QString& suffix, double scale = 1); // the same for every thing
    FieldForm& shown(std::function<bool()> shown);
    FieldForm& label(Text text); // a toggle's own text, else the row's
    FieldForm& name(const QString& objectName); // a row of tiles: every tile's
    FieldForm& tip(const QString& text);
    // The values, units, texts and rows shown from the thing; reports nothing. `load(i)` puts the i-th of `count`
    // things into what the fields read (0, the active one, last); a field where they differ shows "—".
    void fill(int count = 1, const std::function<void(int)>& load = {});
    void repeat() const; // the last edit once more, into what the fields read now

private:
    struct Row {
        Row(QWidget* w, double s = 0, Get g = {}, Set f = {}) : widget(w), input(w), step(s), get(std::move(g)), set(std::move(f)) {}
        QWidget* widget; // the row's
        QWidget* input;  // what holds the value (a slider's own, inside its row)
        double step;
        Get get;
        Set set;
        GetVector getVector;
        SetVector setVector;
        std::function<Unit()> unit;
        std::function<bool()> shown;
        Text text;
        std::function<std::vector<Option>()> options;
        std::vector<double> values; // a choice's items' values as filled
        std::vector<double> read() const; // what the thing holds: none, one number or three
    };
    struct Edit { size_t row = size_t(-1); int axis = 0; double value = 0; };
    FieldForm& add(const QString& label, Row row);
    void report(size_t row, double value, bool click, int axis = 0);
    void show(Row& r);
    void showMixed(Row& r, const std::vector<bool>& mixed);
    void texts(); // the notes, labels and choices' words from the thing
    QFormLayout* layout_;
    std::vector<Row> rows_;
    std::function<void(bool)> edited_;
    Edit last_;
    bool filling_ = false;
};
