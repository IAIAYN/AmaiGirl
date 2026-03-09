#pragma once
#include <QString>
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <vector>
#include <optional>
#include <functional>
#include <stdexcept>

struct Vec2 {
    float x{0};
    float y{0};
};

// Declarations only; implementations are in Utils.cpp
QByteArray readFileAll(const QString &path);
QJsonDocument jsonFromFile(const QString &path);
void ensure(bool cond, const QString &msg);
float clampf(float v, float lo, float hi);
float easingSine(float rate);

// Resource path helpers.
// - macOS bundle: <App>.app/Contents/Resources
// - Windows portable build/package: <appDir>/res
// - Linux install/AppImage: prefer APPDIR and ../share/AmaiGirl/res when available
// - Fallback: <appDir>/res (dev/local layout)
QString appResourceRootPath();
QString appResourcePath(const QString& relativePath);
