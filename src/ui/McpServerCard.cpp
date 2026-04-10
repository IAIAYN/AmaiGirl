#include "ui/McpServerCard.hpp"

#include "ui/theme/ThemeWidgets.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QEvent>

McpServerCard::McpServerCard(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("mcpServerCard"));
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(300);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(10);

    m_nameLabel = new QLabel(this);
    m_nameLabel->setObjectName(QStringLiteral("mcpServerCardName"));
    m_nameLabel->setWordWrap(true);
    m_nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_typeLabel = new QLabel(this);
    m_typeLabel->setObjectName(QStringLiteral("mcpServerCardType"));

    m_enabledSwitch = new ThemeWidgets::Switch(tr("启用"), this);

    m_editButton = new ThemeWidgets::Button(tr("编辑"), this);
    m_editButton->setTone(ThemeWidgets::Button::Tone::Link);
    m_editButton->setMinimumWidth(62);

    m_deleteButton = new ThemeWidgets::Button(tr("删除"), this);
    m_deleteButton->setTone(ThemeWidgets::Button::Tone::Danger);
    m_deleteButton->setMinimumWidth(62);

    auto* top = new QHBoxLayout();
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(8);
    top->addWidget(m_nameLabel, 1, Qt::AlignLeft | Qt::AlignTop);
    top->addWidget(m_enabledSwitch, 0, Qt::AlignRight | Qt::AlignTop);
    layout->addLayout(top);

    auto* bottom = new QHBoxLayout();
    bottom->setContentsMargins(0, 0, 0, 0);
    bottom->setSpacing(8);
    bottom->addWidget(m_typeLabel, 0, Qt::AlignLeft | Qt::AlignBottom);
    bottom->addStretch(1);
    bottom->addWidget(m_editButton, 0, Qt::AlignRight | Qt::AlignBottom);
    bottom->addWidget(m_deleteButton, 0, Qt::AlignRight | Qt::AlignBottom);
    layout->addLayout(bottom);

    connect(m_enabledSwitch, &ThemeWidgets::Switch::toggled, this, &McpServerCard::enabledToggled);
    connect(m_editButton, &QPushButton::clicked, this, &McpServerCard::editRequested);
    connect(m_deleteButton, &QPushButton::clicked, this, &McpServerCard::deleteRequested);

    refreshView();
}

void McpServerCard::setServerConfig(const McpServerConfig& config)
{
    m_config = config;
    refreshView();
}

McpServerConfig McpServerCard::serverConfig() const
{
    McpServerConfig out = m_config;
    out.enabled = m_enabledSwitch ? m_enabledSwitch->isChecked() : out.enabled;
    return out;
}

void McpServerCard::refreshView()
{
    if (m_nameLabel)
        m_nameLabel->setText(m_config.name.isEmpty() ? tr("未命名服务器") : m_config.name);

    if (m_typeLabel)
    {
        const QString typeText = (m_config.type == McpServerConfig::Type::Stdio)
            ? tr("Stdio")
            : tr("HTTP/SSE");
        m_typeLabel->setText(typeText);
    }

    if (m_enabledSwitch)
        m_enabledSwitch->setChecked(m_config.enabled);
}

void McpServerCard::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);

    if (event->type() != QEvent::LanguageChange)
        return;

    if (m_enabledSwitch)
        m_enabledSwitch->setText(tr("启用"));
    if (m_editButton)
        m_editButton->setText(tr("编辑"));
    if (m_deleteButton)
        m_deleteButton->setText(tr("删除"));

    refreshView();
}
