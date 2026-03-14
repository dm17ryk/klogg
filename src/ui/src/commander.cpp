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
    case CommanderAction::CloseKlogg:
        return QStringLiteral( "close_klogg" );
    case CommanderAction::CloseAll:
        return QStringLiteral( "close_all" );
    case CommanderAction::GetInfo:
        return QStringLiteral( "get_info" );
    case CommanderAction::GetFilters:
        return QStringLiteral( "get_filters" );
    case CommanderAction::GetActions:
        return QStringLiteral( "get_actions" );
    case CommanderAction::GetResponses:
        return QStringLiteral( "get_responses" );
    case CommanderAction::CreateAction:
        return QStringLiteral( "create_action" );
    case CommanderAction::UpdateAction:
        return QStringLiteral( "update_action" );
    case CommanderAction::DeleteAction:
        return QStringLiteral( "delete_action" );
    case CommanderAction::CreateResponse:
        return QStringLiteral( "create_response" );
    case CommanderAction::UpdateResponse:
        return QStringLiteral( "update_response" );
    case CommanderAction::DeleteResponse:
        return QStringLiteral( "delete_response" );
    case CommanderAction::SendAction:
        return QStringLiteral( "send_action" );
    case CommanderAction::WaitResponse:
        return QStringLiteral( "wait_response" );
    case CommanderAction::StartComm:
        return QStringLiteral( "start_comm" );
    case CommanderAction::StopComm:
        return QStringLiteral( "stop_comm" );
    case CommanderAction::GetCommStatus:
        return QStringLiteral( "get_comm_status" );
    case CommanderAction::StartLogging:
        return QStringLiteral( "start_logging" );
    case CommanderAction::StopLogging:
        return QStringLiteral( "stop_logging" );
    case CommanderAction::AddComment:
        return QStringLiteral( "add_comment" );
    case CommanderAction::GetResponseCounter:
        return QStringLiteral( "get_response_counter" );
    case CommanderAction::ResetResponseCounter:
        return QStringLiteral( "reset_response_counter" );
    case CommanderAction::ClearComm:
        return QStringLiteral( "clear_comm" );
    case CommanderAction::FocusTab:
        return QStringLiteral( "focus_tab" );
    case CommanderAction::SetFilter:
        return QStringLiteral( "set_filter" );
    case CommanderAction::CloseTab:
        return QStringLiteral( "close_tab" );
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
    if ( normalized == QStringLiteral( "close_klogg" ) ) {
        return CommanderAction::CloseKlogg;
    }
    if ( normalized == QStringLiteral( "close_all" ) ) {
        return CommanderAction::CloseAll;
    }
    if ( normalized == QStringLiteral( "get_info" ) ) {
        return CommanderAction::GetInfo;
    }
    if ( normalized == QStringLiteral( "get_filters" ) ) {
        return CommanderAction::GetFilters;
    }
    if ( normalized == QStringLiteral( "get_actions" ) ) {
        return CommanderAction::GetActions;
    }
    if ( normalized == QStringLiteral( "get_responses" ) ) {
        return CommanderAction::GetResponses;
    }
    if ( normalized == QStringLiteral( "create_action" ) ) {
        return CommanderAction::CreateAction;
    }
    if ( normalized == QStringLiteral( "update_action" ) ) {
        return CommanderAction::UpdateAction;
    }
    if ( normalized == QStringLiteral( "delete_action" ) ) {
        return CommanderAction::DeleteAction;
    }
    if ( normalized == QStringLiteral( "create_response" ) ) {
        return CommanderAction::CreateResponse;
    }
    if ( normalized == QStringLiteral( "update_response" ) ) {
        return CommanderAction::UpdateResponse;
    }
    if ( normalized == QStringLiteral( "delete_response" ) ) {
        return CommanderAction::DeleteResponse;
    }
    if ( normalized == QStringLiteral( "send_action" ) ) {
        return CommanderAction::SendAction;
    }
    if ( normalized == QStringLiteral( "wait_response" ) ) {
        return CommanderAction::WaitResponse;
    }
    if ( normalized == QStringLiteral( "start_comm" ) ) {
        return CommanderAction::StartComm;
    }
    if ( normalized == QStringLiteral( "stop_comm" ) ) {
        return CommanderAction::StopComm;
    }
    if ( normalized == QStringLiteral( "get_comm_status" ) ) {
        return CommanderAction::GetCommStatus;
    }
    if ( normalized == QStringLiteral( "start_logging" ) ) {
        return CommanderAction::StartLogging;
    }
    if ( normalized == QStringLiteral( "stop_logging" ) ) {
        return CommanderAction::StopLogging;
    }
    if ( normalized == QStringLiteral( "add_comment" ) ) {
        return CommanderAction::AddComment;
    }
    if ( normalized == QStringLiteral( "get_response_counter" ) ) {
        return CommanderAction::GetResponseCounter;
    }
    if ( normalized == QStringLiteral( "reset_response_counter" ) ) {
        return CommanderAction::ResetResponseCounter;
    }
    if ( normalized == QStringLiteral( "clear_comm" ) ) {
        return CommanderAction::ClearComm;
    }
    if ( normalized == QStringLiteral( "focus_tab" ) ) {
        return CommanderAction::FocusTab;
    }
    if ( normalized == QStringLiteral( "set_filter" ) ) {
        return CommanderAction::SetFilter;
    }
    if ( normalized == QStringLiteral( "close_tab" ) ) {
        return CommanderAction::CloseTab;
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
    if ( !request.tabId.isEmpty() ) {
        map.insert( QStringLiteral( "tabId" ), request.tabId );
    }
    if ( !request.filterId.isEmpty() ) {
        map.insert( QStringLiteral( "filterId" ), request.filterId );
    }
    if ( !request.filterString.isEmpty() ) {
        map.insert( QStringLiteral( "filterString" ), request.filterString );
    }
    if ( !request.entityName.isEmpty() ) {
        map.insert( QStringLiteral( "entityName" ), request.entityName );
    }
    if ( !request.commentText.isEmpty() ) {
        map.insert( QStringLiteral( "commentText" ), request.commentText );
    }
    if ( request.windowIndex ) {
        map.insert( QStringLiteral( "windowIndex" ), *request.windowIndex );
    }
    if ( request.tabIndex ) {
        map.insert( QStringLiteral( "tabIndex" ), *request.tabIndex );
    }
    if ( request.filterIndex ) {
        map.insert( QStringLiteral( "filterIndex" ), *request.filterIndex );
    }
    if ( request.entityId ) {
        map.insert( QStringLiteral( "entityId" ), *request.entityId );
    }
    if ( request.timeoutMs ) {
        map.insert( QStringLiteral( "timeoutMs" ), *request.timeoutMs );
    }
    if ( request.allEntities ) {
        map.insert( QStringLiteral( "allEntities" ), true );
    }
    if ( request.timestampComment ) {
        map.insert( QStringLiteral( "timestampComment" ), true );
    }
    if ( request.followFile ) {
        map.insert( QStringLiteral( "followFile" ), true );
    }
    if ( request.predefinedFilters ) {
        map.insert( QStringLiteral( "predefinedFilters" ), true );
    }
    if ( request.prettyOutput ) {
        map.insert( QStringLiteral( "prettyOutput" ), true );
    }
    if ( request.runSearch ) {
        map.insert( QStringLiteral( "runSearch" ), true );
    }
    if ( request.rearmAutoRefresh ) {
        map.insert( QStringLiteral( "rearmAutoRefresh" ), true );
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
    if ( !request.definitionPayload.isEmpty() ) {
        map.insert( QStringLiteral( "definitionPayload" ), request.definitionPayload );
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
    request.tabId = map.value( QStringLiteral( "tabId" ) ).toString();
    request.filterId = map.value( QStringLiteral( "filterId" ) ).toString();
    request.filterString = map.value( QStringLiteral( "filterString" ) ).toString();
    request.entityName = map.value( QStringLiteral( "entityName" ) ).toString();
    request.commentText = map.value( QStringLiteral( "commentText" ) ).toString();
    request.allEntities = map.value( QStringLiteral( "allEntities" ) ).toBool();
    request.timestampComment = map.value( QStringLiteral( "timestampComment" ) ).toBool();
    request.followFile = map.value( QStringLiteral( "followFile" ) ).toBool();
    request.predefinedFilters = map.value( QStringLiteral( "predefinedFilters" ) ).toBool();
    request.prettyOutput = map.value( QStringLiteral( "prettyOutput" ) ).toBool();
    request.runSearch = map.value( QStringLiteral( "runSearch" ) ).toBool();
    request.rearmAutoRefresh = map.value( QStringLiteral( "rearmAutoRefresh" ) ).toBool();

    const auto windowIndexIt = map.find( QStringLiteral( "windowIndex" ) );
    if ( windowIndexIt != map.end() ) {
        bool ok = false;
        const auto value = windowIndexIt->toInt( &ok );
        if ( !ok ) {
            setError( errorMessage, QStringLiteral( "Invalid commander window index." ) );
            return std::nullopt;
        }
        request.windowIndex = value;
    }

    const auto tabIndexIt = map.find( QStringLiteral( "tabIndex" ) );
    if ( tabIndexIt != map.end() ) {
        bool ok = false;
        const auto value = tabIndexIt->toInt( &ok );
        if ( !ok ) {
            setError( errorMessage, QStringLiteral( "Invalid commander tab index." ) );
            return std::nullopt;
        }
        request.tabIndex = value;
    }

    const auto filterIndexIt = map.find( QStringLiteral( "filterIndex" ) );
    if ( filterIndexIt != map.end() ) {
        bool ok = false;
        const auto value = filterIndexIt->toInt( &ok );
        if ( !ok ) {
            setError( errorMessage, QStringLiteral( "Invalid commander filter index." ) );
            return std::nullopt;
        }
        request.filterIndex = value;
    }

    const auto entityIdIt = map.find( QStringLiteral( "entityId" ) );
    if ( entityIdIt != map.end() ) {
        bool ok = false;
        const auto value = entityIdIt->toInt( &ok );
        if ( !ok ) {
            setError( errorMessage, QStringLiteral( "Invalid commander entity id." ) );
            return std::nullopt;
        }
        request.entityId = value;
    }

    const auto timeoutMsIt = map.find( QStringLiteral( "timeoutMs" ) );
    if ( timeoutMsIt != map.end() ) {
        bool ok = false;
        const auto value = timeoutMsIt->toInt( &ok );
        if ( !ok ) {
            setError( errorMessage, QStringLiteral( "Invalid commander timeout." ) );
            return std::nullopt;
        }
        request.timeoutMs = value;
    }

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

    const auto definitionPayloadIt = map.find( QStringLiteral( "definitionPayload" ) );
    if ( definitionPayloadIt != map.end() ) {
        if ( !definitionPayloadIt->canConvert<QVariantMap>() ) {
            setError( errorMessage, QStringLiteral( "Invalid commander definition payload." ) );
            return std::nullopt;
        }
        request.definitionPayload = definitionPayloadIt->toMap();
    }

    return request;
}

QVariantMap commanderResultToVariantMap( const CommanderResult& result )
{
    QVariantMap map;
    map.insert( QStringLiteral( "code" ), commanderResultCodeToString( result.code ) );
    map.insert( QStringLiteral( "message" ), result.message );
    if ( !result.payload.isEmpty() ) {
        map.insert( QStringLiteral( "payload" ), result.payload );
    }
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
    const auto payloadIt = map.find( QStringLiteral( "payload" ) );
    if ( payloadIt != map.end() ) {
        if ( !payloadIt->canConvert<QVariantMap>() ) {
            setError( errorMessage, QStringLiteral( "Invalid commander result payload." ) );
            return std::nullopt;
        }
        result.payload = payloadIt->toMap();
    }
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

CommanderResult commanderSuccess( const QString& message, const QVariantMap& payload )
{
    return { CommanderResultCode::Success, message, payload };
}

CommanderResult commanderFailure( CommanderResultCode code, const QString& message )
{
    return { code, message, {} };
}
