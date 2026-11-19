// The Sequencer tab (SequencerPanel.h): the bar - the sequence, ⇤ to its start, ▶ a preview, the loop, ◆ a key - and the
// timeline, painted here: a ruler of seconds, a row a bound object (a lost one greyed), its keys as diamonds, the
// playhead. The sequences and their keys are the core's (scene/Animation.h); the preview is SceneBuilder's
// (SceneBuilderSequence.cpp).
#include "SequencerPanel.h"

#include "EditorOps.h"
#include "SceneBuilder.h"
#include "scene/Animation.h"

#include <QComboBox>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

const int kNames = 160, kRuler = 22, kRow = 22, kPad = 12;
const QColor kKey(240, 192, 64), kHead(255, 106, 90);

} // namespace

class SequencerTimeline : public QWidget {
public:
    explicit SequencerTimeline(SequencerPanel* panel) : QWidget(panel), panel_(panel) {
        setObjectName("sequencerTimeline");
        setFocusPolicy(Qt::ClickFocus);
        setMinimumHeight(kRuler + 3 * kRow);
    }
    const ke::SequenceObject* sequence() const { return ke::activeSequence(panel_->builder_->graph()); }
    double span() const {
        const ke::SequenceObject* q = sequence();
        return std::max(5.0, std::ceil(double(q ? q->length() : 0) + 1));
    }
    double xOf(double t) const { return kNames + kPad + t * double(width() - kNames - 2 * kPad) / span(); }
    double timeAt(double x) const { return std::clamp((x - kNames - kPad) * span() / std::max(1, width() - kNames - 2 * kPad), 0.0, span()); }

    // The key under a point: its object and time.
    bool keyAt(const QPointF& at, uint32_t& object, double& time) const {
        const ke::SequenceObject* q = sequence();
        const int row = at.y() < kRuler ? -1 : int((at.y() - kRuler) / kRow);
        if (!q || row < 0 || row >= int(q->tracks.size())) return false;
        for (const ke::AnimKey& k : q->tracks[size_t(row)].keys)
            if (std::fabs(xOf(double(k.time)) - at.x()) <= 6) return object = q->tracks[size_t(row)].object, time = double(k.time), true;
        return false;
    }
    void forgetKey() { keyObject_ = 0, keyTime_ = -1; }

    uint32_t keyObject_ = 0; // the key picked: Delete removes it
    double keyTime_ = -1;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPalette& pal = palette();
        const QColor text = pal.color(QPalette::Text), faint(text.red(), text.green(), text.blue(), 40);
        p.fillRect(rect(), pal.color(QPalette::Base));
        p.fillRect(QRect(0, 0, kNames, height()), pal.color(QPalette::Window));
        p.fillRect(QRect(kNames, 0, width() - kNames, kRuler), pal.color(QPalette::AlternateBase));

