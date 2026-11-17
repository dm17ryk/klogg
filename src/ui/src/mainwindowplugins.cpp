#include "analysissource.h"
#include "log.h"
#include "mainwindow.h"
#include "pluginuicontroller.h"
#include "pluginviewhost.h"

void MainWindow::setPluginManager(
    cilogg::plugins::PluginManager& manager,
    std::function<CommanderResult( const CommanderRequest& )> executor )
{
    if ( pluginController_ ) {
        return;
    }
    pluginController_ = new PluginUiController( this, &manager, std::move( executor ) );
    LOG_DEBUG << "Plugin UI attached window=" << session_.windowId().toStdString();
}

QVariantMap MainWindow::pluginContext( const CommanderRequest& request ) const
{
    if ( request.windowIndex
         && *request.windowIndex != static_cast<int>( session_.windowIndex() ) ) {
        return {};
    }
    auto* crawler = commanderTargetCrawler( request );
    if ( !crawler && ( !request.tabId.isEmpty() || request.tabIndex ) ) {
        return {};
    }
    QVariantMap context{ { "windowId", session_.windowId() },
                         { "windowIndex", static_cast<int>( session_.windowIndex() ) } };
    if ( crawler ) {
        const auto tabContext = scriptEventContextForFile( session_.getFilename( crawler ) );
        for ( auto it = tabContext.cbegin(); it != tabContext.cend(); ++it ) {
            context.insert( it.key(), it.value() );
        }
    }
    return context;
}

QVariantMap MainWindow::pluginContextForSource( const QString& source, QString* error )
{
    const auto fail = [ error ]( const QString& message ) {
        if ( error ) {
            *error = message;
        }
        return QVariantMap{};
    };
    QString message;
    const auto parsed = AnalysisSource::parse( source, &message );
    if ( !parsed ) {
        return fail( message );
    }
    if ( parsed->kind == AnalysisSource::Kind::Focused ) {
        return pluginContext( {} );
    }
    auto resolution = resolveAnalysisSource( *parsed, OpenSource::fromVariantList( toolSources() ),
                                             SourceRules::oneSource() );
    if ( resolution.ok() && !resolution.fileToOpen.isEmpty() ) {
        if ( !loadFile( resolution.fileToOpen, false ) ) {
            return fail( tr( "Failed to open %1." ).arg( resolution.fileToOpen ) );
        }
        resolution = resolveAnalysisSource( *parsed, OpenSource::fromVariantList( toolSources() ),
                                            SourceRules::oneSource() );
    }
    if ( !resolution.ok() || resolution.tabIds.isEmpty() ) {
        return fail( resolution.message.isEmpty() ? tr( "The selected tab is closed." )
                                                  : resolution.message );
    }
    CommanderRequest request;
    request.tabId = resolution.tabIds.front();
    auto context = pluginContext( request );
    if ( context.isEmpty() && windowForTab_ ) {
        if ( auto* owner = windowForTab_( request.tabId ); owner && owner != this ) {
            context = owner->pluginContext( request );
        }
    }
    if ( context.isEmpty() ) {
        return fail( tr( "The selected tab is closed." ) );
    }
    // The view lives in this window even when it follows a tab of another window.
    context.insert( "windowId", session_.windowId() );
    context.insert( "windowIndex", static_cast<int>( session_.windowIndex() ) );
    return context;
}

CommanderResult MainWindow::commanderOpenPluginView( const CommanderRequest& request )
{
    auto* host = pluginController_ ? pluginController_->viewHost() : nullptr;
    if ( host == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Plugins are not available in this window." ) );
    }
    // An explicit tab selector wins over the source.
    auto source = request.source;
    if ( hasCommanderTabSelector( request ) ) {
        auto* crawler = commanderTargetCrawler( request );
        const auto tabId
            = crawler ? paneArea_->tabIdAt( paneArea_->indexOf( crawler ) ) : request.tabId;
        source = QStringLiteral( "tab:" ) + tabId;
    }
    QJsonObject overrides;
    if ( !source.isEmpty() ) {
        overrides.insert( "source", source );
    }
    QString error;
    if ( !host->openView( request.pluginId, request.viewId, overrides, &error ) ) {
        LOG_DEBUG << "open_plugin_view refused plugin=" << request.pluginId.toStdString()
                  << " view=" << request.viewId.toStdString() << " reason=" << error.toStdString();
        return { CommanderResultCode::InvalidRequest, error, { { "reason", "view_not_opened" } } };
    }
    return commanderSuccess( {},
                             { { "pluginId", request.pluginId },
                               { "viewId", request.viewId },
                               { "source", host->viewSource( request.pluginId, request.viewId ) },
                               { "windowIndex", static_cast<int>( session_.windowIndex() ) } } );
}
