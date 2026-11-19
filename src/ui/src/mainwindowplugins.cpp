#include "abstractlogview.h"
#include "analysissource.h"
#include "log.h"
#include "mainwindow.h"
#include "pluginmenus.h"
#include "pluginuicontroller.h"
#include "pluginview.h"
#include "pluginviewhost.h"
#include "shortcuts.h"
#include "streamsession.h"
#include <QEventLoop>
#include <QPointer>
#include <QRegularExpression>
#include <QShortcut>
#include <QTimer>
#include <algorithm>
#include <memory>

namespace {
// `--lines 10-20,30`: 1-based file line ranges, at most 64 (docs/plugin-sdk-advanced.md 15).
bool parseLineRanges( const QString& text, QList<QPair<qint64, qint64>>* ranges, QString* error )
{
    static const QRegularExpression part(
        QRegularExpression::anchoredPattern( QStringLiteral( "\\s*(\\d{1,12})(?:\\s*-\\s*(\\d{1,12}))?\\s*" ) ) );
    for ( const auto& piece : text.split( QLatin1Char( ',' ), Qt::SkipEmptyParts ) ) {
        const auto match = part.match( piece );
        const auto first = match.captured( 1 ).toLongLong();
        const auto last = match.captured( 2 ).isEmpty() ? first : match.captured( 2 ).toLongLong();
        if ( !match.hasMatch() || first < 1 || last < first ) {
            *error = QStringLiteral( "Invalid --lines: %1 (use 10-20,30)." ).arg( piece.left( 32 ) );
            return false;
        }
        ranges->append( { first, last } );
    }
    if ( ranges->size() > cilogg::plugins::MaxMenuSelectionRanges ) {
        *error = QStringLiteral( "--lines names more than %1 ranges." )
                     .arg( cilogg::plugins::MaxMenuSelectionRanges );
        return false;
    }
    return true;
}

void applySelection( PluginMenuContext* context, const PluginMenuHost::LogSelection& selection )
{
    context->view = selection.view;
    context->line = selection.line;
    context->ranges = selection.ranges;
    context->selectedLines = selection.count;
    context->portion = selection.portion;
    context->text = selection.text;
}
} // namespace

PluginMenuContext MainWindow::pluginMenuContext( const QString& location, const QString& tabId,
                                                 const QList<QPair<qint64, qint64>>& lines ) const
{
    PluginMenuContext context;
    context.location = location;
    context.windowId = session_.windowId();
    context.windowIndex = static_cast<int>( session_.windowIndex() );
    const auto fillPane = [ this, &context ]( LogPane* pane ) {
        if ( pane == nullptr ) {
            return;
        }
        context.paneIndex = paneArea_->paneIndexOf( pane );
        const auto* tabs = pane->tabs();
        for ( int index = 0; index < tabs->count(); ++index ) {
            context.paneTabIds.append( tabs->tabIdAt( index ) );
        }
        if ( tabs->count() > 0 ) {
            context.currentTabId = tabs->tabIdAt( tabs->currentIndex() );
        }
    };
    CommanderRequest request;
    request.tabId = tabId;
    auto* crawler = commanderTargetCrawler( request );
    if ( crawler == nullptr ) {
        if ( tabId.isEmpty() ) {
            fillPane( activePane() );
        }
        return context;
    }
    context.tabId = paneArea_->tabIdAt( paneArea_->indexOf( crawler ) );
    context.filePath = session_.getFilename( crawler );
    if ( auto* stream = paneArea_->streamSessionForPath( context.filePath ) ) {
        context.portName = stream->captureSettings().portName;
    }
    const auto* manager = pluginController_ ? pluginController_->manager() : nullptr;
    context.tabKind = !context.portName.isEmpty() ? QStringLiteral( "serial" )
                      : manager != nullptr && manager->isPluginCapture( context.filePath )
                          ? QStringLiteral( "plugin_stream" )
                          : QStringLiteral( "file" );
    fillPane( paneArea_->paneForTabId( context.tabId ) );
    if ( location == QLatin1String( "log" ) ) {
        if ( !lines.isEmpty() ) {
            // Automation names file lines; the main view shows every one of them. Ranges are
            // clamped to the file, sorted and merged (none left: an empty selection).
            const QPointer<AbstractLogView> view( crawler->mainLogView() );
            const auto total = view ? view->pluginLineCount() : 0;
            auto sorted = lines;
            std::sort( sorted.begin(), sorted.end() );
            QList<QPair<qint64, qint64>> ranges;
            for ( const auto& range : sorted ) {
                const auto first = qMax( qint64( 1 ), range.first );
                const auto last = qMin( total, range.second );
                if ( first > last ) {
                    continue;
                }
                if ( !ranges.isEmpty() && first <= ranges.back().second + 1 ) {
                    ranges.back().second = qMax( ranges.back().second, last );
                }
                else {
                    ranges.append( { first, last } );
                }
            }
            PluginMenuHost::LogSelection selection;
            selection.view = QStringLiteral( "main" );
            selection.ranges = ranges;
            for ( const auto& range : ranges ) {
                selection.count += range.second - range.first + 1;
            }
            selection.text = [ view, ranges ] {
                return view ? view->pluginLinesText( ranges,
                                                     cilogg::plugins::MaxMenuSelectionTextBytes + 1 )
                            : QString{};
            };
            applySelection( &context, selection );
        }
        else if ( auto* view = crawler->pluginLogView() ) {
            applySelection( &context, view->pluginSelection( {} ) );
        }
    }
    return context;
}

