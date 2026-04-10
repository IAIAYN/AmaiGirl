#include "ai/core/IMcpAdapter.hpp"
#include "ai/core/McpServerConfig.hpp"
#include "ai/McpStdioAdapter.hpp"
#include "ai/McpHttpSseAdapter.hpp"

std::unique_ptr<IMcpAdapter> IMcpAdapter::create(const McpServerConfig& config, QString* errorMessage)
{
    if (!config.isValid()) {
        if (errorMessage) *errorMessage = config.validate();
        return nullptr;
    }

    std::unique_ptr<IMcpAdapter> adapter;

    if (config.type == McpServerConfig::Type::Stdio) {
        auto stdioAdapter = std::make_unique<McpStdioAdapter>();
        if (!stdioAdapter->setConfig(config, errorMessage)) {
            return nullptr;
        }
        adapter = std::move(stdioAdapter);
    } else if (config.type == McpServerConfig::Type::HttpSSE) {
        auto httpAdapter = std::make_unique<McpHttpSseAdapter>();
        if (!httpAdapter->setServerConfig(config, errorMessage)) {
            return nullptr;
        }
        adapter = std::move(httpAdapter);
    } else {
        if (errorMessage) *errorMessage = QStringLiteral("Unknown MCP type");
        return nullptr;
    }

    return adapter;
}
