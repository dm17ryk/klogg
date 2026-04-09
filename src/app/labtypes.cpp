#include "labtypes.h"

#include <QByteArray>
#include <QMetaType>

namespace {

QVariantList toVariantList( const QStringList& values )
{
    QVariantList result;
    for ( const auto& value : values ) {
        result.push_back( value );
    }
    return result;
}

QStringList toStringList( const QVariant& value )
{
    QStringList result;
    for ( const auto& item : value.toList() ) {
        const auto text = item.toString().trimmed();
        if ( !text.isEmpty() ) {
            result.push_back( text );
        }
    }
    return result;
}

QVariantList toVariantList( const QList<LabLogicalDeviceDefinition>& values )
{
    QVariantList result;
    for ( const auto& value : values ) {
        result.push_back( labLogicalDeviceToVariantMap( value ) );
    }
    return result;
}

QVariantList toVariantList( const QList<LabAgentPortDefinition>& values )
{
    QVariantList result;
    for ( const auto& value : values ) {
        result.push_back( labAgentPortToVariantMap( value ) );
    }
    return result;
}

QVariantList toVariantList( const QList<LabBundleFile>& values )
{
    QVariantList result;
    for ( const auto& value : values ) {
        result.push_back( labBundleFileToVariantMap( value ) );
    }
    return result;
}

} // namespace

QVariantMap commanderComSettingsToVariantMap( const CommanderComSettings& settings )
{
    CommanderRequest request;
    request.comSettings = settings;
    return commanderRequestToVariantMap( request ).value( QStringLiteral( "comSettings" ) ).toMap();
}

CommanderComSettings commanderComSettingsFromVariantMap( const QVariantMap& map )
{
    CommanderComSettings settings;
    settings.portName = map.value( QStringLiteral( "portName" ),
                                   map.value( QStringLiteral( "port" ) ) )
                            .toString();
    if ( map.contains( QStringLiteral( "filePath" ) ) ) {
        settings.filePath = map.value( QStringLiteral( "filePath" ) ).toString();
    }
    if ( map.contains( QStringLiteral( "baudRate" ) ) ) {
        settings.baudRate = map.value( QStringLiteral( "baudRate" ) ).toInt();
    }
    if ( map.contains( QStringLiteral( "dataBits" ) ) ) {
        settings.dataBits
            = static_cast<QSerialPort::DataBits>( map.value( QStringLiteral( "dataBits" ) ).toInt() );
    }
    if ( map.contains( QStringLiteral( "parity" ) ) ) {
        const auto value = map.value( QStringLiteral( "parity" ) );
        if ( value.typeId() == QMetaType::QString ) {
            const auto parity = value.toString().trimmed().toLower();
            if ( parity == QStringLiteral( "none" ) ) {
                settings.parity = QSerialPort::NoParity;
            }
            else if ( parity == QStringLiteral( "even" ) ) {
                settings.parity = QSerialPort::EvenParity;
            }
            else if ( parity == QStringLiteral( "odd" ) ) {
                settings.parity = QSerialPort::OddParity;
            }
            else if ( parity == QStringLiteral( "mark" ) ) {
                settings.parity = QSerialPort::MarkParity;
            }
            else if ( parity == QStringLiteral( "space" ) ) {
                settings.parity = QSerialPort::SpaceParity;
            }
        }
        else {
            settings.parity = static_cast<QSerialPort::Parity>( value.toInt() );
        }
    }
    if ( map.contains( QStringLiteral( "stopBits" ) ) ) {
        const auto value = map.value( QStringLiteral( "stopBits" ) );
        if ( value.typeId() == QMetaType::QString ) {
            const auto stopBits = value.toString().trimmed();
            if ( stopBits == QStringLiteral( "1" ) ) {
                settings.stopBits = QSerialPort::OneStop;
            }
            else if ( stopBits == QStringLiteral( "1.5" ) ) {
                settings.stopBits = QSerialPort::OneAndHalfStop;
            }
            else if ( stopBits == QStringLiteral( "2" ) ) {
                settings.stopBits = QSerialPort::TwoStop;
            }
        }
        else {
            settings.stopBits = static_cast<QSerialPort::StopBits>( value.toInt() );
        }
    }
    if ( map.contains( QStringLiteral( "flowControl" ) ) ) {
        const auto value = map.value( QStringLiteral( "flowControl" ) );
        if ( value.typeId() == QMetaType::QString ) {
            const auto flowControl = value.toString().trimmed().toLower();
            if ( flowControl == QStringLiteral( "none" ) ) {
                settings.flowControl = QSerialPort::NoFlowControl;
            }
            else if ( flowControl == QStringLiteral( "hardware" )
                      || flowControl == QStringLiteral( "rts/cts" ) ) {
                settings.flowControl = QSerialPort::HardwareControl;
            }
            else if ( flowControl == QStringLiteral( "software" )
                      || flowControl == QStringLiteral( "xon/xoff" ) ) {
                settings.flowControl = QSerialPort::SoftwareControl;
            }
        }
        else {
            settings.flowControl = static_cast<QSerialPort::FlowControl>( value.toInt() );
        }
    }
    if ( map.contains( QStringLiteral( "addTimestamps" ) ) ) {
        settings.addTimestamps = map.value( QStringLiteral( "addTimestamps" ) ).toBool();
    }
    else if ( map.contains( QStringLiteral( "timestamps" ) ) ) {
        settings.addTimestamps = map.value( QStringLiteral( "timestamps" ) ).toBool();
    }
    if ( map.contains( QStringLiteral( "timestampFormat" ) ) ) {
        settings.timestampFormat = map.value( QStringLiteral( "timestampFormat" ) ).toString();
    }
    if ( map.contains( QStringLiteral( "logTransmits" ) ) ) {
        settings.logTransmits = map.value( QStringLiteral( "logTransmits" ) ).toBool();
    }
    if ( map.contains( QStringLiteral( "useForActions" ) ) ) {
        settings.useForActions = map.value( QStringLiteral( "useForActions" ) ).toBool();
    }
    return settings;
}

