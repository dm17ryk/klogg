#pragma once

#include <QByteArray>
#include <QMap>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

struct ActionSequenceResult {
    bool ok = false;
    QByteArray bytes;
    QString error;
};

enum class ActionSequenceType { String, HexString };
QString actionSequenceTypeToString( ActionSequenceType type );
ActionSequenceType actionSequenceTypeFromString( const QString& text, bool* ok = nullptr );

enum class ResponseMatchType { String, HexString, Regex, Wildcard };
QString responseMatchTypeToString( ResponseMatchType type );
ResponseMatchType responseMatchTypeFromString( const QString& text, bool* ok = nullptr );

struct ActionChecksumDefinition {
    bool enabled = false;
    QString algorithm = QStringLiteral( "sum8" );
    QString placeholder = QStringLiteral( "${CHECKSUM}" );
};

struct ActionSequence {
    ActionSequenceType type = ActionSequenceType::String;
    QString value;
};

struct ActionParameters {
    bool repeat = false;
    int delay = 0;
    int repeatCount = 1;
    int repeatInterval = 0;
    QStringList variableNames;
};

struct ActionDefinition {
    int id = -1;
    int order = 0;
    bool enabled = true;
    bool hidden = false;
    QString name;
    QString description;
    ActionSequence sequence;
    ActionParameters parameters;
    ActionChecksumDefinition checksum;
};

struct ResponseMatchDefinition {
    ResponseMatchType type = ResponseMatchType::String;
    QString value;
    QRegularExpression compiled;
};

struct ResponseActionDefinition {
    bool hasActionId = false;
    int actionId = -1;
    bool hasInlineAction = false;
    ActionSequence inlineAction;
    QString comment;
    bool linebreak = false;
    bool timestamp = false;
    bool snapshot = false;
    bool stopCommunication = false;
};

struct ResponseDefinition {
    int id = -1;
    int order = 0;
    bool enabled = true;
    bool hidden = false;
    QString name;
    QString description;
    ResponseMatchDefinition match;
    ResponseActionDefinition response;
};

struct ActionsParseResult {
    QVector<ActionDefinition> actions;
    QVector<ResponseDefinition> responses;
    QStringList errors;
    QStringList warnings;
};

ActionSequenceResult actionSequenceToBytes( const ActionSequence& sequence,
                                            const QMap<QString, QString>& substitutions = {},
                                            QStringList* missing = nullptr );
ActionSequenceResult actionDefinitionToBytes( const ActionDefinition& action,
                                              const QMap<QString, QString>& substitutions = {},
                                              QStringList* missing = nullptr );
bool validateActionDefinition( const ActionDefinition& action, QString* errorMessage = nullptr );
bool validateResponseDefinition( const ResponseDefinition& response,
                                 QString* errorMessage = nullptr );
void normalizeActionDefinitions( QVector<ActionDefinition>* actions );
void normalizeResponseDefinitions( QVector<ResponseDefinition>* responses );
QVariantMap actionDefinitionToVariantMap( const ActionDefinition& action );
QVariantMap responseDefinitionToVariantMap( const ResponseDefinition& response );
ActionDefinition actionDefinitionFromVariantMap( const QVariantMap& map,
                                                QString* errorMessage = nullptr );
ResponseDefinition responseDefinitionFromVariantMap( const QVariantMap& map,
                                                    QString* errorMessage = nullptr );