bool MainWindow::hasPluginLogEntries() const
{
    const auto* manager = pluginController_ ? pluginController_->manager() : nullptr;
    if ( manager == nullptr ) {
        return false;
    }
    for ( const auto& source : manager->menuSources() ) {
        if ( source.hidden.contains( QStringLiteral( "log" ) ) ) {
            continue;
        }
        for ( const auto& menu : source.manifest.value( "menus" ).toArray() ) {
            if ( menu.toObject().value( "location" ) == QLatin1String( "log" ) ) {
                return true;
            }
        }
    }
    return false;
}

QList<QAction*> MainWindow::addPluginLogEntries( QMenu& menu, QAction* before, QWidget* origin,
                                                 const LogSelection& selection )
{
    auto* menus = pluginController_ ? pluginController_->menus() : nullptr;
    if ( menus == nullptr ) {
        return {};
    }
    QString tabId;
    for ( auto* pane : paneArea_->panes() ) {
        const auto* tabs = pane->tabs();
        for ( int index = 0; index < tabs->count() && tabId.isEmpty(); ++index ) {
            const auto* tab = tabs->widget( index );
            if ( tab != nullptr && ( tab == origin || tab->isAncestorOf( origin ) ) ) {
                tabId = tabs->tabIdAt( index );
            }
        }
    }
    if ( tabId.isEmpty() ) {
        return {};
    }
    // The tab's context, with the selection of the view the menu belongs to.
    auto context = pluginMenuContext( QStringLiteral( "tab" ), tabId );
    context.location = QStringLiteral( "log" );
    applySelection( &context, selection );
    return menus->addSection( &menu, before, context );
}

void MainWindow::removePluginLogEntries( QMenu& menu, const QList<QAction*>& entries )
{
    PluginMenus::removeSection( &menu, entries );
}

void MainWindow::addPluginMenuSection( QMenu& menu, const QString& location, const QString& tabId,
                                       LogPane* pane )
{
    auto* menus = pluginController_ ? pluginController_->menus() : nullptr;
    if ( menus == nullptr ) {
        return;
    }
    auto context = pluginMenuContext( location, tabId );
    if ( tabId.isEmpty() && pane != nullptr ) {
        // An empty pane: its own index, not the active pane's.
        context.paneIndex = paneArea_->paneIndexOf( pane );
        context.paneTabIds.clear();
        context.currentTabId.clear();
    }
    menus->addSection( &menu, nullptr, context );
}

