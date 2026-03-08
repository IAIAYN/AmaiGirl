#include "engine/Renderer.hpp"
#include <QOpenGLTexture>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <algorithm>
#include <random>
#include <cmath>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QWindow>
#include <QStringList>
#include <QApplication>
#include <QGuiApplication>
#include "common/SettingsManager.hpp"
#if defined(Q_OS_MACOS)
#include <ApplicationServices/ApplicationServices.h>
#elif defined(Q_OS_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#pragma comment(lib, "user32.lib")
#endif

// physics removed

static const char* VS = R"GLSL(
#version 120
attribute vec2 aPos;
attribute vec2 aUv;
varying vec2 vUv;
void main(){
    vUv = aUv;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

static const char* FS = R"GLSL(
#version 120
uniform sampler2D uTex;
uniform sampler2D uMaskTex;
uniform float uOpacity;
uniform int uMaskWrite; // 1 when writing mask
uniform int uUseMask;   // 1 when sampling prebuilt mask texture
uniform int uInvertMask;
uniform vec2 uInvViewport;
uniform float uMaskThreshold; // discard low-alpha fringes when > 0
varying vec2 vUv;
void main(){
    if (uMaskWrite == 1) {
        // mask pass: write source alpha into offscreen mask texture
        vec4 c = texture2D(uTex, vUv);
        if (c.a <= 0.0) discard;
        gl_FragColor = vec4(0.0, 0.0, 0.0, c.a);
    } else {
        vec4 c = texture2D(uTex, vUv);
        // 可选丢弃：仅当阈值 > 0 时启用，避免产生锯齿
        if (uMaskThreshold > 0.0 && c.a < uMaskThreshold) discard;
        float factor = uOpacity;
        if (uUseMask == 1) {
            vec2 suv = vec2(gl_FragCoord.x * uInvViewport.x, gl_FragCoord.y * uInvViewport.y);
            float ma = texture2D(uMaskTex, suv).a;
            float m = (uInvertMask == 1) ? (1.0 - ma) : ma;
            factor *= m;
        }
        float a = c.a * factor;
        if (a <= 0.0) discard;
        gl_FragColor = vec4(c.rgb * factor, a);
    }
}
)GLSL";

Renderer::Renderer(QWidget *parent) : QOpenGLWidget(parent) {
    connect(&m_timer, &QTimer::timeout, this, [this]{
        double dt = m_elapsed.nsecsElapsed() / 1e9;
        m_elapsed.restart();
        // Procedural
        updateProcedural(dt);
        // WASD move origin
        float step = 20.0f;
        if (m_keyW) m_originOffset.y -= step;
        if (m_keyS) m_originOffset.y += step;
        if (m_keyA) m_originOffset.x -= step;
        if (m_keyD) m_originOffset.x += step;
        update();
    });
    m_timer.start(16);
    m_elapsed.start();
    setMouseTracking(true);
    setContextMenuPolicy(Qt::DefaultContextMenu);

    // 采用指针命中测试 + WA_TransparentForMouseEvents 切换，避免窗口形状裁剪绘制
    m_hitTestTimer.setSingleShot(false);
    m_hitTestTimer.setInterval(50); // 20Hz 足够平滑且开销很低
    connect(&m_hitTestTimer, &QTimer::timeout, this, [this]{ updateMouseTransparent(); });
    m_hitTestTimer.start();
}

Renderer::~Renderer() {
    // 确保计时器停止，避免析构后回调
    m_timer.stop();
    // 在 GL 上下文有效的情况下释放 GL 资源
    makeCurrent();
    cleanupModelGL();
    if (m_gpu.inited) {
        if (m_gpu.vbo.isCreated()) m_gpu.vbo.destroy();
        if (m_gpu.ebo.isCreated()) m_gpu.ebo.destroy();
        if (m_gpu.maskTex) {
            glDeleteTextures(1, &m_gpu.maskTex);
            m_gpu.maskTex = 0;
        }
        if (m_gpu.maskFbo) {
            glDeleteFramebuffers(1, &m_gpu.maskFbo);
            m_gpu.maskFbo = 0;
        }
        m_gpu.maskW = 0;
        m_gpu.maskH = 0;
        m_gpu.prog.removeAllShaders();
        m_gpu.inited = false;
    }
    doneCurrent();
}

void Renderer::setEnablePhysics(bool v) {
    if (m_enablePhysics == v) return;
    m_enablePhysics = v;
    if (m_enablePhysics) {
        if (!m_physics) m_physics = std::make_unique<PhysicsEngine>();
        if (m_model) m_physics->init(m_model);
    } else {
        if (m_physics) m_physics->reset(m_model);
    }
}

void Renderer::setEnableBreath(bool v) {
    if (m_enableBreath == v) return;
    m_prevEnableBreath = m_enableBreath;
    m_enableBreath = v;
    if (!m_model) return;

    // 重置呼吸输出与累积，避免从上一次状态留下偏置
    m_breathTimer = 0.0;
    m_breathOut = {};
    m_breathAccumValid = false;
}

void Renderer::setEnableGaze(bool v) {
    if (m_enableGaze == v) return;
    m_prevEnableGaze = m_enableGaze;
    m_enableGaze = v;
    if (!m_model) return;

    int pc = (int)csmGetParameterCount(m_model->moc.model);
    float* params = const_cast<float*>(csmGetParameterValues(m_model->moc.model));
    const float* minVals = csmGetParameterMinimumValues(m_model->moc.model);
    const float* maxVals = csmGetParameterMaximumValues(m_model->moc.model);
    const float* defVals = csmGetParameterDefaultValues(m_model->moc.model);

    if (!m_enableGaze) {
        // Reset eyeballs and head/body accumulators to defaults smoothly.
        resetGazeParamsToDefault(params, minVals, maxVals, defVals);
        m_gazeAccum.valid = false; m_gazeX = 0.0f; m_gazeY = 0.0f;
        m_gazeOut = {}; m_eyeOutX = m_eyeOutY = 0.0f;
        // physics stabilize next frame
        m_physicsNeedsStabilize = true;
    } else {
        // When enabling gaze, clear accum/outputs to avoid discontinuity
        m_gazeAccum.valid = false; m_gazeX = 0.0f; m_gazeY = 0.0f;
        m_gazeOut = {}; m_eyeOutX = m_eyeOutY = 0.0f;
        m_physicsNeedsStabilize = true;
    }
}

void Renderer::setWatermarkExpression(const QString &expPath) {
    m_wmPath = expPath;
    m_wmExpr.reset();
    if (!m_wmPath.isEmpty()) {
        try {
            m_wmExpr = ExpressionLoader::load(m_wmPath);
        } catch (...) {
            m_wmExpr.reset();
        }
    }
}

void Renderer::load(const QString &modelJson) {
    // 在切换模型前，释放与旧模型相关的 GPU 资源，避免纹理持有导致泄漏
    makeCurrent();
    cleanupModelGL();
    doneCurrent();

    m_model = ModelLoader::loadModel(modelJson);

    // 从 per-model 配置读取渲染选项
    m_textureCap = SettingsManager::instance().textureMaxDim();
    m_msaaSamples = SettingsManager::instance().msaaSamples();

    // 在有效 GL 上下文中创建新模型的纹理并分配给 drawables
    makeCurrent();
    buildModelTextures();
    doneCurrent();

    csmVector2 size, origin; float ppu;
    csmReadCanvasInfo(m_model->moc.model, &size, &origin, &ppu);
    m_canvasSize = { size.X, size.Y };
    m_canvasOrigin = { origin.X, origin.Y };

    // cache parameter ids list
    m_paramIds.clear();
    {
        int pcnt = (int)csmGetParameterCount(m_model->moc.model);
        const char** pids = csmGetParameterIds(m_model->moc.model);
        for (int i=0;i<pcnt;++i) m_paramIds << QString::fromUtf8(pids[i]);
    }

    // init physics engine
    if (!m_physics) m_physics = std::make_unique<PhysicsEngine>();
    m_physics->init(m_model);
    m_physicsNeedsStabilize = true; // 标记首帧需要稳定物理

    computeModelBounds();
    setupParamIndices();
    recomputeMapping();

    // 初始化 pose 运行态：与组数量对齐
    m_poseState.clear();
    if (m_model && m_model->pose.has_value() && m_model->pose->valid) {
        m_poseState.resize(m_model->pose->groups.size());
        for (auto &s : m_poseState) s.lastChosen = -1;
        m_poseJustInitialized = true; // 首帧强制稳定

        // 从配置读取用户选择的 A/B（0/1）
        m_userPoseAB = SettingsManager::instance().poseAB();
        // 仅选择一个可控 A/B 组，避免把所有 2-entry 组都强制到同一分支。
        m_userPoseGroupIndex = -1;
        for (int gi = 0; gi < m_model->pose->groups.size(); ++gi) {
            if (m_model->pose->groups[gi].entries.size() == 2) {
                m_userPoseGroupIndex = gi;
                break;
            }
        }
        m_poseFadeTimer = 0.0f; m_poseSwitching = false;

        // 立刻执行一次 pose 应用 + Core 更新，确保第一帧之前就稳定
        applyPose(0.0f);
        csmResetDrawableDynamicFlags(m_model->moc.model);
        csmUpdateModel(m_model->moc.model);
        syncModelDrawables();
    } else {
        m_poseJustInitialized = false;
        m_userPoseAB = 0; m_userPoseGroupIndex = -1; m_poseFadeTimer = 0.0f; m_poseSwitching = false;
    }

    // 清空用户水印表达式缓存，保持当前设置（若 SettingsManager 有保存，可在外部调用 setWatermarkExpression() 再注入）
    if (!m_wmPath.isEmpty()) {
        setWatermarkExpression(m_wmPath);
    }

    // 加载模型后，依据当前窗口高度建议新宽度
    if (auto* tl = window()) {
        int h = tl->height();
        emit requestFitWidthForHeight(h, suggestWidthForHeight(h));
    }

    // 切换模型后刷新透明性（不使用窗口形状遮罩）
    updateMouseTransparent();

    // 新模型：重置所有输出状态，防止旧状态残留
    m_breathTimer = 0.0; m_breathOut = {}; m_breathAccumValid = false;
    m_gazeX = m_gazeY = 0.0f; m_gazeAccum = {}; m_gazeOut = {}; m_eyeOutX = m_eyeOutY = 0.0f;
}

