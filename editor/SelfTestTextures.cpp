// Texture controls, persistence and actual GL pixels in edit/play, using a local image fixture.
#include "SelfTestSupport.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QImage>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
using namespace ke;
using namespace selftest;

namespace {
const Entity* entity(const SceneBuilder& builder, uint32_t id) {
    for (const Entity& e : builder.graph().entities) if (e.id == id) return &e;
    return nullptr;
}
QColor pixelAt(Viewport* v, const QImage& image, Vector3 point) {
    if (image.isNull()) return QColor();
    const Vector2 p = v->gizmoView().project(point);
    const double ratio = double(image.width()) / v->width();
    const int x = std::clamp(int(p.x * ratio), 0, image.width() - 1);
    const int y = std::clamp(int(p.y * ratio), 0, image.height() - 1);
    return image.pixelColor(x, y);
}
}
static uint32_t checkerStage(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir) {
    b.newScene();
    const uint32_t cube = made(b, b.createActions()[0]);
    b.editEntity(cube, [](Entity& e) {
        e.size = Vector3(0.6f); e.position = {0, 0.3f, 0}; e.color = Vector3(0.8f);
        e.rigid.enabled = e.collider.enabled = e.rigid.fixed = true;
    });
    const uint32_t camera = made(b, b.cameraAction());
    b.editCamera(camera, [](Camera& k) { k.position = {0, 0.3f, 2.2f}; k.rotationDeg = Vector3(0); k.fovDeg = 40; });
    b.lookThrough(camera); b.select(cube); pump(200);
    auto* kind = w.findChild<QComboBox*>("textureKind");
    auto* repeats = w.findChild<QDoubleSpinBox*>("textureRepeats");
    auto* file = w.findChild<QLineEdit*>("textureFile");
    c.check(kind && repeats && file, "texture controls exist in object properties");
    if (!kind || !repeats || !file) return 0;
    kind->setCurrentIndex(1); repeats->setValue(4); pump(150);
    c.check(entity(b, cube)->texture.kind == TextureKind::Checker && entity(b, cube)->texture.repeats == 4,
            "checker and repeat scale change through inspector widgets");
    b.undoAction()->trigger();
    c.check(entity(b, cube)->texture.repeats == 8, "undo restores texture scale");
    b.redoAction()->trigger(); b.select(0); pump(200);
    const Vector3 dark(-0.1875f, 0.3625f, 0.3f), light(-0.0625f, 0.3625f, 0.3f);
    QImage image = v->grabFramebuffer();
    c.check(brightnessAt(v, image, light) > brightnessAt(v, image, dark) + 50, "GL pixels show alternating checker cells in edit mode");
    if (!shotsDir.isEmpty()) image.save(shotsDir + "/texture-checker.png");
    b.play(); waitFrames(v, 3, 15000); pump(150); image = v->grabFramebuffer();
    c.check(brightnessAt(v, image, light) > brightnessAt(v, image, dark) + 50, "checker stays aligned on rigid body in play mode");
    b.stop(); b.select(cube); pump(150);
    return cube;
}
int runTextureTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir) {
    Checker c; QTemporaryDir tmp;
    const uint32_t cube = checkerStage(c, w, b, v, shotsDir);
    if (!cube) return c.failures;
    auto* kind = w.findChild<QComboBox*>("textureKind");
    auto* file = w.findChild<QLineEdit*>("textureFile");
    const Vector3 light(-0.0625f, 0.3625f, 0.3f);
    QImage image;
    QImage fixture(32, 32, QImage::Format_RGB32); fixture.fill(QColor(240, 20, 10));
    const QString path = tmp.path() + "/фото стены.png"; fixture.save(path);
    file->setText(path); kind->setCurrentIndex(2); pump(200); b.select(0); pump(200);
    image = v->grabFramebuffer();
    const QColor red = pixelAt(v, image, light);
    c.check(red.red() > red.green() + 60 && red.red() > red.blue() + 60, "a PNG with spaces and Cyrillic path reaches actual GL pixels");
    if (!shotsDir.isEmpty()) { image.save(shotsDir + "/texture-image.png"); b.select(cube); pump(100); windowShot(w).save(shotsDir + "/texture-properties.png"); b.select(0); }
    b.play(); waitFrames(v, 3, 15000); pump(150);
    const QColor played = pixelAt(v, v->grabFramebuffer(), light);
    c.check(played.red() > played.green() + 60, "chosen image is also rendered during simulation");
    b.stop(); pump(150);
    QImage photo(48, 24, QImage::Format_RGB32); photo.fill(QColor(240, 20, 10));
    const QString jpeg = tmp.path() + "/своё фото.jpg";
    c.check(photo.save(jpeg), "JPEG encoder is available for image fixture");
    b.select(0); pump(100);
    const Vector2 target = v->gizmoView().project(light);
    QMimeData mime; mime.setUrls({QUrl::fromLocalFile(jpeg)});
    QDragEnterEvent enter(QPoint(int(target.x), int(target.y)), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(v, &enter);
    QDropEvent drop(QPointF(target.x, target.y), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(v, &drop); pump(200);
    c.check(enter.isAccepted() && drop.isAccepted() && entity(b, cube)->texture.file == jpeg.toStdString(),
            "dropping a JPEG directly onto the object assigns it without a material dialog");
    c.check(std::abs(entity(b, cube)->texture.repeats - 1.0f / 0.6f) < 0.001f, "image drop automatically fits initial scale");
    b.select(0); pump(100);
    const QColor jpgColor = pixelAt(v, v->grabFramebuffer(), light);
    c.check(jpgColor.red() > jpgColor.green() + 60, "JPEG drop reaches GL rendering");
    b.undoAction()->trigger(); pump(100);
    c.check(entity(b, cube)->texture.file == path.toStdString(), "one undo restores the preceding image after drop");
    QString error; const QString scene = tmp.path() + "/test.kscene";
    c.check(b.saveFile(scene, error) && b.openFile(scene, error) && entity(b, cube)->texture.file == path.toStdString(),
            "scene file restores image reference and settings");
    b.editEntity(cube, [](Entity& e) { e.texture.kind = TextureKind::None; }); b.select(0); pump(200);
    image = v->grabFramebuffer();
    const QColor plain = pixelAt(v, image, light);
    c.check(std::abs(plain.red() - plain.green()) < 10, "disabling texture restores base color without leaking last image");
    b.editEntity(cube, [](Entity& e) { e.texture.kind = TextureKind::Image; e.texture.file = "/missing/texture.png"; });
    pump(200); image = v->grabFramebuffer();
    const QColor missing = pixelAt(v, image, light);
    c.check(std::abs(missing.red() - plain.red()) < 5, "missing image safely falls back to object's base color");
    b.lookThrough(0);
    return c.failures;
}
