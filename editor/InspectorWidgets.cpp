// The inspector's building blocks (see InspectorWidgets.h).
#include "InspectorWidgets.h"

#include <QBoxLayout>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSignalBlocker>
#include <QToolButton>

#include <algorithm>
#include <cmath>

QDoubleSpinBox* makeSpin(double min, double max, double step, int decimals, const QString& suffix) {
    auto* s = new QDoubleSpinBox;
    s->setRange(min, max);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    s->setKeyboardTracking(false); // typing "1.5" is one edit, not three
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    s->setMinimumWidth(52);
    if (!suffix.isEmpty()) s->setSuffix(" " + suffix);
    return s;
}

// The special value text of a spin box is shown when it holds its minimum: the minimum is moved
// one step below the real range for as long as the field is mixed (the real one kept in a property).
void showMixed(QDoubleSpinBox* s) {
    if (!s->property("realMinimum").isValid()) s->setProperty("realMinimum", s->minimum());
    const QSignalBlocker quiet(s);
    s->setMinimum(s->property("realMinimum").toDouble() - s->singleStep());
    s->setSpecialValueText("—");
    s->setValue(s->minimum());
}

void clearMixed(QWidget* panel) {
    for (QDoubleSpinBox* s : panel->findChildren<QDoubleSpinBox*>()) {
        if (!s->property("realMinimum").isValid()) continue;
        const QSignalBlocker quiet(s);
        s->setSpecialValueText(QString());
        s->setMinimum(s->property("realMinimum").toDouble());
    }
    for (QCheckBox* c : panel->findChildren<QCheckBox*>()) {
        if (!c->property("realText").isValid()) continue;
        c->setText(c->property("realText").toString());
        c->setProperty("realText", QVariant());
    }
}

QLabel* sectionTitle(const QString& text) {
    auto* l = new QLabel(text);
    QFont f = l->font();
    f.setCapitalization(QFont::SmallCaps);
    f.setLetterSpacing(QFont::PercentageSpacing, 108);
    f.setBold(true);
    l->setFont(f);
    l->setObjectName("sectionTitle");
    return l;
}

Vec3Row::Vec3Row(double min, double max, double step, int decimals, QWidget* parent) : QWidget(parent) {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    const char* axes[] = {"x", "y", "z"};
    const char* tints[] = {"#e8706a", "#7ccf6e", "#6aa6f0"}; // the axis colours of 3D packages
    for (int k = 0; k < 3; ++k) {
        spins_[k] = makeSpin(min, max, step, decimals);
        spins_[k]->setPrefix(QString(axes[k]) + "  ");
        spins_[k]->setStyleSheet(QString("QDoubleSpinBox { border-left: 3px solid %1; }").arg(tints[k]));
        connect(spins_[k], &QDoubleSpinBox::valueChanged, this, [this] { emit edited(); });
        row->addWidget(spins_[k], 1);
    }
}

void Vec3Row::setValue(const ke::Vector3& v) {
    for (int k = 0; k < 3; ++k) {
        const QSignalBlocker quiet(spins_[k]);
        spins_[k]->setValue(v[k]);
    }
}

ke::Vector3 Vec3Row::value() const {
    return ke::Vector3(float(spins_[0]->value()), float(spins_[1]->value()), float(spins_[2]->value()));
}

CollapsibleSection::CollapsibleSection(const QString& title, const QIcon& icon, QWidget* parent) : QWidget(parent) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(4);
    header_ = new QPushButton(icon, title);
    header_->setIconSize(QSize(20, 20));
    header_->setFlat(true);
    header_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    header_->setObjectName("sectionHeader");
    connect(header_, &QPushButton::clicked, this, [this] { setOpen(!open_); });
    col->addWidget(header_);
    bodyWidget_ = new QWidget;
    bodyLayout_ = new QVBoxLayout(bodyWidget_);
    bodyLayout_->setContentsMargins(26, 2, 2, 6);
    bodyLayout_->setSpacing(6);
    col->addWidget(bodyWidget_);
    setOpen(false);
}

void CollapsibleSection::setOpen(bool open) {
    open_ = open;
    bodyWidget_->setVisible(open);
    const QString t = header_->text();
    const QString bare = t.startsWith(QChar(0x25B8)) || t.startsWith(QChar(0x25BE)) ? t.mid(2) : t;
    header_->setText(QString(open ? QChar(0x25BE) : QChar(0x25B8)) + " " + bare); // ▾ open, ▸ closed
}