void Renderer::initializeGL() {
    initializeOpenGLFunctions();
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    // 只使用 MSAA；alpha-to-coverage 将在遮罩 pass 中按需开启
    glEnable(GL_MULTISAMPLE);

    m_gpu.prog.addShaderFromSourceCode(QOpenGLShader::Vertex, VS);
    m_gpu.prog.addShaderFromSourceCode(QOpenGLShader::Fragment, FS);
    m_gpu.prog.link();
    m_gpu.prog.bind();
    m_gpu.loc_pos = m_gpu.prog.attributeLocation("aPos");
    m_gpu.loc_uv  = m_gpu.prog.attributeLocation("aUv");
    m_gpu.uni_tex = m_gpu.prog.uniformLocation("uTex");
    m_gpu.uni_maskTex = m_gpu.prog.uniformLocation("uMaskTex");
    m_gpu.uni_opacity = m_gpu.prog.uniformLocation("uOpacity");
    m_gpu.uni_maskWrite = m_gpu.prog.uniformLocation("uMaskWrite");
    m_gpu.uni_useMask = m_gpu.prog.uniformLocation("uUseMask");
    m_gpu.uni_invertMask = m_gpu.prog.uniformLocation("uInvertMask");
    m_gpu.uni_invViewport = m_gpu.prog.uniformLocation("uInvViewport");
    m_gpu.uni_maskThreshold = m_gpu.prog.uniformLocation("uMaskThreshold");

    m_gpu.prog.setUniformValue(m_gpu.uni_tex, 0);
    if (m_gpu.uni_maskTex >= 0) m_gpu.prog.setUniformValue(m_gpu.uni_maskTex, 0);

    m_gpu.vbo.create();
    m_gpu.ebo.create();
    m_gpu.inited = true;

    // 若模型已先行加载但尚未创建纹理，这里补建一次
    if (m_model && !m_texturesReady) {
        buildModelTextures();
    }
}

void Renderer::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
    recomputeMapping();
    // 当高度变化时建议新的宽度
    emit requestFitWidthForHeight(h, suggestWidthForHeight(h));
    // 尺寸变化时刷新透明性
    updateMouseTransparent();
}

void Renderer::recomputeMapping() {
    int w = std::max(1, width());
    int h = std::max(1, height());

    // Fit entire model bounds to window while preserving aspect ratio, with margins
    if (!m_haveBounds) { m_fitScale = 1.0f; m_fitOffsetPx = {0,0}; return; }
    float modelW = (m_maxB.x - m_minB.x);
    float modelH = (m_maxB.y - m_minB.y);
    if (modelW <= 0 || modelH <= 0) { m_fitScale = 1.0f; m_fitOffsetPx = {0,0}; return; }

    // 计算边距后的可用区域尺寸
    float mx = m_marginRatio * w;
    float my = m_marginRatio * h;
    float availW = std::max(1.0f, w - 2.0f * mx);
    float availH = std::max(1.0f, h - 2.0f * my);

    float sx = availW / modelW;
    float sy = availH / modelH;
    m_fitScale = std::min(sx, sy); // canvas->pixels

    // Centering offset in pixels so that model AABB is centered, with margins
    float modelPxW = modelW * m_fitScale;
    float modelPxH = modelH * m_fitScale;
    float offX = (w - modelPxW) * 0.5f - m_minB.x * m_fitScale;
    float offY = (h - modelPxH) * 0.5f + m_maxB.y * m_fitScale; // + because we flip Y later

    m_fitOffsetPx = { offX + m_originOffset.x, offY + m_originOffset.y };
}

void Renderer::ensureMaskBuffer(int w, int h) {
    w = std::max(1, w);
    h = std::max(1, h);
    if (m_gpu.maskTex && m_gpu.maskFbo && m_gpu.maskW == w && m_gpu.maskH == h) return;

    if (m_gpu.maskTex) {
        glDeleteTextures(1, &m_gpu.maskTex);
        m_gpu.maskTex = 0;
    }
    if (m_gpu.maskFbo) {
        glDeleteFramebuffers(1, &m_gpu.maskFbo);
        m_gpu.maskFbo = 0;
    }

    glGenTextures(1, &m_gpu.maskTex);
    glBindTexture(GL_TEXTURE_2D, m_gpu.maskTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &m_gpu.maskFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_gpu.maskFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_gpu.maskTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());

    m_gpu.maskW = w;
    m_gpu.maskH = h;
}

int Renderer::suggestWidthForHeight(int targetHeightPx) const {
    if (!m_haveBounds) return std::max(150, targetHeightPx/2);
    float modelW = (m_maxB.x - m_minB.x);
    float modelH = (m_maxB.y - m_minB.y);
    if (modelW <= 0 || modelH <= 0) return std::max(150, targetHeightPx/2);

    // 给定高度和边距，计算使模型完整显示所需宽度
    float marginY = m_marginRatio * targetHeightPx;
    float availH = std::max(1.0f, targetHeightPx - 2.0f * marginY);
    float scale = availH / modelH; // 以高度为主
    float contentW = modelW * scale;
    float marginX = m_marginRatio * (contentW + 2.0f * m_marginRatio * targetHeightPx); // 估算宽度后再给左右边距
    int suggested = (int)std::ceil(contentW + 2.0f * marginX);
    suggested = std::clamp(suggested, 150, 5000);
    return suggested;
}

void Renderer::computeModelBounds() {
    if (!m_model) { m_haveBounds = false; return; }
    bool first = true;
    Vec2 minB{0,0}, maxB{0,0};
    for (const auto &d : m_model->drawables) {
        for (const auto &p : d.pos) {
            if (first) { minB = maxB = p; first = false; }
            else {
                minB.x = std::min(minB.x, p.x); minB.y = std::min(minB.y, p.y);
                maxB.x = std::max(maxB.x, p.x); maxB.y = std::max(maxB.y, p.y);
            }
        }
    }
    m_minB = minB; m_maxB = maxB; m_haveBounds = !first;
}

