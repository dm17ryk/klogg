#include "scenarioheadlessrunner.h"

#include "commander.h"
#include "kloggapp.h"
#include "log.h"
#include "mainwindow.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QXmlStreamWriter>

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

namespace {
constexpr int DeviceOpenRetryCount = 3;
constexpr int DeviceOpenRetryDelayMs = 1000;

struct LogicalDeviceDefinition {
    QString name;
    CommanderComSettings settings;
};

struct SuiteScenarioEntry {
    QString scenarioFilePath;
    QString argsJsonFilePath;
    bool enabled = true;
    QStringList requiredDevices;
};

struct SuiteDefinition {
    QString suiteFilePath;
    QString suiteId;
    QString suiteName;
    QList<LogicalDeviceDefinition> devices;
    QList<SuiteScenarioEntry> scenarios;
    QStringList requiredDevices;
};

struct ResolvedDeviceBinding {
    QString logicalName;
    CommanderComSettings settings;
    QString captureFilePath;
    QString tabId;
    int windowIndex = -1;
    int tabIndex = -1;
    QString displayName;
    QString filePath;
};

struct LoadJsonResult {
    QJsonObject object;
    QString error;
};

QString absolutePath( const QString& baseDir, const QString& value )
{
    if ( value.isEmpty() ) {
        return {};
    }

    const QFileInfo fileInfo( value );
    return fileInfo.isAbsolute() ? fileInfo.absoluteFilePath()
                                 : QFileInfo( QDir( baseDir ).filePath( value ) ).absoluteFilePath();
}

QString sanitizePathPart( const QString& value )
{
    auto sanitized = value;
    sanitized.replace( QRegularExpression( QStringLiteral( "[^A-Za-z0-9._-]+" ) ),
                       QStringLiteral( "_" ) );
    if ( sanitized.isEmpty() ) {
        sanitized = QStringLiteral( "device" );
    }
    return sanitized;
}

QString optionalString( const QJsonObject& object, const QString& key )
{
    const auto value = object.value( key );
    return value.isString() ? value.toString().trimmed() : QString{};
}

std::optional<bool> optionalBool( const QJsonObject& object, const QString& key )
{
    const auto value = object.value( key );
    return value.isBool() ? std::make_optional( value.toBool() ) : std::nullopt;
}

std::optional<int> optionalInt( const QJsonObject& object, const QString& key )
{
    const auto value = object.value( key );
    return value.isDouble() ? std::make_optional( value.toInt() ) : std::nullopt;
}

std::optional<QSerialPort::DataBits> parseDataBitsValue( const QJsonObject& object,
                                                         const QString& key,
                                                         QStringList* errors,
                                                         const QString& context )
{
    const auto value = object.value( key );
    if ( value.isUndefined() || value.isNull() ) {
        return std::nullopt;
    }

    switch ( value.toInt( -1 ) ) {
    case 5:
        return QSerialPort::Data5;
    case 6:
        return QSerialPort::Data6;
    case 7:
        return QSerialPort::Data7;
    case 8:
        return QSerialPort::Data8;
    default:
        errors->push_back( QObject::tr( "%1 has invalid dataBits value." ).arg( context ) );
        return std::nullopt;
    }
}

std::optional<QSerialPort::Parity> parseParityValue( const QJsonObject& object,
                                                     const QString& key,
                                                     QStringList* errors,
                                                     const QString& context )
{
    const auto raw = optionalString( object, key ).toLower();
    if ( raw.isEmpty() ) {
        return std::nullopt;
    }
    if ( raw == QStringLiteral( "none" ) ) {
        return QSerialPort::NoParity;
    }
    if ( raw == QStringLiteral( "even" ) ) {
        return QSerialPort::EvenParity;
    }
    if ( raw == QStringLiteral( "odd" ) ) {
        return QSerialPort::OddParity;
    }
    if ( raw == QStringLiteral( "mark" ) ) {
        return QSerialPort::MarkParity;
    }
    if ( raw == QStringLiteral( "space" ) ) {
        return QSerialPort::SpaceParity;
    }
    errors->push_back( QObject::tr( "%1 has invalid parity value." ).arg( context ) );
    return std::nullopt;
}

std::optional<QSerialPort::StopBits> parseStopBitsValue( const QJsonObject& object,
                                                         const QString& key,
                                                         QStringList* errors,
                                                         const QString& context )
{
    const auto raw = optionalString( object, key ).toLower();
    if ( raw.isEmpty() ) {
        return std::nullopt;
    }
    if ( raw == QStringLiteral( "1" ) ) {
        return QSerialPort::OneStop;
    }
    if ( raw == QStringLiteral( "1.5" ) ) {
        return QSerialPort::OneAndHalfStop;
    }
    if ( raw == QStringLiteral( "2" ) ) {
        return QSerialPort::TwoStop;
    }
    errors->push_back( QObject::tr( "%1 has invalid stopBits value." ).arg( context ) );
    return std::nullopt;
}

std::optional<QSerialPort::FlowControl> parseFlowControlValue( const QJsonObject& object,
                                                               const QString& key,
                                                               QStringList* errors,
                                                               const QString& context )
{
    const auto raw = optionalString( object, key ).toLower();
    if ( raw.isEmpty() ) {
        return std::nullopt;
    }
    if ( raw == QStringLiteral( "none" ) ) {
        return QSerialPort::NoFlowControl;
    }
    if ( raw == QStringLiteral( "hardware" ) || raw == QStringLiteral( "rts/cts" ) ) {
        return QSerialPort::HardwareControl;
    }
    if ( raw == QStringLiteral( "software" ) || raw == QStringLiteral( "xon/xoff" ) ) {
        return QSerialPort::SoftwareControl;
    }
    errors->push_back( QObject::tr( "%1 has invalid flowControl value." ).arg( context ) );
    return std::nullopt;
}

CommanderComSettings parseComSettings( const QJsonObject& object, QStringList* errors,
                                       const QString& context, bool allowPortName )
{
    CommanderComSettings settings;
    if ( allowPortName ) {
        const auto portName = optionalString( object, QStringLiteral( "portName" ) );
        const auto portAlias = optionalString( object, QStringLiteral( "port" ) );
        settings.portName = !portName.isEmpty() ? portName : portAlias;
    }

    const auto filePath = optionalString( object, QStringLiteral( "filePath" ) );
    if ( !filePath.isEmpty() ) {
        settings.filePath = filePath;
    }
    if ( const auto baudRate = optionalInt( object, QStringLiteral( "baudRate" ) ) ) {
        settings.baudRate = *baudRate;
    }
    settings.dataBits = parseDataBitsValue( object, QStringLiteral( "dataBits" ), errors, context );
    settings.parity = parseParityValue( object, QStringLiteral( "parity" ), errors, context );
    settings.stopBits = parseStopBitsValue( object, QStringLiteral( "stopBits" ), errors, context );
    settings.flowControl
        = parseFlowControlValue( object, QStringLiteral( "flowControl" ), errors, context );

    if ( const auto addTimestamps = optionalBool( object, QStringLiteral( "addTimestamps" ) ) ) {
        settings.addTimestamps = *addTimestamps;
    }
    else if ( const auto legacyTimestamps = optionalBool( object, QStringLiteral( "timestamps" ) ) ) {
        settings.addTimestamps = *legacyTimestamps;
    }

    const auto timestampFormat = optionalString( object, QStringLiteral( "timestampFormat" ) );
    if ( !timestampFormat.isEmpty() ) {
        settings.timestampFormat = timestampFormat;
    }
    if ( const auto logTransmits = optionalBool( object, QStringLiteral( "logTransmits" ) ) ) {
        settings.logTransmits = *logTransmits;
    }
    if ( const auto useForActions = optionalBool( object, QStringLiteral( "useForActions" ) ) ) {
        settings.useForActions = *useForActions;
    }

    return settings;
}

CommanderComSettings mergeSettings( const CommanderComSettings& base,
                                    const CommanderComSettings& overrides )
{
    CommanderComSettings merged = base;
    if ( !overrides.portName.isEmpty() ) {
        merged.portName = overrides.portName;
    }
    if ( overrides.filePath ) {
        merged.filePath = overrides.filePath;
    }
    if ( overrides.baudRate ) {
        merged.baudRate = overrides.baudRate;
    }
    if ( overrides.dataBits ) {
        merged.dataBits = overrides.dataBits;
    }
    if ( overrides.parity ) {
        merged.parity = overrides.parity;
    }
    if ( overrides.stopBits ) {
        merged.stopBits = overrides.stopBits;
    }
    if ( overrides.flowControl ) {
        merged.flowControl = overrides.flowControl;
    }
    if ( overrides.addTimestamps ) {
        merged.addTimestamps = overrides.addTimestamps;
    }
    if ( overrides.timestampFormat ) {
        merged.timestampFormat = overrides.timestampFormat;
    }
    if ( overrides.logTransmits ) {
        merged.logTransmits = overrides.logTransmits;
    }
    if ( overrides.useForActions ) {
        merged.useForActions = overrides.useForActions;
    }
    return merged;
}

QString parityToString( QSerialPort::Parity parity )
{
    switch ( parity ) {
    case QSerialPort::NoParity:
        return QStringLiteral( "none" );
    case QSerialPort::EvenParity:
        return QStringLiteral( "even" );
    case QSerialPort::OddParity:
        return QStringLiteral( "odd" );
    case QSerialPort::SpaceParity:
        return QStringLiteral( "space" );
    case QSerialPort::MarkParity:
        return QStringLiteral( "mark" );
    }
    return QStringLiteral( "unknown" );
}

QString stopBitsToString( QSerialPort::StopBits stopBits )
{
    switch ( stopBits ) {
    case QSerialPort::OneStop:
        return QStringLiteral( "1" );
    case QSerialPort::OneAndHalfStop:
        return QStringLiteral( "1.5" );
    case QSerialPort::TwoStop:
        return QStringLiteral( "2" );
    }
    return QStringLiteral( "unknown" );
}

QString flowControlToString( QSerialPort::FlowControl flowControl )
{
    switch ( flowControl ) {
    case QSerialPort::NoFlowControl:
        return QStringLiteral( "none" );
    case QSerialPort::HardwareControl:
        return QStringLiteral( "hardware" );
    case QSerialPort::SoftwareControl:
        return QStringLiteral( "software" );
    }
    return QStringLiteral( "unknown" );
}

QVariantMap settingsToVariantMap( const CommanderComSettings& settings )
{
    QVariantMap map;
    if ( !settings.portName.isEmpty() ) {
        map.insert( QStringLiteral( "portName" ), settings.portName );
    }
    if ( settings.filePath ) {
        map.insert( QStringLiteral( "filePath" ), *settings.filePath );
    }
    if ( settings.baudRate ) {
        map.insert( QStringLiteral( "baudRate" ), *settings.baudRate );
    }
    if ( settings.dataBits ) {
        switch ( *settings.dataBits ) {
        case QSerialPort::Data5:
            map.insert( QStringLiteral( "dataBits" ), 5 );
            break;
        case QSerialPort::Data6:
            map.insert( QStringLiteral( "dataBits" ), 6 );
            break;
        case QSerialPort::Data7:
            map.insert( QStringLiteral( "dataBits" ), 7 );
            break;
        case QSerialPort::Data8:
            map.insert( QStringLiteral( "dataBits" ), 8 );
            break;
        }
    }
    if ( settings.parity ) {
        map.insert( QStringLiteral( "parity" ), parityToString( *settings.parity ) );
    }
    if ( settings.stopBits ) {
        map.insert( QStringLiteral( "stopBits" ), stopBitsToString( *settings.stopBits ) );
    }
    if ( settings.flowControl ) {
        map.insert( QStringLiteral( "flowControl" ), flowControlToString( *settings.flowControl ) );
    }
    if ( settings.addTimestamps ) {
        map.insert( QStringLiteral( "addTimestamps" ), *settings.addTimestamps );
    }
    if ( settings.timestampFormat ) {
        map.insert( QStringLiteral( "timestampFormat" ), *settings.timestampFormat );
    }
    if ( settings.logTransmits ) {
        map.insert( QStringLiteral( "logTransmits" ), *settings.logTransmits );
    }
    if ( settings.useForActions ) {
        map.insert( QStringLiteral( "useForActions" ), *settings.useForActions );
    }
    return map;
}

LoadJsonResult loadJsonObject( const QString& path )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        return { {}, QObject::tr( "Failed to open %1: %2" ).arg( path, file.errorString() ) };
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson( file.readAll(), &parseError );
    if ( parseError.error != QJsonParseError::NoError || !document.isObject() ) {
        return { {}, QObject::tr( "Invalid JSON object in %1." ).arg( path ) };
    }

