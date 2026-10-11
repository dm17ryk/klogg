#include "log.h"
#include "mainwindow.h"
#include "pluginuicontroller.h"

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
