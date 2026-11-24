// The Sequencer tab (SequencerPanel.h): the bar, the two views (SequencerViews.h) on a stack, the preview's clock, and
// what the views do through it - the playhead dragged, a key's menu. The sequences and their keys are the core's
// (scene/Animation.h); the preview is SceneBuilder's (SceneBuilderSequence.cpp).
#include "SequencerPanel.h"
#include "UiLanguage.h"

#include "EditorOps.h"
#include "SceneBuilder.h"
#include "SequencerViews.h"
#include "scene/Animation.h"
#include "scene/SceneEdits.h"
#include "core/Format.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QJsonDocument>
#include <QDockWidget>
#include <QMenu>
#include <QScrollBar>
#include <QStackedLayout>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

SequencerPanel::SequencerPanel(EditorOps* ops, SceneBuilder* builder, QWidget* parent) : QWidget(parent), ops_(ops), builder_(builder) {
    setObjectName("sequencerPanel");
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(4, 4, 4, 4);
    col->setSpacing(4);
    // The bar's buttons do not set the window's least width (they made it 1552 px, wider than a laptop's screen): a
    // panel narrower than the bar cuts the bar's right end.
    auto* strip = new QWidget(this);
    strip->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    auto* bar = new QHBoxLayout(strip);
    bar->setContentsMargins(0, 0, 0, 0);
    const auto button = [this, bar](const QString& text, const QString& tip, const char* name, bool checkable = false) {
        auto* b = new QToolButton(this);
        b->setText(text), b->setToolTip(tip), b->setObjectName(name), b->setCheckable(checkable);
        b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed); // its words are never cut: the tail makes room (placeTail)
        b->setProperty("full", text), b->setProperty("short", text.section(' ', 0, 0)); // a narrow bar: the glyph alone
        bar->addWidget(b);
        return b;
    };
    sequences_ = new QComboBox(this);
    sequences_->setObjectName("sequencerSequences");
    sequences_->setToolTip(uiText("Kinetica", "Какая секвенция открыта. Секвенций в сцене сколько угодно: сценарии, записанные прогоны"));
    sequences_->setMinimumContentsLength(12);
    bar->addWidget(sequences_);
    add_ = button("+", uiText("Kinetica", "Новая секвенция, пустая"), "sequencerAdd");
    plays_ = button(uiText("Kinetica", "▶ в Пуске"), uiText("Kinetica", "Пуск играет эту секвенцию: её тела идут по дорожкам как анимация и толкают встречное. Выключено — "
                                 "секвенция только для просмотра, физика тел своя (так приходит запись физики)"), "sequencerPlays", true);
    plays_->setProperty("short", uiText("Kinetica", "в Пуске")); // its glyph is the preview's
    start_ = button("⇤", uiText("Kinetica", "Бегунок в начало"), "sequencerStart");
    // The transport is glyphs alone, as in every timeline (After Effects, Blender): its words are in the tips.
    play_ = button("▶", uiText("Kinetica", "Просмотр: проиграть секвенцию в правке, объекты едут по дорожкам. Пробел над секвенсором — проиграть и "
                                 "остановить, бегунок остаётся, где остановили"), "sequencerPlay", true);
    loop_ = button("↻", uiText("Kinetica", "Петля: после последнего ключа — сначала"), "sequencerLoop", true);
    key_ = button(uiText("Kinetica", "◆ Ключ"), uiText("Kinetica", "Ключ выделенных объектов в момент бегунка: где они стоят сейчас (I)"), "sequencerKey");
    autoKey_ = button(uiText("Kinetica", "◈ Автоключ"), uiText("Kinetica", "Пока включён: сдвинули или повернули объект или поменяли число в инспекторе (цвет, свет, цель сервопривода) - ключ ставится на бегунке сам; у того, у чего ключей ещё не было, - и в 0 с, каким оно было. Выключен - ключ ставит только «◆ Ключ»"), "sequencerAutoKey", true);
    connect(autoKey_, &QToolButton::toggled, this, [this](bool on) { builder_->watchHands(on); autoKey_->setStyleSheet(on ? "QToolButton { color: #ff6a5a; font-weight: bold; }" : QString()); });
    connect(builder_, &SceneBuilder::handPlaced, this, &SequencerPanel::autoKey);
    record_ = button(uiText("Kinetica", "⏺ Запись"), uiText("Kinetica", "Записать физику: сцена играет, каждое свободное тело пишется каждый кадр; ещё раз или «Стоп» — "
                                 "запись становится новой секвенцией «Запись N»"), "sequencerRecord", true);
    curves_ = button(uiText("Kinetica", "〰 Кривые"), uiText("Kinetica", "Кривые дорожки выделенного объекта: место и поворот от времени. Ключ тянется вверх-вниз (значение) и "
                                 "вдоль (время); правый щелчок — как ключ переходит к следующему: линейно, плавно, ступенькой"), "sequencerCurvesButton", true);
    // What is right of the buttons - the filter, a refusal, the time, the play range - is a group of its own: when the
    // panel is narrower than the bar, it goes to a second row and the buttons keep their words (placeTail).
    tail_ = new QWidget(strip);
    auto* rest = new QHBoxLayout(tail_);
    rest->setContentsMargins(0, 0, 0, 0);
    row1_ = bar, row2_ = new QHBoxLayout;
    filter_ = new QLineEdit(this);
    { // hidden with the curves, it keeps its place: the buttons do not jump under the hand
        QSizePolicy keep = filter_->sizePolicy();
        keep.setRetainSizeWhenHidden(true);
        filter_->setSizePolicy(keep);
    }
    filter_->setObjectName("sequencerFilter");
    filter_->setPlaceholderText(uiText("Kinetica", "Фильтр дорожек"));
    filter_->setClearButtonEnabled(true);
    filter_->setFixedWidth(140);
    rest->addWidget(filter_);
    note_ = new QLabel(this);
    note_->setObjectName("sequencerNote");
    note_->setStyleSheet("color: #ff6a5a;");
    rest->addWidget(note_, 1);
    time_ = new QLabel(this);
    time_->setObjectName("sequencerTime");
    rest->addWidget(time_);
    // The take's window on the world's clock: «От» where a take starts, «До» where a run and a take end.
    const auto seconds = [&](const QString& label, const char* name, const QString& none, const QString& tip, void (SequencerPanel::*set)(double)) {
        rest->addWidget(new QLabel(label, this));
        auto* box = new QDoubleSpinBox(this);
        box->setObjectName(name);
        box->setRange(0, 86400);
        box->setDecimals(2);
        box->setSingleStep(0.5);
        box->setSuffix(uiText("Kinetica", " с"));
        box->setSpecialValueText(none);
        box->setKeyboardTracking(false);
        box->setToolTip(tip);
        rest->addWidget(box);
        connect(box, &QDoubleSpinBox::valueChanged, this, set);
        return box;
    };
    from_ = seconds(uiText("Kinetica", "От"), "sequencerFrom", uiText("Kinetica", "с начала"),
                    uiText("Kinetica", "Начало записи: «Запись» хранит движение с этой секунды до «До»; до неё мир идёт, но ничего не пишется"),
                    &SequencerPanel::setFrom);
    end_ = seconds(uiText("Kinetica", "До"), "sequencerEnd", uiText("Kinetica", "без конца"),
                   uiText("Kinetica", "Время проигрывания: «Пуск» и «Запись» идут до этой секунды и встают на паузу, дубль закрывается; "
                          "▶ после — дальше без записи; с «Петлёй» «Пуск» начинает сначала. 0 — без конца (запись — не дольше 2 мин)"),
                   &SequencerPanel::setEnd);
    bar->addWidget(tail_, 1);
    col->addWidget(strip);
    col->addLayout(row2_);
    strip->installEventFilter(this); // its width says where the tail goes
    strip_ = strip;

    auto* tracks = new QWidget(this);
    auto* row = new QHBoxLayout(tracks);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    auto* scroll = new QScrollBar(Qt::Vertical, tracks);
    timeline_ = new SequencerTimeline(this, scroll);
    row->addWidget(timeline_, 1);
    row->addWidget(scroll);
    curvesView_ = new SequencerCurves(this);
    views_ = new QStackedLayout;
    views_->addWidget(tracks);
    views_->addWidget(curvesView_);
    col->addLayout(views_, 1);

    previewTimer_ = new QTimer(this);
    previewTimer_->setInterval(16);
    connect(previewTimer_, &QTimer::timeout, this, [this] {
        const ke::SequenceObject* q = ke::activeSequence(builder_->graph());
        const double length = q ? double(q->length()) : 0;
        double t = previewFrom_ + double(previewClock_.nsecsElapsed()) * 1e-9;
        if (q && q->loop && length > 0) t = std::fmod(t, length);
        const bool end = !q || (!q->loop && t >= length);
        scrubTime_ = std::min(t, length);
        builder_->previewSequence(scrubTime_);
        if (end) play_->setChecked(false), togglePreview(false);
    });
    connect(sequences_, qOverload<int>(&QComboBox::activated), this, [this](int k) {
        run("sequence_set", QJsonObject{{"sequence", sequences_->itemData(k).toInt()}, {"active", true}});
    });
    connect(add_, &QToolButton::clicked, this, [this] { run("sequence_add", QJsonObject{{"active", true}}); });
    connect(start_, &QToolButton::clicked, this, [this] { seek(0); });
    connect(play_, &QToolButton::toggled, this, &SequencerPanel::togglePreview);
    for (QWidget* view : {static_cast<QWidget*>(timeline_), static_cast<QWidget*>(curvesView_)}) view->installEventFilter(this);
    connect(loop_, &QToolButton::clicked, this, [this](bool on) { run("sequence_set", QJsonObject{{"loop", on}}); });
    connect(plays_, &QToolButton::clicked, this, [this](bool on) { run("sequence_set", QJsonObject{{"play", on}}); });
    connect(key_, &QToolButton::clicked, this, &SequencerPanel::keySelected);
    connect(record_, &QToolButton::clicked, this, [this](bool on) {
        if (!on && recordClock_.isValid() && recordClock_.elapsed() < 500) return record_->setChecked(true); // a double click
        if (on) recordClock_.start();
        run("sim_record", QJsonObject{{"on", on}});
    });
    connect(curves_, &QToolButton::toggled, this, &SequencerPanel::showCurves);
    connect(filter_, &QLineEdit::textChanged, timeline_, &SequencerTimeline::setFilter);
    sync();
}

