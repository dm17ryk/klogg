#include "commander.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QUrl>

#include "serialcaptureworker.h"

namespace {
template <typename Enum>
void insertOptionalEnum( QVariantMap& map, const QString& key, const std::optional<Enum>& value )
{
    if ( value ) {
        map.insert( key, static_cast<int>( *value ) );
    }
}

template <typename Enum>
std::optional<Enum> optionalEnumFromMap( const QVariantMap& map, const QString& key )
{
    const auto it = map.find( key );
    if ( it == map.end() ) {
        return std::nullopt;
    }

    bool ok = false;
    const auto rawValue = it->toInt( &ok );
    if ( !ok ) {
        return std::nullopt;
    }

    return static_cast<Enum>( rawValue );
}

void setError( QString* errorMessage, const QString& message )
{
    if ( errorMessage != nullptr ) {
        *errorMessage = message;
    }
}
} // namespace

QString commanderActionToString( CommanderAction action )
{
    switch ( action ) {
    case CommanderAction::OpenFile:
        return QStringLiteral( "open_file" );
    case CommanderAction::OpenUrl:
        return QStringLiteral( "open_url" );
    case CommanderAction::OpenCom:
        return QStringLiteral( "open_com" );
    case CommanderAction::CloseFile:
        return QStringLiteral( "close_file" );
    case CommanderAction::CloseUrl:
        return QStringLiteral( "close_url" );
    case CommanderAction::CloseCom:
        return QStringLiteral( "close_com" );
    case CommanderAction::None:
    default:
        return {};
    }
}

std::optional<CommanderAction> commanderActionFromString( const QString& action )
{
    const auto normalized = action.trimmed().toLower();
    if ( normalized == QStringLiteral( "open_file" ) ) {
        return CommanderAction::OpenFile;
    }
    if ( normalized == QStringLiteral( "open_url" ) ) {
        return CommanderAction::OpenUrl;
    }
    if ( normalized == QStringLiteral( "open_com" ) ) {
        return CommanderAction::OpenCom;
    }
    if ( normalized == QStringLiteral( "close_file" ) ) {
        return CommanderAction::CloseFile;
    }
    if ( normalized == QStringLiteral( "close_url" ) ) {
        return CommanderAction::CloseUrl;
    }
    if ( normalized == QStringLiteral( "close_com" ) ) {
        return CommanderAction::CloseCom;
    }

    return std::nullopt;
}

QString commanderResultCodeToString( CommanderResultCode code )
{
    switch ( code ) {
    case CommanderResultCode::Success:
        return QStringLiteral( "success" );
    case CommanderResultCode::InvalidRequest:
        return QStringLiteral( "invalid_request" );
    case CommanderResultCode::NotFound:
        return QStringLiteral( "not_found" );
    case CommanderResultCode::ExecutionFailed:
        return QStringLiteral( "execution_failed" );
    case CommanderResultCode::TransportError:
        return QStringLiteral( "transport_error" );
    default:
        return QStringLiteral( "execution_failed" );
    }
}

QString normalizeCommanderFilePath( const QString& path )
{
    const auto trimmed = path.trimmed();
    if ( trimmed.isEmpty() ) {
        return {};
    }

    return QFileInfo{ trimmed }.absoluteFilePath();
}

QString normalizeCommanderUrl( const QString& urlString )
{
    const auto trimmed = urlString.trimmed();
    if ( trimmed.isEmpty() ) {
        return {};
    }

    auto url = QUrl::fromUserInput( trimmed );
    if ( !url.isValid() || url.isEmpty() ) {
        return {};
    }

    url = url.adjusted( QUrl::NormalizePathSegments );
    return url.toString( QUrl::FullyEncoded );
}

bool isCommanderOpenAction( CommanderAction action )
{
    return action == CommanderAction::OpenFile || action == CommanderAction::OpenUrl
           || action == CommanderAction::OpenCom;
}

