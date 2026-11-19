// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugintooltips.h"

#include "log.h"
#include "plugintablemodel.h"
#include <QCursor>
#include <QEvent>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QSet>
#include <QToolTip>
#include <QWidget>
#include <algorithm>
#include <memory>
#include <utility>

namespace {
constexpr int MaxTooltipWidth = 480;
constexpr QChar Separator( 0x1f );

QString escaped( const QString& text )
{
    auto html = text.toHtmlEscaped();
    html.replace( '\n', QStringLiteral( "<br>" ) );
    return html;
}
} // namespace

QString PluginTooltips::Target::cacheKey() const
{
    // Length-prefixed parts: plugin ids may contain any character.
    QString result;
    for ( const auto* part : { &elementId, &rowId, &columnId, &nodeId, &key } ) {
        result += QString::number( part->size() ) + Separator + *part;
    }
    return result;
}

PluginTooltips::PluginTooltips( QObject* parent )
    : QObject( parent )
{
    clock_.start();
    loadingTimer_.setSingleShot( true );
    connect( &loadingTimer_, &QTimer::timeout, this, &PluginTooltips::showLoading );
}

PluginTooltips::~PluginTooltips()
{
    cancelPending();
}

void PluginTooltips::setQueries( PluginViewQueries queries )
{
    queries_ = std::move( queries );
}

void PluginTooltips::watch( QWidget* widget, Locator locator )
{
    if ( !widget ) {
        return;
    }
    if ( !locators_.contains( widget ) ) {
        widget->installEventFilter( this );
        connect( widget, &QObject::destroyed, this,
                 [ this, widget ] { locators_.remove( widget ); } );
    }
    widget->setMouseTracking( true );
    locators_.insert( widget, std::move( locator ) );
}

void PluginTooltips::invalidateElement( const QString& elementId )
{
    ++generation_;
    cache_.removeIf( [ & ]( const auto& item ) { return item.value().elementId == elementId; } );
}

void PluginTooltips::invalidateRows( const QString& elementId, const QStringList& rowIds )
{
    ++generation_;
    const QSet<QString> ids( rowIds.begin(), rowIds.end() ); // one lookup per cache entry
    cache_.removeIf( [ & ]( const auto& item ) {
        return item.value().elementId == elementId && ids.contains( item.value().rowId );
    } );
}

void PluginTooltips::invalidateNodes( const QString& elementId, const QStringList& nodeIds )
{
    ++generation_;
    const QSet<QString> ids( nodeIds.begin(), nodeIds.end() );
    cache_.removeIf( [ & ]( const auto& item ) {
        return item.value().elementId == elementId && ids.contains( item.value().nodeId );
    } );
}

void PluginTooltips::clear()
{
    ++generation_;
    cache_.clear();
}

void PluginTooltips::cancelPending()
{
    if ( !pendingId_.isEmpty() && queries_.cancel ) {
        queries_.cancel( pendingId_ );
    }
    pendingId_.clear();
    pendingKey_.clear();
    loadingTimer_.stop();
}

const QJsonObject* PluginTooltips::cached( const QString& cacheKey )
{
    const auto found = cache_.find( cacheKey );
    if ( found == cache_.end() ) {
        return nullptr;
    }
    if ( found->expires <= clock_.elapsed() ) {
        cache_.erase( found );
        return nullptr;
    }
    found->used = ++useCounter_;
    return &found->tooltip;
}

void PluginTooltips::store( const Target& target, const QJsonObject& tooltip, quint64 generation )
{
    const auto cacheKey = target.cacheKey();
    const auto ttl = static_cast<qint64>( tooltip.value( "ttl_ms" ).toDouble( 0 ) );
    if ( ttl <= 0 || generation != generation_ ) {
        cache_.remove( cacheKey ); // not kept, or asked before an invalidation
        return;
    }
    if ( !cache_.contains( cacheKey ) && cache_.size() >= CacheEntries ) {
        // Least recently used first out.
        const auto oldest = std::min_element(
            cache_.begin(), cache_.end(),
            []( const Entry& left, const Entry& right ) { return left.used < right.used; } );
        cache_.erase( oldest );
    }
    cache_.insert( cacheKey, { tooltip, clock_.elapsed() + ttl, ++useCounter_, target.elementId,
                               target.rowId, target.nodeId } );
}