double SequencerPanel::atLeast() const {
    const double end = double(builder_->graph().world.duration); // the play range's end stays in sight
    return std::max(builder_->editing() ? 0.0 : std::ceil(liveTime_) + 1, end > 0 ? std::ceil(end) + 1 : 0.0);
}

// The two flags never pass each other: a number that would is put back a twentieth of a second inside.
void SequencerPanel::setEnd(double seconds) {
    if (seconds > 0) seconds = std::max(seconds, double(builder_->graph().world.recordFrom) + 0.05);
    run("world_set", QJsonObject{{"line", "world"}, {"key", "duration"}, {"values", QJsonArray{seconds}}});
    sync(); // the field shows what the scene took
}

void SequencerPanel::setFrom(double seconds) {
    if (const double end = double(builder_->graph().world.duration); end > 0) seconds = std::min(seconds, end - 0.05);
    run("world_set", QJsonObject{{"line", "world"}, {"key", "record_from"}, {"values", QJsonArray{std::max(0.0, seconds)}}});
    sync();
}

sequencer::Axis SequencerPanel::axis(int width) const {
    sequencer::Axis a = sequencer::axisOf(ke::activeSequence(builder_->graph()), width, atLeast());
    if (zoomSpan_ > 0) a.from = zoomFrom_, a.span = zoomSpan_;
    return a;
}