std::map<QString, QString> MainWindow::hostShortcutKeys() const
{
    std::map<QString, QString> keys;
    const auto add = [ &keys ]( const QKeySequence& sequence, const QString& owner ) {
        const auto text = sequence.toString( QKeySequence::PortableText );
        if ( !text.isEmpty() ) {
            keys.emplace( text, owner );
        }
    };
    const auto configured = Configuration::get().shortcuts();
    for ( const auto& [ action, shortcut ] : ShortcutAction::defaultShortcutList() ) {
        for ( const auto& sequence : ShortcutAction::shortcutKeys( action, configured ) ) {
            add( sequence, shortcut.name );
        }
    }
    // The window's own actions and shortcuts (split panes, log view commands); not the plugins'.
    for ( const auto* action : findChildren<QAction*>() ) {
        if ( action->objectName().startsWith( QLatin1String( "pluginMenuItem_" ) ) ) {
            continue;
        }
        for ( const auto& sequence : action->shortcuts() ) {
            add( sequence, action->text().remove( QLatin1Char( '&' ) ) );
        }
    }
    for ( const auto* shortcut : findChildren<QShortcut*>() ) {
        if ( !shortcut->objectName().startsWith( QLatin1String( "pluginShortcut_" ) ) ) {
            add( shortcut->key(), tr( "shortcut" ) );
        }
    }
    return keys;
}

void MainWindow::updatePluginShortcuts()
{
    auto* menus = pluginController_ ? pluginController_->menus() : nullptr;
    if ( menus == nullptr ) {
        return;
    }
    std::map<QString, QStringList> user;
    for ( const auto& [ action, keys ] : Configuration::get().shortcuts() ) {
        const auto name = QString::fromStdString( action );
        if ( name.startsWith( QLatin1String( "plugin/" ) ) ) {
            user[ name ] = keys;
        }
    }
    menus->registerShortcuts( hostShortcutKeys(), user );
}

bool MainWindow::pluginElementMenuContext( const CommanderRequest& request,
                                           PluginMenuContext* context, QString* error ) const
{
    const auto elementId = request.pluginArguments.value( "elementId" ).toString();
    if ( request.pluginId.isEmpty() || request.viewId.isEmpty() || elementId.isEmpty() ) {
        *error = tr( "Element menus need --plugin-id, --view-id and --element-id." );
        return false;
    }
    auto* host = pluginController_ ? pluginController_->viewHost() : nullptr;
    const auto* view = host ? host->view( request.pluginId, request.viewId ) : nullptr;
    if ( view == nullptr ) {
        *error = tr( "The plugin view is not open in this window." );
        return false;
    }
    QString menuId;
    QJsonObject target;
    if ( !view->elementMenu( elementId, request.pluginArguments.value( "rowId" ).toString(),
                             &menuId, &target, error ) ) {
        return false;
    }
    *context = host->elementMenuContext( request.viewId, elementId, menuId, target );
    context->windowIndex = static_cast<int>( session_.windowIndex() );
    return true;
}

