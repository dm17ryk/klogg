// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Advanced plugin SDK (E17, docs/plugin-sdk-advanced.md): negotiation, manifests, host queries
// and golden frames.
#include "cli.h"
#include "commander.h"
#include "pluginfeatures.h"
#include "pluginmanager.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <catch2/catch.hpp>
#include <functional>
#include <map>
#include <utility>

using namespace cilogg::plugins;

namespace {
bool waitUntil( const std::function<bool()>& predicate, int timeout = 5000 )
{
    QElapsedTimer timer;
    timer.start();
    while ( !predicate() && timer.elapsed() < timeout ) {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
        QThread::msleep( 2 );
    }
    return predicate();
}

void writeBytes( const QString& path, const QByteArray& bytes )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    REQUIRE( file.write( bytes ) == bytes.size() );
}

// The E13/E14 fixture manifest (tests/unit/plugins_test.cpp) with views and no `handles`.
QJsonObject fixtureManifest( const QString& id )
{
    return { { "id", id },
             { "name", "Fixture" },
             { "version", "1.0.0" },
             { "protocol_version", 1 },
             { "executable", QFileInfo( QString::fromUtf8( PLUGIN_FIXTURE_PATH ) ).fileName() },
             { "arguments", QJsonArray{} },
             { "capabilities", QJsonArray{ "datasource", "converter", "tool" } },
             { "configuration", QJsonArray{} },
             { "actions",
               QJsonArray{ QJsonObject{ { "id", "summary" }, { "capability", "tool" } },
                           QJsonObject{ { "id", "wait" }, { "capability", "tool" } } } },
             { "views", QJsonArray{ QJsonObject{ { "id", "main" }, { "title", "Main" } } } } };
}

QString fixturePackage( const QString& root, const QJsonObject& json )
{
    const auto directory = QDir( root ).filePath( json.value( "id" ).toString() );
    REQUIRE( QDir().mkpath( directory ) );
    writeBytes( QDir( directory ).filePath( "plugin.json" ), QJsonDocument( json ).toJson() );
    REQUIRE( QFile::copy( QString::fromUtf8( PLUGIN_FIXTURE_PATH ),
                          QDir( directory ).filePath( json[ "executable" ].toString() ) ) );
    return directory;
}

QList<QJsonObject> recorded( const QString& folder, const QString& method )
{
    QList<QJsonObject> result;
    QFile log( QDir( folder ).filePath( "host-messages.jsonl" ) );
    if ( !log.open( QIODevice::ReadOnly ) ) {
        return result;
    }
    for ( const auto& line : log.readAll().split( '\n' ) ) {
        const auto frame = QJsonDocument::fromJson( line ).object();
        if ( frame.value( "method" ) == method ) {
            result.append( frame );
        }
    }
    return result;
}

bool finishedJob( PluginManager& manager, const QString& id )
{
    const auto state = manager.job( id ).payload.value( "state" ).toString();
    return state == "completed" || state == "failed" || state == "cancelled";
}

// Replaces generated ids and temporary paths, so a recording compares across runs.
class FrameNormalizer {
public:
    explicit FrameNormalizer( QString temporaryRoot )
        : root_( QDir::fromNativeSeparators( std::move( temporaryRoot ) ) )
    {
    }
    QJsonValue normalize( const QJsonValue& value )
    {
        if ( value.isObject() ) {
            QJsonObject result;
            const auto object = value.toObject();
            for ( auto it = object.begin(); it != object.end(); ++it ) {
                result.insert( it.key(), normalize( it.value() ) );
            }
            return result;
        }
        if ( value.isArray() ) {
            QJsonArray result;
            for ( const auto& item : value.toArray() ) {
                result.append( normalize( item ) );
            }
            return result;
        }
        if ( !value.isString() ) {
            return value;
        }
        auto text = QDir::fromNativeSeparators( value.toString() );
        text.replace( root_, QStringLiteral( "<temp>" ), Qt::CaseInsensitive );
        static const QRegularExpression uuid(
            "[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}",
            QRegularExpression::CaseInsensitiveOption );
        auto match = uuid.match( text );
        while ( match.hasMatch() ) {
            const auto found = match.captured( 0 ).toLower();
            auto known = ids_.find( found );
            if ( known == ids_.end() ) {
                known = ids_.emplace( found, QStringLiteral( "<id-%1>" ).arg( ids_.size() + 1 ) )
                            .first;
            }
            text.replace( match.capturedStart( 0 ), match.capturedLength( 0 ), known->second );
            match = uuid.match( text );
        }
        return text;
    }

private:
    QString root_;
    std::map<QString, QString> ids_;
};

// E17 adds keys to `initialize` only (principle 11): drop them, so a recording made before E17
// stays comparable. Nothing else is removed.
QJsonObject withoutAdditiveInitializeKeys( QJsonObject frame )
{
    if ( frame.value( "method" ) == "initialize" ) {
        auto params = frame.value( "params" ).toObject();
        params.remove( "host_feature_versions" );
        params.remove( "host_version" );
        params.remove( "host_limits" );
        QJsonArray features;
        for ( const auto& feature : params.value( "host_features" ).toArray() ) {
            if ( feature == "views" || feature == "view_edit" ) {
                features.append( feature );
            }
        }
        params.insert( "host_features", features );
        frame.insert( "params", params );
    }
    else if ( frame.value( "id" ) == "initialize" && frame.value( "result" ).isObject() ) {
        auto result = frame.value( "result" ).toObject();
        result.remove( "sdk" );
        result.remove( "features" );
        frame.insert( "result", result );
    }
    return frame;
}

QString goldenPath()
{
    return QDir( QString::fromUtf8( PLUGIN_GOLDEN_DIR ) )
        .filePath( "plugin_frames_without_handles.json" );
}