    return { document.object(), {} };
}

std::optional<SuiteDefinition> loadSuiteDefinition( const QString& suiteFilePath, QStringList* errors )
{
    const auto loadResult = loadJsonObject( suiteFilePath );
    if ( !loadResult.error.isEmpty() ) {
        errors->push_back( loadResult.error );
        return std::nullopt;
    }

    const QFileInfo suiteInfo( suiteFilePath );
    const auto suiteObject = loadResult.object;

    SuiteDefinition suite;
    suite.suiteFilePath = suiteInfo.absoluteFilePath();
    suite.suiteId = optionalString( suiteObject, QStringLiteral( "suiteId" ) );
    suite.suiteName = optionalString( suiteObject, QStringLiteral( "name" ) );
    if ( suite.suiteId.isEmpty() ) {
        suite.suiteId = suiteInfo.completeBaseName();
    }
    if ( suite.suiteName.isEmpty() ) {
        suite.suiteName = suiteInfo.completeBaseName();
    }

    const auto devicesValue = suiteObject.value( QStringLiteral( "devices" ) );
    if ( devicesValue.isObject() ) {
        const auto devicesObject = devicesValue.toObject();
        for ( auto it = devicesObject.begin(); it != devicesObject.end(); ++it ) {
            if ( !it.value().isObject() ) {
                errors->push_back(
                    QObject::tr( "Device %1 in %2 must be a JSON object." ).arg( it.key(), suiteFilePath ) );
                continue;
            }

            LogicalDeviceDefinition device;
            device.name = it.key();
            device.settings = parseComSettings( it.value().toObject(), errors,
                                                QStringLiteral( "Device %1" ).arg( it.key() ),
                                                false );
            suite.devices.push_back( device );
        }
    }

    const auto scenariosValue = suiteObject.value( QStringLiteral( "scenarios" ) );
    if ( !scenariosValue.isArray() ) {
        errors->push_back( QObject::tr( "Suite %1 must contain a scenarios array." ).arg( suiteFilePath ) );
        return std::nullopt;
    }

    const auto suiteBaseDir = suiteInfo.absolutePath();
    for ( const auto& scenarioValue : scenariosValue.toArray() ) {
        if ( !scenarioValue.isObject() ) {
            errors->push_back(
                QObject::tr( "Each scenario entry in %1 must be a JSON object." ).arg( suiteFilePath ) );
            continue;
        }

        const auto object = scenarioValue.toObject();
        SuiteScenarioEntry entry;
        entry.scenarioFilePath = absolutePath(
            suiteBaseDir, optionalString( object, QStringLiteral( "scenarioFile" ) ) );
        entry.argsJsonFilePath
            = absolutePath( suiteBaseDir, optionalString( object, QStringLiteral( "argsJsonFile" ) ) );
        entry.enabled = !object.contains( QStringLiteral( "enabled" ) )
                        || object.value( QStringLiteral( "enabled" ) ).toBool( true );

        if ( entry.scenarioFilePath.isEmpty() ) {
            errors->push_back(
                QObject::tr( "Suite %1 contains a scenario entry without scenarioFile." ).arg( suiteFilePath ) );
            continue;
        }
        if ( !QFileInfo::exists( entry.scenarioFilePath ) ) {
            errors->push_back(
                QObject::tr( "Scenario file %1 was not found." ).arg( entry.scenarioFilePath ) );
        }
        if ( !entry.argsJsonFilePath.isEmpty() && !QFileInfo::exists( entry.argsJsonFilePath ) ) {
            errors->push_back(
                QObject::tr( "Scenario args file %1 was not found." ).arg( entry.argsJsonFilePath ) );
        }

        const auto requiredDevicesValue = object.value( QStringLiteral( "requiredDevices" ) );
        if ( requiredDevicesValue.isArray() ) {
            for ( const auto& value : requiredDevicesValue.toArray() ) {
                if ( value.isString() ) {
                    entry.requiredDevices.push_back( value.toString().trimmed() );
                }
            }
        }
        suite.scenarios.push_back( entry );
    }

    for ( const auto& entry : suite.scenarios ) {
        for ( const auto& deviceName : entry.requiredDevices ) {
            suite.requiredDevices.push_back( deviceName );
            const auto declared = std::any_of(
                suite.devices.cbegin(), suite.devices.cend(),
                [ &deviceName ]( const LogicalDeviceDefinition& device ) { return device.name == deviceName; } );
            if ( !declared ) {
                errors->push_back(
                    QObject::tr( "Scenario file %1 references undeclared logical device %2." )
                        .arg( entry.scenarioFilePath, deviceName ) );
            }
        }
    }

    if ( suite.requiredDevices.isEmpty() ) {
        for ( const auto& device : suite.devices ) {
            suite.requiredDevices.push_back( device.name );
        }
    }
    suite.requiredDevices.removeDuplicates();
    return suite;
}