void Renderer::paintGL() {
    // 若模型存在但纹理尚未构建（例如 load 在 initializeGL 之前调用），现在构建一次
    if (m_model && !m_texturesReady) {
        buildModelTextures();
    }

    glClearColor(0.0f,0.0f,0.0f,0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    if (!m_model || !m_gpu.inited) return;

    GLint vp[4] = {0, 0, 1, 1};
    glGetIntegerv(GL_VIEWPORT, vp);
    m_framebufferW = std::max(1, vp[2]);
    m_framebufferH = std::max(1, vp[3]);

    // order
    std::vector<Drawable*> order;
    order.reserve(m_model->drawables.size());
    for (auto &d : m_model->drawables) order.push_back(&d);
    std::sort(order.begin(), order.end(), [](auto a, auto b){
        if (a->order != b->order) return a->order < b->order;
        return a->index < b->index;
    });

    // 缓存 Core 动态标志用于可见性判断
    const csmFlags* dynFlags = csmGetDrawableDynamicFlags(m_model->moc.model);

    for (auto d : order) {

        // 1) 尊重 Core 可见性
        if (!(dynFlags[d->index] & csmIsVisible)) continue;

        // 使用 Core 输出的 drawable opacity，避免重复叠乘导致局部透明/空白
        float finalOpacity = d->opacity;
        if (finalOpacity <= 0.0f) continue;

        if (!d->masks.empty()) {
            // Offscreen mask pass: render mask sources into alpha texture.
            ensureMaskBuffer(m_framebufferW, m_framebufferH);
            glBindFramebuffer(GL_FRAMEBUFFER, m_gpu.maskFbo);
            glViewport(0, 0, m_gpu.maskW, m_gpu.maskH);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glDisable(GL_STENCIL_TEST);
            glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

            for (auto midx : d->masks) {
                if (midx >= m_model->drawables.size()) continue;
                const Drawable& maskD = m_model->drawables[midx];
                if (!maskD.texture || maskD.idx.empty()) continue;
                drawDrawable(maskD, true, 1.0f);
            }

            // Content pass on default framebuffer, sampling mask texture.
            glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
            glViewport(0, 0, m_framebufferW, m_framebufferH);
            glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
            drawDrawable(*d, false, finalOpacity);
        } else {
            // 普通内容绘制，保持关闭以保留细节
            glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
            drawDrawable(*d, false, finalOpacity);
        }
    }

}

void Renderer::upload(const Drawable &d, std::vector<float>& verts) {
    // build interleaved [pos.x pos.y uv.x uv.y] in NDC space
    verts.clear();
    verts.reserve(d.pos.size()*4);
    int w = width(), h = height();

    float scale = m_fitScale;
    Vec2 offset = m_fitOffsetPx;
    for (size_t i = 0; i < d.pos.size(); ++i) {
        float x = d.pos[i].x * scale + offset.x;
        float y = -d.pos[i].y * scale + offset.y; // flip Y for screen space
        float nx = (x / (w*0.5f)) - 1.0f;
        float ny = 1.0f - (y / (h*0.5f));
        float u = d.uv[i].x;
        float v = 1.0f - d.uv[i].y;
        verts.push_back(nx); verts.push_back(ny); verts.push_back(u); verts.push_back(v);
    }

    m_gpu.vbo.bind();
    m_gpu.vbo.allocate(verts.data(), (int)(verts.size()*sizeof(float)));
    m_gpu.ebo.bind();
    m_gpu.ebo.allocate(d.idx.data(), (int)(d.idx.size()*sizeof(uint16_t)));
}

void Renderer::drawDrawable(const Drawable &d, bool asMask, float overrideOpacity) {
    if (!d.texture) return;
    std::vector<float> verts;
    upload(d, verts);

    m_gpu.prog.bind();

    glEnableVertexAttribArray(m_gpu.loc_pos);
    glEnableVertexAttribArray(m_gpu.loc_uv);
    m_gpu.vbo.bind();
    glVertexAttribPointer(m_gpu.loc_pos, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (const void*)0);
    glVertexAttribPointer(m_gpu.loc_uv,  2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (const void*)(2*sizeof(float)));

    d.texture->bind(0);
    m_gpu.prog.setUniformValue(m_gpu.uni_tex, 0);
    if (!asMask && !d.masks.empty() && m_gpu.maskTex != 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_gpu.maskTex);
        glActiveTexture(GL_TEXTURE0);
    }

    // 还原：不再对高光层做额外调制
    float contentOpacity = overrideOpacity;

    m_gpu.prog.setUniformValue(m_gpu.uni_opacity, contentOpacity);
    m_gpu.prog.setUniformValue(m_gpu.uni_maskWrite, asMask ? 1 : 0);
    const bool useMask = (!asMask && !d.masks.empty() && m_gpu.maskTex != 0);
    if (m_gpu.uni_maskTex >= 0) {
        m_gpu.prog.setUniformValue(m_gpu.uni_maskTex, useMask ? 1 : 0);
    }
    m_gpu.prog.setUniformValue(m_gpu.uni_useMask, useMask ? 1 : 0);
    m_gpu.prog.setUniformValue(m_gpu.uni_invertMask, ((d.cflag & csmIsInvertedMask) != 0) ? 1 : 0);
    m_gpu.prog.setUniformValue(m_gpu.uni_invViewport, 1.0f / std::max(1, m_framebufferW), 1.0f / std::max(1, m_framebufferH));
    // 遮罩阶段不做 alpha 阈值丢弃，避免半透明遮罩细节被误裁
    float thr = 0.0f;
    m_gpu.prog.setUniformValue(m_gpu.uni_maskThreshold, thr);

    if (asMask) {
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        // 根据 Cubism cflag 切换混合模式（预乘 alpha 下的推荐设置）
        const uint8_t cf = d.cflag;
        const bool isAdd = (cf & csmBlendAdditive) != 0;
        const bool isMul = (cf & csmBlendMultiplicative) != 0;
        if (isAdd) {
            glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        } else if (isMul) {
            glBlendFuncSeparate(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }
    }

    m_gpu.ebo.bind();
    glDrawElements(GL_TRIANGLES, (GLsizei)d.idx.size(), GL_UNSIGNED_SHORT, 0);

    d.texture->release();
    if (useMask) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
    }
    glDisableVertexAttribArray(m_gpu.loc_pos);
    glDisableVertexAttribArray(m_gpu.loc_uv);
}

void Renderer::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_W) m_keyW = true;
    if (e->key() == Qt::Key_A) m_keyA = true;
    if (e->key() == Qt::Key_S) m_keyS = true;
    if (e->key() == Qt::Key_D) m_keyD = true;
    if (!m_player) return;
    if (e->key() == Qt::Key_Space && !m_motionGroups.isEmpty()) {
        m_motionIndex = (m_motionIndex + 1) % m_motionGroups.size();
        m_player->playRandom(m_motionGroups[m_motionIndex], true);
    }
}

void Renderer::keyReleaseEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_W) m_keyW = false;
    if (e->key() == Qt::Key_A) m_keyA = false;
    if (e->key() == Qt::Key_S) m_keyS = false;
    if (e->key() == Qt::Key_D) m_keyD = false;
}

// Alpha-hit test by sampling drawables top-to-bottom at pointer
bool Renderer::isOpaqueAtGlobal(const QPoint &globalPos, float alphaThreshold) const {
    if (!m_model) return false;
    QPoint p = mapFromGlobal(globalPos);
    int w = width(), h = height();
    if (p.x() < 0 || p.y() < 0 || p.x() >= w || p.y() >= h) return false;

    // Iterate by descending render order: find topmost hit
    std::vector<const Drawable*> order;
    order.reserve(m_model->drawables.size());
    for (auto &d : m_model->drawables) order.push_back(&d);
    std::sort(order.begin(), order.end(), [](auto a, auto b){ return a->order > b->order; });

    float scale = m_fitScale; Vec2 off = m_fitOffsetPx;
    for (auto d : order) {
        if (!d->texture) continue;
        int triCount = (int)d->idx.size()/3;
        for (int ti=0; ti<triCount; ++ti) {
            int i0 = d->idx[ti*3+0];
            int i1 = d->idx[ti*3+1];
            int i2 = d->idx[ti*3+2];
            auto P0 = d->pos[i0]; auto P1 = d->pos[i1]; auto P2 = d->pos[i2];
            auto toScreen = [&](const Vec2& v){
                float x = v.x * scale + off.x; float y = -v.y * scale + off.y;
                return QPointF(x,y);
            };
            QPointF s0 = toScreen(P0), s1 = toScreen(P1), s2 = toScreen(P2);
            auto edge = [](const QPointF& a, const QPointF& b, const QPointF& c){ return (c.x()-a.x())*(b.y()-a.y()) - (c.y()-a.y())*(b.x()-a.x()); };
            QPointF pp(p);
            float e0 = edge(s0,s1,pp);
            float e1 = edge(s1,s2,pp);
            float e2 = edge(s2,s0,pp);
            bool hasNeg = (e0<0)||(e1<0)||(e2<0);
            bool hasPos = (e0>0)||(e1>0)||(e2>0);
            if (hasNeg && hasPos) continue; // outside triangle
            // 粗略命中：以 drawable 不透明度作为判定
            if (d->opacity >= alphaThreshold) return true;
        }
    }
    return false;
}

#if defined(Q_OS_MACOS)
static CGEventRef mkMouse(CGEventType type, CGPoint loc, CGMouseButton btn=kCGMouseButtonLeft) {
    return CGEventCreateMouseEvent(nullptr, type, loc, btn);
}
#endif

void Renderer::forwardMousePressToSystem(const QPoint &globalPos) {
#if defined(Q_OS_MACOS)
    CGPoint pt = CGPointMake(globalPos.x(), globalPos.y());
    CGEventRef e1 = mkMouse(kCGEventLeftMouseDown, pt);
    if (e1) { CGEventPost(kCGHIDEventTap, e1); CFRelease(e1); }
#else
    Q_UNUSED(globalPos);
#endif
}
void Renderer::forwardMouseMoveToSystem(const QPoint &globalPos) {
#if defined(Q_OS_MACOS)
    CGPoint pt = CGPointMake(globalPos.x(), globalPos.y());
    CGEventRef e1 = mkMouse(kCGEventMouseMoved, pt);
    if (e1) { CGEventPost(kCGHIDEventTap, e1); CFRelease(e1); }
#else
    Q_UNUSED(globalPos);
#endif
}
void Renderer::forwardMouseReleaseToSystem(const QPoint &globalPos) {
#if defined(Q_OS_MACOS)
    CGPoint pt = CGPointMake(globalPos.x(), globalPos.y());
    CGEventRef e1 = mkMouse(kCGEventLeftMouseUp, pt);
    if (e1) { CGEventPost(kCGHIDEventTap, e1); CFRelease(e1); }
#else
    Q_UNUSED(globalPos);
#endif
}
void Renderer::forwardWheelToSystem(const QPoint &globalPos, const QPoint &angleDelta, const QPoint &pixelDelta) {
#if defined(Q_OS_MACOS)
    CGEventRef ev = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 2, angleDelta.y()/120, angleDelta.x()/120);
    if (ev) { CGEventPost(kCGHIDEventTap, ev); CFRelease(ev); }