// The first difference between two frame lists, for a readable failure.
QString firstDifference( const QJsonArray& expected, const QJsonArray& actual )
{
    for ( qsizetype index = 0; index < std::max( expected.size(), actual.size() ); ++index ) {
        const auto left = index < expected.size() ? expected.at( index ) : QJsonValue{};
        const auto right = index < actual.size() ? actual.at( index ) : QJsonValue{};
        if ( left != right ) {
            return QStringLiteral( "frame %1\nexpected: %2\nactual:   %3" )
                .arg( index )
                .arg( QString::fromUtf8( QJsonDocument( QJsonArray{ left } ).toJson(
                    QJsonDocument::Compact ) ) )
                .arg( QString::fromUtf8( QJsonDocument( QJsonArray{ right } ).toJson(
                    QJsonDocument::Compact ) ) );
        }
    }
    return {};
}
} // namespace

TEST_CASE( "A plugin without handles gets the frames it got before E17",
           "[plugins][E17][golden]" )
{
    QTemporaryDir temp;
    const auto shipped = QDir( temp.path() ).filePath( "shipped" );
    const auto id = QStringLiteral( "org.test.fixture" );
    const auto folder = fixturePackage( shipped, fixtureManifest( id ) );
    // Declared before the manager, whose destructor can still report frames.
    FrameNormalizer normalizer( temp.path() );
    QJsonArray outgoing;
    QJsonArray incoming;
    PluginManager manager( QDir( temp.path() ).filePath( "data" ), shipped );
    manager.setInstanceId( "golden-instance" );
    QObject::connect( &manager, &PluginManager::protocolFrame,
                      [ & ]( const QString& pluginId, bool toPlugin, const QJsonObject& frame ) {
                          if ( pluginId != id ) {
                              return;
                          }
                          const auto normalized
                              = normalizer.normalize( withoutAdditiveInitializeKeys( frame ) );
                          ( toPlugin ? outgoing : incoming ).append( normalized );
                      } );
    QSignalSpy requests( &manager, &PluginManager::viewRequest );
    const auto sawRequest = [ & ]( const QString& method, const QString& text ) {
        return std::any_of( requests.begin(), requests.end(), [ & ]( const QList<QVariant>& args ) {
            return args[ 1 ] == method
                   && QString::fromUtf8( QJsonDocument( args[ 2 ].toJsonObject() ).toJson() )
                          .contains( text );
        } );
    };

    REQUIRE( manager.manage( "enable", id, {} ).ok() );
    manager.noteActiveWindow( "w1" );
    // An action with progress, a panel and a result.
    const auto summary = manager.invoke( id, "summary", {}, { { "windowId", "w1" } } );
    REQUIRE( summary.ok() );
    const auto summaryId = summary.payload.value( "jobId" ).toString();
    REQUIRE( waitUntil( [ & ] { return finishedJob( manager, summaryId ); } ) );
    // A view with the E13/E14 elements of the fixture: a label, fields, buttons and an editable
    // table.
    REQUIRE( manager.openView( id, "main", { { "windowId", "w1" } } ).ok() );
    REQUIRE( waitUntil( [ & ] { return sawRequest( "view/update", "ready main" ); } ) );
    REQUIRE( manager.sendViewEvent( id, "main", "w1",
                                    { { "element_id", "ping" },
                                      { "type", "clicked" },
                                      { "value", QJsonValue::Null },
                                      { "inputs", QJsonObject{ { "name", "abc" } } } } ) );
    REQUIRE( waitUntil( [ & ] { return sawRequest( "view/update", "pong:abc" ); } ) );
    REQUIRE( manager.sendViewEvent( id, "main", "w1",
                                    { { "element_id", "rows" },
                                      { "type", "edited" },
                                      { "row_id", "r1" },
                                      { "column_id", "c" },
                                      { "value", "5" },
                                      { "previous", "1" },
                                      { "inputs", QJsonObject{} } } ) );
    REQUIRE( waitUntil( [ & ] {
        const auto events = recorded( folder, "view/event" );
        return events.size() == 2;
    } ) );
    // A message box and its answer.
    REQUIRE( manager.sendViewEvent( id, "main", "w1",
                                    { { "element_id", "ask" },
                                      { "type", "clicked" },
                                      { "value", QJsonValue::Null },
                                      { "inputs", QJsonObject{} } } ) );
    REQUIRE( waitUntil( [ & ] { return sawRequest( "ui/message", "Proceed?" ); } ) );
    manager.finishMessage( id, "ask", "yes", false );
    REQUIRE( waitUntil( [ & ] { return sawRequest( "view/update", "answer:ask:yes:user" ); } ) );
    manager.closeView( id, "main", "w1" );
    REQUIRE( waitUntil( [ & ] { return recorded( folder, "view/closed" ).size() == 1; } ) );
    // A cancelled action.
    const auto wait = manager.invoke( id, "wait", {}, { { "windowId", "w1" } } );
    REQUIRE( wait.ok() );
    const auto waitId = wait.payload.value( "jobId" ).toString();
    REQUIRE( waitUntil(
        [ & ] { return manager.job( waitId ).payload.value( "state" ) == "running"; } ) );
    REQUIRE( manager.cancel( waitId ).ok() );
    REQUIRE( waitUntil( [ & ] { return finishedJob( manager, waitId ); } ) );
    REQUIRE( manager.manage( "disable", id, {} ).ok() );
    REQUIRE( waitUntil( [ & ] { return manager.pluginState( id ) == "stopped"; } ) );

    const QJsonObject actual{ { "outgoing", outgoing }, { "incoming", incoming } };
    if ( qEnvironmentVariableIsSet( "CILOGG_RECORD_GOLDEN" ) ) {
        writeBytes( goldenPath(), QJsonDocument( actual ).toJson( QJsonDocument::Indented ) );
        WARN( "Recorded " << goldenPath().toStdString() );
        return;
    }
    QFile file( goldenPath() );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    const auto golden = QJsonDocument::fromJson( file.readAll() ).object();
    INFO( "host to plugin: "
          << firstDifference( golden.value( "outgoing" ).toArray(), outgoing ).toStdString() );
    CHECK( golden.value( "outgoing" ).toArray() == outgoing );
    INFO( "plugin to host: "
          << firstDifference( golden.value( "incoming" ).toArray(), incoming ).toStdString() );
    CHECK( golden.value( "incoming" ).toArray() == incoming );
}

