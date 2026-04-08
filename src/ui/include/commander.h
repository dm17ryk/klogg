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
    CloseKlogg,
    CloseAll,
    GetInfo,
    GetFilters,
    FocusTab,
    SetFilter,
    CloseTab,
    Search,
    SetFollowMode,
    InvokeAction,
    DumpState,
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
    QString tabId;
    QString filterId;
    QString filterString;
    QString searchText;
    QString objectName;
    std::optional<int> windowIndex;
    std::optional<int> tabIndex;
    std::optional<int> filterIndex;
    std::optional<bool> enabled;
    bool followFile = false;
    bool predefinedFilters = false;
    bool prettyOutput = false;
    bool runSearch = false;
    bool rearmAutoRefresh = false;
    bool searchUseRegex = false;
    bool searchCaseSensitive = false;
    bool searchInverseMatch = false;
    bool searchUseBoolean = false;
    bool searchAutoRefresh = false;
    bool searchKeepResults = false;
    CommanderComSettings comSettings;
};

struct CommanderResult {
    CommanderResultCode code = CommanderResultCode::Success;
    QString message;
    QVariantMap payload;

    bool ok() const
    {
        return code == CommanderResultCode::Success;
    }

    bool hasPayload() const
    {
        return !payload.isEmpty();
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

CommanderResult commanderSuccess( const QString& message = {}, const QVariantMap& payload = {} );
CommanderResult commanderFailure( CommanderResultCode code, const QString& message );

Q_DECLARE_METATYPE( CommanderRequest )
Q_DECLARE_METATYPE( CommanderResult )