#else
    Q_UNUSED(globalPos);
    Q_UNUSED(angleDelta);
    Q_UNUSED(pixelDelta);
#endif
}

void Renderer::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        m_systemMoveActive = false;
        m_lastGlobalPos = e->globalPosition().toPoint();
        bool opaque = isOpaqueAtGlobal(m_lastGlobalPos);
        bool canSystemPassthrough = false;
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN32)
        canSystemPassthrough = true;
#endif
        m_passthroughActive = (!opaque && canSystemPassthrough); // transparent -> forward to OS
        m_forceMouseOpaqueDuringDrag = opaque;
        if (m_passthroughActive) {
            forwardMousePressToSystem(m_lastGlobalPos);
            e->ignore();
            return;
        }

#if defined(Q_OS_LINUX)
        if (opaque && QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
            if (auto* topLevel = window()) {
                if (auto* wh = topLevel->windowHandle()) {
                    m_forceMouseOpaqueDuringDrag = false;
                    m_systemMoveActive = true;
                    wh->startSystemMove();
                    e->accept();
                    return;
                }
            }
        }
#elif defined(Q_OS_WIN32)
        if (opaque) {
            if (auto* topLevel = window()) {
                if (auto* wh = topLevel->windowHandle()) {
                    m_forceMouseOpaqueDuringDrag = false;
                    if (wh->startSystemMove()) {
                        m_systemMoveActive = true;
                        e->accept();
                        return;
                    }
                }
            }
        }
#endif
    }
}

void Renderer::mouseMoveEvent(QMouseEvent *e) {
    if (m_passthroughActive) {
        forwardMouseMoveToSystem(e->globalPosition().toPoint());
        e->ignore();
        return;
    }
    if (e->buttons() & Qt::LeftButton) {
        if (m_systemMoveActive) {
            e->accept();
            updateMouseTransparent();
            return;
        }
#if defined(Q_OS_LINUX)
        if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
            e->accept();
            updateMouseTransparent();
            return;
        }
#endif
        auto* w = window();
        QPoint current = e->globalPosition().toPoint();
        QPoint delta = current - m_lastGlobalPos;
        w->move(w->pos() + delta);
        m_lastGlobalPos = current;
    }
    updateMouseTransparent();
}

void Renderer::mouseReleaseEvent(QMouseEvent *e) {
    if (m_passthroughActive) {
        forwardMouseReleaseToSystem(e->globalPosition().toPoint());
        m_passthroughActive = false;
        e->ignore();
        return;
    }
    m_systemMoveActive = false;
    m_forceMouseOpaqueDuringDrag = false;
    updateMouseTransparent();
}

void Renderer::wheelEvent(QWheelEvent *e) {
    if (m_passthroughActive) {
        forwardWheelToSystem(e->globalPosition().toPoint(), e->angleDelta(), e->pixelDelta());
        e->ignore();
        return;
    }
    // Resize window with wheel to adjust size; fit mapping keeps model fully visible
    auto* tl = window();
    QSize cur = tl->size();
    int delta = e->angleDelta().y();
    float factor = delta > 0 ? 1.1f : 0.9f;
    int newW = qMax(120, (int)std::round(cur.width() * factor));
    int newH = qMax(240, (int)std::round(cur.height() * factor));
    tl->resize(newW, newH);
    e->accept();
    updateMouseTransparent();
}

void Renderer::updateInputMask() {
    // 已弃用：不再设置窗口形状，避免裁剪绘制区域
}

void Renderer::scheduleMaskUpdate() {
    // 已弃用
}

void Renderer::updateMouseTransparent() {
    // 暂停穿透：有弹出菜单/模态窗口时避免影响点击
    if (QApplication::activePopupWidget() || QApplication::activeModalWidget()) {
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        if (auto* w = window()) {
            if (auto* wh = w->windowHandle()) {
                wh->setFlag(Qt::WindowTransparentForInput, false);
            }
        }
        return;
    }

#if defined(Q_OS_LINUX)
    if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
        const QByteArray passthroughEnv = qgetenv("AMAIGIRL_WAYLAND_PASSTHROUGH").trimmed().toLower();
        const bool enableWaylandPassthrough =
            (passthroughEnv == QByteArrayLiteral("1") ||
             passthroughEnv == QByteArrayLiteral("true") ||
             passthroughEnv == QByteArrayLiteral("yes") ||
             passthroughEnv == QByteArrayLiteral("on"));
        if (!enableWaylandPassthrough) {
            setAttribute(Qt::WA_TransparentForMouseEvents, false);
            if (auto* w = window()) {
                if (auto* wh = w->windowHandle()) {
                    wh->setFlag(Qt::WindowTransparentForInput, false);
                }
            }
            return;
        }
    }
#endif

    if (m_forceMouseOpaqueDuringDrag) {
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX) || defined(Q_OS_WIN32)
        if (auto* w = window()) {
            if (auto* wh = w->windowHandle()) {
                wh->setFlag(Qt::WindowTransparentForInput, false);
            }
        }
#endif
        return;
    }
    QPoint g = QCursor::pos();
    bool opaque = isOpaqueAtGlobal(g);
    // 1) 确保顶层窗口在透明区域真正穿透
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX) || defined(Q_OS_WIN32)
    if (auto* w = window()) {
        if (auto* wh = w->windowHandle()) {
            wh->setFlag(Qt::WindowTransparentForInput, !opaque);
        }
    }
#endif
    // 2) 作为补充，也让当前小部件在透明区域不吃事件
    setAttribute(Qt::WA_TransparentForMouseEvents, !opaque);
}

// ==== Restored implementations for procedural animation, pose, sync and GL resources ====

void Renderer::setLipSyncValue(float v01)
{
    if (v01 < 0.0f) v01 = 0.0f;
    if (v01 > 1.0f) v01 = 1.0f;
    m_lipSync01.store(v01, std::memory_order_relaxed);
}

void Renderer::setLipSyncActive(bool active)
{
    m_lipSyncActive.store(active, std::memory_order_relaxed);
    if (!active)
    {
        // 结束时把缓存值也归零，避免下一帧读到旧值。
        m_lipSync01.store(0.0f, std::memory_order_relaxed);
    }
}

void Renderer::setupParamIndices()
{
    if (!m_model) return;
    int32_t pc = (int32_t)csmGetParameterCount(m_model->moc.model);
    const char** pids = csmGetParameterIds(m_model->moc.model);

    auto findParam = [&](const char* name)->int{
        for (int i=0;i<pc;++i) if (QString::fromUtf8(pids[i]) == QLatin1String(name)) return i;
        return -1;
    };

    // 常见参数名（Cubism 官方规范）
    m_idxEyeLOpen   = findParam("ParamEyeLOpen");
    m_idxEyeROpen   = findParam("ParamEyeROpen");
    m_idxBreath     = findParam("ParamBreath");
    m_idxBodyAngleY = findParam("ParamBodyAngleY");
    m_idxAngleX     = findParam("ParamAngleX");

    // 额外：头部/身体角度 + 眼球转动
    m_idxAngleY     = findParam("ParamAngleY");
    m_idxAngleZ     = findParam("ParamAngleZ");
    m_idxBodyAngleX = findParam("ParamBodyAngleX");
    m_idxBodyAngleZ = findParam("ParamBodyAngleZ");
    m_idxEyeBallX   = findParam("ParamEyeBallX");
    m_idxEyeBallY   = findParam("ParamEyeBallY");

    // mouth open (lip sync)
    // Prefer Cubism standard ParamMouthOpenY, then fall back to other commonly-seen ids.
    m_idxMouthOpen = findParam("ParamMouthOpenY");
    if (m_idxMouthOpen < 0) m_idxMouthOpen = findParam("PARAM_MOUTH_OPEN_Y");
    if (m_idxMouthOpen < 0) m_idxMouthOpen = findParam("ParamMouthOpen");
    if (m_idxMouthOpen < 0) m_idxMouthOpen = findParam("ParamMouthOpenX");
    if (m_idxMouthOpen < 0)
    {
        // Last resort: pick first parameter id that looks like mouth open.
        // This is intentionally conservative: only accept ids that contain "MouthOpen".
        for (int i = 0; i < pc; ++i)
        {
            const QString id = QString::fromUtf8(pids[i]);
            if (id.contains(QStringLiteral("MouthOpen"), Qt::CaseInsensitive))
            {
                m_idxMouthOpen = i;
                break;
            }
        }
    }

    // 初始化高斯分布的眨眼间隔
    {
        static thread_local std::mt19937 gen(std::random_device{}());
        std::normal_distribution<double> nd(3.5, 0.8);
        double v = nd(gen);
        if (v < 1.5) v = 1.5;
        if (v > 6.0) v = 6.0;
        m_nextBlinkIn = v;
        m_blinkTimer = 0.0;
        m_blinkPhase = BlinkIdle;
        m_blinkPhaseTime = 0.0;
        m_glintTimer = 0.0;
        m_glintDuration = 0.0;
    }
    // reset gaze smoothing
    m_gazeX = 0.0f; m_gazeY = 0.0f;
}