namespace {
using Status = PluginQueries::Status;

// The fixture with the E17a test surface: `handles` for tooltip/resolve, the `tips` view and the
// actions respond (a raw answer with a chosen id), icons (ui/icons) and invalidate
// (tooltip/invalidate). `mode` is the fixture's first argument ("no-queries": no handler).
QJsonObject queryManifest( const QString& id, const QString& mode = {}, bool withHandles = true )
{
    auto json = fixtureManifest( id );
    if ( withHandles ) {
        json[ "handles" ] = QJsonArray{ "tooltip/resolve", "query/cancel" };
    }
    json[ "actions" ] = QJsonArray{ QJsonObject{ { "id", "summary" }, { "capability", "tool" } },
                                    QJsonObject{ { "id", "respond" }, { "capability", "tool" } },
                                    QJsonObject{ { "id", "icons" }, { "capability", "tool" } },
                                    QJsonObject{ { "id", "invalidate" }, { "capability", "tool" } } };
    json[ "views" ] = QJsonArray{ QJsonObject{ { "id", "main" }, { "title", "Main" } },
                                  QJsonObject{ { "id", "tips" }, { "title", "Tips" } } };
    if ( !mode.isEmpty() ) {
        json[ "arguments" ] = QJsonArray{ mode };
    }
    return json;
}

struct Answer {
    bool done = false;
    PluginQueries::Outcome outcome;
};

std::shared_ptr<Answer> answer()
{
    return std::make_shared<Answer>();
}

struct QueryFixture {
    QTemporaryDir temp;
    QString shipped;
    // Declared before the manager: its destructor still sends frames (shutdown) to the lists.
    QList<QPair<QString, QJsonObject>> outgoing; // plugin id, frame
    QList<QPair<QString, QJsonObject>> incoming;
    std::unique_ptr<PluginManager> manager;

    explicit QueryFixture( const QList<QJsonObject>& manifests )
    {
        shipped = QDir( temp.path() ).filePath( "shipped" );
        for ( const auto& json : manifests ) {
            fixturePackage( shipped, json );
        }
        manager = std::make_unique<PluginManager>( QDir( temp.path() ).filePath( "data" ), shipped );
        QObject::connect( manager.get(), &PluginManager::protocolFrame,
                          [ this ]( const QString& id, bool toPlugin, const QJsonObject& frame ) {
                              ( toPlugin ? outgoing : incoming ).append( { id, frame } );
                          } );
        for ( const auto& json : manifests ) {
            REQUIRE( manager->manage( "enable", json.value( "id" ).toString(), {} ).ok() );
        }
    }
    void start( const QString& id )
    {
        const auto job = manager->invoke( id, "summary", {}, {} );
        REQUIRE( job.ok() );
        const auto jobId = job.payload.value( "jobId" ).toString();
        REQUIRE( waitUntil( [ & ] { return finishedJob( *manager, jobId ); } ) );
        REQUIRE( manager->pluginState( id ) == "ready" );
    }
    QString ask( const QString& id, const QString& element, const std::shared_ptr<Answer>& result,
                 int timeoutMs = 0, const QString& exclusive = {}, const QString& scope = {},
                 QString* reason = nullptr )
    {
        return manager->query(
            id, "tooltip/resolve",
            { { "view_id", "tips" }, { "window_id", "w1" }, { "element_id", element } }, timeoutMs,
            exclusive, scope,
            [ result ]( const PluginQueries::Outcome& outcome ) {
                result->done = true;
                result->outcome = outcome;
            },
            reason );
    }
    int count( const QString& method, const QString& id = {} ) const
    {
        return static_cast<int>( std::count_if(
            outgoing.begin(), outgoing.end(), [ & ]( const QPair<QString, QJsonObject>& item ) {
                return item.second.value( "method" ) == method
                       && ( id.isEmpty()
                            || item.second.value( "params" ).toObject().value( "id" ) == id
                            || item.second.value( "id" ) == id );
            } ) );
    }
    bool received( const QString& pluginId, const QString& id ) const
    {
        return std::any_of( incoming.begin(), incoming.end(),
                            [ & ]( const QPair<QString, QJsonObject>& item ) {
                                return item.first == pluginId && item.second.value( "id" ) == id;
                            } );
    }
    QVariantMap stats( const QString& id ) const
    {
        for ( const auto& item : manager->queries().statistics( id ) ) {
            if ( item.toMap().value( "method" ) == "tooltip/resolve" ) {
                return item.toMap();
            }
        }
        return {};
    }
};
} // namespace

