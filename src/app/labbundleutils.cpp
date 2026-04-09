#include "labbundleutils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QVariantMap loadJsonObject( const QString& path, QString* errorMessage )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = QStringLiteral( "Failed to read %1: %2" ).arg( path, file.errorString() );
        }
        return {};
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson( file.readAll(), &parseError );
    if ( parseError.error != QJsonParseError::NoError || !document.isObject() ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = QStringLiteral( "Invalid JSON object in %1." ).arg( path );
        }
        return {};
    }

    return document.object().toVariantMap();
}

QString absolutePath( const QString& baseDir, const QString& value )
{
    if ( value.isEmpty() ) {
        return {};
    }

    const QFileInfo fileInfo( value );
    return fileInfo.isAbsolute() ? fileInfo.absoluteFilePath()
                                 : QFileInfo( QDir( baseDir ).filePath( value ) ).absoluteFilePath();
}

bool appendFile( const QString& absolutePathValue, const QString& relativePath,
                 QList<LabBundleFile>* files, QString* errorMessage )
{
    QFile file( absolutePathValue );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = QStringLiteral( "Failed to read %1: %2" )
                                .arg( absolutePathValue, file.errorString() );
        }
        return false;
    }

    files->push_back( LabBundleFile{ relativePath, file.readAll() } );
    return true;
}

QStringList stringList( const QVariant& value )
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

} // namespace

std::optional<LabJobBundle> loadLabJobBundle( const LabCliRequest& request, QString* errorMessage )
{
    LabJobBundle bundle;
    bundle.agentLabel = request.agentLabel;

    if ( !request.scenarioFilePath.isEmpty() ) {
        const QFileInfo scenarioInfo( request.scenarioFilePath );
        if ( !scenarioInfo.exists() || !scenarioInfo.isFile() ) {
            if ( errorMessage != nullptr ) {
                *errorMessage = QStringLiteral( "Scenario file %1 was not found." )
                                    .arg( request.scenarioFilePath );
            }
            return std::nullopt;
        }

        bundle.kind = QStringLiteral( "scenario" );
        bundle.suiteId = QStringLiteral( "single-scenario" );
        bundle.suiteName = scenarioInfo.completeBaseName();
        bundle.scenarioFile = scenarioInfo.fileName();
        if ( !appendFile( scenarioInfo.absoluteFilePath(), bundle.scenarioFile, &bundle.files,
                          errorMessage ) ) {
            return std::nullopt;
        }

        if ( !request.argsJsonFilePath.isEmpty() ) {
            const QFileInfo argsInfo( request.argsJsonFilePath );
            if ( !argsInfo.exists() || !argsInfo.isFile() ) {
                if ( errorMessage != nullptr ) {
                    *errorMessage = QStringLiteral( "Args file %1 was not found." )
                                        .arg( request.argsJsonFilePath );
                }
                return std::nullopt;
            }
            bundle.argsJsonFile = argsInfo.fileName();
            if ( !appendFile( argsInfo.absoluteFilePath(), bundle.argsJsonFile, &bundle.files,
                              errorMessage ) ) {
                return std::nullopt;
            }
        }

        return bundle;
    }

    const QFileInfo suiteInfo( request.suiteFilePath );
    if ( !suiteInfo.exists() || !suiteInfo.isFile() ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = QStringLiteral( "Suite file %1 was not found." ).arg( request.suiteFilePath );
        }
        return std::nullopt;
    }

    const auto suiteObject = loadJsonObject( suiteInfo.absoluteFilePath(), errorMessage );
    if ( suiteObject.isEmpty() ) {
        return std::nullopt;
    }

    bundle.kind = QStringLiteral( "suite" );
    bundle.suiteId = suiteObject.value( QStringLiteral( "suiteId" ) ).toString().trimmed();
    if ( bundle.suiteId.isEmpty() ) {
        bundle.suiteId = suiteInfo.completeBaseName();
    }
    bundle.suiteName = suiteObject.value( QStringLiteral( "name" ) ).toString().trimmed();
    if ( bundle.suiteName.isEmpty() ) {
        bundle.suiteName = suiteInfo.completeBaseName();
    }
    bundle.suiteFile = suiteInfo.fileName();
    if ( !appendFile( suiteInfo.absoluteFilePath(), bundle.suiteFile, &bundle.files, errorMessage ) ) {
        return std::nullopt;
    }

    const auto suiteBaseDir = suiteInfo.absolutePath();
    const auto devicesObject = suiteObject.value( QStringLiteral( "devices" ) ).toMap();
    for ( auto it = devicesObject.begin(); it != devicesObject.end(); ++it ) {
        LabLogicalDeviceDefinition device;
        device.name = it.key();
        const auto object = it.value().toMap();
        device.capabilityTags = stringList( object.value( QStringLiteral( "capabilityTags" ) ) );
        device.settings = commanderComSettingsFromVariantMap( object );
        bundle.logicalDevices.push_back( device );
    }

    for ( const auto& scenarioValue : suiteObject.value( QStringLiteral( "scenarios" ) ).toList() ) {
        const auto scenario = scenarioValue.toMap();
        const auto scenarioFile = absolutePath( suiteBaseDir, scenario.value( QStringLiteral( "scenarioFile" ) ).toString() );
        if ( scenarioFile.isEmpty() ) {
            continue;
        }
        const auto scenarioInfo = QFileInfo( scenarioFile );
        if ( !appendFile( scenarioInfo.absoluteFilePath(), scenario.value( QStringLiteral( "scenarioFile" ) ).toString(),
                          &bundle.files, errorMessage ) ) {
            return std::nullopt;
        }

        const auto argsFileValue = scenario.value( QStringLiteral( "argsJsonFile" ) ).toString();
        if ( !argsFileValue.trimmed().isEmpty() ) {
            const auto argsFile = absolutePath( suiteBaseDir, argsFileValue );
            if ( !appendFile( argsFile, argsFileValue, &bundle.files, errorMessage ) ) {
                return std::nullopt;
            }
        }

        for ( const auto& deviceName : stringList( scenario.value( QStringLiteral( "requiredDevices" ) ) ) ) {
            if ( !bundle.requiredDevices.contains( deviceName ) ) {
                bundle.requiredDevices.push_back( deviceName );
            }
        }
    }

    if ( bundle.requiredDevices.isEmpty() ) {
        for ( const auto& device : bundle.logicalDevices ) {
            bundle.requiredDevices.push_back( device.name );
        }
    }

    return bundle;
}

std::optional<LabAgentConfig> loadLabAgentConfig( const QString& path, QString* errorMessage )
{
    const auto object = loadJsonObject( path, errorMessage );
    if ( object.isEmpty() ) {
        return std::nullopt;
    }

    auto config = labAgentConfigFromVariantMap( object );
    if ( config.agentId.trimmed().isEmpty() ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = QStringLiteral( "Agent config is missing agentId." );
        }
        return std::nullopt;
    }
    if ( config.displayName.trimmed().isEmpty() ) {
        config.displayName = config.agentId;
    }
    if ( config.ports.isEmpty() ) {
        if ( errorMessage != nullptr ) {
            *errorMessage = QStringLiteral( "Agent config must define at least one port." );
        }
        return std::nullopt;
    }
    return config;
}