std::optional<QMap<QString, CommanderComSettings>> loadDeviceMap( const QString& deviceMapFilePath,
                                                                  QStringList* errors )
{
    if ( deviceMapFilePath.isEmpty() ) {
        return QMap<QString, CommanderComSettings>{};
    }

    const auto loadResult = loadJsonObject( deviceMapFilePath );
    if ( !loadResult.error.isEmpty() ) {
        errors->push_back( loadResult.error );
        return std::nullopt;
    }

    QJsonObject devicesObject = loadResult.object;
    if ( const auto nested = loadResult.object.value( QStringLiteral( "devices" ) ); nested.isObject() ) {
        devicesObject = nested.toObject();
    }

    QMap<QString, CommanderComSettings> deviceMap;
    for ( auto it = devicesObject.begin(); it != devicesObject.end(); ++it ) {
        CommanderComSettings settings;
        if ( it.value().isString() ) {
            settings.portName = it.value().toString().trimmed();
        }
        else if ( it.value().isObject() ) {
            settings = parseComSettings( it.value().toObject(), errors,
                                         QStringLiteral( "Device map entry %1" ).arg( it.key() ), true );
        }
        else {
            errors->push_back(
                QObject::tr( "Device map entry %1 must be a string or JSON object." ).arg( it.key() ) );
            continue;
        }

        if ( settings.portName.isEmpty() ) {
            errors->push_back(
                QObject::tr( "Device map entry %1 is missing a port name." ).arg( it.key() ) );
        }
        deviceMap.insert( it.key(), settings );
    }
    return deviceMap;
}

