// The Sequencer tab (SequencerPanel.h): the bar, the two views (SequencerViews.h) on a stack, the preview's clock, and
// what the views do through it - the playhead dragged, a key's menu. The sequences and their keys are the core's
// (scene/Animation.h); the preview is SceneBuilder's (SceneBuilderSequence.cpp).
#include "SequencerPanel.h"

#include "EditorOps.h"
#include "SceneBuilder.h"
#include "SequencerViews.h"
#include "scene/Animation.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
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
    auto* bar = new QHBoxLayout;
    const auto button = [this, bar](const QString& text, const QString& tip, const char* name, bool checkable = false) {
        auto* b = new QToolButton(this);
        b->setText(text), b->setToolTip(tip), b->setObjectName(name), b->setCheckable(checkable);
        bar->addWidget(b);
        return b;
    };
    sequences_ = new QComboBox(this);
    sequences_->setObjectName("sequencerSequences");
    sequences_->setToolTip("Какая секвенция открыта. Секвенций в сцене сколько угодно: сценарии, записанные прогоны");
    sequences_->setMinimumContentsLength(12);
    bar->addWidget(sequences_);
    add_ = button("+", "Новая секвенция, пустая", "sequencerAdd");
    plays_ = button("▶ в Пуске", "Пуск играет эту секвенцию: её тела идут по дорожкам как анимация и толкают встречное. Выключено — "
                                 "секвенция только для просмотра, физика тел своя (так приходит запись физики)", "sequencerPlays", true);
    start_ = button("⇤", "Бегунок в начало", "sequencerStart");
    play_ = button("▶ Просмотр", "Проиграть секвенцию в правке: объекты едут по дорожкам", "sequencerPlay", true);
    loop_ = button("↻ Петля", "После последнего ключа — сначала", "sequencerLoop", true);
    key_ = button("◆ Ключ", "Ключ выделенных объектов в момент бегунка: где они стоят сейчас (I)", "sequencerKey");
    record_ = button("⏺ Запись", "Записать физику: сцена играет, каждое свободное тело пишется каждый кадр; ещё раз или «Стоп» — "
                                 "запись становится новой секвенцией «Запись N»", "sequencerRecord", true);
    curves_ = button("〰 Кривые", "Кривые дорожки выделенного объекта: место и поворот от времени. Ключ тянется вверх-вниз (значение) и "
                                 "вдоль (время); правый щелчок — как ключ переходит к следующему: линейно, плавно, ступенькой", "sequencerCurvesButton", true);
    filter_ = new QLineEdit(this);
    filter_->setObjectName("sequencerFilter");
    filter_->setPlaceholderText("Фильтр дорожек");
    filter_->setClearButtonEnabled(true);
    filter_->setFixedWidth(140);
    bar->addWidget(filter_);
    live_ = button("● Вживую", "Назад к последнему кадру запуска: сцена пойдёт дальше, если шла", "sequencerLive");
    live_->setStyleSheet("QToolButton { color: #7cff9a; font-weight: bold; }");
    live_->hide();
    note_ = new QLabel(this);
    note_->setStyleSheet("color: #ff6a5a;");
    bar->addWidget(note_, 1);
    time_ = new QLabel(this);
    time_->setObjectName("sequencerTime");
    bar->addWidget(time_);
    col->addLayout(bar);

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
    connect(loop_, &QToolButton::clicked, this, [this](bool on) { run("sequence_set", QJsonObject{{"loop", on}}); });
    connect(plays_, &QToolButton::clicked, this, [this](bool on) { run("sequence_set", QJsonObject{{"play", on}}); });
    connect(key_, &QToolButton::clicked, this, &SequencerPanel::keySelected);
    connect(record_, &QToolButton::clicked, this, [this](bool on) {
        if (!on && recordClock_.isValid() && recordClock_.elapsed() < 500) return record_->setChecked(true); // a double click
        if (on) recordClock_.start();
        run("sim_record", QJsonObject{{"on", on}});
    });
    connect(curves_, &QToolButton::toggled, this, &SequencerPanel::showCurves);
    connect(live_, &QToolButton::clicked, this, &SequencerPanel::liveWanted);
    connect(filter_, &QLineEdit::textChanged, timeline_, &SequencerTimeline::setFilter);
    sync();
}

