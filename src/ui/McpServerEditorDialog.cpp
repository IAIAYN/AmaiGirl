#include "ui/McpServerEditorDialog.hpp"

#include "ai/core/IMcpAdapter.hpp"
#include "ui/theme/ThemeWidgets.hpp"
#include "ui/widgets/TypingDotsWidget.hpp"

#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QApplication>
#include <QStackedWidget>
#include <QThread>
#include <QVBoxLayout>

namespace {
QMap<QString, QString> parseHeaders(const QString& text)
{
    QMap<QString, QString> headers;
    const QStringList lines = text.split('\n');
    for (const QString& line : lines)
    {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;

        const int sep = trimmed.indexOf(':');
        if (sep <= 0)
            continue;

        const QString key = trimmed.left(sep).trimmed();
        const QString value = trimmed.mid(sep + 1).trimmed();
        if (!key.isEmpty())
            headers.insert(key, value);
    }
    return headers;
}

QMap<QString, QString> parseEnvLines(const QString& text)
{
    QMap<QString, QString> env;
    const QStringList lines = text.split('\n');
    for (const QString& line : lines)
    {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;

        const int sep = trimmed.indexOf('=');
        if (sep <= 0)
            continue;

        const QString key = trimmed.left(sep).trimmed();
        const QString value = trimmed.mid(sep + 1);
        if (!key.isEmpty())
            env.insert(key, value);
    }
    return env;
}

QString envToText(const QMap<QString, QString>& env)
{
    QStringList lines;
    for (auto it = env.begin(); it != env.end(); ++it)
        lines.push_back(it.key() + QStringLiteral("=") + it.value());
    return lines.join('\n');
}

QString validateEnvLines(const QString& text)
{
    const QStringList lines = text.split('\n');
    for (int i = 0; i < lines.size(); ++i)
    {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.isEmpty())
            continue;

        const int sep = trimmed.indexOf('=');
        if (sep <= 0)
            return QObject::tr("环境变量第 %1 行格式无效，请使用 NAME=VALUE").arg(i + 1);

        const QString key = trimmed.left(sep).trimmed();
        if (key.isEmpty())
            return QObject::tr("环境变量第 %1 行名称不能为空").arg(i + 1);
    }
    return QString();
}

QString headersToText(const QMap<QString, QString>& headers)
{
    QStringList lines;
    for (auto it = headers.begin(); it != headers.end(); ++it)
        lines.push_back(it.key() + QStringLiteral(": ") + it.value());
    return lines.join('\n');
}
}  // namespace