void Renderer::updateProcedural(double dt) {
    if (!m_model) return;
    int32_t pc = (int32_t)csmGetParameterCount(m_model->moc.model);
    float* params = const_cast<float*>(csmGetParameterValues(m_model->moc.model));
    const float* minVals = csmGetParameterMinimumValues(m_model->moc.model);
    const float* maxVals = csmGetParameterMaximumValues(m_model->moc.model);
    const float* defVals = csmGetParameterDefaultValues(m_model->moc.model);

    auto clampTo = [&](int idx, float v){ if (idx>=0) params[idx] = clampf(v, minVals[idx], maxVals[idx]); };
    auto setToDefault = [&](int idx){ if (idx>=0) params[idx] = clampf(defVals[idx], minVals[idx], maxVals[idx]); };

    // 第一阶段：先处理眨眼（它直接写 ParamEyeLOpen/ParamEyeROpen）
    if (m_enableBlink)  {
        applyBlink(dt, params, minVals, maxVals, pc);
    } else {
        setToDefault(m_idxEyeLOpen);
        setToDefault(m_idxEyeROpen);
        m_blinkPhase = BlinkIdle;
        m_blinkPhaseTime = 0.0;
        m_blinkTimer = 0.0;
        m_glintTimer = 0.0;
        m_glintDuration = 0.0;
    }

    // 第二阶段：计算视线追踪输出（但不立刻写参数），或眼球微动输出
    m_gazeOut = {}; // 清空本帧 gaze 输出
    if (m_enableGaze) {
        applyGaze(dt, params, minVals, maxVals); // 该函数内部仅更新 m_gazeOut 与 m_eyeOutX/Y
    } else if (m_enableBlink) {
        applyEyeBallWander(dt, params, minVals, maxVals, pc); // 直接对 eyeball 写入（微动）
        m_gazeAccum.valid = false;
        if (!m_enableBreath) {
            auto relaxAngle = [&](int idx){ if (idx>=0) { float v = params[idx]; float base = defVals[idx]; float k = 1.0f - std::exp(-dt * 6.0f); params[idx] = clampf(v + (base - v)*k, minVals[idx], maxVals[idx]); } };
            relaxAngle(m_idxAngleX);
            relaxAngle(m_idxAngleY);
            relaxAngle(m_idxAngleZ);
            relaxAngle(m_idxBodyAngleY);
            relaxAngle(m_idxBodyAngleX);
            relaxAngle(m_idxBodyAngleZ);
        }
    } else {
        setToDefault(m_idxEyeBallX);
        setToDefault(m_idxEyeBallY);
        m_gazeAccum.valid = false;
        if (!m_enableBreath) {
            auto relaxAngle = [&](int idx){ if (idx>=0) { float v = params[idx]; float base = defVals[idx]; float k = 1.0f - std::exp(-dt * 6.0f); params[idx] = clampf(v + (base - v)*k, minVals[idx], maxVals[idx]); } };
            relaxAngle(m_idxAngleX);
            relaxAngle(m_idxAngleY);
            relaxAngle(m_idxAngleZ);
            relaxAngle(m_idxBodyAngleY);
            relaxAngle(m_idxBodyAngleX);
            relaxAngle(m_idxBodyAngleZ);
        }
    }

    // 第三阶段：计算呼吸输出（但不立刻写参数）
    m_breathOut = {};
    if (m_enableBreath) {
        applyBreath(dt, params, minVals, maxVals, pc); // 该函数内部仅更新 m_breathOut（后续合成）
    } else {
        // 关闭时回到默认值由合成阶段处理，这里只重置内部状态
        m_breathTimer = 0.0;
        m_breathAccumValid = false;
    }

    // 第四阶段：合成（Absolute Composition）
    // 以默认值为基线，叠加 gazeOut 和 breathOut 的目标值，最后再写入参数，避免历史增量残留导致一侧卡住
    auto composeAngle = [&](int idx, float gazeV, float breathV){
        if (idx < 0) return;
        float v = defVals[idx];
        if (m_enableGaze && m_gazeOut.valid) v += gazeV;
        if (m_enableBreath && m_breathOut.valid) v += breathV;
        clampTo(idx, v);
    };

    // 眼球：若启用了 gaze，eyeOutX/Y 已由 applyGaze 写入，直接覆盖到默认之上
    if (m_enableGaze && m_gazeOut.valid) {
        clampTo(m_idxEyeBallX, m_eyeOutX);
        clampTo(m_idxEyeBallY, m_eyeOutY);
    }

    // 当启用视线跟踪时，自动呼吸不再驱动头部，只驱动身体
    float b_ax = (m_enableGaze ? 0.0f : (m_breathOut.valid ? m_breathOut.ax : 0.0f));
    float b_ay = (m_enableGaze ? 0.0f : (m_breathOut.valid ? m_breathOut.ay : 0.0f));
    float b_az = (m_enableGaze ? 0.0f : (m_breathOut.valid ? m_breathOut.az : 0.0f));

    composeAngle(m_idxAngleX,     m_gazeOut.ax,  b_ax);
    composeAngle(m_idxAngleY,     m_gazeOut.ay,  b_ay);
    composeAngle(m_idxAngleZ,     m_gazeOut.az,  b_az);
    composeAngle(m_idxBodyAngleX, m_gazeOut.bodyX, m_breathOut.valid ? m_breathOut.bodyX : 0.0f);
    composeAngle(m_idxBodyAngleY, m_gazeOut.bodyY, m_breathOut.valid ? m_breathOut.bodyY : 0.0f);
    composeAngle(m_idxBodyAngleZ, m_gazeOut.bodyZ, m_breathOut.valid ? m_breathOut.bodyZ : 0.0f);

    if (m_idxBreath >= 0) {
        float base = defVals[m_idxBreath];
        float v = base;
        if (m_enableBreath && m_breathOut.valid) v += m_breathOut.breath;
        clampTo(m_idxBreath, v);
    }

    // physics
    if (m_enablePhysics && m_physics && m_physics->isValid()) {
        if (m_physicsNeedsStabilize) {
            m_physics->stabilize(m_model); // 仅首帧：在 Evaluate 之前稳定
            m_physicsNeedsStabilize = false;
        }
        m_physics->update(dt, m_model);
    } else if (m_physics && !m_enablePhysics) {
        m_physics->reset(m_model);
    }

    // 在 Core 更新前，最后一步叠加“去水印”表达式到当前帧的基线参数上（不产生累积）
    bool appliedExpr = false;
    std::vector<float> savedParams;
    std::vector<int> exprTouched;
    // 关键：记录本帧口型参数值，避免后续表达式回滚把口型一起擦掉，导致“播放语音但不张嘴”。
    float mouthValueBeforeExpr = 0.0f;
    const int mouthIndex = m_idxMouthOpen;
    if (mouthIndex >= 0)
    {
        const float* curVals = csmGetParameterValues(m_model->moc.model);
        mouthValueBeforeExpr = curVals[mouthIndex];
    }
    if (m_wmExpr.has_value()) {
        int pc2 = (int)csmGetParameterCount(m_model->moc.model);
        const float* cur = csmGetParameterValues(m_model->moc.model);
        savedParams.assign(cur, cur + pc2);

        // 让表达式在当前帧生效：
        // 表达式的实现会基于 base 计算并写回到 model 参数。
        std::vector<float> base = savedParams;
        m_wmExpr->apply(m_model.get(), m_exprWeight, base.data());

        // 记录哪些参数被表达式改动了，便于后续只回滚这些参数。
        const float* after = csmGetParameterValues(m_model->moc.model);
        exprTouched.reserve(pc2);
        for (int i = 0; i < pc2; ++i) {
            if (std::fabs(after[i] - savedParams[i]) > 1e-6f) {
                exprTouched.push_back(i);
            }
        }
        appliedExpr = true;
    }

    // pose 与 Core 更新
    applyPose((float)dt);
    csmResetDrawableDynamicFlags(m_model->moc.model);
    csmUpdateModel(m_model->moc.model);
    syncModelDrawables();

    // 恢复到表达式前的基线，避免残留到下一帧。
    // 注意：只能回滚“表达式改动的参数”，否则会把本帧的口型/视线/物理等也一起擦掉。
    if (appliedExpr) {
        float* vals = const_cast<float*>(csmGetParameterValues(m_model->moc.model));
        for (int idx : exprTouched) {
            if (idx >= 0 && idx < (int)savedParams.size()) {
                vals[idx] = savedParams[idx];
            }
        }
        // 额外：口型属于“非表达式”的本帧输出，应保持本帧值。
        if (mouthIndex >= 0)
        {
            vals[mouthIndex] = mouthValueBeforeExpr;
        }
    }

    // ---- Lip sync override (must run every frame, independent of gaze/breath) ----
    // 重要：csmUpdateModel() 可能会导致参数缓冲发生变化，因此在这里重新获取参数指针。
    params  = const_cast<float*>(csmGetParameterValues(m_model->moc.model));
    minVals = csmGetParameterMinimumValues(m_model->moc.model);
    maxVals = csmGetParameterMaximumValues(m_model->moc.model);
    defVals = csmGetParameterDefaultValues(m_model->moc.model);

    // 放在 updateProcedural() 最末尾，确保不会被物理/pose/表达式等覆盖。
    if (m_idxMouthOpen >= 0)
    {
        const float v01 = m_lipSync01.load(std::memory_order_relaxed);
        const bool active = m_lipSyncActive.load(std::memory_order_relaxed);

        if (active)
        {
            const float minV = minVals[m_idxMouthOpen];
            const float maxV = maxVals[m_idxMouthOpen];
            const float target01 = std::clamp(v01, 0.0f, 1.0f);
            const float target = minV + (maxV - minV) * target01;
            params[m_idxMouthOpen] = clampf(target, minV, maxV);
        }
        else
        {
            // 非播放期：立即闭嘴。
            params[m_idxMouthOpen] = clampf(defVals[m_idxMouthOpen], minVals[m_idxMouthOpen], maxVals[m_idxMouthOpen]);
        }
    }
}