TEST_CASE( "E17 the C++ SDK needs host_features before a feature version counts",
           "[plugins][E17]" )
{
    // A host that lists `menus` only in host_feature_versions does not announce it.
    QTemporaryDir temp;
    auto json = fixtureManifest( "org.test.supports" );
    json[ "actions" ] = QJsonArray{ QJsonObject{ { "id", "supports" }, { "capability", "tool" } } };
    const auto folder = fixturePackage( temp.path(), json );
    QProcess process;
    process.setWorkingDirectory( folder );
    process.start( QDir( folder ).filePath( json.value( "executable" ).toString() ), QStringList{} );
    REQUIRE( process.waitForStarted() );
    const auto send = [ & ]( const QJsonObject& frame ) {
        process.write( QJsonDocument( frame ).toJson( QJsonDocument::Compact ) + '\n' );
    };
    const auto answer = [ & ]( const QString& id ) {
        QElapsedTimer timer;
        timer.start();
        QByteArray buffer;
        while ( timer.elapsed() < 10000 ) {
            process.waitForReadyRead( 100 );
            buffer += process.readAllStandardOutput();
            for ( const auto& line : buffer.split( '\n' ) ) {
                const auto frame = QJsonDocument::fromJson( line ).object();
                if ( frame.value( "id" ) == id ) {
                    return frame;
                }
            }
        }
        return QJsonObject{};
    };
    send( { { "jsonrpc", "2.0" },
            { "id", "initialize" },
            { "method", "initialize" },
            { "params", QJsonObject{ { "protocol_version", 1 },
                                     { "plugin_id", "org.test.supports" },
                                     { "configuration", QJsonObject{} },
                                     { "host_features", QJsonArray{ "views", "tooltips" } },
                                     { "host_feature_versions",
                                       QJsonObject{ { "views", 2 }, { "menus", 1 } } } } } } );
    REQUIRE( answer( "initialize" ).contains( "result" ) );
    send( { { "jsonrpc", "2.0" },
            { "id", "j1" },
            { "method", "invoke" },
            { "params", QJsonObject{ { "job_id", "j1" },
                                     { "action", "supports" },
                                     { "context", QJsonObject{} },
                                     { "arguments", QJsonObject{ { "features",
                                                                   QJsonArray{ "views", "tooltips",
                                                                               "menus" } } } } } } } );
    const auto result = answer( "j1" ).value( "result" ).toObject();
    process.closeWriteChannel();
    process.waitForFinished( 5000 );
    REQUIRE( result.value( "views" ) == true );
    REQUIRE( result.value( "views@2" ) == true );
    REQUIRE( result.value( "tooltips" ) == true );   // listed: version 1
    REQUIRE( result.value( "tooltips@2" ) == false );
    REQUIRE( result.value( "menus" ) == false );     // a version alone announces nothing
}

TEST_CASE( "E17 a closed view ends its own queries, not those of a view with a longer id",
           "[plugins][E17]" )
{
    QStringList sent;
    PluginQueries queries(
        [ &sent ]( const QString&, const QString& id, const QString& method, const QJsonObject& ) {
            sent.append( method + ':' + id );
            return true;
        } );
    QStringList ended;
    const auto send = [ & ]( const QString& scope ) {
        PluginQueries::Request request;
        request.pluginId = "org.p";
        request.method = "tooltip/resolve";
        request.scope = scope;
        QString reason;
        return queries.send(
            request, []( const QJsonValue& value, QString* ) { return value; },
            [ &ended, scope ]( const PluginQueries::Outcome& outcome ) {
                if ( outcome.status == Status::Cancelled ) {
                    ended.append( scope );
                }
            },
            &reason );
    };
    REQUIRE_FALSE( send( "view:w1/tips" ).isEmpty() );
    REQUIRE_FALSE( send( "view:w1/tips2" ).isEmpty() );
    REQUIRE_FALSE( send( "view:w1/tips/inner" ).isEmpty() );
    queries.dropScope( "org.p", "view:w1/tips" );
    ended.sort();
    REQUIRE( ended == QStringList{ "view:w1/tips", "view:w1/tips/inner" } );
}

TEST_CASE( "E17 host features, versions and limits are announced in initialize",
           "[plugins][E17]" )
{
    const auto features = hostFeatureNames();
    REQUIRE( features
             == QJsonArray{ "views", "view_edit", "queries", "tooltips", "icons", "menus",
                            "shortcuts", "choices", "toggles", "field_kinds", "validation",
                            "rich_tables", "element_menus", "tree", "file_tree",
                            "python_decoders", "layout", "text_view", "images", "charts",
                            "view_dialogs" } );
    REQUIRE( hostFeatureVersions().value( "views" ).toInt() == 2 );
    REQUIRE( hostLimits().value( "max_elements" ).toInt() == 512 );
    REQUIRE( hostLimits().value( "max_message_bytes" ).toInt() == MaxMessageBytes );

    QueryFixture fixture( { queryManifest( "org.test.a" ) } );
    fixture.start( "org.test.a" );
    const auto initialize = std::find_if(
        fixture.outgoing.begin(), fixture.outgoing.end(),
        []( const auto& item ) { return item.second.value( "method" ) == "initialize"; } );
    REQUIRE( initialize != fixture.outgoing.end() );
    const auto params = initialize->second.value( "params" ).toObject();
    REQUIRE( params.value( "host_features" ).toArray() == features );
    REQUIRE( params.value( "host_feature_versions" ).toObject() == hostFeatureVersions() );
    REQUIRE( params.value( "host_version" ).toString() == fixture.manager->hostVersion() );
    REQUIRE( params.value( "host_limits" ).toObject() == hostLimits() );
    // The plugin's own announcement, shown by get_plugins.
    const auto plugin = fixture.manager->plugins().front().toMap();
    REQUIRE( plugin.value( "features" ).toStringList().contains( "tooltip/resolve" ) );
    REQUIRE( plugin.value( "features" ).toStringList().contains( "query/cancel" ) );
    REQUIRE( plugin.value( "sdk" ).toMap().value( "name" ) == "cilogg_plugin.h" );
    REQUIRE( plugin.value( "missingFeatures" ).toStringList().isEmpty() );
}