void ComponentCard::setIcon(const QIcon& icon) { header_->setIcon(icon); }

ComponentCard::ComponentCard(const QString& title, const QIcon& icon, QWidget* parent) : QFrame(parent), title_(title) {
    setObjectName("componentCard");
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(6, 4, 6, 6);
    col->setSpacing(4);
    auto* top = new QHBoxLayout;
    top->setSpacing(2);
    header_ = new QPushButton(icon, title);
    header_->setIconSize(QSize(22, 22));
    header_->setFlat(true);
    header_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    header_->setObjectName("sectionHeader");
    connect(header_, &QPushButton::clicked, this, [this] { setOpen(!open_); });
    remove_ = new QToolButton;
    remove_->setText(QString(QChar(0x2715))); // ✕
    remove_->setObjectName("cardRemove");
    remove_->setToolTip("Убрать компонент «" + title + "»");
    connect(remove_, &QToolButton::clicked, this, &ComponentCard::removeClicked);
    top->addWidget(header_, 1);
    top->addWidget(remove_);
    col->addLayout(top);
    bodyWidget_ = new QWidget;
    bodyLayout_ = new QVBoxLayout(bodyWidget_);
    bodyLayout_->setContentsMargins(8, 2, 2, 2);
    bodyLayout_->setSpacing(6);
    col->addWidget(bodyWidget_);
    setOpen(true);
}

void ComponentCard::setOpen(bool open) {
    open_ = open;
    bodyWidget_->setVisible(open);
    header_->setText(QString(open ? QChar(0x25BE) : QChar(0x25B8)) + " " + title_); // ▾ open, ▸ closed
}

InlineBanner::InlineBanner(QWidget* parent) : QFrame(parent) {
    setObjectName("inlineBanner");
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(8, 6, 6, 6);
    row->setSpacing(8);
    text_ = new QLabel;
    text_->setWordWrap(true);
    button_ = new QPushButton;
    button_->setObjectName("bannerButton");
    connect(button_, &QPushButton::clicked, this, [this] {
        if (fix_) fix_();
    });
    row->addWidget(text_, 1);
    row->addWidget(button_);
    setVisible(false);
}

void InlineBanner::showMessage(const QString& text, const QString& buttonText, std::function<void()> fix) {
    text_->setText(text);
    button_->setText(buttonText);
    button_->setVisible(!buttonText.isEmpty() && fix);
    fix_ = std::move(fix);
    setVisible(true);
}

QString InlineBanner::message() const { return isHidden() ? QString() : text_->text(); }

void InlineBanner::clearMessage() {
    fix_ = nullptr;
    setVisible(false);
}

FieldForm::FieldForm(std::function<void(bool)> edited, QWidget* parent) : QWidget(parent), layout_(new QFormLayout(this)), edited_(std::move(edited)) {
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(6);
}

FieldForm& FieldForm::add(const QString& label, Row row) {
    QWidget* w = row.widget;
    rows_.push_back(std::move(row));
    if (label.isEmpty()) layout_->addRow(w);
    else layout_->addRow(label, w);
    return *this;
}

std::vector<double> FieldForm::Row::read() const {
    if (getVector) {
        const ke::Vector3 v = getVector();
        return {double(v.x), double(v.y), double(v.z)};
    }
    return get ? std::vector<double>{get()} : std::vector<double>{};
}

void FieldForm::report(size_t k, double value, bool click, int axis) {
    if (filling_) return;
    const Row& r = rows_[k];
    last_ = {k, axis, qobject_cast<QComboBox*>(r.widget) ? r.values[size_t(value)] : value / (r.unit ? r.unit().scale : 1)};
    repeat();
    edited_(click);
    texts();
}

void FieldForm::repeat() const {
    if (last_.row >= rows_.size()) return;
    const Row& r = rows_[last_.row];
    if (!r.setVector) return r.set(last_.value);
    ke::Vector3 v = r.getVector();
    v[last_.axis] = ke::Real(last_.value);
    r.setVector(v);
}

FieldForm& FieldForm::number(const QString& label, double min, double max, double step, int decimals, Get get, Set set) {
    QDoubleSpinBox* s = makeSpin(min, max, step, decimals);
    connect(s, &QDoubleSpinBox::valueChanged, this, [this, k = rows_.size()](double v) { report(k, v, false); });
    return add(label, {s, step, std::move(get), std::move(set)});
}