void Renderer::applyBlink(double dt, float* params, const float* minVals, const float* maxVals, int /*paramCount*/) {
    m_blinkTimer += dt;

    if (m_blinkPhase == BlinkIdle) {
        if (m_blinkTimer >= m_nextBlinkIn) {
            m_blinkPhase = BlinkClosing;
            m_blinkPhaseTime = 0.0;
        }
    }

    float eye = 1.0f; // 1 张开, 0 闭合
    if (m_blinkPhase == BlinkClosing) {
        m_blinkPhaseTime += dt;
        double t = std::min(m_blinkPhaseTime / 0.06, 1.0);
        eye = (float)(1.0 - t);
        if (t >= 1.0) { m_blinkPhase = BlinkClosedHold; m_blinkPhaseTime = 0.0; }
    } else if (m_blinkPhase == BlinkClosedHold) {
        m_blinkPhaseTime += dt;
        eye = 0.0f;
        if (m_blinkPhaseTime >= 0.05) { m_blinkPhase = BlinkOpening; m_blinkPhaseTime = 0.0; }
    } else if (m_blinkPhase == BlinkOpening) {
        m_blinkPhaseTime += dt;
        double t = std::min(m_blinkPhaseTime / 0.08, 1.0);
        eye = (float)t;
        if (t >= 1.0) {
            m_blinkPhase = BlinkIdle;
            m_blinkPhaseTime = 0.0;
            m_blinkTimer = 0.0;

            static thread_local std::mt19937 gen(std::random_device{}());
            std::uniform_real_distribution<double> u01(0.0, 1.0);

            // 瞬目：可能发生双连眨
            double pDoubleBlink = 0.25;
            if (u01(gen) < pDoubleBlink) {
                std::uniform_real_distribution<double> shortGap(0.08, 0.20);
                m_nextBlinkIn = shortGap(gen);
                m_glintDuration = 0.12; // 120ms 小闪烁窗口
            } else {
                std::normal_distribution<double> nd(3.5, 0.8);
                double v = nd(gen);
                if (v < 1.5) v = 1.5;
                if (v > 6.0) v = 6.0;
                m_nextBlinkIn = v;
                m_glintDuration = 0.06; // 普通眨眼后小“bling”
            }
            m_glintTimer = 0.0;
        }
    }

    auto clampParam = [&](int idx, float v){ if (idx>=0) params[idx] = clampf(v, minVals[idx], maxVals[idx]); };

    if (m_idxEyeLOpen >= 0 || m_idxEyeROpen >= 0) {
        clampParam(m_idxEyeLOpen, eye);
        clampParam(m_idxEyeROpen, eye);
    }
}

void Renderer::applyBreath(double dt, float* params, const float* minVals, const float* maxVals, int /*paramCount*/) {
    m_breathTimer += dt;

    auto sinPhase = [&](float period, float phase)->float{
        if (period <= 0.0001f) return 0.0f;
        float t = std::fmod((float)m_breathTimer, period) / period; // [0,1)
        return std::sin((t * 2.0f * (float)M_PI) + phase); // [-1,1]
    };

    // 调低头部相关幅度，让自动呼吸更柔和
    float ax   = 5.5f  * sinPhase(6.5345f, 0.0f);
    float ay   = 3.2f  * sinPhase(3.5345f, 0.35f * (float)M_PI);
    float az   = 4.0f  * sinPhase(5.5345f, -0.25f * (float)M_PI);
    float bdx  = 3.2f  * sinPhase(15.5345f, 0.5f * (float)M_PI);
    float bdy  = 0.8f  * sinPhase(12.0f,  0.0f);
    float bdz  = 0.6f  * sinPhase(10.0f,  0.0f);
    float bre  = 0.45f * sinPhase(3.2345f, 0.0f);

    // 细微抖动也同步减弱
    float microAx   = 0.6f * sinPhase(8.0f,   0.2f * (float)M_PI);
    float microAy   = 0.4f * sinPhase(6.5f,  -0.15f * (float)M_PI);
    float microAz   = 0.5f * sinPhase(9.5f,   0.35f * (float)M_PI);

    ax += microAx; ay += microAy; az += microAz;

    // 平滑到输出，避免与 gaze 输出叠加时产生抖动
    const float smooth = 1.0f - std::exp(-std::max(0.0, dt) * 6.0f);
    if (!m_breathOut.valid) {
        m_breathOut = {ax, ay, az, bdx, bdy, bdz, bre, true};
    } else {
        m_breathOut.ax    = m_breathOut.ax    + (ax  - m_breathOut.ax)    * smooth;
        m_breathOut.ay    = m_breathOut.ay    + (ay  - m_breathOut.ay)    * smooth;
        m_breathOut.az    = m_breathOut.az    + (az  - m_breathOut.az)    * smooth;
        m_breathOut.bodyX = m_breathOut.bodyX + (bdx - m_breathOut.bodyX) * smooth;
        m_breathOut.bodyY = m_breathOut.bodyY + (bdy - m_breathOut.bodyY) * smooth;
        m_breathOut.bodyZ = m_breathOut.bodyZ + (bdz - m_breathOut.bodyZ) * smooth;
        m_breathOut.breath= m_breathOut.breath+ (bre - m_breathOut.breath)* smooth;
        m_breathOut.valid = true;
    }
}