TEST_CASE( "E17 manifests validate handles, requires and icon_ref", "[plugins][E17]" )
{
    const auto parse = []( const std::function<void( QJsonObject& )>& change ) {
        auto json = fixtureManifest( "org.test.manifest" );
        change( json );
        return Manifest::parse( QJsonDocument( json ).toJson(), "folder" );
    };
    const auto withAction = []( QJsonObject& json, const char* key, const QJsonValue& value ) {
        auto actions = json[ "actions" ].toArray();
        auto action = actions[ 0 ].toObject();
        action[ QLatin1String( key ) ] = value;
        actions[ 0 ] = action;
        json[ "actions" ] = actions;
    };
    const auto withView = []( QJsonObject& json, const char* key, const QJsonValue& value ) {
        auto views = json[ "views" ].toArray();
        auto view = views[ 0 ].toObject();
        view[ QLatin1String( key ) ] = value;
        views[ 0 ] = view;
        json[ "views" ] = views;
    };
    // Method names this host does not know are allowed: a newer plugin.
    REQUIRE( parse( []( QJsonObject& json ) {
                 json[ "handles" ] = QJsonArray{ "tooltip/resolve", "menu/populate", "x/y" };
             } ).valid() );
    REQUIRE_FALSE( parse( []( QJsonObject& json ) { json[ "handles" ] = "tooltip/resolve"; } )
                       .valid() );
    REQUIRE_FALSE(
        parse( []( QJsonObject& json ) { json[ "handles" ] = QJsonArray{ 1 }; } ).valid() );
    REQUIRE_FALSE( parse( []( QJsonObject& json ) {
                       json[ "handles" ] = QJsonArray{ "Tooltip Resolve" };
                   } ).valid() );

    REQUIRE( parse( []( QJsonObject& json ) {
                 json[ "requires" ]
                     = QJsonObject{ { "host_features", QJsonArray{ "tooltips", "views>=2" } } };
             } ).valid() );
    const auto missing = parse( []( QJsonObject& json ) {
        json[ "requires" ]
            = QJsonObject{ { "host_features", QJsonArray{ "not_a_feature", "views>=3", "icons" } } };
    } );
    REQUIRE_FALSE( missing.valid() );
    REQUIRE( missing.missingFeatures == QStringList{ "not_a_feature", "views>=3" } );
    REQUIRE( missing.error == "Requires a newer CILogg (missing: not_a_feature, views>=3)." );
    REQUIRE_FALSE( parse( []( QJsonObject& json ) {
                       json[ "requires" ]
                           = QJsonObject{ { "host_features", QJsonArray{ "views >= 2" } } };
                   } ).valid() );
    REQUIRE_FALSE( parse( []( QJsonObject& json ) { json[ "requires" ] = "tree"; } ).valid() );

    // icon_ref takes every form; the E14 `icon` stays a plugin file.
    REQUIRE( parse( [ & ]( QJsonObject& json ) {
                 withAction( json, "icon_ref", "builtin:play" );
                 withView( json, "icon_ref",
                           QJsonObject{ { "light", "status:ok" }, { "dark", "image:x" } } );
             } ).valid() );
    REQUIRE_FALSE( parse( [ & ]( QJsonObject& json ) {
                       withView( json, "icon_ref", "https://example.test/icon.png" );
                   } ).valid() );
    REQUIRE_FALSE( parse( [ & ]( QJsonObject& json ) {
                       withAction( json, "icon", "builtin:play" );
                   } ).valid() );
}

TEST_CASE( "E17 icon references and tooltip answers are validated", "[plugins][E17]" )
{
    for ( const auto& valid : { QJsonValue( "icons/a.svg" ), QJsonValue( "builtin:play" ),
                                QJsonValue( "builtin:not-known" ), QJsonValue( "status:warn" ),
                                QJsonValue( "image:board.7" ),
                                QJsonValue( QJsonObject{ { "dark", "icons/d.png" } } ) } ) {
        INFO( valid.toVariant().toString().toStdString() );
        REQUIRE( validateIconReference( valid ).isEmpty() );
    }
    for ( const auto& invalid :
          { QJsonValue( "../a.svg" ), QJsonValue( "icons/a.gif" ), QJsonValue( "C:/a.png" ),
            QJsonValue( "image:" ), QJsonValue( "builtin:" ), QJsonValue( 3 ),
            QJsonValue( QJsonObject{} ),
            QJsonValue( QJsonObject{ { "light", QJsonObject{ { "light", "a.png" } } } } ) } ) {
        INFO( QJsonDocument( QJsonArray{ invalid } ).toJson().toStdString() );
        REQUIRE_FALSE( validateIconReference( invalid ).isEmpty() );
    }

    QString error;
    REQUIRE( sanitizeTooltip( QJsonValue::Null, &error ).isEmpty() );
    REQUIRE( error.isEmpty() );
    REQUIRE( sanitizeTooltip( QJsonObject{}, &error ).isEmpty() );
    REQUIRE( sanitizeTooltip( "plain", &error ).value( "text" ) == "plain" );
    QJsonArray rows;
    for ( int row = 0; row < 40; ++row ) {
        rows.append( QJsonArray{ QStringLiteral( "name %1" ).arg( row ), row } );
    }
    const auto full = sanitizeTooltip( QJsonObject{ { "title", "<b>Board</b>" },
                                                    { "text", "Line 1\nLine 2" },
                                                    { "rows", rows },
                                                    { "tone", "warn" },
                                                    { "icon", "status:ok" },
                                                    { "footer", "f" },
                                                    { "ttl_ms", 999999999 },
                                                    { "unknown", true } },
                                       &error );
    REQUIRE( error.isEmpty() );
    REQUIRE( full.value( "title" ) == "<b>Board</b>" ); // kept as text, escaped when rendered
    REQUIRE( full.value( "rows" ).toArray().size() == MaxTooltipRows );
    REQUIRE( full.value( "rows" ).toArray().at( 1 ).toArray().at( 1 ) == "1" );
    REQUIRE( full.value( "tone" ) == "warn" );
    REQUIRE( full.value( "icon" ) == "status:ok" );
    REQUIRE( full.value( "ttl_ms" ).toDouble() == MaxTooltipTtlMs );
    REQUIRE_FALSE( full.contains( "unknown" ) );
    const auto defaults = sanitizeTooltip( QJsonObject{ { "text", QString( 20000, 'x' ) },
                                                        { "tone", "sparkly" },
                                                        { "icon", "../bad.png" } },
                                           &error );
    REQUIRE( defaults.value( "text" ).toString().size() == MaxTooltipTextChars );
    REQUIRE( defaults.value( "ttl_ms" ).toDouble() == DefaultTooltipTtlMs );
    REQUIRE_FALSE( defaults.contains( "tone" ) );
    REQUIRE_FALSE( defaults.contains( "icon" ) );
    // An icon alone is something to show (Codex review of #130).
    const auto iconOnly = sanitizeTooltip( QJsonObject{ { "icon", "status:ok" } }, &error );
    REQUIRE( iconOnly.value( "icon" ) == "status:ok" );
    REQUIRE( sanitizeTooltip( QJsonObject{ { "icon", "../bad.png" } }, &error ).isEmpty() );
    for ( const auto& invalid :
          { QJsonValue( QJsonArray{ 1, 2 } ), QJsonValue( 7 ),
            QJsonValue( QJsonObject{ { "title", 3 } } ),
            QJsonValue( QJsonObject{ { "rows", QJsonArray{ "a" } } } ),
            QJsonValue( QJsonObject{ { "text", "t" }, { "ttl_ms", "1" } } ) } ) {
        error.clear();
        sanitizeTooltip( invalid, &error );
        REQUIRE_FALSE( error.isEmpty() );
    }
    REQUIRE( boundedQueryTimeout( "tooltip/resolve", 0 ) == 1500 );
    REQUIRE( boundedQueryTimeout( "tooltip/resolve", 90000 ) == 5000 );
    REQUIRE( boundedQueryTimeout( "tree/children", 0 ) == 10000 );
    REQUIRE_FALSE( isQueryMethod( "invoke" ) );
}