QVariantMap labLogicalDeviceToVariantMap( const LabLogicalDeviceDefinition& device )
{
    QVariantMap map;
    map.insert( QStringLiteral( "name" ), device.name );
    map.insert( QStringLiteral( "capabilityTags" ), toVariantList( device.capabilityTags ) );
    map.insert( QStringLiteral( "settings" ), commanderComSettingsToVariantMap( device.settings ) );
    return map;
}

LabLogicalDeviceDefinition labLogicalDeviceFromVariantMap( const QVariantMap& map )
{
    LabLogicalDeviceDefinition device;
    device.name = map.value( QStringLiteral( "name" ) ).toString();
    device.capabilityTags = toStringList( map.value( QStringLiteral( "capabilityTags" ) ) );
    device.settings = commanderComSettingsFromVariantMap(
        map.value( QStringLiteral( "settings" ) ).toMap() );
    return device;
}

QVariantMap labAgentPortToVariantMap( const LabAgentPortDefinition& port )
{
    QVariantMap map;
    map.insert( QStringLiteral( "portName" ), port.portName );
    map.insert( QStringLiteral( "displayName" ), port.displayName );
    map.insert( QStringLiteral( "capabilityTags" ), toVariantList( port.capabilityTags ) );
    map.insert( QStringLiteral( "labels" ), toVariantList( port.labels ) );
    map.insert( QStringLiteral( "settings" ), commanderComSettingsToVariantMap( port.settings ) );
    return map;
}

LabAgentPortDefinition labAgentPortFromVariantMap( const QVariantMap& map )
{
    LabAgentPortDefinition port;
    port.portName = map.value( QStringLiteral( "portName" ) ).toString();
    port.displayName = map.value( QStringLiteral( "displayName" ) ).toString();
    port.capabilityTags = toStringList( map.value( QStringLiteral( "capabilityTags" ) ) );
    port.labels = toStringList( map.value( QStringLiteral( "labels" ) ) );
    port.settings = commanderComSettingsFromVariantMap(
        map.value( QStringLiteral( "settings" ) ).toMap() );
    if ( port.settings.portName.isEmpty() ) {
        port.settings.portName = port.portName;
    }
    return port;
}

QVariantMap labAgentConfigToVariantMap( const LabAgentConfig& config )
{
    QVariantMap map;
    map.insert( QStringLiteral( "agentId" ), config.agentId );
    map.insert( QStringLiteral( "displayName" ), config.displayName );
    map.insert( QStringLiteral( "labels" ), toVariantList( config.labels ) );
    map.insert( QStringLiteral( "ports" ), toVariantList( config.ports ) );
    return map;
}

LabAgentConfig labAgentConfigFromVariantMap( const QVariantMap& map )
{
    LabAgentConfig config;
    config.agentId = map.value( QStringLiteral( "agentId" ) ).toString();
    config.displayName = map.value( QStringLiteral( "displayName" ) ).toString();
    config.labels = toStringList( map.value( QStringLiteral( "labels" ) ) );
    for ( const auto& item : map.value( QStringLiteral( "ports" ) ).toList() ) {
        config.ports.push_back( labAgentPortFromVariantMap( item.toMap() ) );
    }
    return config;
}

QVariantMap labBundleFileToVariantMap( const LabBundleFile& file )
{
    QVariantMap map;
    map.insert( QStringLiteral( "relativePath" ), file.relativePath );
    map.insert( QStringLiteral( "contentBase64" ),
                QString::fromLatin1( file.content.toBase64( QByteArray::Base64Encoding ) ) );
    return map;
}