void SequencerPanel::zoomTime(double factor, double at) {
    const sequencer::Axis a = axis(timeline_->width());
    const double span = std::clamp(a.span * factor, 0.05, 86400.0);
    zoomFrom_ = std::max(0.0, at - (at - a.from) * span / a.span), zoomSpan_ = span;
    timeline_->update(), curvesView_->update();
}

void SequencerPanel::panTime(double seconds) {
    const sequencer::Axis a = axis(timeline_->width());
    zoomFrom_ = std::max(0.0, a.from + seconds), zoomSpan_ = a.span;
    timeline_->update(), curvesView_->update();
}

void SequencerPanel::frameTime(double from, double to) {
    zoomFrom_ = std::max(0.0, from), zoomSpan_ = std::max(0.05, to - zoomFrom_);
    timeline_->update(), curvesView_->update();
}

void SequencerPanel::setCurves(bool on) { curves_->setChecked(on); } // its toggled shows them
bool SequencerPanel::curvesShown() const { return curves_->isChecked(); }

void SequencerPanel::fitTime() {
    zoomFrom_ = zoomSpan_ = 0;
    curvesView_->fitValues();
    timeline_->update();
}

// The tail - the filter, the time, the play range - beside the buttons when the bar is wide enough, else on a row below;
// still too narrow, the buttons show their glyphs alone, their words in the tips, as a crowded toolbar does.
void SequencerPanel::placeTail() {
    const auto width = [this](bool shortWords) {
        int sum = 0;
        for (int i = 0; i < row1_->count(); ++i) {
            QWidget* w = row1_->itemAt(i)->widget();
            auto* b = qobject_cast<QToolButton*>(w);
            if (!w || w == tail_ || w->isHidden()) continue;
            const QString words = b ? b->property(shortWords ? "short" : "full").toString() : QString();
            sum += (b && !words.isEmpty() ? b->fontMetrics().horizontalAdvance(words) + 18 : w->sizeHint().width()) + row1_->spacing();
        }
        return sum;
    };
    const bool below = strip_->width() < width(false) + tail_->sizeHint().width();
    compact_ = strip_->width() < width(false);
    for (QToolButton* b : findChildren<QToolButton*>())
        if (const QString words = b->property(compact_ ? "short" : "full").toString(); !words.isEmpty() && b != record_) b->setText(words);
    syncRecordWords();
    if (below == (row2_->indexOf(tail_) >= 0)) return;
    (below ? row1_ : row2_)->removeWidget(tail_);
    if (below) {
        row2_->addWidget(tail_);
        row1_->addStretch(1); // the buttons stay left, the sequences' list keeps its width
    } else {
        delete row1_->takeAt(row1_->count() - 1);
        row1_->addWidget(tail_, 1);
    }
}