QString PluginTooltips::render( const QJsonObject& tooltip, const QString& iconHtml )
{
    QStringList plain;
    QString html;
    const auto title = tooltip.value( "title" ).toString();
    if ( !title.isEmpty() || !iconHtml.isEmpty() ) {
        const auto color = pluginview::toneColor( tooltip.value( "tone" ).toString() );
        html += QStringLiteral( "<p style=\"margin:0\">" ) + iconHtml;
        if ( !title.isEmpty() ) {
            html += ( iconHtml.isEmpty() ? QString{} : QStringLiteral( "&nbsp;" ) )
                    + ( color.isValid()
                            ? QStringLiteral( "<b style=\"color:%1\">" ).arg( color.name() )
                            : QStringLiteral( "<b>" ) )
                    + escaped( title ) + QStringLiteral( "</b>" );
            plain.append( title );
        }
        html += QStringLiteral( "</p>" );
    }
    const auto text = tooltip.value( "text" ).toString();
    if ( !text.isEmpty() ) {
        html += QStringLiteral( "<p style=\"margin:0\">" ) + escaped( text )
                + QStringLiteral( "</p>" );
        plain.append( text.split( '\n' ) );
    }
    const auto rows = tooltip.value( "rows" ).toArray();
    if ( !rows.isEmpty() ) {
        html += QStringLiteral( "<table cellspacing=\"0\" cellpadding=\"1\">" );
        for ( const auto& row : rows ) {
            const auto pair = row.toArray();
            html += QStringLiteral( "<tr><td>%1&nbsp;&nbsp;</td><td>%2</td></tr>" )
                        .arg( escaped( pair.at( 0 ).toString() ),
                              escaped( pair.at( 1 ).toString() ) );
            plain.append( pair.at( 0 ).toString() + QStringLiteral( "    " )
                          + pair.at( 1 ).toString() );
        }
        html += QStringLiteral( "</table>" );
    }
    const auto footer = tooltip.value( "footer" ).toString();
    if ( !footer.isEmpty() ) {
        html += QStringLiteral( "<p style=\"margin:0; color:#808080\">" ) + escaped( footer )
                + QStringLiteral( "</p>" );
        plain.append( footer.split( '\n' ) );
    }
    // Wider than the limit: let the text wrap inside a fixed-width table.
    const QFontMetrics metrics( QToolTip::font() );
    const bool wide = std::any_of( plain.begin(), plain.end(), [ &metrics ]( const QString& line ) {
        return metrics.horizontalAdvance( line ) > MaxTooltipWidth;
    } );
    if ( wide ) {
        html = QStringLiteral( "<table width=\"%1\"><tr><td>%2</td></tr></table>" )
                   .arg( MaxTooltipWidth )
                   .arg( html );
    }
    return QStringLiteral( "<qt>" ) + html + QStringLiteral( "</qt>" );
}

QString PluginTooltips::renderWithIcon( const QJsonObject& tooltip ) const
{
    const auto icon = tooltip.value( "icon" );
    const auto iconHtml
        = !icon.isUndefined() && queries_.iconHtml ? queries_.iconHtml( icon ) : QString{};
    return render( tooltip, iconHtml );
}

void PluginTooltips::startQuery( const Target& target )
{
    if ( !queries_.resolveTooltip ) {
        return;
    }
    QJsonObject params{ { "element_id", target.elementId } };
    for ( const auto& [ key, value ] : { std::pair{ "row_id", target.rowId },
                                         std::pair{ "column_id", target.columnId },
                                         std::pair{ "node_id", target.nodeId },
                                         std::pair{ "tooltip_key", target.tooltipKey } } ) {
        if ( !value.isEmpty() ) {
            params.insert( QLatin1String( key ), value );
        }
    }
    const QPointer<PluginTooltips> guard( this );
    pendingId_ = queries_.resolveTooltip(
        params, target.timeoutMs, true,
        [ guard, target, generation = generation_ ]( bool valid, const QJsonObject& tooltip ) {
            if ( guard ) {
                guard->answered( target, valid, tooltip, generation );
            }
        } );
    pendingKey_ = pendingId_.isEmpty() ? QString{} : target.cacheKey();
}

bool PluginTooltips::showTooltip( QWidget* widget, const QPoint& position, const Target& target )
{
    if ( !target.valid() || !target.provider ) {
        if ( hover_.widget ) {
            leave();
        }
        return false; // Qt shows the static tooltip
    }
    const auto cacheKey = target.cacheKey();
    if ( hover_.widget != widget || hover_.target.cacheKey() != cacheKey ) {
        leave();
    }
    hover_ = { widget, target, widget->mapToGlobal( position ) };
    if ( const auto* tooltip = cached( cacheKey ) ) {
        QToolTip::showText( hover_.globalPosition, renderWithIcon( *tooltip ), widget,
                            target.rect );
        return true;
    }
    if ( pendingKey_ != cacheKey ) {
        cancelPending();
        startQuery( target );
    }
    if ( !target.staticTip.isEmpty() ) {
        QToolTip::showText( hover_.globalPosition, pluginview::plainToolTip( target.staticTip ),
                            widget, target.rect );
    }
    else if ( !pendingId_.isEmpty() ) {
        loadingTimer_.start( LoadingDelayMs );
    }
    else {
        QToolTip::hideText();
    }
    return true;
}

