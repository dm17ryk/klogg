#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace cilogg::plugins {
constexpr int ProtocolVersion = 1;
constexpr int MaxMessageBytes = 1024 * 1024;
constexpr int MaxPendingBytes = 8 * MaxMessageBytes;
// Plugin views (docs/plugin-views.md).
constexpr int MaxViewsPerPlugin = 8;
constexpr int MaxViewSize = 10000;

struct Manifest {
    QString id;
    QString name;
    QString version;
    QString directory;
    QString executable;
    QStringList arguments;
    QJsonObject json;
    QString error;
    // `requires` entries this host does not meet (docs/plugin-sdk-advanced.md section 4.3); the
    // manifest is then not usable and `error` says "Requires a newer CILogg".
    QStringList missingFeatures;

    bool valid() const
    {
        return error.isEmpty() && !id.isEmpty();
    }
    static Manifest read( const QString& directory );
    static Manifest parse( const QByteArray& bytes, const QString& directory );
};

// Shared by manifest, handshake, configuration and action forms.
QString validateFields( const QJsonArray& fields );
QString validateValues( const QJsonArray& fields, QJsonObject* values );
QString validateActions( const QJsonArray& actions, const QJsonArray& capabilities );
// Manifest `views`: id/title like actions, kind dock|window|dialog, dock area, sizes and flags.
QString validateViews( const QJsonArray& views );
bool safeRelativePath( const QString& path );
QString newId();
} // namespace cilogg::plugins