void Renderer::applyGaze(double dt, float* params, const float* minVals, const float* maxVals) {
    QPoint gpos = QCursor::pos();
    QPoint wpos = mapFromGlobal(gpos);
    int w = std::max(1, width());
    int h = std::max(1, height());

    float cx = (m_minB.x + m_maxB.x) * 0.5f;
    float cy = (m_minB.y + m_maxB.y) * 0.5f;
    float centerPxX = cx * m_fitScale + m_fitOffsetPx.x;
    float centerPxY = -cy * m_fitScale + m_fitOffsetPx.y;

    float dxWin = (float)wpos.x() - centerPxX;
    float dyWin = centerPxY - (float)wpos.y();
    float halfRef = 0.35f * std::min(w, h);
    float dx = std::clamp(dxWin / halfRef, -1.0f, 1.0f);
    float dy = std::clamp(dyWin / halfRef, -1.0f, 1.0f);

    float d = std::sqrt(dxWin*dxWin + dyWin*dyWin);
    float r = 0.55f * std::min(w, h);
    float att = 1.0f / (1.0f + (d/r)*(d/r));
    att = std::clamp(att, 0.55f, 1.0f);

    float k = 1.0f - std::exp(-dt * 14.0f);
    m_gazeX = m_gazeX + (dx - m_gazeX) * k;
    m_gazeY = m_gazeY + (dy - m_gazeY) * k;

    // Eyeball movement output
    m_eyeOutX = std::clamp(m_gazeX * 1.15f * att, -1.0f, 1.0f);
    m_eyeOutY = std::clamp(m_gazeY * 1.10f * att, -1.0f, 1.0f);

    const float* defVals = csmGetParameterDefaultValues(m_model->moc.model);
    auto capPos = [&](int idx){ return idx>=0 ? std::max(1e-4f, maxVals[idx] - defVals[idx]) : 0.0f; };
    auto capNeg = [&](int idx){ return idx>=0 ? std::max(1e-4f, defVals[idx] - minVals[idx]) : 0.0f; };

    float effPosX = 0.0f, effNegX = 0.0f;
    effPosX += 26.0f * capPos(m_idxAngleX);
    effNegX += 26.0f * capNeg(m_idxAngleX);
    effPosX += 11.0f * capPos(m_idxAngleZ);
    effNegX += 11.0f * capNeg(m_idxAngleZ);
    effPosX += 16.0f * capPos(m_idxBodyAngleY);
    effNegX += 16.0f * capNeg(m_idxBodyAngleY);

    float gxPos = 1.0f, gxNeg = 1.0f;
    if (effPosX > 0.0f && effNegX > 0.0f) {
        if (effPosX > effNegX) { gxPos = std::clamp(effNegX / effPosX, 0.4f, 1.0f); gxNeg = 1.0f; }
        else                   { gxNeg = std::clamp(effPosX / effNegX, 0.4f, 1.0f); gxPos = 1.0f; }
    }

    float effPosY = 0.0f, effNegY = 0.0f;
    effPosY += 30.0f * capPos(m_idxAngleY);
    effNegY += 30.0f * capNeg(m_idxAngleY);
    effPosY += 12.0f * capPos(m_idxBodyAngleX);
    effNegY += 12.0f * capNeg(m_idxBodyAngleX);

    float gyPos = 1.0f, gyNeg = 1.0f;
    if (effPosY > 0.0f && effNegY > 0.0f) {
        if (effPosY > effNegY) { gyPos = std::clamp(effNegY / effPosY, 0.4f, 1.0f); gyNeg = 1.0f; }
        else                   { gyNeg = std::clamp(effPosY / effNegY, 0.4f, 1.0f); gyPos = 1.0f; }
    }

    float xForHead = m_gazeX * ((m_gazeX >= 0.0f) ? gxPos : gxNeg);
    float yForHead = m_gazeY * ((m_gazeY >= 0.0f) ? gyPos : gyNeg);

    float tgtAX = (xForHead * 26.0f) * att;    // AngleX (yaw)
    float tgtAY = (yForHead * 30.0f) * att;    // AngleY (pitch)
    float tgtAZ = (xForHead * -11.0f) * att;   // AngleZ (roll)
    float tgtBY = (xForHead * 16.0f) * att;    // BodyAngleY (torso yaw)
    float tgtBX = (yForHead * 12.0f) * att;    // BodyAngleX (torso pitch)
    float tgtBZ = (xForHead * 8.0f)  * att;    // BodyAngleZ (torso roll)

    // 平滑至 gaze 输出（与呼吸输出一致的时间常数）
    const float smooth = 1.0f - std::exp(-std::max(0.0, dt) * 10.0f);
    if (!m_gazeOut.valid) {
        m_gazeOut = {tgtBX, tgtBY, tgtBZ, tgtBX, tgtBY, tgtBZ}; // 占位，随即覆盖
        m_gazeOut.ax = tgtAX; m_gazeOut.ay = tgtAY; m_gazeOut.az = tgtAZ;
        m_gazeOut.bodyX = tgtBX; m_gazeOut.bodyY = tgtBY; m_gazeOut.bodyZ = tgtBZ;
        m_gazeOut.valid = true;
    } else {
        m_gazeOut.ax    = m_gazeOut.ax    + (tgtAX - m_gazeOut.ax)    * smooth;
        m_gazeOut.ay    = m_gazeOut.ay    + (tgtAY - m_gazeOut.ay)    * smooth;
        m_gazeOut.az    = m_gazeOut.az    + (tgtAZ - m_gazeOut.az)    * smooth;
        m_gazeOut.bodyY = m_gazeOut.bodyY + (tgtBY - m_gazeOut.bodyY) * smooth;
        m_gazeOut.bodyX = m_gazeOut.bodyX + (tgtBX - m_gazeOut.bodyX) * smooth;
        m_gazeOut.bodyZ = m_gazeOut.bodyZ + (tgtBZ - m_gazeOut.bodyZ) * smooth;
        m_gazeOut.valid = true;
    }
}

void Renderer::applyEyeBallWander(double dt, float* params, const float* minVals, const float* maxVals, int /*paramCount*/) {
    static thread_local std::mt19937 gen(std::random_device{}());
    static float targetX = 0.0f, targetY = 0.0f;
    static float curX = 0.0f, curY = 0.0f;
    static double wanderT = 0.0;

    wanderT -= dt;
    if (wanderT <= 0.0) {
        std::normal_distribution<float> nd(0.0f, 0.12f);
        targetX = std::clamp(nd(gen), -0.6f, 0.6f);
        targetY = std::clamp(nd(gen), -0.4f, 0.4f);
        std::uniform_real_distribution<double> gap(0.6, 1.6);
        wanderT = gap(gen);
    }

    if (m_glintDuration > 0.0) {
        m_glintTimer += dt;
        float amp = 0.25f;
        curX = curX * 0.7f + (targetX + amp) * 0.3f;
        curY = curY * 0.7f + (targetY - amp*0.4f) * 0.3f;
        if (m_glintTimer >= m_glintDuration) { m_glintDuration = 0.0; m_glintTimer = 0.0; }
    } else {
        curX = curX * 0.9f + targetX * 0.1f;
        curY = curY * 0.9f + targetY * 0.1f;
    }

    auto clampParam = [&](int idx, float v){ if (idx>=0) params[idx] = clampf(v, minVals[idx], maxVals[idx]); };
    if (m_idxEyeBallX >= 0) clampParam(m_idxEyeBallX, curX);
    if (m_idxEyeBallY >= 0) clampParam(m_idxEyeBallY, curY);
}

void Renderer::resetGazeParamsToDefault(float* params, const float* minVals, const float* maxVals, const float* defVals) {
    auto relaxTo = [&](int idx){ if (idx>=0) params[idx] = clampf(defVals[idx], minVals[idx], maxVals[idx]); };
    relaxTo(m_idxEyeBallX);
    relaxTo(m_idxEyeBallY);
    relaxTo(m_idxAngleX);
    relaxTo(m_idxAngleY);
    relaxTo(m_idxAngleZ);
    relaxTo(m_idxBodyAngleY);
    relaxTo(m_idxBodyAngleX);
    relaxTo(m_idxBodyAngleZ);

    m_gazeAccum = {};
}