McpServerEditorDialog::McpServerEditorDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("MCP 服务器设置"));
    setModal(true);
    resize(640, 470);
    setMinimumWidth(620);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(12);

    m_mainForm = new QFormLayout();
    m_mainForm->setContentsMargins(0, 0, 0, 0);
    m_mainForm->setHorizontalSpacing(14);
    m_mainForm->setVerticalSpacing(10);
    m_mainForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_mainForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_nameEdit = new ThemeWidgets::LineEdit(this);
    m_nameEdit->setPlaceholderText(tr("例如：filesystem"));
    m_mainForm->addRow(tr("名称："), m_nameEdit);

    m_typeCombo = new ThemeWidgets::ComboBox(this);
    m_typeCombo->addItem(tr("Stdio"), static_cast<int>(McpServerConfig::Type::Stdio));
    m_typeCombo->addItem(tr("HTTP/SSE"), static_cast<int>(McpServerConfig::Type::HttpSSE));
    m_mainForm->addRow(tr("类型："), m_typeCombo);

    root->addLayout(m_mainForm);

    m_typeStack = new QStackedWidget(this);

    auto* stdioPage = new QWidget(m_typeStack);
    {
        m_stdioForm = new QFormLayout(stdioPage);
        m_stdioForm->setContentsMargins(0, 0, 0, 0);
        m_stdioForm->setHorizontalSpacing(14);
        m_stdioForm->setVerticalSpacing(10);
        m_stdioForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_stdioForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        m_stdioCommandEdit = new ThemeWidgets::LineEdit(stdioPage);
        m_stdioCommandEdit->setPlaceholderText(tr("例如：npx"));
        m_stdioArgsEdit = new ThemeWidgets::LineEdit(stdioPage);
        m_stdioArgsEdit->setPlaceholderText(tr("例如：-y @modelcontextprotocol/server-filesystem ."));
        m_stdioEnvEdit = new ThemeWidgets::PlainTextEdit(stdioPage);
        m_stdioEnvEdit->setPlaceholderText(tr("每行一个变量，格式：\nNAME=VALUE"));
        m_stdioEnvEdit->setMinimumHeight(88);
        m_stdioForm->addRow(tr("命令："), m_stdioCommandEdit);
        m_stdioForm->addRow(tr("参数："), m_stdioArgsEdit);
        m_stdioForm->addRow(tr("环境变量："), m_stdioEnvEdit);
    }

    auto* httpPage = new QWidget(m_typeStack);
    {
        m_httpForm = new QFormLayout(httpPage);
        m_httpForm->setContentsMargins(0, 0, 0, 0);
        m_httpForm->setHorizontalSpacing(14);
        m_httpForm->setVerticalSpacing(10);
        m_httpForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_httpForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        m_httpUrlEdit = new ThemeWidgets::LineEdit(httpPage);
        m_httpUrlEdit->setPlaceholderText(tr("例如：http://127.0.0.1:3000/mcp"));
        m_httpHeadersEdit = new ThemeWidgets::PlainTextEdit(httpPage);
        m_httpHeadersEdit->setPlaceholderText(tr("每行一个 Header，格式：\nAuthorization: Bearer xxx"));
        m_httpHeadersEdit->setMinimumHeight(88);
        m_httpForm->addRow(tr("URL："), m_httpUrlEdit);
        m_httpForm->addRow(tr("Headers："), m_httpHeadersEdit);
    }

    m_typeStack->addWidget(stdioPage);
    m_typeStack->addWidget(httpPage);
    root->addWidget(m_typeStack, 1);

    m_testButton = new QPushButton(tr("测试连接"), this);

    m_testBusyContainer = new QWidget(this);
    auto* testBusyLayout = new QHBoxLayout(m_testBusyContainer);
    testBusyLayout->setContentsMargins(0, 0, 0, 0);
    testBusyLayout->setSpacing(6);
    m_testDots = new TypingDotsWidget(m_testBusyContainer);
    testBusyLayout->addWidget(m_testDots, 0, Qt::AlignVCenter);
    m_testBusyContainer->setVisible(false);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = m_buttonBox->button(QDialogButtonBox::Ok);
    if (m_okButton)
        m_okButton->setText(tr("确认"));
    if (QPushButton* cancelButton = m_buttonBox->button(QDialogButtonBox::Cancel))
        cancelButton->setText(tr("取消"));
    m_buttonBox->setCenterButtons(false);

    auto* actionLayout = new QHBoxLayout();
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(8);
    actionLayout->addWidget(m_testButton, 0, Qt::AlignVCenter);
    actionLayout->addWidget(m_testBusyContainer, 0, Qt::AlignVCenter);
    actionLayout->addStretch(1);
    actionLayout->addWidget(m_buttonBox, 0, Qt::AlignVCenter);
    root->addLayout(actionLayout);

    connect(m_typeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        updateTypeUi();
    });
    connect(m_testButton, &QPushButton::clicked, this, &McpServerEditorDialog::onTestConnection);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &McpServerEditorDialog::onAccept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateTypeUi();
}

void McpServerEditorDialog::setServerConfig(const McpServerConfig& config)
{
    m_nameEdit->setText(config.name);
    m_timeoutMs = config.timeoutMs > 0 ? config.timeoutMs : 8000;

    const int typeIndex = (config.type == McpServerConfig::Type::Stdio) ? 0 : 1;
    m_typeCombo->setCurrentIndex(typeIndex);

    m_stdioCommandEdit->setText(config.command);
    m_stdioArgsEdit->setText(config.args);
    m_stdioEnvEdit->setPlainText(envToText(config.env));

    m_httpUrlEdit->setText(config.url);
    m_httpHeadersEdit->setPlainText(headersToText(config.headers));

    updateTypeUi();
}