LabBundleFile labBundleFileFromVariantMap( const QVariantMap& map )
{
    LabBundleFile file;
    file.relativePath = map.value( QStringLiteral( "relativePath" ) ).toString();
    file.content = QByteArray::fromBase64(
        map.value( QStringLiteral( "contentBase64" ) ).toByteArray(),
        QByteArray::Base64Encoding );
    return file;
}

QVariantMap labJobBundleToVariantMap( const LabJobBundle& bundle )
{
    QVariantMap map;
    map.insert( QStringLiteral( "kind" ), bundle.kind );
    map.insert( QStringLiteral( "suiteId" ), bundle.suiteId );
    map.insert( QStringLiteral( "suiteName" ), bundle.suiteName );
    map.insert( QStringLiteral( "suiteFile" ), bundle.suiteFile );
    map.insert( QStringLiteral( "scenarioFile" ), bundle.scenarioFile );
    map.insert( QStringLiteral( "argsJsonFile" ), bundle.argsJsonFile );
    map.insert( QStringLiteral( "requiredDevices" ), toVariantList( bundle.requiredDevices ) );
    map.insert( QStringLiteral( "agentLabel" ), bundle.agentLabel );
    map.insert( QStringLiteral( "logicalDevices" ), toVariantList( bundle.logicalDevices ) );
    map.insert( QStringLiteral( "files" ), toVariantList( bundle.files ) );
    return map;
}

LabJobBundle labJobBundleFromVariantMap( const QVariantMap& map )
{
    LabJobBundle bundle;
    bundle.kind = map.value( QStringLiteral( "kind" ) ).toString();
    bundle.suiteId = map.value( QStringLiteral( "suiteId" ) ).toString();
    bundle.suiteName = map.value( QStringLiteral( "suiteName" ) ).toString();
    bundle.suiteFile = map.value( QStringLiteral( "suiteFile" ) ).toString();
    bundle.scenarioFile = map.value( QStringLiteral( "scenarioFile" ) ).toString();
    bundle.argsJsonFile = map.value( QStringLiteral( "argsJsonFile" ) ).toString();
    bundle.requiredDevices = toStringList( map.value( QStringLiteral( "requiredDevices" ) ) );
    bundle.agentLabel = map.value( QStringLiteral( "agentLabel" ) ).toString();
    for ( const auto& item : map.value( QStringLiteral( "logicalDevices" ) ).toList() ) {
        bundle.logicalDevices.push_back( labLogicalDeviceFromVariantMap( item.toMap() ) );
    }
    for ( const auto& item : map.value( QStringLiteral( "files" ) ).toList() ) {
        bundle.files.push_back( labBundleFileFromVariantMap( item.toMap() ) );
    }
    return bundle;
}

QVariantMap labResolvedBindingToVariantMap( const LabResolvedBinding& binding )
{
    QVariantMap map;
    map.insert( QStringLiteral( "logicalName" ), binding.logicalName );
    map.insert( QStringLiteral( "portName" ), binding.portName );
    map.insert( QStringLiteral( "displayName" ), binding.displayName );
    map.insert( QStringLiteral( "capabilityTags" ), toVariantList( binding.capabilityTags ) );
    map.insert( QStringLiteral( "settings" ), commanderComSettingsToVariantMap( binding.settings ) );
    return map;
}

LabResolvedBinding labResolvedBindingFromVariantMap( const QVariantMap& map )
{
    LabResolvedBinding binding;
    binding.logicalName = map.value( QStringLiteral( "logicalName" ) ).toString();
    binding.portName = map.value( QStringLiteral( "portName" ) ).toString();
    binding.displayName = map.value( QStringLiteral( "displayName" ) ).toString();
    binding.capabilityTags = toStringList( map.value( QStringLiteral( "capabilityTags" ) ) );
    binding.settings = commanderComSettingsFromVariantMap(
        map.value( QStringLiteral( "settings" ) ).toMap() );
    if ( binding.settings.portName.isEmpty() ) {
        binding.settings.portName = binding.portName;
    }
    return binding;
}

QVariantMap labStoredArtifactToVariantMap( const LabStoredArtifact& artifact )
{
    QVariantMap map;
    map.insert( QStringLiteral( "name" ), artifact.name );
    map.insert( QStringLiteral( "path" ), artifact.path );
    return map;
}

LabStoredArtifact labStoredArtifactFromVariantMap( const QVariantMap& map )
{
    LabStoredArtifact artifact;
    artifact.name = map.value( QStringLiteral( "name" ) ).toString();
    artifact.path = map.value( QStringLiteral( "path" ) ).toString();
    return artifact;
}