QVariantMap buildDeviceBindingPayload( const ResolvedDeviceBinding& binding )
{
    QVariantMap payload;
    payload.insert( QStringLiteral( "logicalName" ), binding.logicalName );
    payload.insert( QStringLiteral( "portName" ), binding.settings.portName );
    payload.insert( QStringLiteral( "tabId" ), binding.tabId );
    payload.insert( QStringLiteral( "windowIndex" ), binding.windowIndex );
    payload.insert( QStringLiteral( "tabIndex" ), binding.tabIndex );
    payload.insert( QStringLiteral( "displayName" ), binding.displayName );
    payload.insert( QStringLiteral( "filePath" ), binding.filePath );
    payload.insert( QStringLiteral( "captureFilePath" ), binding.captureFilePath );
    payload.insert( QStringLiteral( "settings" ), settingsToVariantMap( binding.settings ) );
    return payload;
}

QVariantMap buildHeadlessFailureReport( const QString& suiteId, const QString& suiteName,
                                        const QString& reportJsonPath, const QString& reportJunitPath,
                                        const QString& errorText,
                                        const QVariantList& deviceBindings )
{
    QVariantMap counts;
    counts.insert( QStringLiteral( "total" ), 0 );
    counts.insert( QStringLiteral( "passed" ), 0 );
    counts.insert( QStringLiteral( "failed" ), 1 );
    counts.insert( QStringLiteral( "skipped" ), 0 );
    counts.insert( QStringLiteral( "cancelled" ), 0 );

    QVariantMap infrastructure;
    infrastructure.insert( QStringLiteral( "status" ), QStringLiteral( "failed" ) );
    infrastructure.insert( QStringLiteral( "error" ), errorText );
    infrastructure.insert( QStringLiteral( "deviceBindings" ), deviceBindings );

    QVariantMap report;
    report.insert( QStringLiteral( "suiteId" ), suiteId );
    report.insert( QStringLiteral( "suiteName" ), suiteName );
    report.insert( QStringLiteral( "status" ), QStringLiteral( "failed" ) );
    report.insert( QStringLiteral( "startedAt" ),
                   QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs ) );
    report.insert( QStringLiteral( "finishedAt" ),
                   QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs ) );
    report.insert( QStringLiteral( "counts" ), counts );
    report.insert( QStringLiteral( "scenarios" ), QVariantList{} );
    report.insert( QStringLiteral( "infrastructure" ), infrastructure );
    report.insert( QStringLiteral( "deviceBindings" ), deviceBindings );
    report.insert( QStringLiteral( "reportJsonFile" ), reportJsonPath );
    report.insert( QStringLiteral( "reportJunitFile" ), reportJunitPath );
    return report;
}

