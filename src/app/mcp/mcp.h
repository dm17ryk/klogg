#ifndef CILOGG_MCP_H
#define CILOGG_MCP_H

#include <QJsonObject>
#include <QStringList>
#include <functional>
#include <optional>

namespace cilogg::mcp {
using ToolHandler = std::function<QJsonObject( const QString&, const QJsonObject& )>;

QJsonObject parseCommandHelp( const QString& text );
QJsonObject serverEntry( const QString& executable );
QString generatedSkill( const QString& executable );
QString mergeClientConfig( const QString& text, const QString& client, const QJsonObject& entry,
                           bool remove = false );
QJsonObject installClients( const QString& executable, const QStringList& clients,
                            const QString& home = {}, bool dryRun = false, bool skills = true,
                            bool remove = false );
QJsonObject toolResult( const QJsonObject& payload, bool error = false );
QString saveSkill( const QString& executable, const QString& directory, bool dryRun = false );

class Session {
public:
    Session( QString version, ToolHandler handler );
    std::optional<QJsonObject> dispatch( const QJsonObject& request );
    static QJsonObject tools();
    static QJsonObject error( const QJsonValue& id, int code, const QString& message );

private:
    QString version_;
    ToolHandler handler_;
    bool initialized_ = false;
    bool ready_ = false;
};

// Called before QApplication, persistent settings, or the normal GUI startup.
int runCli( int argc, char* argv[], const QString& version );
} // namespace cilogg::mcp

#endif