McpServerConfig McpServerEditorDialog::serverConfig() const
{
    McpServerConfig config;
    config.name = m_nameEdit->text().trimmed();
    config.type = (m_typeCombo->currentIndex() == 0) ? McpServerConfig::Type::Stdio : McpServerConfig::Type::HttpSSE;
    config.timeoutMs = m_timeoutMs;
    config.enabled = true;

    if (config.type == McpServerConfig::Type::Stdio)
    {
        config.command = m_stdioCommandEdit->text().trimmed();
        config.args = m_stdioArgsEdit->text().trimmed();
        config.env = parseEnvLines(m_stdioEnvEdit->toPlainText());
    }
    else
    {
        config.url = m_httpUrlEdit->text().trimmed();
        config.headers = parseHeaders(m_httpHeadersEdit->toPlainText());
    }

    return config;
}

void McpServerEditorDialog::onAccept()
{
    if (m_testInProgress)
        return;

    if (m_typeCombo->currentIndex() == 0)
    {
        const QString envValidationError = validateEnvLines(m_stdioEnvEdit->toPlainText());
        if (!envValidationError.isEmpty())
        {
            QMessageBox::warning(this, tr("输入无效"), envValidationError);
            return;
        }
    }

    const McpServerConfig config = serverConfig();
    const QString validationError = config.validate();
    if (!validationError.isEmpty())
    {
        QMessageBox::warning(this, tr("输入无效"), validationError);
        return;
    }

    accept();
}

void McpServerEditorDialog::onTestConnection()
{
    if (m_testInProgress)
        return;

    if (m_typeCombo->currentIndex() == 0)
    {
        const QString envValidationError = validateEnvLines(m_stdioEnvEdit->toPlainText());
        if (!envValidationError.isEmpty())
        {
            QMessageBox::warning(this, tr("输入无效"), envValidationError);
            return;
        }
    }

    const McpServerConfig config = serverConfig();
    const QString validationError = config.validate();
    if (!validationError.isEmpty())
    {
        QMessageBox::warning(this, tr("输入无效"), validationError);
        return;
    }

    m_testInProgress = true;
    setTestUiState(true);

    QPointer<McpServerEditorDialog> guarded(this);
    QThread* testThread = QThread::create([guarded, config] {
        TestConnectionResult result;

        QString error;
        std::unique_ptr<IMcpAdapter> adapter = IMcpAdapter::create(config, &error);
        if (!adapter)
        {
            result.error = error.isEmpty() ? QObject::tr("无法创建MCP适配器") : error;
        }
        else
        {
            const QJsonArray tools = adapter->listTools(&error);
            if (!error.isEmpty())
            {
                result.error = error;
            }
            else
            {
                result.success = true;
                result.toolsCount = tools.size();
                for (const QJsonValue& toolVal : tools)
                {
                    if (!toolVal.isObject())
                        continue;

                    const QJsonObject toolObj = toolVal.toObject();
                    const QString toolName = toolObj.value(QStringLiteral("name")).toString();
                    if (!toolName.isEmpty())
                        result.toolNames.push_back(toolName);
                }
            }
        }

        QMetaObject::invokeMethod(qApp, [guarded, result] {
            if (!guarded)
                return;
            guarded->onTestConnectionFinished(result);
        }, Qt::QueuedConnection);
    });
    QObject::connect(testThread, &QThread::finished, testThread, &QObject::deleteLater);
    testThread->start();
}

void McpServerEditorDialog::onTestConnectionFinished(const TestConnectionResult& result)
{
    m_testInProgress = false;
    setTestUiState(false);

    if (!isVisible())
        return;

    if (!result.success)
    {
        QMessageBox::warning(this, tr("连接测试失败"), result.error);
        return;
    }

    QString message = tr("连接成功，发现 %1 个工具。").arg(result.toolsCount);
    if (!result.toolNames.isEmpty())
        message += QStringLiteral("\n\n") + tr("工具列表：\n%1").arg(result.toolNames.join('\n'));
    QMessageBox::information(this, tr("连接测试成功"), message);
}