void PluginTooltips::showLoading()
{
    if ( pendingId_.isEmpty() || !hover_.widget || !hover_.widget->underMouse() ) {
        return;
    }
    loadingShown_ = true;
    QToolTip::showText( hover_.globalPosition,
                        QStringLiteral( "<qt><span style=\"color:#808080\">%1</span></qt>" )
                            .arg( tr( "Loading…" ) ),
                        hover_.widget, hover_.target.rect );
}

void PluginTooltips::answered( const Target& target, bool valid, const QJsonObject& tooltip,
                               quint64 generation )
{
    const auto cacheKey = target.cacheKey();
    if ( pendingKey_ == cacheKey ) {
        pendingId_.clear();
        pendingKey_.clear();
        loadingTimer_.stop();
    }
    if ( valid && !tooltip.isEmpty() ) {
        store( target, tooltip, generation );
    }
    if ( !hover_.widget || hover_.target.cacheKey() != cacheKey ) {
        return; // the pointer moved on
    }
    if ( valid && !tooltip.isEmpty() ) {
        loadingShown_ = false;
        QToolTip::showText( hover_.globalPosition, renderWithIcon( tooltip ), hover_.widget,
                            hover_.target.rect );
    }
    else if ( loadingShown_ ) {
        loadingShown_ = false;
        QToolTip::hideText(); // nothing, or the static tooltip that is already shown
    }
}

void PluginTooltips::leave()
{
    cancelPending();
    if ( loadingShown_ ) {
        loadingShown_ = false;
        QToolTip::hideText();
    }
    hover_ = {};
}

bool PluginTooltips::eventFilter( QObject* watched, QEvent* event )
{
    auto* widget = qobject_cast<QWidget*>( watched );
    const auto found = widget ? locators_.constFind( widget ) : locators_.cend();
    if ( found == locators_.cend() ) {
        return QObject::eventFilter( watched, event );
    }
    switch ( event->type() ) {
    case QEvent::ToolTip: {
        const auto* help = static_cast<QHelpEvent*>( event );
        return showTooltip( widget, help->pos(), ( *found )( help->pos() ) );
    }
    case QEvent::MouseMove:
        if ( hover_.widget == widget ) {
            const auto position = widget->mapFromGlobal( QCursor::pos() );
            if ( ( *found )( position ).cacheKey() != hover_.target.cacheKey() ) {
                leave(); // another cell: the old query is no longer wanted
            }
        }
        break;
    case QEvent::Leave:
        // A tooltip window shown under the pointer can send a Leave while the pointer is still
        // on the target: only a pointer that really left ends the query.
        if ( hover_.widget == widget
             && !widget->rect().contains( widget->mapFromGlobal( QCursor::pos() ) ) ) {
            leave();
        }
        break;
    case QEvent::Hide:
    case QEvent::MouseButtonPress:
    case QEvent::Wheel:
    case QEvent::KeyPress:
        if ( hover_.widget == widget ) {
            leave();
        }
        break;
    default:
        break;
    }
    return QObject::eventFilter( watched, event );
}

void PluginTooltips::resolve( const Target& target, bool useCache, Resolved done )
{
    if ( !target.valid() || !target.provider || !queries_.resolveTooltip ) {
        done( QStringLiteral( "static" ), {}, 0 );
        return;
    }
    const auto cacheKey = target.cacheKey();
    if ( useCache ) {
        if ( const auto* tooltip = cached( cacheKey ) ) {
            done( QStringLiteral( "cache" ), *tooltip, 0 );
            return;
        }
    }
    QJsonObject params{ { "element_id", target.elementId } };
    for ( const auto& [ key, value ] : { std::pair{ "row_id", target.rowId },
                                         std::pair{ "column_id", target.columnId },
                                         std::pair{ "node_id", target.nodeId },
                                         std::pair{ "tooltip_key", target.tooltipKey } } ) {
        if ( !value.isEmpty() ) {
            params.insert( QLatin1String( key ), value );
        }
    }
    QElapsedTimer elapsed;
    elapsed.start();
    const QPointer<PluginTooltips> guard( this );
    auto shared = std::make_shared<Resolved>( std::move( done ) );
    const auto id = queries_.resolveTooltip(
        params, target.timeoutMs, false,
        [ guard, target, shared, elapsed,
          generation = generation_ ]( bool valid, const QJsonObject& tooltip ) {
            if ( guard && valid && !tooltip.isEmpty() ) {
                guard->store( target, tooltip, generation );
            }
            if ( valid && !tooltip.isEmpty() ) {
                ( *shared )( QStringLiteral( "plugin" ), tooltip, elapsed.elapsed() );
            }
            else {
                ( *shared )( QStringLiteral( "static" ), {}, elapsed.elapsed() );
            }
        } );
    if ( id.isEmpty() ) {
        ( *shared )( QStringLiteral( "static" ), {}, 0 );
    }
}