FieldForm& FieldForm::vector(const QString& label, double min, double max, double step, int decimals, GetVector get, SetVector set) {
    auto* v = new Vec3Row(min, max, step, decimals);
    for (int axis = 0; axis < 3; ++axis) // an edit is the one number typed: with several selected the other two stay each its own
        connect(v->spin(axis), &QDoubleSpinBox::valueChanged, this, [this, k = rows_.size(), axis](double x) { report(k, x, false, axis); });
    Row r{v, step};
    r.getVector = std::move(get);
    r.setVector = std::move(set);
    return add(label, std::move(r));
}

FieldForm& FieldForm::toggle(const QString& label, Get get, Set set) {
    auto* box = new QCheckBox(label);
    connect(box, &QCheckBox::toggled, this, [this, k = rows_.size()](bool on) { report(k, on ? 1 : 0, true); });
    return add({}, {box, 0, std::move(get), std::move(set)});
}

FieldForm& FieldForm::choice(const QString& label, std::function<std::vector<Option>()> options, Get get, Set set) {
    auto* box = new QComboBox;
    connect(box, &QComboBox::currentIndexChanged, this, [this, k = rows_.size()](int i) { if (i >= 0) report(k, i, true); });
    Row row{box, 0, std::move(get), std::move(set)};
    row.options = std::move(options);
    return add(label, std::move(row));
}

FieldForm& FieldForm::choice(const QString& label, const QStringList& items, Get get, Set set) {
    return choice(label, [items] {
        std::vector<Option> out;
        for (int i = 0; i < items.size(); ++i) out.push_back({items[i], double(i)});
        return out;
    }, std::move(get), std::move(set));
}

FieldForm& FieldForm::tiles(const std::vector<Tile>& tiles, int iconSize, Get get, Set set) {
    auto* row = new QWidget;
    auto* line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(3);
    auto* group = new QButtonGroup(row);
    for (const Tile& t : tiles) {
        auto* b = new QToolButton;
        b->setCheckable(true);
        b->setIcon(t.icon);
        b->setIconSize(QSize(iconSize, iconSize));
        b->setToolTip(t.tip);
        if (!t.text.isEmpty()) b->setText(t.text), b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        group->addButton(b, group->buttons().size());
        line->addWidget(b);
    }
    line->addStretch(1);
    connect(group, &QButtonGroup::idClicked, this, [this, k = rows_.size()](int i) { report(k, i, true); });
    return add({}, {row, 0, std::move(get), std::move(set)});
}

FieldForm& FieldForm::slider(const QString& label, int min, int max, Get get, Set set, int detent) {
    auto* row = new QWidget;
    auto* line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    auto* s = new QSlider(Qt::Horizontal);
    s->setRange(min, max);
    auto* words = new QLabel;
    words->setMinimumWidth(48);
    line->addWidget(s, 1);
    line->addWidget(words);
    const size_t k = rows_.size();
    connect(s, &QSlider::valueChanged, this, [this, k, words](int v) {
        words->setText(QString::number(v) + (rows_[k].unit ? rows_[k].unit().suffix : QString()));
        report(k, v, false);
    });
    connect(s, &QSlider::sliderReleased, this, [s, detent] { if (s->value() != 0 && std::abs(s->value()) <= detent) s->setValue(0); });
    Row r{row, 0, std::move(get), std::move(set)};
    r.input = s;
    return add(label, std::move(r));
}

FieldForm& FieldForm::note(Text text) {
    auto* words = new QLabel;
    words->setWordWrap(true);
    Row row{words};
    row.text = std::move(text);
    return add({}, std::move(row));
}

FieldForm& FieldForm::unit(std::function<Unit()> unit) { return rows_.back().unit = std::move(unit), *this; }
FieldForm& FieldForm::unit(const QString& suffix, double scale) { return unit([=] { return Unit{suffix, scale}; }); }
FieldForm& FieldForm::shown(std::function<bool()> shown) { return rows_.back().shown = std::move(shown), *this; }
FieldForm& FieldForm::label(Text text) { return rows_.back().text = std::move(text), *this; }
FieldForm& FieldForm::tip(const QString& text) { return rows_.back().input->setToolTip(text), *this; }