void SequencerPanel::syncRecordWords() {
    const bool on = ops_->recording();
    const QString words = on ? (compact_ ? "■" : uiText("Kinetica", "■ Закончить запись")) : (compact_ ? "⏺" : uiText("Kinetica", "⏺ Запись"));
    if (record_->text() != words) record_->setText(words);
    record_->setStyleSheet(on ? "QToolButton { color: #ff6a5a; font-weight: bold; }" : QString());
}

// The motion path of the selected objects in the 3D view, as Blender's Motion Paths and After Effects' position path show
// it: the line the active sequence takes each along - sampled as the world plays it, 30 points a second (samplePose) -
// and its keys as dots. Drawn with view_draw, as a plugin or an agent draws; redrawn only when it changed.
void SequencerPanel::drawPaths() {
    const ke::SceneGraph& g = builder_->graph();
    const ke::SequenceObject* q = ke::activeSequence(g);
    QJsonArray draws;
    for (uint32_t id : builder_->selection()) {
        const ke::SequenceTrack* t = q ? ke::trackOf(*q, id) : nullptr;
        const ke::SceneObject* o = ke::findObject(g, id);
        if (!t || !o || t->keys.size() < 2) continue;
        ke::Vector3 at(0.0f);
        ke::Quaternion turn;
        if (const ke::SceneObject* parent = o->parent ? ke::findObject(g, o->parent) : nullptr) ke::worldPose(g, *parent, at, turn); // keys are on it
        const auto world = [&](const ke::Vector3& p) { const ke::Vector3 w = at + turn.rotate(p); return QJsonArray{double(w.x), double(w.y), double(w.z)}; };
        const double from = double(t->keys.front().time), to = double(t->keys.back().time);
        const int n = std::clamp(int((to - from) * 30), 2, 900);
        QJsonArray line, dots;
        for (int k = 0; k <= n; ++k) {
            ke::Vector3 p;
            ke::Quaternion r;
            if (ke::samplePose(t->keys, ke::Real(from + (to - from) * k / n), false, 0, p, r)) line.append(world(p));
        }
        for (const ke::AnimKey& key : t->keys) dots.append(world(key.position));
        const QString tag = QString::number(id);
        draws.append(QJsonObject{{"key", "sequencer/path/" + tag}, {"line", line}, {"color", QJsonArray{0.42, 0.72, 1.0}}});
        draws.append(QJsonObject{{"key", "sequencer/keys/" + tag}, {"points", dots}, {"color", QJsonArray{1.0, 1.0, 1.0}}, {"size", 8}});
    }
    const QByteArray now = QJsonDocument(draws).toJson(QJsonDocument::Compact);
    if (now == paths_) return;
    paths_ = now;
    const auto quiet = [](const QJsonObject&) {};
    ops_->run("view_draw", QJsonObject{{"key", "sequencer/"}, {"clear", true}}, quiet, "ui");
    for (const QJsonValue& d : draws) ops_->run("view_draw", d.toObject(), quiet, "ui");
}