        // The ruler: a figure every 1, 5, 10 or 30 s by how long the view is, a tick between.
        const double span = this->span(), every = span <= 12 ? 1 : span <= 40 ? 5 : span <= 120 ? 10 : 30;
        for (int k = 0; k * every / 2 <= span + 1e-9; ++k) {
            const double t = k * every / 2, x = xOf(t);
            p.setPen(text);
            p.drawLine(QPointF(x, kRuler - (k % 2 ? 4 : 8)), QPointF(x, kRuler));
            if (k % 2) continue;
            const bool edge = x + 40 > width(); // the last figure at the right edge: written before its tick
            p.drawText(edge ? QRectF(x - 63, 1, 60, kRuler - 7) : QRectF(x + 3, 1, 60, kRuler - 7), (edge ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
                       QString::number(t) + " с");
            p.setPen(faint);
            p.drawLine(QPointF(x, kRuler), QPointF(x, height()));
        }

        const ke::SequenceObject* q = sequence();
        const ke::SceneGraph& g = panel_->builder_->graph();
        if (!q || q->tracks.empty()) {
            p.setPen(pal.color(QPalette::PlaceholderText));
            p.drawText(QRectF(kNames + kPad, kRuler + 4, width() - kNames - 2 * kPad, height() - kRuler - 8), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                       "Выделите объект и нажмите «◆ Ключ» (I): секвенция запомнит, где он стоит в момент бегунка. Передвиньте бегунок, "
                       "переставьте объект, ещё ключ — между ключами он поедет сам. В «Пуске» тела секвенции идут по своим дорожкам и толкают встречное.");
        }
        for (size_t i = 0; q && i < q->tracks.size(); ++i) {
            const ke::SequenceTrack& t = q->tracks[i];
            const int y = kRuler + int(i) * kRow;
            const double mid = y + kRow / 2.0;
            if (i % 2) p.fillRect(QRect(kNames, y, width() - kNames, kRow), QColor(128, 128, 128, 18));
            const ke::SceneObject* o = ke::findObject(g, t.object);
            QFont f = font();
            f.setItalic(!o);
            f.setBold(o && o->id == panel_->builder_->selectedId());
            p.setFont(f);
            p.setPen(o ? text : pal.color(QPalette::PlaceholderText));
            p.drawText(QRectF(8, y, kNames - 12, kRow), Qt::AlignLeft | Qt::AlignVCenter,
                       p.fontMetrics().elidedText(o ? QString::fromStdString(o->name) : QString("потерян (id %1)").arg(t.object), Qt::ElideRight, kNames - 12));
            p.setFont(font());
            if (t.keys.size() > 1) {
                p.setPen(QPen(QColor(kKey.red(), kKey.green(), kKey.blue(), 90), 2));
                p.drawLine(QPointF(xOf(double(t.keys.front().time)), mid), QPointF(xOf(double(t.keys.back().time)), mid));
            }
            for (const ke::AnimKey& k : t.keys) {
                const double x = xOf(double(k.time));
                const bool picked = t.object == keyObject_ && std::fabs(double(k.time) - keyTime_) < 1e-6;
                const QPointF d[4] = {{x, mid - 6}, {x + 6, mid}, {x, mid + 6}, {x - 6, mid}};
                p.setPen(QPen(picked ? text : QColor(60, 45, 10), 1.2));
                p.setBrush(picked ? QColor(255, 236, 170) : (o ? kKey : QColor(150, 150, 150)));
                p.drawPolygon(d, 4);
            }
        }
        p.setPen(QPen(pal.color(QPalette::Mid), 1));
        p.drawLine(QPointF(kNames, 0), QPointF(kNames, height()));

        if (q && q->length() > 0) { // where the sequence ends
            p.setPen(QPen(faint, 1, Qt::DashLine));
            p.drawLine(QPointF(xOf(double(q->length())), kRuler), QPointF(xOf(double(q->length())), height()));
        }
        const double x = xOf(std::min(panel_->playhead(), span));
        p.setPen(QPen(kHead, 1.6));
        p.drawLine(QPointF(x, 0), QPointF(x, height()));
        p.setBrush(kHead);
        p.setPen(Qt::NoPen);
        const QPointF head[3] = {{x - 6, 0}, {x + 6, 0}, {x, 9}};
        p.drawPolygon(head, 3);
    }

    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) return;
        const ke::SequenceObject* q = sequence();
        uint32_t object = 0;
        double t = 0;
        if (keyAt(e->position(), object, t)) {
            keyObject_ = object, keyTime_ = t;
            panel_->builder_->select(object);
            update();
            return;
        }
        forgetKey();
        const int row = e->position().y() < kRuler ? -1 : int((e->position().y() - kRuler) / kRow);
        if (e->position().x() < kNames) {
            if (q && row >= 0 && row < int(q->tracks.size())) panel_->builder_->select(q->tracks[size_t(row)].object);
            update();
            return;
        }
        panel_->scrub(timeAt(e->position().x()));
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (panel_->scrubbing_) panel_->scrub(timeAt(e->position().x()));
    }
    void mouseReleaseEvent(QMouseEvent*) override {
        if (panel_->scrubbing_) panel_->endScrub();
    }
    void mouseDoubleClickEvent(QMouseEvent* e) override {
        uint32_t object = 0;
        double t = 0;
        if (keyAt(e->position(), object, t)) panel_->seek(t);
    }
    void keyPressEvent(QKeyEvent* e) override {
        if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && keyObject_) {
            QJsonObject a{{"id", int(keyObject_)}, {"time", keyTime_}};
            forgetKey();
            panel_->run("sequence_delete_key", a);
        } else if (e->key() == Qt::Key_I) panel_->keySelected();
        else QWidget::keyPressEvent(e);
    }
    void contextMenuEvent(QContextMenuEvent* e) override {
        uint32_t object = 0;
        double t = 0;
        if (!keyAt(e->pos(), object, t)) return;
        QMenu menu(this);
        menu.addAction("Бегунок на ключ", this, [this, t] { panel_->seek(t); });
        menu.addAction("Удалить ключ", this, [this, object, t] {
            forgetKey();
            panel_->run("sequence_delete_key", QJsonObject{{"id", int(object)}, {"time", t}});
        });
        menu.exec(e->globalPos());
    }

private:
    SequencerPanel* panel_;
};

