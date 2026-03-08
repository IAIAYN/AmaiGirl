#pragma once

#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QElapsedTimer>
#include <QPoint>
#include <QTimer>
#include <optional>
#include <atomic>

#include "engine/Expression.hpp"
#include "engine/Model.hpp"
#include "engine/Motion.hpp"
#include "engine/PhysicsEngine.hpp"

class Renderer : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit Renderer(QWidget* parent=nullptr);
    ~Renderer() override; // ensure GL cleanup
    void load(const QString& modelJson);
    // toggles
    void setEnableBlink(bool v) { m_enableBlink = v; }
    void setEnableBreath(bool v); // 改为带复位逻辑的声明
    void setEnableGaze(bool v);
    void setEnablePhysics(bool v);

    // 根据当前已加载模型，在目标高度下建议窗口宽度（包含留白）。
    int suggestWidthForHeight(int targetHeightPx) const;

public Q_SLOTS:
    // 设置或清除“去水印”表达式路径（绝对路径），空字符串表示清除
    void setWatermarkExpression(const QString& expPath);
    // 热更新：贴图上限与 MSAA
    void setTextureCap(int dim);
    void setMsaaSamples(int samples);

    // 外部口型同步（0..1），用于 TTS 播放期间驱动嘴巴开合。
    void setLipSyncValue(float v01);

    // 口型同步是否处于“播放期”。播放期内即使某些帧 rms=0 也应继续保持口型驱动链路工作。
    void setLipSyncActive(bool active);

