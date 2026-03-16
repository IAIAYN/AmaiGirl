#pragma once

#include <QMainWindow>
#include <QScopedPointer>
#include <QString>

class QWidget;
class QEvent;

class SettingsWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit SettingsWindow(QWidget* parent=nullptr);
    ~SettingsWindow() override;

signals:
    void requestLoadModel(const QString& modelJsonPath);
    void requestResetWindow();
    void toggleBlink(bool enabled);
    void toggleBreath(bool enabled);
    void toggleGaze(bool enabled);
    void togglePhysics(bool enabled);
    void watermarkChanged(const QString& expPath);
    void textureCapChanged(int dim);
    void msaaChanged(int samples);

    // Advanced
    void preferredScreenChanged(const QString& screenName);              // empty => default
    void preferredAudioOutputChanged(const QString& deviceIdBase64);     // empty => default

    // 清理
    void requestClearAllChats();

    // AI
    void aiSettingsChanged();
    void requestOpenChat();

    // i18n
    void languageChanged(const QString& languageCode);

    // style/theme scheme
    void themeChanged(const QString& themeId);

protected:
    bool event(QEvent* e) override;

private:
    class Impl; QScopedPointer<Impl> d;
    void refreshModelList();
};
