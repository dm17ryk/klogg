#pragma once

#include <QByteArray>
#include <QMap>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

struct ActionSequenceResult {
    bool ok = false;
    QByteArray bytes;
    QString error;
};

enum class ActionSequenceType { String, HexString };
QString actionSequenceTypeToString( ActionSequenceType type );
ActionSequenceType actionSequenceTypeFromString( const QString& text, bool* ok = nullptr );

enum class ResponseMatchType { String, HexString, Regex };
QString responseMatchTypeToString( ResponseMatchType type );
ResponseMatchType responseMatchTypeFromString( const QString& text, bool* ok = nullptr );

struct ActionSequence {
    ActionSequenceType type = ActionSequenceType::String;
    QString value;
};

struct ActionParameters {
    bool repeat = false;
    int delay = 0;
};

struct ActionDefinition {
    int id = -1;
    bool enabled = true;
    bool hidden = false;
    QString name;
    QString description;
    ActionSequence sequence;
    ActionParameters parameters;
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