Q_SIGNALS:
    // 通知上层：给定当前窗口高度，建议将窗口宽度调整为 suggestedWidth
    void requestFitWidthForHeight(int currentHeight, int suggestedWidth);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override; // new: for passthrough sequence
    void wheelEvent(QWheelEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override; // use right-click to cycle pose

private:
    struct GPU {
        QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
        QOpenGLBuffer ebo{QOpenGLBuffer::IndexBuffer};
        QOpenGLShaderProgram prog;
        int loc_pos{-1}, loc_uv{-1};
        int uni_tex{-1}, uni_maskTex{-1}, uni_opacity{-1}, uni_maskWrite{-1};
        int uni_useMask{-1}, uni_invertMask{-1}, uni_invViewport{-1};
        int uni_maskThreshold{-1};
        GLuint maskFbo{0};
        GLuint maskTex{0};
        int maskW{0};
        int maskH{0};
        bool inited{false};
    } m_gpu;

    QSharedPointer<Model> m_model;
    std::unique_ptr<MotionPlayer> m_player;
    std::unique_ptr<PhysicsEngine> m_physics;

    // 单一水印表达式（用户选择）
    QString m_wmPath;
    std::optional<Expression> m_wmExpr;
    float m_exprWeight{1.0f};

    QTimer m_timer;
    QElapsedTimer m_elapsed;

    // canvas info (units)
    Vec2 m_canvasSize{1,1};
    Vec2 m_canvasOrigin{0,0};

    // model bounds in canvas space
    bool m_haveBounds{false};
    Vec2 m_minB{0,0}, m_maxB{0,0};

    // derived pixel mapping
    float m_fitScale{1.0f}; // scale to fit entire model into the widget
    Vec2 m_fitOffsetPx{0,0}; // to center model
    int m_framebufferW{1};
    int m_framebufferH{1};

    // controls
    Vec2 m_originOffset{0,0};
    QPoint m_lastGlobalPos;
    QStringList m_motionGroups;
    int m_motionIndex{0};
    bool m_keyW{false}, m_keyA{false}, m_keyS{false}, m_keyD{false};

    // Procedural animation (blink & breath & eyeball & gaze)
    int m_idxEyeLOpen{-1};
    int m_idxEyeROpen{-1};
    int m_idxBreath{-1};
    int m_idxBodyAngleY{-1};
    int m_idxAngleX{-1};

    int m_idxAngleY{-1};
    int m_idxAngleZ{-1};
    int m_idxBodyAngleX{-1};
    int m_idxBodyAngleZ{-1};
    int m_idxEyeBallX{-1};
    int m_idxEyeBallY{-1};

    double m_blinkTimer{0.0};
    double m_nextBlinkIn{2.0};
    enum BlinkPhase { BlinkIdle, BlinkClosing, BlinkClosedHold, BlinkOpening } m_blinkPhase{BlinkIdle};
    double m_blinkPhaseTime{0.0};
    // post-blink glint timer
    double m_glintTimer{0.0};
    double m_glintDuration{0.0};

    // 呼吸：计时器 + 输出（绝对合成，不直接写参数）
    double m_breathTimer{0.0};
    struct BreathOut { float ax{0}, ay{0}, az{0}, bodyX{0}, bodyY{0}, bodyZ{0}, breath{0}; bool valid{false}; } m_breathOut;
    bool m_breathAccumValid{false}; // 兼容旧逻辑，后续不再使用增量

    // 视线：输入平滑 + 输出（绝对合成，不直接写参数）
    bool m_enableBlink{true};
    bool m_enableBreath{true};
    bool m_enableGaze{true};
    bool m_enablePhysics{true};
    bool m_prevEnableGaze{true};
    bool m_prevEnableBreath{true};

    // 布局留白比率（相对于窗口尺寸，四边各留该比例的边距）
    float m_marginRatio{0.06f};

    // pose runtime state
    struct PoseGroupState { int lastChosen{-1}; };
    QVector<PoseGroupState> m_poseState;
    bool m_poseJustInitialized{false};

    // physics stabilization first-frame flag
    bool m_physicsNeedsStabilize{false};

    // gaze smoothing state
    float m_gazeX{0.0f};
    float m_gazeY{0.0f};
    struct GazeAccum { float ax{0}, ay{0}, az{0}, bodyY{0}, bodyX{0}, bodyZ{0}; bool valid{false}; } m_gazeAccum;
    // gaze 输出（绝对合成）+ eyeball 输出
    struct GazeOut { float ax{0}, ay{0}, az{0}, bodyX{0}, bodyY{0}, bodyZ{0}; bool valid{false}; } m_gazeOut;
    float m_eyeOutX{0.0f}, m_eyeOutY{0.0f};
    // cache parameter ids
    QStringList m_paramIds;

    // Click-through handling
    bool m_passthroughActive{false}; // true when current mouse sequence is forwarded to OS
    bool m_systemMoveActive{false};
    bool isOpaqueAtGlobal(const QPoint& globalPos, float alphaThreshold = 0.05f) const;
    void forwardMousePressToSystem(const QPoint& globalPos);
    void forwardMouseMoveToSystem(const QPoint& globalPos);
    void forwardMouseReleaseToSystem(const QPoint& globalPos);
    void forwardWheelToSystem(const QPoint& globalPos, const QPoint& angleDelta, const QPoint& pixelDelta);

    // 输入穿透：通过动态切换 WA_TransparentForMouseEvents 实现，不再改变窗口形状
    void updateMouseTransparent();
    QTimer m_hitTestTimer; // 小周期轮询当前指针位置用于切换透明
    bool m_forceMouseOpaqueDuringDrag{false};

    // 旧的窗口 mask（保留但不再使用）
    void updateInputMask(); // no-op unless explicitly called
    void scheduleMaskUpdate(); // no-op
    QTimer m_maskUpdateTimer; // unused
    bool m_maskDirty{false};

    void recomputeMapping();
    void drawDrawable(const Drawable& d, bool asMask, float overrideOpacity);
    void upload(const Drawable& d, std::vector<float>& verts);
    void computeModelBounds();

    // Helpers for procedural params
    void setupParamIndices();
    void updateProcedural(double dt);
    void applyBlink(double dt, float* params, const float* minVals, const float* maxVals, int paramCount);
    void applyBreath(double dt, float* params, const float* minVals, const float* maxVals, int paramCount);
    void applyEyeBallWander(double dt, float* params, const float* minVals, const float* maxVals, int paramCount);
    void applyGaze(double dt, float* params, const float* minVals, const float* maxVals);
    void resetGazeParamsToDefault(float* params, const float* minVals, const float* maxVals, const float* defVals);
    void syncModelDrawables();

    void applyPose(float dt);
    void applyExpressions(float dt);

    // GL/resource cleanup helpers
    void cleanupModelGL();
    void ensureMaskBuffer(int w, int h);

    // texture build state
    bool m_texturesReady{false};
    void buildModelTextures();

    int m_textureCap{2048};
    int m_msaaSamples{4};
    void rebuildSurfaceForMsaa();

    // Pose A/B selection (persisted). We only support groups with exactly 2 mutually exclusive parts (A/B) for UI switching.
    int m_userPoseAB{0}; // 0 -> first entry, 1 -> second entry
    int m_userPoseGroupIndex{-1}; // only this pose group is controlled by A/B toggle
    float m_poseFadeTimer{0.0f};
    float m_poseFadeDuration{0.3f};
    bool  m_poseSwitching{false};
    void applyUserPoseOverride(float dt);
    void cycleUserPoseAB();

    // Lip-sync state (driven by ChatController during TTS播放)
    std::atomic<float> m_lipSync01{0.0f};
    std::atomic<bool>  m_lipSyncActive{false};
    int m_idxMouthOpen{-1};

};