CommanderResult MainWindow::commanderGetPluginMenus( const CommanderRequest& request )
{
    auto* menus = pluginController_ ? pluginController_->menus() : nullptr;
    if ( menus == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Plugins are not available in this window." ) );
    }
    const auto& arguments = request.pluginArguments;
    const auto location = arguments.value( "location" ).toString();
    if ( !cilogg::plugins::renderedMenuLocations().contains( location ) ) {
        return commanderFailure( CommanderResultCode::InvalidRequest,
                                 tr( "--location is one of %1." )
                                     .arg( cilogg::plugins::renderedMenuLocations().join( ", " ) ) );
    }
    QString tabId;
    if ( hasCommanderTabSelector( request ) ) {
        auto* crawler = commanderTargetCrawler( request );
        if ( crawler == nullptr ) {
            return commanderFailure( CommanderResultCode::NotFound, tr( "The tab was not found." ) );
        }
        tabId = paneArea_->tabIdAt( paneArea_->indexOf( crawler ) );
    }
    QList<QPair<qint64, qint64>> lines;
    QString error;
    if ( !parseLineRanges( arguments.value( "lines" ).toString(), &lines, &error ) ) {
        return commanderFailure( CommanderResultCode::InvalidRequest, error );
    }
    PluginMenuContext context;
    if ( location == QLatin1String( "element" ) ) {
        // E17d: the context menu of a table in an open plugin view.
        if ( !pluginElementMenuContext( request, &context, &error ) ) {
            return commanderFailure( CommanderResultCode::InvalidRequest, error );
        }
    }
    else {
        context = pluginMenuContext( location, tabId, lines );
    }
    if ( !lines.isEmpty() && context.selectedLines == 0 ) {
        return commanderFailure( CommanderResultCode::InvalidRequest,
                                 tr( "--lines names no line of the file." ) );
    }
    const auto entries = menus->resolve( location, context, request.pluginId );
    LOG_DEBUG << "get_plugin_menus location=" << location.toStdString()
              << " tab=" << context.tabId.toStdString() << " entries=" << entries.size();
    return commanderSuccess( {}, { { "location", location },
                                   { "windowIndex", context.windowIndex },
                                   { "tabId", context.tabId },
                                   { "context", context.toJson( false ).toVariantMap() },
                                   { "entries", entries } } );
}

CommanderResult MainWindow::commanderTriggerPluginMenu( const CommanderRequest& request )
{
    auto* menus = pluginController_ ? pluginController_->menus() : nullptr;
    auto* manager = pluginController_ ? pluginController_->manager() : nullptr;
    if ( menus == nullptr || manager == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "Plugins are not available in this window." ) );
    }
    const auto& arguments = request.pluginArguments;
    const auto menuId = arguments.value( "menuId" ).toString();
    const auto itemId = arguments.value( "itemId" ).toString();
    QString location;
    for ( const auto& source : manager->menuSources() ) {
        if ( source.id == request.pluginId ) {
            location = cilogg::plugins::findMenu( source.manifest, menuId )
                           .value( "location" )
                           .toString();
        }
    }
    if ( location.isEmpty() ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "The plugin is not enabled or declares no such menu." ) );
    }
    QString tabId;
    if ( hasCommanderTabSelector( request ) ) {
        auto* crawler = commanderTargetCrawler( request );
        if ( crawler == nullptr ) {
            return commanderFailure( CommanderResultCode::NotFound, tr( "The tab was not found." ) );
        }
        tabId = paneArea_->tabIdAt( paneArea_->indexOf( crawler ) );
    }
    QList<QPair<qint64, qint64>> lines;
    QString error;
    if ( !parseLineRanges( arguments.value( "lines" ).toString(), &lines, &error ) ) {
        return commanderFailure( CommanderResultCode::InvalidRequest, error );
    }
    std::optional<bool> checked;
    if ( arguments.contains( "checked" ) ) {
        checked = arguments.value( "checked" ).toBool();
    }
    PluginMenuContext context;
    if ( location == QLatin1String( "element" ) ) {
        // E17d: an entry of a table's context menu, on the given row and the table's selection.
        if ( !pluginElementMenuContext( request, &context, &error ) ) {
            return commanderFailure( CommanderResultCode::InvalidRequest, error );
        }
    }
    else {
        context = pluginMenuContext( location, tabId, lines );
    }
    if ( !lines.isEmpty() && context.selectedLines == 0 ) {
        return commanderFailure( CommanderResultCode::InvalidRequest,
                                 tr( "--lines names no line of the file." ) );
    }
    auto result = menus->trigger( request.pluginId, menuId, itemId, std::move( context ), checked );
    LOG_DEBUG << "trigger_plugin_menu plugin=" << request.pluginId.toStdString()
              << " menu=" << menuId.toStdString() << " item=" << itemId.toStdString()
              << " ok=" << result.ok();
    return result;
}

