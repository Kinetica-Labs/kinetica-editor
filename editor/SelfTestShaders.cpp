// Compiles every shader of shaders/ as GLSL 130 and 330 on this GL, every shadow slot kind of the mesh
// shader among them (the view builds only the kinds its lights use: a variant it has not used yet is checked here).
#include "SelfTestSupport.h"
#include "Shaders.h"
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
using namespace selftest;

namespace {
bool compilePair(const QString& header, const char* vertex, const char* fragment, const QString& defines = {}) {
    QOpenGLShaderProgram program;
    const QString vs = header + shaderSource(vertex);
    const QString fs = header + defines + shaderSource(fragment);
    const bool ok = program.addShaderFromSourceCode(QOpenGLShader::Vertex, vs) &&
                    program.addShaderFromSourceCode(QOpenGLShader::Fragment, fs) && program.link();
    if (!ok) qWarning("Shader pair %s / %s: %s", vertex, fragment, qPrintable(program.log()));
    return ok;
}
bool compileShadows(const QString& header) {
    for (int slot = 0; slot < 4; ++slot) for (const char* kind : {"MAP", "CUBE"}) {
        const QString defines = QString("#define KE_SHADOWS\n#define KE_SLOT%1_%2\n").arg(slot).arg(kind);
        if (!compilePair(header, "mesh.vert", "mesh.frag", defines)) return false;
    }
    return compilePair(header, "mesh.vert", "mesh.frag",
        "#define KE_SHADOWS\n#define KE_SLOT0_MAP\n#define KE_SLOT1_CUBE\n#define KE_SLOT2_MAP\n#define KE_SLOT3_CUBE\n");
}
}
int runShaderTests(Viewport* viewport) {
    Checker c; viewport->makeCurrent();
    std::printf("  GL renderer: %s\n", reinterpret_cast<const char*>(viewport->context()->extraFunctions()->glGetString(GL_RENDERER)));
    const std::pair<const char*, const char*> pairs[] = {
        {"background.vert", "background.frag"}, {"mesh.vert", "mesh.frag"}, {"lines.vert", "lines.frag"},
        {"background.vert", "outline.frag"}, {"shadow.vert", "shadow.frag"}, {"shadow-distance.vert", "shadow-distance.frag"}
    };
    const auto version = viewport->context()->format();
    for (const QString& header : {QString("#version 130\n"), QString("#version 330 core\n")}) {
        if (header.contains("330") && version.majorVersion() * 10 + version.minorVersion() < 33) continue;
        for (const auto& pair : pairs) {
            const QString label = header.trimmed() + ": " + pair.second;
            c.check(compilePair(header, pair.first, pair.second), qPrintable(label));
        }
        c.check(compileShadows(header), qPrintable(header.trimmed() + ": all shadow slot variants"));
    }
    viewport->doneCurrent();
    return c.failures;
}