void McpServerEditorDialog::setTestUiState(bool testing)
{
    if (testing)
    {
        if (m_testButton)
        {
            m_testButton->setEnabled(false);
            m_testButton->setText(tr("测试中"));
        }
        if (m_testBusyContainer)
            m_testBusyContainer->setVisible(true);
        if (m_testDots)
            m_testDots->setActive(true);
        if (m_okButton)
            m_okButton->setEnabled(false);
        return;
    }

    if (m_testDots)
        m_testDots->setActive(false);
    if (m_testBusyContainer)
        m_testBusyContainer->setVisible(false);
    if (m_testButton)
    {
        m_testButton->setEnabled(true);
        m_testButton->setText(tr("测试连接"));
    }
    if (m_okButton)
        m_okButton->setEnabled(true);
}

void McpServerEditorDialog::updateTypeUi()
{
    const bool isStdio = (m_typeCombo->currentIndex() == 0);
    m_typeStack->setCurrentIndex(isStdio ? 0 : 1);
}

void McpServerEditorDialog::changeEvent(QEvent* event)
{
    QDialog::changeEvent(event);

    if (event->type() != QEvent::LanguageChange)
        return;

    setWindowTitle(tr("MCP 服务器设置"));

    if (m_nameEdit)
        m_nameEdit->setPlaceholderText(tr("例如：filesystem"));
    if (m_stdioCommandEdit)
        m_stdioCommandEdit->setPlaceholderText(tr("例如：npx"));
    if (m_stdioArgsEdit)
        m_stdioArgsEdit->setPlaceholderText(tr("例如：-y @modelcontextprotocol/server-filesystem ."));
    if (m_stdioEnvEdit)
        m_stdioEnvEdit->setPlaceholderText(tr("每行一个变量，格式：\nNAME=VALUE"));
    if (m_httpUrlEdit)
        m_httpUrlEdit->setPlaceholderText(tr("例如：http://127.0.0.1:3000/mcp"));
    if (m_httpHeadersEdit)
        m_httpHeadersEdit->setPlaceholderText(tr("每行一个 Header，格式：\nAuthorization: Bearer xxx"));

    if (m_typeCombo)
    {
        m_typeCombo->setItemText(0, tr("Stdio"));
        m_typeCombo->setItemText(1, tr("HTTP/SSE"));
    }

    if (m_mainForm)
    {
        if (auto* label = qobject_cast<QLabel*>(m_mainForm->labelForField(m_nameEdit)))
            label->setText(tr("名称："));
        if (auto* label = qobject_cast<QLabel*>(m_mainForm->labelForField(m_typeCombo)))
            label->setText(tr("类型："));
    }

    if (m_stdioForm)
    {
        if (auto* label = qobject_cast<QLabel*>(m_stdioForm->labelForField(m_stdioCommandEdit)))
            label->setText(tr("命令："));
        if (auto* label = qobject_cast<QLabel*>(m_stdioForm->labelForField(m_stdioArgsEdit)))
            label->setText(tr("参数："));
        if (auto* label = qobject_cast<QLabel*>(m_stdioForm->labelForField(m_stdioEnvEdit)))
            label->setText(tr("环境变量："));
    }

    if (m_httpForm)
    {
        if (auto* label = qobject_cast<QLabel*>(m_httpForm->labelForField(m_httpUrlEdit)))
            label->setText(tr("URL："));
        if (auto* label = qobject_cast<QLabel*>(m_httpForm->labelForField(m_httpHeadersEdit)))
            label->setText(tr("Headers："));
    }

    if (m_okButton)
        m_okButton->setText(tr("确认"));
    if (m_buttonBox)
    {
        if (QPushButton* cancelButton = m_buttonBox->button(QDialogButtonBox::Cancel))
            cancelButton->setText(tr("取消"));
    }

    setTestUiState(m_testInProgress);
}