QVariantMap commanderRequestToVariantMap( const CommanderRequest& request )
{
    QVariantMap map;
    map.insert( QStringLiteral( "action" ), commanderActionToString( request.action ) );

    if ( !request.filePath.isEmpty() ) {
        map.insert( QStringLiteral( "filePath" ), request.filePath );
    }
    if ( !request.url.isEmpty() ) {
        map.insert( QStringLiteral( "url" ), request.url );
    }
    if ( !request.portName.isEmpty() ) {
        map.insert( QStringLiteral( "portName" ), request.portName );
    }
    if ( request.followFile ) {
        map.insert( QStringLiteral( "followFile" ), true );
    }

    QVariantMap comMap;
    if ( !request.comSettings.portName.isEmpty() ) {
        comMap.insert( QStringLiteral( "portName" ), request.comSettings.portName );
    }
    if ( request.comSettings.filePath ) {
        comMap.insert( QStringLiteral( "filePath" ), *request.comSettings.filePath );
    }
    if ( request.comSettings.baudRate ) {
        comMap.insert( QStringLiteral( "baudRate" ), *request.comSettings.baudRate );
    }
    insertOptionalEnum( comMap, QStringLiteral( "dataBits" ), request.comSettings.dataBits );
    insertOptionalEnum( comMap, QStringLiteral( "parity" ), request.comSettings.parity );
    insertOptionalEnum( comMap, QStringLiteral( "stopBits" ), request.comSettings.stopBits );
    insertOptionalEnum( comMap, QStringLiteral( "flowControl" ), request.comSettings.flowControl );
    if ( request.comSettings.addTimestamps ) {
        comMap.insert( QStringLiteral( "addTimestamps" ), *request.comSettings.addTimestamps );
    }
    if ( request.comSettings.timestampFormat ) {
        comMap.insert( QStringLiteral( "timestampFormat" ), *request.comSettings.timestampFormat );
    }
    if ( request.comSettings.logTransmits ) {
        comMap.insert( QStringLiteral( "logTransmits" ), *request.comSettings.logTransmits );
    }
    if ( request.comSettings.useForActions ) {
        comMap.insert( QStringLiteral( "useForActions" ), *request.comSettings.useForActions );
    }
    if ( !comMap.isEmpty() ) {
        map.insert( QStringLiteral( "comSettings" ), comMap );
    }

    return map;
}

std::optional<CommanderRequest> commanderRequestFromVariantMap( const QVariantMap& map,
                                                                QString* errorMessage )
{
    const auto action = commanderActionFromString( map.value( QStringLiteral( "action" ) ).toString() );
    if ( !action ) {
        setError( errorMessage, QStringLiteral( "Invalid or missing commander action." ) );
        return std::nullopt;
    }

    CommanderRequest request;
    request.action = *action;
    request.filePath = map.value( QStringLiteral( "filePath" ) ).toString();
    request.url = map.value( QStringLiteral( "url" ) ).toString();
    request.portName = map.value( QStringLiteral( "portName" ) ).toString();
    request.followFile = map.value( QStringLiteral( "followFile" ) ).toBool();

    const auto comSettingsValue = map.value( QStringLiteral( "comSettings" ) );
    if ( comSettingsValue.canConvert<QVariantMap>() ) {
        const auto comMap = comSettingsValue.toMap();
        request.comSettings.portName = comMap.value( QStringLiteral( "portName" ) ).toString();

        const auto filePathIt = comMap.find( QStringLiteral( "filePath" ) );
        if ( filePathIt != comMap.end() ) {
            request.comSettings.filePath = filePathIt->toString();
        }

        const auto baudRateIt = comMap.find( QStringLiteral( "baudRate" ) );
        if ( baudRateIt != comMap.end() ) {
            bool ok = false;
            const auto baudRate = baudRateIt->toInt( &ok );
            if ( !ok ) {
                setError( errorMessage, QStringLiteral( "Invalid COM baud rate." ) );
                return std::nullopt;
            }
            request.comSettings.baudRate = baudRate;
        }

        request.comSettings.dataBits
            = optionalEnumFromMap<QSerialPort::DataBits>( comMap, QStringLiteral( "dataBits" ) );
        request.comSettings.parity
            = optionalEnumFromMap<QSerialPort::Parity>( comMap, QStringLiteral( "parity" ) );
        request.comSettings.stopBits
            = optionalEnumFromMap<QSerialPort::StopBits>( comMap, QStringLiteral( "stopBits" ) );
        request.comSettings.flowControl = optionalEnumFromMap<QSerialPort::FlowControl>(
            comMap, QStringLiteral( "flowControl" ) );

        const auto addTimestampsIt = comMap.find( QStringLiteral( "addTimestamps" ) );
        if ( addTimestampsIt != comMap.end() ) {
            request.comSettings.addTimestamps = addTimestampsIt->toBool();
        }

        const auto timestampFormatIt = comMap.find( QStringLiteral( "timestampFormat" ) );
        if ( timestampFormatIt != comMap.end() ) {
            request.comSettings.timestampFormat = timestampFormatIt->toString();
        }

        const auto logTransmitsIt = comMap.find( QStringLiteral( "logTransmits" ) );
        if ( logTransmitsIt != comMap.end() ) {
            request.comSettings.logTransmits = logTransmitsIt->toBool();
        }

        const auto useForActionsIt = comMap.find( QStringLiteral( "useForActions" ) );
        if ( useForActionsIt != comMap.end() ) {
            request.comSettings.useForActions = useForActionsIt->toBool();
        }
    }

    return request;
}