bool writeJsonReport( const QString& path, const QVariantMap& payload, QString* errorMessage )
{
    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
        if ( errorMessage != nullptr ) {
            *errorMessage
                = QObject::tr( "Failed to write report file %1: %2" ).arg( path, file.errorString() );
        }
        return false;
    }
    file.write( QJsonDocument::fromVariant( payload ).toJson( QJsonDocument::Indented ) );
    return true;
}

bool writeJunitFailureReport( const QString& path, const QString& suiteName, const QString& errorText,
                              QString* errorMessage )
{
    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
        if ( errorMessage != nullptr ) {
            *errorMessage
                = QObject::tr( "Failed to write report file %1: %2" ).arg( path, file.errorString() );
        }
        return false;
    }

    QXmlStreamWriter xml( &file );
    xml.setAutoFormatting( true );
    xml.writeStartDocument();
    xml.writeStartElement( QStringLiteral( "testsuite" ) );
    xml.writeAttribute( QStringLiteral( "name" ), suiteName );
    xml.writeAttribute( QStringLiteral( "tests" ), QStringLiteral( "1" ) );
    xml.writeAttribute( QStringLiteral( "failures" ), QStringLiteral( "1" ) );
    xml.writeAttribute( QStringLiteral( "skipped" ), QStringLiteral( "0" ) );
    xml.writeStartElement( QStringLiteral( "testcase" ) );
    xml.writeAttribute( QStringLiteral( "name" ), QStringLiteral( "Infrastructure" ) );
    xml.writeAttribute( QStringLiteral( "classname" ), suiteName );
    xml.writeStartElement( QStringLiteral( "failure" ) );
    xml.writeAttribute( QStringLiteral( "message" ), errorText );
    xml.writeCharacters( errorText );
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndDocument();
    return true;
}

bool isTerminalScenarioState( const QString& state )
{
    return state == QStringLiteral( "finished" ) || state == QStringLiteral( "failed" )
           || state == QStringLiteral( "cancelled" );
}
} // namespace

ScenarioHeadlessRunner::ScenarioHeadlessRunner( KloggApp& app )
    : app_( app )
{
}