double SequencerPanel::playhead() const {
    if (!builder_->editing()) return playTime_;
    if (scrubbing_ || previewTimer_->isActive()) return scrubTime_;
    const ke::SequenceObject* q = ke::activeSequence(builder_->graph());
    return q ? double(q->time) : 0;
}

uint32_t SequencerPanel::pickedObject() const { return timeline_->pickedObject(); }

void SequencerPanel::addBarAction(QAction* a, const QString& text) {
    auto* b = new QToolButton(this);
    b->setDefaultAction(a);
    b->setText(text);
    b->setObjectName(a->objectName() + "Button");
    static_cast<QHBoxLayout*>(layout()->itemAt(0)->widget()->layout())->insertWidget(10, b); // after «〰 Кривые»
}

void SequencerPanel::run(const QString& name, const QJsonObject& args) {
    ops_->run(name, args, [this](const QJsonObject& r) {
        note_->setText(r["ok"].toBool() ? QString() : r["error"].toString());
        sync();
    }, "ui");
}

void SequencerPanel::seek(double time) {
    if (builder_->editing()) run("sequence_seek", QJsonObject{{"time", time}});
}

void SequencerPanel::scrub(double time) {
    if (!builder_->editing()) // a run is not gone back over: nothing of it is kept - said, a silent ruler was taken for broken
        return note_->setText(uiText("Kinetica", "Прогон не запоминается, назад не перемотать: бегунок ходит после ■ Стоп"));
    if (!ke::activeSequence(builder_->graph())) return;
    if (!scrubbing_) builder_->beginSequencePreview(), scrubbing_ = true, note_->clear();
    scrubTime_ = time;
    builder_->previewSequence(time); // the view redrawn: sync follows (MainWindow, editSnapshot)
}

void SequencerPanel::endScrub() {
    scrubbing_ = false;
    if (!builder_->editing()) return;
    builder_->endSequencePreview();
    seek(scrubTime_);
}

bool SequencerPanel::spacePlays() const {
    const ke::SequenceObject* q = ke::activeSequence(builder_->graph());
    return previewTimer_->isActive() || (builder_->editing() && q && !q->tracks.empty());
}

bool SequencerPanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == strip_) { // the bar: only its width matters here
        if (event->type() == QEvent::Resize) placeTail();
        return false;
    }
    auto* view = static_cast<QWidget*>(watched);
    if (event->type() == QEvent::Enter && window()->isActiveWindow() && !QApplication::activePopupWidget())
        view->setFocus(Qt::MouseFocusReason); // the keys follow the mouse, as into the 3D view
    const auto* key = event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress ? static_cast<QKeyEvent*>(event) : nullptr;
    if (key && key->key() == Qt::Key_F && key->modifiers() == Qt::NoModifier) { // over the Sequencer F shows the whole sequence, not the scene
        event->accept();
        if (event->type() == QEvent::KeyPress) fitTime();
        return true;
    }
    const bool space = key && key->key() == Qt::Key_Space && key->modifiers() == Qt::NoModifier;
    if (!space || !spacePlays()) return QWidget::eventFilter(watched, event);
    event->accept(); // before the window's «Пуск»
    if (event->type() == QEvent::KeyPress && !static_cast<QKeyEvent*>(event)->isAutoRepeat()) play_->toggle();
    return true;
}

void SequencerPanel::togglePreview(bool on) {
    if (on == previewTimer_->isActive()) return;
    if (on) {
        const ke::SequenceObject* q = ke::activeSequence(builder_->graph());
        if (!builder_->editing() || !q || q->tracks.empty()) return play_->setChecked(false);
        previewFrom_ = !q->loop && double(q->time) >= double(q->length()) ? 0 : double(q->time); // at the end: from the start
        scrubTime_ = previewFrom_;
        builder_->beginSequencePreview();
        previewClock_.start();
        previewTimer_->start();
    } else {
        previewTimer_->stop();
        builder_->endSequencePreview();
        seek(scrubTime_);
    }
    play_->setText(on ? "■" : "▶"); // the transport's glyphs; the tip says what they do
}

