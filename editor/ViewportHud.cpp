// The performance plate in the view's lower left corner (see Viewport.h): while the scene plays, the frames a second in
// large figures coloured by how smooth they are (green from 55, amber from 30, red below), the last two seconds of
// frame times as a line against the 60 Hz frame (16.7 ms), and in small print what one frame took to paint, what the
// physics' step took, what the GPU took for the frame and its shadows (timer queries: a view bound by the GPU shows
// there, not in the processor's time), how fast the scene runs against the wall clock and who draws. It is there while
// the scene runs or stands paused - not while it is edited, when the view is painted on a change only. The operation view_hud shows and
// hides it; its numbers are the frame_timing of sim_state.
#include "Viewport.h"
#include "UiLanguage.h"

#include <QFontMetrics>
#include <QOpenGLTimeMonitor>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace {

QColor smoothness(double fps) { return fps >= 55 ? QColor(94, 224, 122) : fps >= 30 ? QColor(240, 192, 64) : QColor(255, 106, 90); }

// "Mesa Intel(R) UHD Graphics (TGL GT1)" -> "Intel UHD Graphics"; llvmpipe is said as the processor.
QString rendererWords(const QString& renderer) {
    if (renderer.contains("llvmpipe", Qt::CaseInsensitive) || renderer.contains("softpipe", Qt::CaseInsensitive)) return uiText("Kinetica", "процессор (llvmpipe)");
    QString r = renderer;
    r.remove("Mesa ").remove("(R)").remove("(TM)");
    if (const int paren = r.indexOf(" ("); paren > 0) r = r.left(paren);
    if (const int slash = r.indexOf('/'); slash > 0) r = r.left(slash);
    return uiText("Kinetica", "видеокарта ") + r.simplified();
}

} // namespace

// The GPU's clock: three marks a frame - its start, after the shadow pass, its end - in one of two monitors by turns,
// read when the GPU has passed them (a frame later, never waiting), smoothed for reading.
void Viewport::beginGpuFrame() {
    gpuTimed_ = false;
    if (!snap_) return;
    if (!gpuClock_[0]) {
        for (QOpenGLTimeMonitor*& m : gpuClock_) {
            m = new QOpenGLTimeMonitor(this);
            m->setSampleCount(3);
            if (!m->create()) delete m, m = nullptr;
        }
        if (!gpuClock_[0] || !gpuClock_[1]) return;
    }
    const auto read = [this](int k, bool wait) {
        if (!gpuPending_[k] || (!wait && !gpuClock_[k]->isResultAvailable())) return;
        const QVector<GLuint64> ns = gpuClock_[k]->waitForIntervals();
        if (ns.size() == 2 && ns[0] < 1000000000ull && ns[1] < 1000000000ull) { // a mark lost (a context's reset) reads as ages: dropped
            const float shadow = float(double(ns[0]) * 1e-6), all = float(double(ns[0] + ns[1]) * 1e-6);
            gpuShadowMs_ = gpuShadowMs_ < 0 ? shadow : 0.9f * gpuShadowMs_ + 0.1f * shadow;
            gpuMs_ = gpuMs_ < 0 ? all : 0.9f * gpuMs_ + 0.1f * all;
        }
        gpuClock_[k]->reset();
        gpuPending_[k] = false;
    };
    read(1 - gpuAt_, false);
    read(gpuAt_, true); // two frames old: the GPU is through it
    gpuClock_[gpuAt_]->recordSample();
    gpuTimed_ = true;
}

void Viewport::markGpuShadows() {
    if (gpuTimed_) gpuClock_[gpuAt_]->recordSample();
}

void Viewport::endGpuFrame() {
    if (!gpuTimed_) return;
    gpuClock_[gpuAt_]->recordSample();
    gpuPending_[gpuAt_] = true;
    gpuAt_ = 1 - gpuAt_;
    gpuTimed_ = false;
}

// Called after each painted frame: its interval from the last one goes into the ring of the last 120.
void Viewport::recordFrameInterval() {
    if (frameClock_.isValid()) {
        frameMs_[frameAt_] = float(double(frameClock_.nsecsElapsed()) * 1e-6);
        frameAt_ = (frameAt_ + 1) % frameMs_.size();
        frameCount_ = std::min(frameCount_ + 1, frameMs_.size());
    }
    frameClock_.start();
}

// The frames a second over the last 30 frames: quick to follow a change, steady enough to read.
double Viewport::liveFps() const {
    const size_t n = std::min<size_t>(frameCount_, 30);
    if (n == 0) return 0;
    double sum = 0;
    for (size_t k = 0; k < n; ++k) sum += frameMs_[(frameAt_ + frameMs_.size() - 1 - k) % frameMs_.size()];
    return sum > 0 ? 1000.0 * double(n) / sum : 0;
}