QVariantMap commanderResultToVariantMap( const CommanderResult& result )
{
    QVariantMap map;
    map.insert( QStringLiteral( "code" ), commanderResultCodeToString( result.code ) );
    map.insert( QStringLiteral( "message" ), result.message );
    return map;
}

std::optional<CommanderResult> commanderResultFromVariantMap( const QVariantMap& map,
                                                              QString* errorMessage )
{
    const auto codeString = map.value( QStringLiteral( "code" ) ).toString().trimmed().toLower();
    CommanderResultCode code = CommanderResultCode::ExecutionFailed;
    if ( codeString == QStringLiteral( "success" ) ) {
        code = CommanderResultCode::Success;
    }
    else if ( codeString == QStringLiteral( "invalid_request" ) ) {
        code = CommanderResultCode::InvalidRequest;
    }
    else if ( codeString == QStringLiteral( "not_found" ) ) {
        code = CommanderResultCode::NotFound;
    }
    else if ( codeString == QStringLiteral( "execution_failed" ) ) {
        code = CommanderResultCode::ExecutionFailed;
    }
    else if ( codeString == QStringLiteral( "transport_error" ) ) {
        code = CommanderResultCode::TransportError;
    }
    else {
        setError( errorMessage, QStringLiteral( "Invalid commander result code." ) );
        return std::nullopt;
    }

    CommanderResult result;
    result.code = code;
    result.message = map.value( QStringLiteral( "message" ) ).toString();
    return result;
}

bool writeCommanderResult( const QString& resultPath, const CommanderResult& result )
{
    QSaveFile resultFile{ resultPath };
    if ( !resultFile.open( QIODevice::WriteOnly ) ) {
        return false;
    }

    const auto payload = QJsonDocument::fromVariant( commanderResultToVariantMap( result ) ).toJson(
        QJsonDocument::Compact );
    const auto written = resultFile.write( payload );
    if ( written != payload.size() ) {
        resultFile.cancelWriting();
        return false;
    }

    return resultFile.commit();
}

std::optional<CommanderResult> readCommanderResult( const QString& resultPath,
                                                    QString* errorMessage )
{
    QFile resultFile{ resultPath };
    if ( !resultFile.open( QIODevice::ReadOnly ) ) {
        setError( errorMessage, resultFile.errorString() );
        return std::nullopt;
    }

    const auto payload = resultFile.readAll();
    resultFile.close();

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson( payload, &parseError );
    if ( parseError.error != QJsonParseError::NoError || !document.isObject() ) {
        setError( errorMessage, QStringLiteral( "Invalid commander response payload." ) );
        return std::nullopt;
    }

    return commanderResultFromVariantMap( document.toVariant().toMap(), errorMessage );
}

CommanderResult commanderSuccess( const QString& message )
{
    return { CommanderResultCode::Success, message };
}

CommanderResult commanderFailure( CommanderResultCode code, const QString& message )
{
    return { code, message };
}