TEST_CASE( "E17 host queries: answers, nothing, errors, invalid answers and timeouts",
           "[plugins][E17]" )
{
    const QString id = "org.test.a";
    QueryFixture fixture( { queryManifest( id ) } );
    fixture.start( id );
    const auto tip = answer();
    const auto nothing = answer();
    const auto failed = answer();
    const auto invalid = answer();
    const auto slow = answer();
    REQUIRE_FALSE( fixture.ask( id, "tip", tip ).isEmpty() );
    REQUIRE( waitUntil( [ & ] { return tip->done; } ) );
    REQUIRE( tip->outcome.status == Status::Answered );
    REQUIRE( tip->outcome.result.toObject().value( "title" ) == "Title <b>bold</b>" );
    REQUIRE( tip->outcome.result.toObject().value( "rows" ).toArray().size() == 1 );
    // One at a time: the SDK's tooltip lane is latest-wins, so a query sent while another one
    // still waits for the worker would replace it.
    REQUIRE_FALSE( fixture.ask( id, "nulltip", nothing ).isEmpty() );
    REQUIRE( waitUntil( [ & ] { return nothing->done; } ) );
    REQUIRE_FALSE( fixture.ask( id, "errortip", failed ).isEmpty() );
    REQUIRE( waitUntil( [ & ] { return failed->done; } ) );
    REQUIRE( nothing->outcome.status == Status::Nothing );
    REQUIRE( failed->outcome.status == Status::Error );
    REQUIRE( failed->outcome.error.contains( "tooltip failed" ) );
    REQUIRE_FALSE( fixture.ask( id, "invalidtip", invalid ).isEmpty() );
    REQUIRE( waitUntil( [ & ] { return invalid->done; } ) );
    REQUIRE( invalid->outcome.status == Status::Invalid );
    // The query carries its bounded timeout; a timeout sends query/cancel.
    const auto slowId = fixture.ask( id, "slowtip", slow, 200 );
    REQUIRE_FALSE( slowId.isEmpty() );
    REQUIRE( waitUntil( [ & ] { return slow->done; } ) );
    REQUIRE( slow->outcome.status == Status::TimedOut );
    REQUIRE( fixture.count( "query/cancel", slowId ) == 1 );
    const auto sentSlow = std::find_if(
        fixture.outgoing.begin(), fixture.outgoing.end(),
        [ & ]( const auto& item ) { return item.second.value( "id" ) == slowId; } );
    REQUIRE( sentSlow != fixture.outgoing.end() );
    REQUIRE( sentSlow->second.value( "params" ).toObject().value( "timeout_ms" ).toInt() == 200 );
    // A C++ SDK handler that returns an undefined value or throws something that is not a
    // std::exception keeps the plugin running.
    const auto undefinedAnswer = answer();
    const auto thrown = answer();
    REQUIRE_FALSE( fixture.ask( id, "undefinedtip", undefinedAnswer ).isEmpty() );
    REQUIRE( waitUntil( [ & ] { return undefinedAnswer->done; } ) );
    REQUIRE_FALSE( fixture.ask( id, "throwtip", thrown ).isEmpty() );
    REQUIRE( waitUntil( [ & ] { return thrown->done; } ) );
    REQUIRE( undefinedAnswer->outcome.status == Status::Nothing );
    REQUIRE( thrown->outcome.status == Status::Error );
    REQUIRE( fixture.manager->pluginState( id ) == "ready" );
    const auto stats = fixture.stats( id );
    REQUIRE( stats.value( "sent" ).toInt() == 7 );
    REQUIRE( stats.value( "answered" ).toInt() == 3 );
    REQUIRE( stats.value( "failed" ).toInt() == 3 );
    REQUIRE( stats.value( "timedOut" ).toInt() == 1 );
    REQUIRE( fixture.manager->plugins().front().toMap().value( "queries" ).toList().size() == 1 );
}

