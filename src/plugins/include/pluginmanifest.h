#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace cilogg::plugins {
constexpr int ProtocolVersion = 1;
constexpr int MaxMessageBytes = 1024 * 1024;
constexpr int MaxPendingBytes = 8 * MaxMessageBytes;

struct Manifest {
    QString id;
    QString name;
    QString version;
    QString directory;
    QString executable;
    QStringList arguments;
    QJsonObject json;
    QString error;

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
bool safeRelativePath( const QString& path );
QString newId();
} // namespace cilogg::plugins
