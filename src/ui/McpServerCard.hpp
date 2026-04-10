#pragma once

#include "ai/core/McpServerConfig.hpp"
#include "ui/theme/ThemeWidgets.hpp"

#include <QWidget>

class QLabel;
class QEvent;

class McpServerCard : public QWidget
{
    Q_OBJECT
public:
    explicit McpServerCard(QWidget* parent = nullptr);

    void setServerConfig(const McpServerConfig& config);
    McpServerConfig serverConfig() const;

signals:
    void enabledToggled(bool enabled);
    void editRequested();
    void deleteRequested();

protected:
    void changeEvent(QEvent* event) override;

private:
    void refreshView();

    McpServerConfig m_config;
    QLabel* m_nameLabel{nullptr};
    QLabel* m_typeLabel{nullptr};
    ThemeWidgets::Switch* m_enabledSwitch{nullptr};
    ThemeWidgets::Button* m_editButton{nullptr};
    ThemeWidgets::Button* m_deleteButton{nullptr};
};