ScenarioHeadlessRunner::Result ScenarioHeadlessRunner::run( const ScenarioBatchRequest& request )
{
    Result result;
    QStringList validationErrors;

    std::optional<SuiteDefinition> suite;
    if ( !request.suiteFilePath.isEmpty() ) {
        suite = loadSuiteDefinition( request.suiteFilePath, &validationErrors );
    }
    else if ( request.action != ScenarioBatchAction::Run ) {
        validationErrors.push_back( QObject::tr( "Suite file is required." ) );
    }

    if ( request.action == ScenarioBatchAction::Run && request.suiteFilePath.isEmpty() ) {
        if ( request.scenarioFilePath.isEmpty() ) {
            validationErrors.push_back(
                QObject::tr( "Scenario run requires a scenario file or suite file." ) );
        }
        else if ( !QFileInfo::exists( request.scenarioFilePath ) ) {
            validationErrors.push_back(
                QObject::tr( "Scenario file %1 was not found." ).arg( request.scenarioFilePath ) );
        }

        if ( !request.argsJsonFilePath.isEmpty() && !QFileInfo::exists( request.argsJsonFilePath ) ) {
            validationErrors.push_back(
                QObject::tr( "Scenario args file %1 was not found." ).arg( request.argsJsonFilePath ) );
        }
    }

    const auto deviceMap = loadDeviceMap( request.deviceMapFilePath, &validationErrors );
    if ( !deviceMap ) {
        result.exitCode = 2;
        result.outputToStderr = true;
        result.message = validationErrors.join( QLatin1Char( '\n' ) );
        return result;
    }

    QStringList requiredDevices;
    if ( suite ) {
        requiredDevices = suite->requiredDevices;
    }
    else {
        requiredDevices = deviceMap->keys();
    }

    for ( const auto& deviceName : requiredDevices ) {
        if ( !deviceMap->contains( deviceName ) ) {
            validationErrors.push_back(
                QObject::tr( "Missing device-map entry for logical device %1." ).arg( deviceName ) );
        }
    }

    if ( !validationErrors.isEmpty() ) {
        result.exitCode = 2;
        result.outputToStderr = true;
        result.message = validationErrors.join( QLatin1Char( '\n' ) );
        if ( request.action == ScenarioBatchAction::Validate ) {
            result.payload.insert( QStringLiteral( "valid" ), false );
            QVariantList errorsPayload;
            for ( const auto& error : validationErrors ) {
                errorsPayload.push_back( error );
            }
            result.payload.insert( QStringLiteral( "errors" ), errorsPayload );
        }
        return result;
    }

    if ( request.action == ScenarioBatchAction::ListDevices ) {
        QVariantList devicesPayload;
        for ( const auto& device : suite->devices ) {
            QVariantMap item;
            item.insert( QStringLiteral( "name" ), device.name );
            item.insert( QStringLiteral( "settings" ), settingsToVariantMap( device.settings ) );
            devicesPayload.push_back( item );
        }
        result.exitCode = 0;
        result.payload.insert( QStringLiteral( "suiteId" ), suite->suiteId );
        result.payload.insert( QStringLiteral( "suiteName" ), suite->suiteName );
        result.payload.insert( QStringLiteral( "devices" ), devicesPayload );
        return result;
    }

    if ( request.action == ScenarioBatchAction::Validate ) {
        QVariantList devicesPayload;
        for ( const auto& deviceName : requiredDevices ) {
            QVariantMap item;
            item.insert( QStringLiteral( "name" ), deviceName );
            item.insert( QStringLiteral( "mappedPortName" ),
                         deviceMap->value( deviceName ).portName );
            devicesPayload.push_back( item );
        }
        result.exitCode = 0;
        result.payload.insert( QStringLiteral( "valid" ), true );
        result.payload.insert( QStringLiteral( "suiteId" ), suite->suiteId );
        result.payload.insert( QStringLiteral( "suiteName" ), suite->suiteName );
        result.payload.insert( QStringLiteral( "requiredDevices" ), devicesPayload );
        return result;
    }

    const auto reportRoot = request.reportDirPath.isEmpty()
                                ? QCoreApplication::applicationDirPath()
                                : QFileInfo( request.reportDirPath ).absoluteFilePath();
    QDir reportDir( reportRoot );
    reportDir.mkpath( QStringLiteral( "." ) );
    reportDir.mkpath( QStringLiteral( "captures" ) );
    const auto reportJsonPath = reportDir.filePath( QStringLiteral( "scenario-report.json" ) );
    const auto reportJunitPath
        = reportDir.filePath( QStringLiteral( "scenario-report.junit.xml" ) );

    QList<ResolvedDeviceBinding> resolvedBindings;
    if ( suite ) {
        for ( const auto& device : suite->devices ) {
            if ( !requiredDevices.contains( device.name ) ) {
                continue;
            }

            ResolvedDeviceBinding binding;
            binding.logicalName = device.name;
            binding.settings = mergeSettings( device.settings, deviceMap->value( device.name ) );
            binding.captureFilePath = reportDir.filePath(
                QStringLiteral( "captures/%1.log" ).arg( sanitizePathPart( device.name ) ) );
            binding.settings.filePath = binding.captureFilePath;
            resolvedBindings.push_back( binding );
        }
    }
    else {
        for ( auto it = deviceMap->cbegin(); it != deviceMap->cend(); ++it ) {
            ResolvedDeviceBinding binding;
            binding.logicalName = it.key();
            binding.settings = it.value();
            binding.captureFilePath = reportDir.filePath(
                QStringLiteral( "captures/%1.log" ).arg( sanitizePathPart( it.key() ) ) );
            binding.settings.filePath = binding.captureFilePath;
            resolvedBindings.push_back( binding );
        }
    }

    const auto failInfrastructure = [ & ]( const QString& errorText ) -> Result {
        QVariantList bindingsPayload;
        for ( const auto& binding : resolvedBindings ) {
            bindingsPayload.push_back( buildDeviceBindingPayload( binding ) );
        }

        const auto suiteId
            = suite ? suite->suiteId : QStringLiteral( "single-scenario" );
        const auto suiteName
            = suite ? suite->suiteName : QFileInfo( request.scenarioFilePath ).completeBaseName();
        const auto payload = buildHeadlessFailureReport( suiteId, suiteName, reportJsonPath,
                                                         reportJunitPath, errorText, bindingsPayload );
        QString writeError;
        writeJsonReport( reportJsonPath, payload, &writeError );
        writeJunitFailureReport( reportJunitPath, suiteName, errorText, &writeError );

        Result failure;
        failure.exitCode = 2;
        failure.outputToStderr = true;
        failure.message = errorText;
        failure.payload = payload;
        return failure;
    };

    std::vector<std::unique_ptr<QLockFile>> locks;
    const auto lockRoot = QDir( QStandardPaths::writableLocation( QStandardPaths::TempLocation ) )
                              .filePath( QStringLiteral( "cilogg-scenario-locks" ) );
    QDir().mkpath( lockRoot );
    for ( const auto& binding : resolvedBindings ) {
        auto lock = std::make_unique<QLockFile>(
            QDir( lockRoot ).filePath(
                QStringLiteral( "%1.lock" )
                    .arg( sanitizePathPart( binding.settings.portName.toLower() ) ) ) );
        lock->setStaleLockTime( 0 );
        if ( !lock->tryLock( 0 ) ) {
            return failInfrastructure(
                QObject::tr( "COM port %1 is already reserved by another process." )
                    .arg( binding.settings.portName ) );
        }
        locks.push_back( std::move( lock ) );
    }

    auto* headlessWindow = app_.newWindow();
    headlessWindow->hide();
    for ( auto& binding : resolvedBindings ) {
        CommanderRequest openRequest;
        openRequest.action = CommanderAction::OpenCom;
        openRequest.comSettings = binding.settings;

        CommanderResult openResult;
        for ( int attempt = 0; attempt < DeviceOpenRetryCount; ++attempt ) {
            openResult = headlessWindow->executeCommanderRequest( openRequest );
            if ( openResult.ok() ) {
                break;
            }
            if ( attempt + 1 < DeviceOpenRetryCount ) {
                QThread::msleep( DeviceOpenRetryDelayMs );
                QCoreApplication::processEvents( QEventLoop::ExcludeUserInputEvents );
            }
        }

        if ( !openResult.ok() ) {
            headlessWindow->close();
            return failInfrastructure(
                QObject::tr( "Failed to open COM port %1: %2" )
                    .arg( binding.settings.portName, openResult.message ) );
        }

        QCoreApplication::processEvents( QEventLoop::ExcludeUserInputEvents );
        const auto windowInfo = headlessWindow->commanderWindowInfo();
        const auto tabs = windowInfo.value( QStringLiteral( "tabs" ) ).toList();
        for ( const auto& tabValue : tabs ) {
            const auto tab = tabValue.toMap();
            if ( tab.value( QStringLiteral( "filePath" ) ).toString() != binding.captureFilePath ) {
                continue;
            }

            binding.tabId = tab.value( QStringLiteral( "tabId" ) ).toString();
            binding.tabIndex = tab.value( QStringLiteral( "tabIndex" ) ).toInt();
            if ( !tab.contains( QStringLiteral( "tabIndex" ) ) ) {
                binding.tabIndex = -1;
            }
            binding.windowIndex = windowInfo.value( QStringLiteral( "windowIndex" ) ).toInt();
            if ( !windowInfo.contains( QStringLiteral( "windowIndex" ) ) ) {
                binding.windowIndex = -1;
            }
            binding.displayName = tab.value( QStringLiteral( "displayName" ) ).toString();
            binding.filePath = tab.value( QStringLiteral( "filePath" ) ).toString();
            break;
        }

        if ( binding.tabId.isEmpty() ) {
            headlessWindow->close();
            return failInfrastructure(
                QObject::tr( "Failed to resolve a tab binding for COM port %1." )
                    .arg( binding.settings.portName ) );
        }
    }

    QVariantMap deviceBindingsMap;
    QVariantList deviceBindingsList;
    for ( const auto& binding : resolvedBindings ) {
        const auto payload = buildDeviceBindingPayload( binding );
        deviceBindingsMap.insert( binding.logicalName, payload );
        deviceBindingsList.push_back( payload );
    }

    const auto previousReportJson = qgetenv( "CILOGG_SCENARIO_REPORT_JSON" );
    const auto previousReportJunit = qgetenv( "CILOGG_SCENARIO_REPORT_JUNIT" );
    const auto previousBindings = qgetenv( "CILOGG_SCENARIO_DEVICE_BINDINGS_JSON" );
    qputenv( "CILOGG_SCENARIO_REPORT_JSON", reportJsonPath.toUtf8() );
    qputenv( "CILOGG_SCENARIO_REPORT_JUNIT", reportJunitPath.toUtf8() );
    qputenv( "CILOGG_SCENARIO_DEVICE_BINDINGS_JSON",
             QJsonDocument::fromVariant( deviceBindingsMap ).toJson( QJsonDocument::Compact ) );

    CommanderRequest runRequest;
    runRequest.action
        = request.suiteFilePath.isEmpty() ? CommanderAction::RunScenario : CommanderAction::RunSuite;
    runRequest.scenarioFilePath = request.scenarioFilePath;
    runRequest.suiteFilePath = request.suiteFilePath;
    runRequest.argsJsonFilePath = request.argsJsonFilePath;

    const auto runResult = app_.executeCommanderRequest( runRequest );
    if ( !runResult.ok() ) {
        headlessWindow->close();
        if ( previousReportJson.isEmpty() ) {
            qunsetenv( "CILOGG_SCENARIO_REPORT_JSON" );
        }
        else {
            qputenv( "CILOGG_SCENARIO_REPORT_JSON", previousReportJson );
        }
        if ( previousReportJunit.isEmpty() ) {
            qunsetenv( "CILOGG_SCENARIO_REPORT_JUNIT" );
        }
        else {
            qputenv( "CILOGG_SCENARIO_REPORT_JUNIT", previousReportJunit );
        }
        if ( previousBindings.isEmpty() ) {
            qunsetenv( "CILOGG_SCENARIO_DEVICE_BINDINGS_JSON" );
        }
        else {
            qputenv( "CILOGG_SCENARIO_DEVICE_BINDINGS_JSON", previousBindings );
        }
        return failInfrastructure( runResult.message );
    }

    while ( true ) {
        CommanderRequest statusRequest;
        statusRequest.action = CommanderAction::GetScenarioStatus;
        const auto statusResult = app_.executeCommanderRequest( statusRequest );
        if ( !statusResult.ok() ) {
            headlessWindow->close();
            return failInfrastructure( statusResult.message );
        }

        if ( isTerminalScenarioState( statusResult.payload.value( QStringLiteral( "state" ) ).toString() ) ) {
            break;
        }

        QEventLoop waitLoop;
        QTimer::singleShot( 100, &waitLoop, &QEventLoop::quit );
        waitLoop.exec( QEventLoop::ExcludeUserInputEvents );
    }

    if ( previousReportJson.isEmpty() ) {
        qunsetenv( "CILOGG_SCENARIO_REPORT_JSON" );
    }
    else {
        qputenv( "CILOGG_SCENARIO_REPORT_JSON", previousReportJson );
    }
    if ( previousReportJunit.isEmpty() ) {
        qunsetenv( "CILOGG_SCENARIO_REPORT_JUNIT" );
    }
    else {
        qputenv( "CILOGG_SCENARIO_REPORT_JUNIT", previousReportJunit );
    }
    if ( previousBindings.isEmpty() ) {
        qunsetenv( "CILOGG_SCENARIO_DEVICE_BINDINGS_JSON" );
    }
    else {
        qputenv( "CILOGG_SCENARIO_DEVICE_BINDINGS_JSON", previousBindings );
    }

    CommanderRequest reportRequest;
    reportRequest.action = CommanderAction::GetScenarioReport;
    const auto reportResult = app_.executeCommanderRequest( reportRequest );

    QVariantMap reportPayload;
    if ( reportResult.ok() ) {
        reportPayload = reportResult.payload;
    }
    else {
        reportPayload = buildHeadlessFailureReport(
            suite ? suite->suiteId : QStringLiteral( "single-scenario" ),
            suite ? suite->suiteName : QFileInfo( request.scenarioFilePath ).completeBaseName(),
            reportJsonPath, reportJunitPath,
            QObject::tr( "Scenario report is not available after run completion." ),
            deviceBindingsList );
    }

    reportPayload.insert( QStringLiteral( "deviceBindings" ), deviceBindingsList );
    QVariantMap infrastructure = reportPayload.value( QStringLiteral( "infrastructure" ) ).toMap();
    if ( infrastructure.isEmpty() ) {
        infrastructure.insert( QStringLiteral( "status" ), QStringLiteral( "ok" ) );
    }
    infrastructure.insert( QStringLiteral( "deviceBindings" ), deviceBindingsList );
    infrastructure.insert( QStringLiteral( "reportDir" ), reportRoot );
    reportPayload.insert( QStringLiteral( "infrastructure" ), infrastructure );
    reportPayload.insert( QStringLiteral( "reportJsonFile" ), reportJsonPath );
    reportPayload.insert( QStringLiteral( "reportJunitFile" ), reportJunitPath );

    QString writeError;
    writeJsonReport( reportJsonPath, reportPayload, &writeError );
    headlessWindow->close();

    result.payload = reportPayload;
    result.exitCode = reportPayload.value( QStringLiteral( "status" ) ).toString()
                              == QStringLiteral( "failed" )
                          ? 1
                          : 0;
    return result;
}