void MainWindow::setPluginManager(
    cilogg::plugins::PluginManager& manager,
    std::function<CommanderResult( const CommanderRequest& )> executor )
{
    if ( pluginController_ ) {
        return;
    }
    pluginController_ = new PluginUiController( this, &manager, std::move( executor ) );
    // The controller's first refresh ran before it was ours: register the plugin shortcuts now.
    updatePluginShortcuts();
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

CommanderResult MainWindow::commanderGetPluginTooltip( const CommanderRequest& request )
{
    auto* host = pluginController_ ? pluginController_->viewHost() : nullptr;
    auto* view = host ? host->view( request.pluginId, request.viewId ) : nullptr;
    if ( view == nullptr ) {
        return commanderFailure( CommanderResultCode::NotFound,
                                 tr( "The plugin view is not open in this window." ) );
    }
    const auto& arguments = request.pluginArguments;
    const auto elementId = arguments.value( "elementId" ).toString();
    const auto rowId = arguments.value( "rowId" ).toString();
    const auto columnId = arguments.value( "columnId" ).toString();
    if ( !arguments.value( "nodeId" ).toString().isEmpty() ) {
        return commanderFailure( CommanderResultCode::InvalidRequest,
                                 tr( "This CILogg has no tree elements; --node-id needs one." ) );
    }
    // The same query path as a hover; the answer arrives within the query timeout (at most
    // 5 s), so a nested event loop waits for it as search --wait does.
    struct State {
        bool finished = false;
        QVariantMap payload;
        QPointer<QEventLoop> loop;
    };
    auto state = std::make_shared<State>();
    QString error;
    const auto started = view->resolveTooltip(
        elementId, rowId, columnId, !arguments.value( "noCache" ).toBool(),
        [ state, tooltips = QPointer<PluginTooltips>( view->tooltips() ) ](
            const QString& source, const QJsonObject& tooltip, const QString& staticTip,
            qint64 latencyMs ) {
            state->finished = true;
            // The HTML the GUI shows, with the answer's icon (render() alone leaves it out).
            const auto html = tooltip.isEmpty() ? QString{}
                              : tooltips        ? tooltips->renderWithIcon( tooltip )
                                                : PluginTooltips::render( tooltip );
            state->payload = { { "source", source },
                               { "tooltip", tooltip.isEmpty() ? QVariant()
                                                              : QVariant( tooltip.toVariantMap() ) },
                               { "html", html },
                               { "staticTooltip", staticTip },
                               { "latencyMs", latencyMs } };
            if ( state->loop ) {
                state->loop->quit();
            }
        },
        &error );
    if ( !started ) {
        return commanderFailure( CommanderResultCode::InvalidRequest, error );
    }
    const QPointer<MainWindow> self( this );
    if ( !state->finished ) {
        QEventLoop loop;
        state->loop = &loop;
        QTimer::singleShot( 6000, &loop, &QEventLoop::quit );
        loop.exec();
    }
    if ( !self ) {
        return commanderFailure( CommanderResultCode::ExecutionFailed,
                                 QStringLiteral( "The window closed." ) );
    }
    if ( !state->finished ) {
        return commanderFailure( CommanderResultCode::ExecutionFailed,
                                 tr( "The tooltip did not arrive in time." ) );
    }
    auto payload = state->payload;
    payload.insert( "pluginId", request.pluginId );
    payload.insert( "viewId", request.viewId );
    payload.insert( "elementId", elementId );
    payload.insert( "rowId", rowId );
    payload.insert( "columnId", columnId );
    LOG_DEBUG << "get_plugin_tooltip plugin=" << request.pluginId.toStdString()
              << " view=" << request.viewId.toStdString() << " element=" << elementId.toStdString()
              << " source=" << payload.value( "source" ).toString().toStdString();
    return commanderSuccess( {}, payload );
}