void SequencerPanel::showCurves(bool on) {
    views_->setCurrentIndex(on ? 1 : 0);
    filter_->setVisible(!on);
    // The curves want height, as After Effects' graph takes the timeline's whole: a low panel grows when they open.
    if (auto* dock = qobject_cast<QDockWidget*>(parentWidget()); on && dock && dock->height() < 320)
        if (auto* main = qobject_cast<QMainWindow*>(dock->parentWidget())) main->resizeDocks({dock}, {340}, Qt::Vertical);
    sync();
}

void SequencerPanel::keyMenu(uint32_t object, double time, const QPoint& at) {
    const ke::SequenceObject* q = ke::activeSequence(builder_->graph());
    const ke::SequenceTrack* t = q ? ke::trackOf(*q, object) : nullptr;
    const auto key = t ? std::find_if(t->keys.begin(), t->keys.end(), [time](const ke::AnimKey& k) { return std::fabs(double(k.time) - time) < 1e-6; })
                       : std::vector<ke::AnimKey>::const_iterator();
    if (!t || key == t->keys.end()) return;
    QMenu menu(this);
    const std::pair<ke::AnimInterp, const char*> kinds[] = {
        {ke::AnimInterp::Linear, uiText("Kinetica", "◆ Линейно — по прямой")}, {ke::AnimInterp::Smooth, uiText("Kinetica", "● Плавно — кривая через ключи")}, {ke::AnimInterp::Step, uiText("Kinetica", "■ Ступенька — держать до следующего")}};
    for (const auto& [interp, words] : kinds) {
        QAction* a = menu.addAction(words);
        a->setCheckable(true), a->setChecked(key->interp == interp);
        const QJsonObject args{{"id", int(object)}, {"time", time}, {"interp", ke::interpName(interp)}};
        connect(a, &QAction::triggered, this, [this, args] { run("sequence_key_set", args); });
    }
    menu.addSeparator();
    menu.addAction(uiText("Kinetica", "Бегунок на ключ"), this, [this, time] { seek(time); });
    menu.addAction(uiText("Kinetica", "Удалить ключ"), this, [this, object, time] { run("sequence_delete_key", QJsonObject{{"id", int(object)}, {"time", time}}); });
    menu.exec(at);
}

// Auto Key, as Blender's Auto Keying: what a hand changed is keyed at the playhead - a moved or turned object its pose, a
// number set in the inspector that field. What had no key yet and changed away from 0 s gets a first key at 0 s with how
// it was before the edit: the motion starts where the object stood (a key at the playhead alone would hold it still).
void SequencerPanel::autoKey(const std::vector<uint32_t>& ids, const ke::SceneGraph& before) {
    if (!autoKey_->isChecked() || !builder_->editing()) return;
    const double t = playhead();
    const ke::SequenceObject* q = ke::activeSequence(builder_->graph());
    const bool later = t > 1e-6;
    std::vector<uint32_t> fresh;
    for (uint32_t id : ids)
        if (later && (!q || !ke::trackOf(*q, id))) fresh.push_back(id);
    std::vector<ke::GraphEdit> fields;
    for (const ke::GraphEdit& e : ke::diffEdits(before, builder_->graph())) {
        bool numbers = e.kind == ke::GraphEdit::Kind::Set && !e.values.empty() && e.key != "position" && e.key != "rotation";
        for (const std::string& v : e.values) {
            double x;
            numbers = numbers && ke::parseNumber(v, x);
        }
        if (numbers) fields.push_back(e);
    }
    QJsonArray moved;
    for (uint32_t id : ids) moved.append(int(id));
    if (!moved.isEmpty()) run("sequence_key", QJsonObject{{"ids", moved}, {"time", t}});
    for (uint32_t id : fresh) // its pose before the hand: the key at 0 s
        if (const ke::SceneObject* was = ke::findObject(before, id)) {
            const auto v3 = [](const ke::Vector3& v) { return QJsonArray{double(v.x), double(v.y), double(v.z)}; };
            run("sequence_key", QJsonObject{{"ids", QJsonArray{int(id)}}, {"time", 0}});
            run("sequence_key_set", QJsonObject{{"id", int(id)}, {"time", 0}, {"position", v3(was->position)}, {"rotation", v3(was->rotationDeg)}});
        }
    for (const ke::GraphEdit& e : fields) {
        const QJsonObject field{{"ids", QJsonArray{int(e.id)}}, {"line", QString::fromStdString(e.line)}, {"key", QString::fromStdString(e.key)}};
        if (later && (!q || !ke::fieldTrackOf(*q, e.id, e.line, e.key))) { // its numbers before the edit: the key at 0 s
            QJsonArray was;
            for (const ke::BlockWord& w : ke::getField(ke::getObject(before, e.id), e.line, e.key, e.values.size())) {
                double x;
                if (ke::parseNumber(w.text, x)) was.append(x);
            }
            if (!was.isEmpty()) {
                QJsonObject first = field;
                first["time"] = 0, first["values"] = was;
                run("sequence_key", first);
            }
        }
        QJsonObject now = field;
        now["time"] = t;
        run("sequence_key", now);
    }
}