void Renderer::applyPose(float dt) {
    if (!m_model || !m_model->pose.has_value() || !m_model->pose->valid) return;

    float* partOp = csmGetPartOpacities(m_model->moc.model);
    const auto& pose = *m_model->pose;

    if (m_poseState.size() != pose.groups.size()) {
        m_poseState.resize(pose.groups.size());
        for (auto &s : m_poseState) s.lastChosen = -1;
        m_poseJustInitialized = true;
    }

    const float Epsilon = 0.001f;
    const float Phi = 0.5f;
    const float BackOpacityThreshold = 0.15f;

    const float* paramVals = csmGetParameterValues(m_model->moc.model);

    // 若正在进行用户触发的 A/B 切换，则推进淡入计时
    if (m_poseSwitching) {
        m_poseFadeTimer += dt;
        if (m_poseFadeTimer >= m_poseFadeDuration) { m_poseFadeTimer = m_poseFadeDuration; m_poseSwitching = false; }
    }

    for (int gi=0; gi<pose.groups.size(); ++gi) {
        const auto& grp = pose.groups[gi];
        if (grp.entries.isEmpty()) continue;

        // 从参数或不透明度推断当前显示项（用于首次初始化）
        if (m_poseJustInitialized) {
            int visibleIdx = 0;
            for (int i=0;i<grp.entries.size();++i) {
                int pidx = grp.entries[i].parameterIndex;
                if (pidx >= 0 && paramVals[pidx] > Epsilon) { visibleIdx = i; break; }
            }
            // 应用用户 A/B 选择：仅对选定的一个 A/B 组生效
            if (gi == m_userPoseGroupIndex && grp.entries.size() == 2) {
                visibleIdx = std::clamp(m_userPoseAB, 0, 1);
            }
            for (int i=0;i<grp.entries.size();++i) {
                const auto& e = grp.entries[i];
                if (e.partIndex < 0) continue;
                float v = (i==visibleIdx) ? 1.0f : 0.0f;
                partOp[e.partIndex] = v;
                for (int li : e.linkPartIndices) partOp[li] = v;
            }
            m_poseState[gi].lastChosen = visibleIdx;
            continue;
        }

        // 正常帧：决定目标可见项
        int targetVisible = -1;
        // 仅选定 A/B 组使用用户选择；其余组按曲线/不透明度推断
        if (gi == m_userPoseGroupIndex && grp.entries.size() == 2) {
            targetVisible = std::clamp(m_userPoseAB, 0, 1);
        } else {
            float bestParam = 0.0f; int bestIdx = -1;
            for (int i=0;i<grp.entries.size();++i) {
                int pidx = grp.entries[i].parameterIndex;
                if (pidx < 0) continue;
                float v = paramVals[pidx];
                if (v > Epsilon && v >= bestParam) { bestParam = v; bestIdx = i; }
            }
            if (bestIdx < 0) {
                float bestOp = -1.0f; int bestI = -1;
                for (int i=0;i<grp.entries.size();++i) {
                    int pi = grp.entries[i].partIndex; if (pi < 0) continue;
                    float v = partOp[pi]; if (v > bestOp) { bestOp = v; bestI = i; }
                }
                targetVisible = (bestI >= 0) ? bestI : 0;
            } else targetVisible = bestIdx;
        }

        // 计算新不透明度（对目标做淡入，对其余做淡出），淡入时间取 pose.fadeInTime 与用户切换计时共同的力度
        float baseFade = m_model->pose->fadeInTime > 0.0f ? m_model->pose->fadeInTime : 0.3f;
        float usedFade = baseFade;
        if (m_poseSwitching) usedFade = std::max(0.18f, m_poseFadeDuration); // 用户切换优先使用我们定义的时长

        float newOpacity = 1.0f;
        int visPartIdx = grp.entries[targetVisible].partIndex;
        if (visPartIdx >= 0) {
            newOpacity = partOp[visPartIdx] + (usedFade > 0.0f ? (dt / usedFade) : dt);
            if (newOpacity > 1.0f) newOpacity = 1.0f;
        }

        for (int i=0;i<grp.entries.size();++i) {
            const auto& e = grp.entries[i];
            if (e.partIndex < 0) continue;
            if (i == targetVisible) {
                partOp[e.partIndex] = newOpacity;
                for (int li : e.linkPartIndices) partOp[li] = newOpacity;
            } else {
                float opacity = partOp[e.partIndex];
                float a1;
                if (newOpacity < Phi) {
                    a1 = newOpacity * (Phi - 1.0f) / Phi + 1.0f;
                } else {
                    a1 = (1.0f - newOpacity) * Phi / (1.0f - Phi);
                }
                float backOpacity = (1.0f - a1) * (1.0f - newOpacity);
                if (backOpacity > BackOpacityThreshold) {
                    a1 = 1.0f - BackOpacityThreshold / (1.0f - newOpacity);
                }
                if (opacity > a1) opacity = a1;
                partOp[e.partIndex] = opacity;
                for (int li : e.linkPartIndices) partOp[li] = opacity;
            }
        }

        m_poseState[gi].lastChosen = targetVisible;
    }

    if (m_poseJustInitialized) m_poseJustInitialized = false;
}

void Renderer::applyExpressions(float dt) {
    Q_UNUSED(dt);
}

void Renderer::syncModelDrawables() {
    int32_t count = (int32_t)csmGetDrawableCount(m_model->moc.model);
    const uint8_t* dflags = csmGetDrawableDynamicFlags(m_model->moc.model);
    const int32_t* orders = csmGetDrawableRenderOrders(m_model->moc.model);
    const float* opacities = csmGetDrawableOpacities(m_model->moc.model);
    const csmVector2** positions = csmGetDrawableVertexPositions(m_model->moc.model);
    const int32_t* vCounts = csmGetDrawableVertexCounts(m_model->moc.model);

    for (int i = 0; i < count; ++i) {
        auto &d = m_model->drawables[i];
        d.dflag = dflags[i];
        d.order = orders[i];
        d.opacity = opacities[i];
        const int vc = (int)vCounts[i];
        d.pos.resize(vc);
        for (int v = 0; v < vc; ++v) d.pos[v] = { positions[i][v].X, positions[i][v].Y };
    }
}

void Renderer::cleanupModelGL() {
    if (m_model) {
        for (auto &d : m_model->drawables) {
            if (d.texture && d.texture->isBound()) d.texture->release();
            d.texture.reset();
        }
    }
    if (m_gpu.maskTex) {
        glDeleteTextures(1, &m_gpu.maskTex);
        m_gpu.maskTex = 0;
    }
    if (m_gpu.maskFbo) {
        glDeleteFramebuffers(1, &m_gpu.maskFbo);
        m_gpu.maskFbo = 0;
    }
    m_gpu.maskW = 0;
    m_gpu.maskH = 0;
    m_texturesReady = false;
}

void Renderer::setTextureCap(int dim) {
    dim = (dim==1024||dim==2048||dim==3072||dim==4096)?dim:2048;
    if (m_textureCap == dim) return;
    m_textureCap = dim;
    if (!context()) return;
    makeCurrent();
    // 重新构建当前模型纹理
    cleanupModelGL();
    buildModelTextures();
    doneCurrent();
    update();
}

void Renderer::setMsaaSamples(int samples) {
    samples = (samples==2||samples==4||samples==8)?samples:4;
    if (m_msaaSamples == samples) return;
    m_msaaSamples = samples;
    rebuildSurfaceForMsaa();
}

void Renderer::rebuildSurfaceForMsaa() {
    QSurfaceFormat fmt = format();
    if (fmt.samples() == m_msaaSamples) { update(); return; }
    fmt.setSamples(m_msaaSamples);
    setFormat(fmt);
    QSurfaceFormat::setDefaultFormat(fmt);

    if (!context()) { update(); return; }
    makeCurrent();
    cleanupModelGL();
    doneCurrent();

    const bool vis = isVisible();
    if (vis) hide();
    show();
    update();
}

void Renderer::buildModelTextures() {
    if (!m_model) { m_texturesReady = false; return; }
    QVector<QSharedPointer<QOpenGLTexture>> texObjs(m_model->texturesPaths.size());
    for (int i=0;i<texObjs.size();++i) {
        try {
            // honor current texture cap by downscaling image further if needed
            QImage img(m_model->texturesPaths[i]);
            if (img.isNull()) throw std::runtime_error("bad texture");
            const int cap = std::clamp(m_textureCap, 1024, 4096);
            if (img.width() > cap || img.height() > cap) {
                const double s = std::min((double)cap / img.width(), (double)cap / img.height());
                const int nw = std::max(1, (int)std::floor(img.width() * s));
                const int nh = std::max(1, (int)std::floor(img.height() * s));
                img = img.scaled(nw, nh, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            img = img.convertToFormat(QImage::Format_RGBA8888);
            // premultiply
            {
                uchar* bits = img.bits(); int pixels = img.width()*img.height();
                for (int p=0;p<pixels;++p) { uchar* b = bits + p*4; float a=b[3]/255.0f; b[0]=(uchar)std::round(b[0]*a); b[1]=(uchar)std::round(b[1]*a); b[2]=(uchar)std::round(b[2]*a); }
            }
            auto tex = QSharedPointer<QOpenGLTexture>::create(QOpenGLTexture::Target2D);
            tex->setFormat(QOpenGLTexture::RGBA8_UNorm);
            tex->setSize(img.width(), img.height());
            bool useMips = (img.width()>=1024 || img.height()>=1024);
            tex->setAutoMipMapGenerationEnabled(useMips);
            tex->allocateStorage();
            tex->setMinificationFilter(useMips?QOpenGLTexture::LinearMipMapLinear:QOpenGLTexture::Linear);
            tex->setMagnificationFilter(QOpenGLTexture::Linear);
            tex->setWrapMode(QOpenGLTexture::ClampToEdge);
            tex->bind(0); tex->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, img.constBits()); if (useMips) tex->generateMipMaps(); tex->release();
            tex->setMaximumAnisotropy(useMips?4.0f:8.0f);
            texObjs[i] = tex;
        } catch (...) { texObjs[i].reset(); }
    }
    for (auto &d : m_model->drawables) {
        int ti = d.textureIndex;
        if (ti >= 0 && ti < texObjs.size()) d.texture = texObjs[ti]; else d.texture.reset();
    }
    m_texturesReady = true;
}

void Renderer::contextMenuEvent(QContextMenuEvent *e) {
    Q_UNUSED(e);
    cycleUserPoseAB();
}

void Renderer::cycleUserPoseAB() {
    if (!m_model || !m_model->pose.has_value() || !m_model->pose->valid) return;
    if (m_userPoseGroupIndex < 0 || m_userPoseGroupIndex >= m_model->pose->groups.size()) return;
    if (m_model->pose->groups[m_userPoseGroupIndex].entries.size() != 2) return;
    m_userPoseAB = (m_userPoseAB==0?1:0);
    SettingsManager::instance().setPoseAB(m_userPoseAB);
    m_poseFadeTimer = 0.0f; m_poseSwitching = true;
}

void Renderer::applyUserPoseOverride(float dt) {
    Q_UNUSED(dt);
    // Logic integrated directly in applyPose(); kept as stub for future extension.
}