void SequencerPanel::showPast(bool past, double time) {
    past_ = past;
    live_->setVisible(past);
    playTime_ = past ? time : liveTime_;
    timeline_->update(), curvesView_->update();
    time_->setText(QString("%1 / %2 с").arg(playTime_, 0, 'f', 2).arg(liveTime_, 0, 'f', 2));
}

double SequencerPanel::atLeast() const { return builder_->editing() ? 0 : std::ceil(liveTime_) + 1; }

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
    static_cast<QHBoxLayout*>(static_cast<QVBoxLayout*>(layout())->itemAt(0)->layout())->insertWidget(9, b); // after «〰 Кривые»
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
    if (!builder_->editing()) { // a run on screen: its frame then, from the history (the scene pauses)
        scrubbing_ = true, scrubTime_ = time;
        return emit pastWanted(std::min(time, liveTime_));
    }
    if (!ke::activeSequence(builder_->graph())) return;
    if (!scrubbing_) builder_->beginSequencePreview(), scrubbing_ = true;
    scrubTime_ = time;
    builder_->previewSequence(time); // the view redrawn: sync follows (MainWindow, editSnapshot)
}

void SequencerPanel::endScrub() {
    scrubbing_ = false;
    if (!builder_->editing()) return; // the run's frame stays on screen until «● Вживую» or ▶
    builder_->endSequencePreview();
    seek(scrubTime_);
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
    play_->setText(on ? "■ Стоп" : "▶ Просмотр");
}

void SequencerPanel::showCurves(bool on) {
    views_->setCurrentIndex(on ? 1 : 0);
    filter_->setVisible(!on);
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
        {ke::AnimInterp::Linear, "◆ Линейно — по прямой"}, {ke::AnimInterp::Smooth, "● Плавно — кривая через ключи"}, {ke::AnimInterp::Step, "■ Ступенька — держать до следующего"}};
    for (const auto& [interp, words] : kinds) {
        QAction* a = menu.addAction(words);
        a->setCheckable(true), a->setChecked(key->interp == interp);
        const QJsonObject args{{"id", int(object)}, {"time", time}, {"interp", ke::interpName(interp)}};
        connect(a, &QAction::triggered, this, [this, args] { run("sequence_key_set", args); });
    }
    menu.addSeparator();
    menu.addAction("Бегунок на ключ", this, [this, time] { seek(time); });
    menu.addAction("Удалить ключ", this, [this, object, time] { run("sequence_delete_key", QJsonObject{{"id", int(object)}, {"time", time}}); });
    menu.exec(at);
}

void SequencerPanel::keySelected() {
    if (!builder_->editing()) return note_->setText("Ключи ставятся в правке: остановите «Пуск»");
    if (builder_->orderedSelection().empty()) return note_->setText("Выделите объект для ключа");
    run("sequence_key", QJsonObject{{"time", playhead()}});
}

// In play only the playhead moves: the time and the views, not the sequences (the edited scene stands still).
void SequencerPanel::setPlayTime(double time) {
    liveTime_ = time;
    if (past_) return; // a kept frame on screen: the playhead stays on it
    playTime_ = time;
    if (builder_->editing()) return;
    const ke::SequenceObject* q = ke::activeSequence(builder_->graph());
    time_->setText(QString("%1 / %2 с").arg(time, 0, 'f', 2).arg(q ? double(q->length()) : 0.0, 0, 'f', 2));
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
    sequences_->setPlaceholderText("нет секвенций");
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
    if (const QString words = ops_->recording() ? "■ Закончить запись" : "⏺ Запись"; record_->text() != words) {
        record_->setText(words);
        record_->setStyleSheet(ops_->recording() ? "QToolButton { color: #ff6a5a; font-weight: bold; }" : QString());
    }
    const double length = active ? double(active->length()) : 0;
    time_->setText(QString("%1 / %2 с").arg(playhead(), 0, 'f', 2).arg(length, 0, 'f', 2));
    timeline_->refresh();
    curvesView_->update();
}