SequencerPanel::SequencerPanel(EditorOps* ops, SceneBuilder* builder, QWidget* parent) : QWidget(parent), ops_(ops), builder_(builder) {
    setObjectName("sequencerPanel");
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(4, 4, 4, 4);
    col->setSpacing(4);
    auto* bar = new QHBoxLayout;
    const auto button = [this, bar](const QString& text, const QString& tip, const char* name) {
        auto* b = new QToolButton(this);
        b->setText(text), b->setToolTip(tip), b->setObjectName(name);
        bar->addWidget(b);
        return b;
    };
    sequences_ = new QComboBox(this);
    sequences_->setObjectName("sequencerSequences");
    sequences_->setToolTip("Какая секвенция играет. Секвенций в сцене сколько угодно: сценарии, записанные прогоны");
    sequences_->setMinimumContentsLength(14);
    bar->addWidget(sequences_);
    add_ = button("+", "Новая секвенция: пустая, сразу играет она", "sequencerAdd");
    start_ = button("⇤", "Бегунок в начало", "sequencerStart");
    play_ = button("▶ Просмотр", "Проиграть секвенцию в правке: объекты едут по дорожкам. В «Пуске» её играет сам мир", "sequencerPlay");
    play_->setCheckable(true);
    loop_ = button("↻ Петля", "После последнего ключа — сначала", "sequencerLoop");
    loop_->setCheckable(true);
    key_ = button("◆ Ключ", "Ключ выделенных объектов в момент бегунка: где они стоят сейчас (I)", "sequencerKey");
    record_ = button("⏺ Запись", "Записать физику: сцена играет, каждое свободное тело пишется каждый кадр; ещё раз или «Стоп» — "
                                 "запись становится новой секвенцией «Запись N», и её можно смотреть бегунком и проиграть как анимацию",
                     "sequencerRecord");
    record_->setCheckable(true);
    note_ = new QLabel(this);
    note_->setStyleSheet("color: #ff6a5a;");
    bar->addWidget(note_, 1);
    time_ = new QLabel(this);
    time_->setObjectName("sequencerTime");
    bar->addWidget(time_);
    col->addLayout(bar);

    timeline_ = new SequencerTimeline(this);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(timeline_);
    col->addWidget(scroll, 1);

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
        sync();
    });
    connect(sequences_, qOverload<int>(&QComboBox::activated), this, [this](int k) {
        run("sequence_set", QJsonObject{{"sequence", sequences_->itemData(k).toInt()}, {"active", true}});
    });
    connect(add_, &QToolButton::clicked, this, [this] { run("sequence_add", QJsonObject{{"active", true}}); });
    connect(start_, &QToolButton::clicked, this, [this] { seek(0); });
    connect(play_, &QToolButton::toggled, this, &SequencerPanel::togglePreview);
    connect(loop_, &QToolButton::clicked, this, [this](bool on) { run("sequence_set", QJsonObject{{"loop", on}}); });
    connect(key_, &QToolButton::clicked, this, &SequencerPanel::keySelected);
    connect(record_, &QToolButton::clicked, this, [this](bool on) { run("sim_record", QJsonObject{{"on", on}}); });
    sync();
}

double SequencerPanel::playhead() const {
    if (scrubbing_ || previewTimer_->isActive()) return scrubTime_;
    if (!builder_->editing()) return playTime_;
    const ke::SequenceObject* q = ke::activeSequence(builder_->graph());
    return q ? double(q->time) : 0;
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
    if (!builder_->editing() || !ke::activeSequence(builder_->graph())) return;
    if (!scrubbing_) builder_->beginSequencePreview(), scrubbing_ = true;
    scrubTime_ = time;
    builder_->previewSequence(time);
    sync();
}

void SequencerPanel::endScrub() {
    scrubbing_ = false;
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

void SequencerPanel::keySelected() {
    if (!builder_->editing()) return note_->setText("Ключи ставятся в правке: остановите «Пуск»");
    if (builder_->orderedSelection().empty()) return note_->setText("Выделите объект для ключа");
    run("sequence_key", QJsonObject{{"time", playhead()}});
}

void SequencerPanel::setPlayTime(double time) {
    playTime_ = time;
    if (!builder_->editing()) sync();
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
    {
        const QSignalBlocker hush(loop_), quiet(record_);
        loop_->setChecked(active && active->loop);
        record_->setChecked(ops_->recording());
    }
    if (const QString words = ops_->recording() ? "■ Закончить запись" : "⏺ Запись"; record_->text() != words) { // not every frame
        record_->setText(words);
        record_->setStyleSheet(ops_->recording() ? "QToolButton { color: #ff6a5a; font-weight: bold; }" : QString());
    }
    const double length = active ? double(active->length()) : 0;
    time_->setText(QString("%1 / %2 с").arg(playhead(), 0, 'f', 2).arg(length, 0, 'f', 2));
    timeline_->setMinimumHeight(kRuler + std::max(3, active ? int(active->tracks.size()) : 0) * kRow + 4);
    timeline_->update();
}