void SequencerPanel::keySelected() {
    if (!builder_->editing()) return note_->setText(uiText("Kinetica", "Ключи ставятся в правке: остановите «Пуск»"));
    if (builder_->orderedSelection().empty()) return note_->setText(uiText("Kinetica", "Выделите объект для ключа"));
    run("sequence_key", QJsonObject{{"time", playhead()}});
}

// In play the playhead follows the world only while the timeline has something of the run: a take recorded, the open
// sequence played in it. A plain «Пуск» keeps nothing - a playhead going on over nothing was taken for a recording.
void SequencerPanel::setPlayTime(double time) {
    if (builder_->editing()) return;
    const ke::SequenceObject* q = ke::activeSequence(builder_->graph());
    if (!ops_->recording() && !(q && q->play)) time = 0;
    liveTime_ = playTime_ = time;
    time_->setText(QString(uiText("Kinetica", "%1 / %2 с")).arg(time, 0, 'f', 2).arg(q ? double(q->length()) : 0.0, 0, 'f', 2));
    timeline_->update(), curvesView_->update();
}

void SequencerPanel::sync() {
    const ke::SceneGraph& g = builder_->graph();
    const ke::SequenceObject* active = ke::activeSequence(g);
    bool same = sequences_->count() == int(g.sequences.size());
    for (size_t i = 0; same && i < g.sequences.size(); ++i)
        same = sequences_->itemData(int(i)).toInt() == int(g.sequences[i].id) && sequences_->itemText(int(i)) == QString::fromStdString(g.sequences[i].name);
    if (!same) {
        sequences_->clear();
        for (const ke::SequenceObject& q : g.sequences) sequences_->addItem(QString::fromStdString(q.name), int(q.id));
    }
    sequences_->setCurrentIndex(active ? sequences_->findData(int(active->id)) : -1);
    sequences_->setPlaceholderText(uiText("Kinetica", "нет секвенций"));
    const bool editing = builder_->editing();
    sequences_->setEnabled(editing && !g.sequences.empty());
    for (QToolButton* b : {add_, start_, key_}) b->setEnabled(editing);
    play_->setEnabled(editing && active && !active->tracks.empty());
    loop_->setEnabled(editing && active);
    plays_->setEnabled(editing && active);
    {
        const QSignalBlocker hush(loop_), quiet(record_), still(plays_);
        loop_->setChecked(active && active->loop);
        plays_->setChecked(active && active->play);
        record_->setChecked(ops_->recording());
    }
    syncRecordWords();
    {
        const QSignalBlocker hush(end_), quiet(from_);
        end_->setValue(double(g.world.duration));
        from_->setValue(double(g.world.recordFrom));
    }
    const double length = active ? double(active->length()) : 0;
    time_->setText(QString(uiText("Kinetica", "%1 / %2 с")).arg(playhead(), 0, 'f', 2).arg(length, 0, 'f', 2));
    timeline_->refresh();
    curvesView_->update();
    drawPaths();
    builder_->refreshKeyPoints(); // the keys on the path, to drag in the view
}