FieldForm& FieldForm::name(const QString& objectName) {
    rows_.back().input->setObjectName(objectName);
    for (QToolButton* b : rows_.back().widget->findChildren<QToolButton*>()) b->setObjectName(objectName);
    return *this;
}

void FieldForm::texts() {
    for (Row& r : rows_) {
        auto* box = qobject_cast<QComboBox*>(r.widget);
        if (const auto options = box && r.options ? r.options() : std::vector<Option>(); int(options.size()) == (box ? box->count() : -1))
            for (int i = 0; i < box->count(); ++i) box->setItemText(i, options[size_t(i)].text); // a choice's words follow an edit too
        if (!r.text) continue;
        if (auto* box = qobject_cast<QCheckBox*>(r.widget)) box->setText(r.text());
        else if (auto* words = qobject_cast<QLabel*>(r.widget)) words->setText(r.text());
        else if (auto* label = qobject_cast<QLabel*>(layout_->labelForField(r.widget))) label->setText(r.text());
    }
}

void FieldForm::fill(int count, const std::function<void(int)>& load) {
    std::vector<std::vector<double>> first(rows_.size());
    std::vector<std::vector<bool>> mixed(rows_.size());
    for (int i = count - 1; i >= 0; --i) { // the active one last: the fields read it when shown
        if (load) load(i);
        for (size_t k = 0; k < rows_.size(); ++k) {
            const std::vector<double> v = rows_[k].read();
            if (i == count - 1) first[k] = v, mixed[k].assign(v.size(), false);
            for (size_t a = 0; a < v.size(); ++a) mixed[k][a] = mixed[k][a] || v[a] != first[k][a];
        }
    }
    filling_ = true;
    clearMixed(this);
    for (Row& r : rows_) show(r);
    texts();
    for (size_t k = 0; k < rows_.size(); ++k) showMixed(rows_[k], mixed[k]);
    filling_ = false;
}

void FieldForm::show(Row& r) {
    const Unit u = r.unit ? r.unit() : Unit{};
    if (auto* s = qobject_cast<QSlider*>(r.input)) {
        s->setValue(int(std::lround(r.get() * u.scale)));
        r.widget->findChild<QLabel*>()->setText(QString::number(s->value()) + u.suffix);
    } else if (auto* s = qobject_cast<QDoubleSpinBox*>(r.widget)) {
        s->setSuffix(u.suffix);
        s->setSingleStep(u.step > 0 ? u.step : r.step);
        s->setValue(r.get() * u.scale);
    } else if (auto* v = qobject_cast<Vec3Row*>(r.widget)) {
        v->setValue(r.getVector());
    } else if (auto* box = qobject_cast<QCheckBox*>(r.widget)) {
        box->setChecked(r.get() != 0);
    } else if (auto* box = qobject_cast<QComboBox*>(r.widget)) {
        box->clear();
        r.values.clear();
        for (const Option& o : r.options()) box->addItem(o.text), r.values.push_back(o.value);
        const auto at = std::find(r.values.begin(), r.values.end(), r.get());
        box->setCurrentIndex(at == r.values.end() ? 0 : int(at - r.values.begin()));
    } else if (auto* group = r.widget->findChild<QButtonGroup*>()) {
        if (QAbstractButton* b = group->button(int(r.get()))) b->setChecked(true);
    }
    layout_->setRowVisible(r.widget, !r.shown || r.shown());
}

// "—" where the selected things differ: a number or one of three, a checkbox unchecked saying so, no tile pressed.
void FieldForm::showMixed(Row& r, const std::vector<bool>& mixed) {
    for (size_t a = 0; a < mixed.size(); ++a) {
        if (!mixed[a]) continue;
        if (auto* v = qobject_cast<Vec3Row*>(r.widget)) ::showMixed(v->spin(int(a)));
        else if (auto* s = qobject_cast<QDoubleSpinBox*>(r.widget)) ::showMixed(s);
        else if (auto* box = qobject_cast<QCheckBox*>(r.widget)) {
            box->setProperty("realText", box->text());
            box->setText(box->text() + "  (—: у всех разное)");
            box->setChecked(false);
        } else if (auto* box = qobject_cast<QComboBox*>(r.widget)) {
            box->setCurrentIndex(-1);
        } else if (auto* group = r.widget->findChild<QButtonGroup*>()) {
            group->setExclusive(false);
            for (QAbstractButton* b : group->buttons()) b->setChecked(false);
            group->setExclusive(true);
        }
    }
}