void Viewport::setShowHud(bool on) {
    showHud_ = on;
    update();
}

void Viewport::drawHud(QPainter& p) {
    if (!showHud_ || !snap_ || !playFrame_) return; // edited, nothing runs: the plate stays away (F3 still sets it)
    const bool playing = !editMode_;
    const double fps = liveFps();
    QFont big = p.font();
    big.setPointSizeF(24);
    big.setBold(true);
    QFont small = p.font();
    small.setPointSizeF(8.5);
    small.setBold(false);
    const QString gpu = gpuMs_ >= 0 ? QString(uiText("Kinetica", "видеокарта %1 мс (тени %2) · ")).arg(double(gpuMs_), 0, 'f', 1).arg(double(gpuShadowMs_), 0, 'f', 1)
                                     : QString();
    const QString row1 = playing ? QString(uiText("Kinetica", "кадр %1 мс · ")).arg(fps > 0 ? 1000.0 / fps : 0.0, 0, 'f', 1) + gpu +
                                       QString(uiText("Kinetica", "процессор %1 мс · физика %2 мс")).arg(double(paintMs_), 0, 'f', 1).arg(double(snap_->stepMs), 0, 'f', 1)
                                 : gpu + QString(uiText("Kinetica", "процессор %1 мс · физика стоит (%2)")).arg(double(paintMs_), 0, 'f', 1)
                                             .arg(snap_->time > 0 ? uiText("Kinetica", "пауза") : uiText("Kinetica", "правка")); // a paused world's frame has its time
    const QString speed = playing && realtime_ > 0 ? QString(uiText("Kinetica", "×%1 реального времени · ")).arg(realtime_, 0, 'f', 2) : QString();
    const QString row2 = speed + rendererWords(renderer_);
    const QFontMetrics fm(small); // the plate as wide as its words
    const double words = std::max(fm.horizontalAdvance(row1), fm.horizontalAdvance(row2));
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF plate(14, height() - 14 - 74, std::max(300.0, 104 + words + 14), 74);
    p.setPen(QPen(QColor(255, 255, 255, 22), 1));
    p.setBrush(QColor(14, 16, 20, 190));
    p.drawRoundedRect(plate, 10, 10);
    const QColor colour = playing ? smoothness(fps) : QColor(150, 160, 175);
    p.setFont(big);
    p.setPen(colour);
    const QString figure = playing ? QString::number(std::lround(fps)) : QString("—");
    const QRectF number(plate.left() + 12, plate.top() + 4, 74, 40);
    p.drawText(number, Qt::AlignLeft | Qt::AlignVCenter, figure);
    p.setFont(small);
    p.setPen(QColor(170, 178, 192));
    p.drawText(QRectF(number.left(), number.bottom() - 4, 90, 14), Qt::AlignLeft | Qt::AlignVCenter, playing ? uiText("Kinetica", "кадров/с") : uiText("Kinetica", "по изменению"));

    // The frame times of the last two seconds against the 60 Hz frame: a flat line is a smooth picture, a spike a hitch.
    const QRectF graph(plate.left() + 104, plate.top() + 10, plate.width() - 116, 30);
    p.setPen(QPen(QColor(255, 255, 255, 40), 1, Qt::DashLine));
    const double top = 50.0; // ms at the graph's top: 20 frames a second
    const double sixty = graph.bottom() - graph.height() * (1000.0 / 60) / top;
    p.drawLine(QPointF(graph.left(), sixty), QPointF(graph.right(), sixty));
    if (playing && frameCount_ > 1) {
        QPainterPath line;
        for (size_t k = 0; k < frameCount_; ++k) {
            const float ms = frameMs_[(frameAt_ + frameMs_.size() - frameCount_ + k) % frameMs_.size()];
            const QPointF at(graph.left() + graph.width() * double(k) / double(frameMs_.size() - 1),
                             graph.bottom() - graph.height() * std::min(double(ms), top) / top);
            if (k == 0) line.moveTo(at);
            else line.lineTo(at);
        }
        p.setPen(QPen(colour, 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawPath(line);
    }

    p.setPen(QColor(200, 206, 218));
    p.drawText(QRectF(graph.left(), graph.bottom() + 4, graph.width(), 13), Qt::AlignLeft | Qt::AlignVCenter, row1);
    p.setPen(renderer_.contains("llvmpipe", Qt::CaseInsensitive) ? QColor(240, 192, 64) : QColor(150, 160, 175));
    p.drawText(QRectF(graph.left(), graph.bottom() + 18, graph.width(), 13), Qt::AlignLeft | Qt::AlignVCenter, row2);
    p.restore();
}