TEST_CASE( "E17 late answers and answers from another plugin are dropped", "[plugins][E17]" )
{
    QueryFixture fixture( { queryManifest( "org.test.a" ), queryManifest( "org.test.b" ) } );
    fixture.start( "org.test.a" );
    fixture.start( "org.test.b" );
    // The plugin ignores the cancellation and answers after the timeout.
    const auto late = answer();
    const auto lateId = fixture.ask( "org.test.a", "latetip", late, 100 );
    REQUIRE( waitUntil( [ & ] { return late->done; } ) );
    REQUIRE( late->outcome.status == Status::TimedOut );
    REQUIRE( waitUntil( [ & ] { return fixture.received( "org.test.a", lateId ); } ) );
    REQUIRE( fixture.manager->pluginState( "org.test.a" ) == "ready" );
    REQUIRE( fixture.stats( "org.test.a" ).value( "answered" ).toInt() == 0 );

    // Another plugin answering a's query id changes nothing.
    const auto slow = answer();
    const auto slowId = fixture.ask( "org.test.a", "slowtip", slow, 1500 );
    REQUIRE_FALSE( slowId.isEmpty() );
    REQUIRE( fixture.manager->invoke( "org.test.b", "respond", { { "id", slowId } }, {} ).ok() );
    REQUIRE( waitUntil( [ & ] { return fixture.received( "org.test.b", slowId ); } ) );
    REQUIRE_FALSE( slow->done );
    REQUIRE( fixture.manager->queries().outstanding( "org.test.a" ) == 1 );
    REQUIRE( fixture.manager->pluginState( "org.test.b" ) == "ready" );
    REQUIRE( waitUntil( [ & ] { return slow->done; } ) );
}

TEST_CASE( "E17 queries go only to ready plugins that list the method in handles and features",
           "[plugins][E17]" )
{
    QueryFixture fixture( { queryManifest( "org.test.ready" ),
                            queryManifest( "org.test.nohandles", {}, false ),
                            queryManifest( "org.test.nofeatures", "no-queries" ) } );
    QString reason;
    // Not started: nothing goes through the start-up queue.
    REQUIRE( fixture.ask( "org.test.ready", "tip", answer(), 0, {}, {}, &reason ).isEmpty() );
    REQUIRE( reason == "not ready" );
    REQUIRE( fixture.manager->declaresHandler( "org.test.ready", "tooltip/resolve" ) );
    REQUIRE_FALSE( fixture.manager->declaresHandler( "org.test.nohandles", "tooltip/resolve" ) );
    fixture.start( "org.test.nohandles" );
    fixture.start( "org.test.nofeatures" );
    REQUIRE( fixture.ask( "org.test.nohandles", "tip", answer(), 0, {}, {}, &reason ).isEmpty() );
    REQUIRE( reason == "not handled" );
    REQUIRE(
        fixture.ask( "org.test.nofeatures", "tip", answer(), 0, {}, {}, &reason ).isEmpty() );
    REQUIRE( reason == "not handled" );
    REQUIRE( fixture.count( "tooltip/resolve" ) == 0 );
    REQUIRE( fixture.manager->query( "org.test.ready", "invoke", {}, 0, {}, {}, {}, &reason )
                 .isEmpty() );
    REQUIRE( reason == "not a query method" );
}

TEST_CASE( "E17 queries respect the outstanding limit and the exclusive key", "[plugins][E17]" )
{
    const QString id = "org.test.a";
    QueryFixture fixture( { queryManifest( id ) } );
    fixture.start( id );
    // A newer query with the same key replaces the pending one.
    const auto first = answer();
    const auto second = answer();
    const auto firstId = fixture.ask( id, "slowtip", first, 3000, "tooltip:main" );
    REQUIRE_FALSE( fixture.ask( id, "slowtip", second, 3000, "tooltip:main" ).isEmpty() );
    REQUIRE( first->done );
    REQUIRE( first->outcome.status == Status::Cancelled );
    REQUIRE( fixture.count( "query/cancel", firstId ) == 1 );
    REQUIRE( fixture.manager->queries().outstanding( id ) == 1 );
    // At most eight outstanding queries per plugin.
    QList<std::shared_ptr<Answer>> answers;
    for ( int index = 1; index < PluginQueries::MaxOutstanding; ++index ) {
        answers.append( answer() );
        REQUIRE_FALSE( fixture.ask( id, "slowtip", answers.back(), 3000 ).isEmpty() );
    }
    QString reason;
    REQUIRE( fixture.ask( id, "slowtip", answer(), 3000, {}, {}, &reason ).isEmpty() );
    REQUIRE( reason == "busy" );
    // Stopping the plugin ends them at once, without query/cancel.
    const auto cancels = fixture.count( "query/cancel" );
    REQUIRE( fixture.manager->manage( "disable", id, {} ).ok() );
    REQUIRE( waitUntil( [ & ] { return second->done; } ) );
    REQUIRE( second->outcome.status == Status::Cancelled );
    REQUIRE( std::all_of( answers.begin(), answers.end(), []( const auto& item ) {
        return item->done && item->outcome.status == Status::Cancelled;
    } ) );
    REQUIRE( fixture.count( "query/cancel" ) == cancels );
    REQUIRE( fixture.manager->queries().outstanding( id ) == 0 );
}

