#pragma once

#include "ai/core/McpServerConfig.hpp"
#include "ui/theme/ThemeWidgets.hpp"

#include <QDialog>
#include <QStringList>

class QDialogButtonBox;
class QFormLayout;
class QEvent;
class QWidget;

class QPushButton;
class QStackedWidget;

class McpServerEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit McpServerEditorDialog(QWidget* parent = nullptr);

    void setServerConfig(const McpServerConfig& config);
    McpServerConfig serverConfig() const;

private slots:
    void onAccept();
    void onTestConnection();

private:
    struct TestConnectionResult
    {
        bool success{false};
        int toolsCount{0};
        QStringList toolNames;
        QString error;
    };

    void onTestConnectionFinished(const TestConnectionResult& result);
    void setTestUiState(bool testing);
    void updateTypeUi();

protected:
    void changeEvent(QEvent* event) override;

    ThemeWidgets::LineEdit* m_nameEdit{nullptr};
    ThemeWidgets::ComboBox* m_typeCombo{nullptr};
    QStackedWidget* m_typeStack{nullptr};
    QFormLayout* m_mainForm{nullptr};
    QFormLayout* m_stdioForm{nullptr};
    QFormLayout* m_httpForm{nullptr};

    ThemeWidgets::LineEdit* m_stdioCommandEdit{nullptr};
    ThemeWidgets::LineEdit* m_stdioArgsEdit{nullptr};
    ThemeWidgets::PlainTextEdit* m_stdioEnvEdit{nullptr};

    ThemeWidgets::LineEdit* m_httpUrlEdit{nullptr};
    ThemeWidgets::PlainTextEdit* m_httpHeadersEdit{nullptr};
    QDialogButtonBox* m_buttonBox{nullptr};
    QPushButton* m_okButton{nullptr};
    QPushButton* m_testButton{nullptr};
    QWidget* m_testBusyContainer{nullptr};
    class TypingDotsWidget* m_testDots{nullptr};
    int m_timeoutMs{8000};
    bool m_testInProgress{false};
};
