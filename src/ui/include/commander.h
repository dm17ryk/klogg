#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QVariantMap>
#include <QSerialPort>
#include <optional>

enum class CommanderAction {
    None,
    OpenFile,
    OpenUrl,
    OpenCom,
    CloseFile,
    CloseUrl,
    CloseCom,
};

enum class CommanderResultCode {
    Success,
    InvalidRequest,
    NotFound,
    ExecutionFailed,
    TransportError,
};

struct CommanderComSettings {
    QString portName;
    std::optional<QString> filePath;
    std::optional<qint32> baudRate;
    std::optional<QSerialPort::DataBits> dataBits;
    std::optional<QSerialPort::Parity> parity;
    std::optional<QSerialPort::StopBits> stopBits;
    std::optional<QSerialPort::FlowControl> flowControl;
    std::optional<bool> addTimestamps;
    std::optional<QString> timestampFormat;
    std::optional<bool> logTransmits;
    std::optional<bool> useForActions;
};

struct CommanderRequest {
    CommanderAction action = CommanderAction::None;
    QString filePath;
    QString url;
    QString portName;
    bool followFile = false;
    CommanderComSettings comSettings;
};

struct CommanderResult {
    CommanderResultCode code = CommanderResultCode::Success;
    QString message;

    bool ok() const
    {
        return code == CommanderResultCode::Success;
    }
};

QString commanderActionToString( CommanderAction action );
std::optional<CommanderAction> commanderActionFromString( const QString& action );
QString commanderResultCodeToString( CommanderResultCode code );
QString normalizeCommanderFilePath( const QString& path );
QString normalizeCommanderUrl( const QString& url );
bool isCommanderOpenAction( CommanderAction action );

QVariantMap commanderRequestToVariantMap( const CommanderRequest& request );
std::optional<CommanderRequest> commanderRequestFromVariantMap( const QVariantMap& map,
                                                                QString* errorMessage = nullptr );
QVariantMap commanderResultToVariantMap( const CommanderResult& result );
std::optional<CommanderResult> commanderResultFromVariantMap( const QVariantMap& map,
                                                              QString* errorMessage = nullptr );

bool writeCommanderResult( const QString& resultPath, const CommanderResult& result );
std::optional<CommanderResult> readCommanderResult( const QString& resultPath,
                                                    QString* errorMessage = nullptr );

CommanderResult commanderSuccess( const QString& message = {} );
CommanderResult commanderFailure( CommanderResultCode code, const QString& message );

Q_DECLARE_METATYPE( CommanderRequest )
Q_DECLARE_METATYPE( CommanderResult )
