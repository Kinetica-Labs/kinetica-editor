// A linear height ramp has an analytical normal: normalize(-height * repeats, 0, 1).
// Compare its actual shaded pixels, then verify zero, constant and missing height-map fallbacks.
#include "SelfTestSupport.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QTemporaryDir>
#include <QImage>
#include <cmath>
using namespace ke;
using namespace selftest;

namespace {
uint32_t stage(SceneBuilder& b) {
    b.newScene();
    const uint32_t cube = made(b, b.createActions()[0]);
    b.editEntity(cube, [](Entity& e) {
        e.size = Vector3(0.6f); e.position = {0, 0.3f, 0}; e.color = Vector3(0.5f);
        e.rigid.enabled = e.collider.enabled = e.rigid.fixed = true;
        e.texture.repeats = 4;
    });
    const uint32_t sun = made(b, b.lightActions()[0]);
    b.editLight(sun, [](Light& light) {
        light.rotationDeg = shining({-0.8f, 0, -0.6f}); light.intensity = 0.6f;
        light.color = Vector3(1); light.shadows = false;
    });
    const uint32_t camera = made(b, b.cameraAction());
    b.editCamera(camera, [](Camera& c) { c.position = {0, 0.3f, 2.2f}; c.rotationDeg = Vector3(0); c.fovDeg = 40; });
    b.lookThrough(camera); b.select(0); pump(200);
    return cube;
}
float expectedPixel(float slope) {
    const Vector3 normal = normalize(Vector3(-slope, 0, 1));
    const Vector3 view = normalize(Vector3(0.0625f, -0.0625f, 1.9f));
    const Vector3 light(0.8f, 0, 0.6f);
    const float diffuse = std::max(0.0f, dot(normal, light));
    const float specular = std::pow(std::max(0.0f, dot(normal, normalize(light + view))), 32) * 0.25f;
    return 255 * (0.5f * (0.18f + 0.1f * dot(normal, view)) + (0.5f * diffuse * 0.8f + specular) * 0.6f);
}
}
int runBumpTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir) {
    Checker c; QTemporaryDir tmp;
    const uint32_t cube = stage(b);
    QImage ramp(512, 32, QImage::Format_RGB32);
    for (int y = 0; y < ramp.height(); ++y) for (int x = 0; x < ramp.width(); ++x) {
        const int gray = x * 255 / (ramp.width() - 1); ramp.setPixelColor(x, y, QColor(gray, gray, gray));
    }
    const QString path = tmp.path() + "/высота.png"; c.check(ramp.save(path), "height ramp fixture saved");
    const Vector3 point(-0.0625f, 0.3625f, 0.3f);
    const float flat = brightnessAt(v, v->grabFramebuffer(), point);
    c.check(std::abs(flat - expectedPixel(0)) < 4, "flat reference matches analytical lighting");
    b.editEntity(cube, [&](Entity& e) { e.texture.heightFile = path.toStdString(); e.texture.bumpHeight = 0.04f; });
    b.select(0); pump(200);
    const QImage image = v->grabFramebuffer(); const float bumped = brightnessAt(v, image, point);
    std::printf("  ramp brightness: flat %.1f, bump %.1f, analytical %.1f\n", flat, bumped, expectedPixel(0.16f));
    c.check(std::abs(bumped - expectedPixel(0.16f)) < 4 && flat > bumped + 8, "height ramp produces analytical surface-gradient normal");
    if (!shotsDir.isEmpty()) image.save(shotsDir + "/bump-ramp.png");
    b.select(cube); pump(100);
    auto* enabled = w.findChild<QCheckBox*>("textureBump");
    auto* height = w.findChild<QDoubleSpinBox*>("textureBumpHeight");
    c.check(enabled && height && enabled->isChecked() && std::abs(height->value() - 40) < 0.01, "bump inspector uses millimetres");
    pump(750); // a separate undo step after the programmatic setup
    if (enabled) enabled->setChecked(false);
    b.select(0); pump(150);
    float stored = -1;
    for (const auto& e : b.graph().entities) if (e.id == cube) stored = e.texture.bumpHeight;
    c.check(stored == 0, "bump checkbox updates scene material");
    c.check(std::abs(brightnessAt(v, v->grabFramebuffer(), point) - flat) < 2, "zero bump restores flat pixels");
    b.undoAction()->trigger(); pump(150);
    c.check(std::abs(brightnessAt(v, v->grabFramebuffer(), point) - bumped) < 2, "one undo restores bump");
    ramp.fill(QColor(128, 128, 128)); const QString constant = tmp.path() + "/constant.png"; ramp.save(constant);
    b.editEntity(cube, [&](Entity& e) { e.texture.heightFile = constant.toStdString(); }); b.select(0); pump(150);
    c.check(std::abs(brightnessAt(v, v->grabFramebuffer(), point) - flat) < 2, "constant height has zero gradient");
    b.editEntity(cube, [](Entity& e) { e.texture.heightFile = "/missing/height.png"; }); pump(150);
    c.check(std::abs(brightnessAt(v, v->grabFramebuffer(), point) - flat) < 2, "missing explicit height map falls back to flat surface");
    b.lookThrough(0);
    return c.failures;
}