TEST_CASE( "E17 closing a view ends its queries with query/cancel", "[plugins][E17]" )
{
    const QString id = "org.test.a";
    QueryFixture fixture( { queryManifest( id ) } );
    fixture.manager->noteActiveWindow( "w1" );
    REQUIRE( fixture.manager->openView( id, "tips", { { "windowId", "w1" } } ).ok() );
    REQUIRE( waitUntil( [ & ] { return fixture.manager->pluginState( id ) == "ready"; } ) );
    const auto slow = answer();
    const auto slowId
        = fixture.ask( id, "slowtip", slow, 3000, {}, PluginManager::viewScope( "w1", "tips" ) );
    REQUIRE_FALSE( slowId.isEmpty() );
    fixture.manager->closeView( id, "tips", "w1" );
    REQUIRE( slow->done );
    REQUIRE( slow->outcome.status == Status::Cancelled );
    REQUIRE( fixture.count( "query/cancel", slowId ) == 1 );
}

TEST_CASE( "E17 the circuit breaker pauses a failing method; null answers do not count",
           "[plugins][E17]" )
{
    const QString id = "org.test.a";
    QueryFixture fixture( { queryManifest( id ) } );
    fixture.start( id );
    fixture.manager->queries().setBreakerPauseMs( 400 );
    const auto run = [ & ]( const QString& element ) {
        const auto result = answer();
        REQUIRE_FALSE( fixture.ask( id, element, result ).isEmpty() );
        REQUIRE( waitUntil( [ & ] { return result->done; } ) );
    };
    for ( int index = 0; index < 4; ++index ) {
        run( "errortip" );
    }
    run( "nulltip" ); // a valid "nothing" resets the count
    for ( int index = 0; index < 4; ++index ) {
        run( "errortip" );
    }
    REQUIRE( fixture.stats( id ).value( "pausedUntil" ).toString().isEmpty() );
    QSignalSpy breaker( &fixture.manager->queries(), &PluginQueries::breakerChanged );
    run( "invalidtip" ); // the fifth failure in a row
    REQUIRE( breaker.size() == 1 );
    REQUIRE_FALSE( fixture.stats( id ).value( "pausedUntil" ).toString().isEmpty() );
    QString reason;
    REQUIRE( fixture.ask( id, "tip", answer(), 0, {}, {}, &reason ).isEmpty() );
    REQUIRE( reason == "paused" );
    QThread::msleep( 450 );
    run( "tip" );
    REQUIRE( breaker.size() == 2 );
}

TEST_CASE( "E17 get_plugin_tooltip reads its target from request fields and CLI arguments",
           "[plugins][E17]" )
{
    // Python scripts send element_id as the request field elementId.
    const auto fromFields = commanderRequestFromVariantMap(
        { { "action", "get_plugin_tooltip" },
          { "pluginId", "org.test.a" },
          { "viewId", "tips" },
          { "elementId", "rows" },
          { "rowId", "r1" },
          { "columnId", "c" },
          { "noCache", true } } );
    REQUIRE( fromFields );
    REQUIRE( fromFields->action == CommanderAction::GetPluginTooltip );
    REQUIRE( fromFields->pluginArguments.value( "elementId" ) == "rows" );
    REQUIRE( fromFields->pluginArguments.value( "rowId" ) == "r1" );
    REQUIRE( fromFields->pluginArguments.value( "columnId" ) == "c" );
    REQUIRE( fromFields->pluginArguments.value( "noCache" ).toBool() );
    // The CLI form, through the request map round trip.
    CommanderRequest cli;
    cli.action = CommanderAction::GetPluginTooltip;
    cli.pluginId = "org.test.a";
    cli.viewId = "tips";
    cli.pluginArguments = { { "elementId", "tip" } };
    const auto roundTrip = commanderRequestFromVariantMap( commanderRequestToVariantMap( cli ) );
    REQUIRE( roundTrip );
    REQUIRE( roundTrip->pluginArguments.value( "elementId" ) == "tip" );
    REQUIRE( commanderActionToString( CommanderAction::GetPluginTooltip ) == "get_plugin_tooltip" );
    // --window-index picks the window; a tab selector is refused (Codex review of #130).
    const QStringList base{ "cilogg", "command", "--action", "get_plugin_tooltip", "--plugin-id",
                            "org.test.a",  "--view-id", "tips",     "--element-id", "rows" };
    const CliParameters windowed( base + QStringList{ "--window-index", "2" } );
    REQUIRE( windowed.commander_request );
    REQUIRE( windowed.commander_request->windowIndex == 2 );
    const CliParameters plain( base );
    REQUIRE( plain.commander_request );
    REQUIRE_FALSE( plain.commander_request->windowIndex );
    REQUIRE_FALSE( CliParameters( base + QStringList{ "--tab-id", "t1" } ).commander_request );
    REQUIRE_FALSE( CliParameters( base + QStringList{ "--window-index", "x" } ).commander_request );
}

TEST_CASE( "E17 ui/icons and tooltip/invalidate from a plugin reach the UI", "[plugins][E17]" )
{
    const QString id = "org.test.a";
    QueryFixture fixture( { queryManifest( id ) } );
    QSignalSpy requests( fixture.manager.get(), &PluginManager::viewRequest );
    fixture.start( id );
    for ( const auto* action : { "icons", "invalidate" } ) {
        const auto job = fixture.manager->invoke( id, action, {}, {} );
        REQUIRE( job.ok() );
        const auto jobId = job.payload.value( "jobId" ).toString();
        REQUIRE( waitUntil( [ & ] { return finishedJob( *fixture.manager, jobId ); } ) );
    }
    REQUIRE( waitUntil( [ & ] { return requests.size() >= 2; } ) );
    REQUIRE( requests.at( 0 ).at( 1 ) == "ui/icons" );
    REQUIRE(
        requests.at( 0 ).at( 2 ).toJsonObject().value( "icons" ).toObject().contains( "dot" ) );
    REQUIRE( requests.at( 1 ).at( 1 ) == "tooltip/invalidate" );
    REQUIRE( requests.at( 1 ).at( 2 ).toJsonObject().value( "element_id" ) == "counttip" );
}
