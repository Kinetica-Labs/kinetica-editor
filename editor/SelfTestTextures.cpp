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
// A model with materials of its own (an OBJ and its .mtl): three squares in a row, the first wearing a red picture, the
// second a green one, the third a blue colour and no picture - one mesh, three runs, each in its own surface. A picture
// put on the object itself then covers the whole model.
static void testModelMaterials(Checker& c, SceneBuilder& b, Viewport* v, const QString& folder) {
    const auto write = [&](const QString& name, const QByteArray& text) { QFile f(folder + "/" + name); return f.open(QIODevice::WriteOnly) && f.write(text) == text.size(); };
    const auto picture = [&](const QString& name, QColor colour) { QImage i(8, 8, QImage::Format_RGB32); i.fill(colour); return i.save(folder + "/" + name); };
    const bool made = picture("red.png", QColor(240, 20, 10)) && picture("green.png", QColor(20, 200, 40)) && picture("yellow.png", QColor(240, 220, 20)) &&
                      write("row.mtl", "newmtl red\nmap_Kd red.png\nnewmtl green\nmap_Kd green.png\nnewmtl blue\nKd 0.1 0.2 0.9\n") &&
                      write("row.obj", "mtllib row.mtl\nv 0 0 0\nv 1 0 0\nv 2 0 0\nv 3 0 0\nv 0 1 0\nv 1 1 0\nv 2 1 0\nv 3 1 0\n"
                                       "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                                       "usemtl red\nf 1/1 2/2 6/3 5/4\nusemtl green\nf 2/1 3/2 7/3 6/4\nusemtl blue\nf 3/1 4/2 8/3 7/4\n");
    c.check(made, "a model's fixture: an OBJ, its .mtl and two pictures");
    b.newScene();
    const uint32_t row = made ? selftest::made(b, b.createActions()[0]) : 0;
    b.editEntity(row, [&](Entity& e) { e.shape = ShapeKind::Mesh; e.meshFile = (folder + "/row.obj").toStdString(); e.size = Vector3(1.5f, 0.5f, 0.5f); e.position = {0, 0.3f, 0}; });
    const uint32_t camera = selftest::made(b, b.cameraAction());
    b.editCamera(camera, [](Camera& k) { k.position = {0, 0.3f, 2.2f}; k.rotationDeg = Vector3(0); k.fovDeg = 40; });
    b.lookThrough(camera); b.select(0); pump(250);
    QImage image = v->grabFramebuffer();
    const QColor left = pixelAt(v, image, {-0.5f, 0.3f, 0}), middle = pixelAt(v, image, {0, 0.3f, 0}), right = pixelAt(v, image, {0.5f, 0.3f, 0});
    std::printf("  a model's materials: red picture %d %d %d, green picture %d %d %d, blue colour %d %d %d\n", left.red(), left.green(), left.blue(),
                middle.red(), middle.green(), middle.blue(), right.red(), right.green(), right.blue());
    c.check(left.red() > left.green() + 60 && middle.green() > middle.red() + 60 && right.blue() > right.red() + 60 && right.blue() > right.green() + 30,
            "a model's materials: each square in its own picture or colour, one mesh");
    b.editEntity(row, [&](Entity& e) { e.texture.kind = TextureKind::Image; e.texture.file = (folder + "/yellow.png").toStdString(); });
    b.select(0); pump(250);
    image = v->grabFramebuffer();
    const QColor covered = pixelAt(v, image, {0.5f, 0.3f, 0});
    c.check(covered.red() > covered.blue() + 60 && covered.green() > covered.blue() + 60, "a picture put on the object covers the model's own materials");
    b.lookThrough(0);
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
    testModelMaterials(c, b, v, tmp.path());
    return c.failures;
}
