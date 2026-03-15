/*
 * Copyright (C) 2014, 2015 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "tabbedcrawlerwidget.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QPalette>
#include <QPointer>
#include <QUuid>
#include <qobjectdefs.h>
#include <qpoint.h>

#include "crawlerwidget.h"

#include "clipboard.h"
#include "configuration.h"
#include "dispatch_to.h"
#include "iconloader.h"
#include "log.h"
#include "openfilehelper.h"
#include "styles.h"
#include "tabnamemapping.h"
#include "streamsession.h"

namespace {
constexpr QLatin1String PathKey = QLatin1String( "path", 4 );
constexpr QLatin1String StatusKey = QLatin1String( "status", 6 );
constexpr QLatin1String TabIdKey = QLatin1String( "tabId", 5 );
constexpr QLatin1String ConnectedKey = QLatin1String( "connected", 9 );
constexpr QLatin1String ActionsPortKey = QLatin1String( "actionsPort", 11 );
constexpr QLatin1String ScriptActiveKey = QLatin1String( "scriptActive", 12 );
} // namespace

TabbedCrawlerWidget::TabbedCrawlerWidget()
    : QTabWidget()
    , newdata_icon_( ":/images/newdata_icon.png" )
    , newfiltered_icon_( ":/images/newfiltered_icon.png" )
{

    QString tabStyle = "QTabBar::tab { height: 24px; }";
    QString tabCloseButtonStyle = " QTabBar::close-button {\
              height: 12px; width: 12px;\
              subcontrol-origin: padding;\
              subcontrol-position: right;\
              %1}";

    QString backgroundImage;
    QString backgroundHoverImage;

    const auto& config = Configuration::get();
    if ( config.style() == StyleManager::DarkStyleKey ) {
        backgroundImage = ":/images/icons8-close-window-16_inverse.png";
        backgroundHoverImage = ":/images/icons8-close-window-hover-16_inverse.png";
    }

#if defined( Q_OS_MAC )
    // work around Qt MacOSX bug missing tab close icons
    // see: https://bugreports.qt.io/browse/QTBUG-61092
    // still broken in document mode in Qt.5.12.2 !!!!
    if ( config.style() != StyleManager::DarkStyleKey ) {
        backgroundImage
            = ":/qt-project.org/styles/commonstyle/images/standardbutton-closetab-16.png";
        backgroundHoverImage
            = ":/qt-project.org/styles/commonstyle/images/standardbutton-closetab-hover-16.png";
    }
#elif defined( Q_OS_WIN )
    if ( config.style() == StyleManager::FusionKey ) {
        backgroundImage = ":/images/icons8-close-window-16.png";
        backgroundHoverImage = ":/images/icons8-close-window-hover-16.png";
    }
#endif

    if ( !backgroundImage.isEmpty() ) {
        const QString backgroundImageTemplate = " image: url(%1);";
        QString tabCloseButtonHoverStyle = " QTabBar::close-button:hover { %1 }";
        backgroundImage = backgroundImageTemplate.arg( backgroundImage );
        backgroundHoverImage = backgroundImageTemplate.arg( backgroundHoverImage );
        tabCloseButtonHoverStyle = tabCloseButtonHoverStyle.arg( backgroundHoverImage );
        tabCloseButtonStyle = tabCloseButtonStyle.arg( backgroundImage );
        tabCloseButtonStyle.append( tabCloseButtonHoverStyle );
    }
    else {
        tabCloseButtonStyle = tabCloseButtonStyle.arg( "" );
    }

    myTabBar_.setStyleSheet( tabStyle.append( tabCloseButtonStyle ) );

    setTabBar( &myTabBar_ );
    defaultTabTextColor_ = myTabBar_.palette().color( QPalette::WindowText );
    updateTabBarVisibility();

    myTabBar_.setContextMenuPolicy( Qt::CustomContextMenu );
    connect( &myTabBar_, &CrawlerTabBar::showTabContextMenu, this,
             &TabbedCrawlerWidget::showContextMenu );

    dispatchToObject( [ this ] { loadIcons(); }, this );
}

void TabbedCrawlerWidget::loadIcons()
{
    IconLoader iconLoader{ this };
    olddata_icon_ = iconLoader.load( "olddata_icon" );
    if ( connectionIcon_.isNull() ) {
        connectionIcon_ = iconLoader.load( "icons8-lock" );
    }
    for ( int tab = 0; tab < count(); ++tab ) {
        updateIcon( tab );
    }
}

void TabbedCrawlerWidget::changeEvent( QEvent* event )
{
    if ( event->type() == QEvent::StyleChange ) {
        defaultTabTextColor_ = myTabBar_.palette().color( QPalette::WindowText );
        for ( int i = 0; i < count(); ++i ) {
            const auto path = tabPathAt( i );
            const auto it = streamSessions_.find( path );
            const bool connected = it != streamSessions_.end() && it->second
                                   && it->second->isConnectionOpen();
            const SerialCaptureSettings* settings
                = ( it != streamSessions_.end() && it->second ) ? &it->second->captureSettings()
                                                                : nullptr;
            setTabConnectionState( path, connected, settings );
        }
        dispatchToObject( [ this ] { loadIcons(); }, this );
    }

    QWidget::changeEvent( event );
}

void TabbedCrawlerWidget::addTabBarItem( int index, const QString& fileName )
{
    QVariantMap tabData;
    tabData[ PathKey ] = fileName;
    tabData[ StatusKey ] = static_cast<int>( DataStatus::OLD_DATA );
    tabData[ TabIdKey ] = QUuid::createUuid().toString( QUuid::WithoutBraces );
    tabData[ ConnectedKey ] = false;
    tabData[ ActionsPortKey ] = false;
    tabData[ ScriptActiveKey ] = false;

    myTabBar_.setTabData( index, tabData );
    updateTabPresentation( index );

    setCurrentIndex( index );

    updateTabBarVisibility();

    const auto sessionIt = streamSessions_.find( fileName );
    if ( sessionIt != streamSessions_.end() && sessionIt->second ) {
        const bool connected = sessionIt->second->isConnectionOpen();
        const auto* settings = &sessionIt->second->captureSettings();
        setTabConnectionState( fileName, connected, settings );
    }
}

void TabbedCrawlerWidget::removeCrawler( int index )
{
    QTabWidget::removeTab( index );

    updateTabBarVisibility();
}

void TabbedCrawlerWidget::setStreamSessionForPath( const QString& fileName,
                                                   const std::shared_ptr<StreamSession>& session )
{
    if ( fileName.isEmpty() ) {
        return;
    }

    if ( session ) {
        streamSessions_[ fileName ] = session;
        setTabConnectionState( fileName, session->isConnectionOpen(), &session->captureSettings() );
    }
    else {
        clearStreamSessionForPath( fileName );
    }
}

void TabbedCrawlerWidget::clearStreamSessionForPath( const QString& fileName )
{
    if ( fileName.isEmpty() ) {
        return;
    }

    setTabConnectionState( fileName, false );
    streamSessions_.erase( fileName );
}

void TabbedCrawlerWidget::mouseReleaseEvent( QMouseEvent* event )
{
    LOG_DEBUG << "TabbedCrawlerWidget::mouseReleaseEvent";

    if ( event->button() == Qt::MiddleButton ) {
        int tab = this->myTabBar_.tabAt( event->pos() );
        if ( -1 != tab ) {
            Q_EMIT tabCloseRequested( tab );
            event->accept();
        }
    }

    event->ignore();
}

QString TabbedCrawlerWidget::tabPathAt( int index ) const
{
    return myTabBar_.tabData( index ).toMap()[ PathKey ].toString();
}

QString TabbedCrawlerWidget::tabIdAt( int index ) const
{
    return myTabBar_.tabData( index ).toMap()[ TabIdKey ].toString();
}

QString TabbedCrawlerWidget::tabDisplayNameAt( int index ) const
{
    if ( index < 0 || index >= count() ) {
        return {};
    }

    return myTabBar_.tabText( index );
}

StreamSession* TabbedCrawlerWidget::streamSessionForTab( int tab ) const
{
    if ( tab < 0 || tab >= count() ) {
        return nullptr;
    }

    const auto fileName = tabPathAt( tab );
    const auto it = streamSessions_.find( fileName );
    if ( it == streamSessions_.end() ) {
        return nullptr;
    }

    return it->second.get();
}

StreamSession* TabbedCrawlerWidget::streamSessionForPath( const QString& fileName ) const
{
    const auto it = streamSessions_.find( fileName );
    if ( it == streamSessions_.end() ) {
        return nullptr;
    }

    return it->second.get();
}

int TabbedCrawlerWidget::findTabById( const QString& tabId ) const
{
    if ( tabId.isEmpty() ) {
        return -1;
    }

    for ( int index = 0; index < count(); ++index ) {
        if ( tabIdAt( index ) == tabId ) {
            return index;
        }
    }

    return -1;
}

void TabbedCrawlerWidget::setTabActionsPort( const QString& fileName, bool isActionsPort )
{
    for ( int index = 0; index < count(); ++index ) {
        if ( tabPathAt( index ) == fileName ) {
            setTabMetadataValue( index, QStringLiteral( "actionsPort" ), isActionsPort );
            updateTabPresentation( index );
            return;
        }
    }
}

void TabbedCrawlerWidget::setTabScriptActive( const QString& tabId, bool isActive )
{
    const auto index = findTabById( tabId );
    if ( index < 0 ) {
        return;
    }

    setTabMetadataValue( index, QStringLiteral( "scriptActive" ), isActive );
    updateTabPresentation( index );
}

bool TabbedCrawlerWidget::hasOpenStreamSession() const
{
    for ( const auto& entry : streamSessions_ ) {
        if ( entry.second && entry.second->isConnectionOpen() ) {
            return true;
        }
    }
    return false;
}

StreamSession* TabbedCrawlerWidget::firstOpenStreamSession() const
{
    for ( const auto& entry : streamSessions_ ) {
        if ( entry.second && entry.second->isConnectionOpen() ) {
            return entry.second.get();
        }
    }
    return nullptr;
}

void CrawlerTabBar::mouseReleaseEvent( QMouseEvent* mouseEvent )
{
    if ( mouseEvent->button() == Qt::RightButton ) {
        int tab = tabAt( mouseEvent->pos() );
        if ( tab != -1 ) {
            Q_EMIT showTabContextMenu( tab, mapToGlobal( mouseEvent->pos() ) );
            mouseEvent->accept();
        }
    }

    mouseEvent->ignore();
}

void TabbedCrawlerWidget::showContextMenu( int tab, QPoint globalPoint )        
{
    QMenu menu( this );
    auto closeThis = menu.addAction( tr( "Close this" ) );
    auto closeOthers = menu.addAction( tr( "Close others" ) );
    auto closeLeft = menu.addAction( tr( "Close to the left" ) );
    auto closeRight = menu.addAction( tr( "Close to the right" ) );
    auto closeAll = menu.addAction( tr( "Close all" ) );
    menu.addSeparator();
    auto copyFullPath = menu.addAction( tr( "Copy full path" ) );
    auto openContainingFolder = menu.addAction( tr( "Open containing folder" ) );
    if ( auto session = streamSessionForTab( tab ) ) {
        menu.addSeparator();
        const auto& settings = session->captureSettings();
        const auto closeText = tr( "Close %1 @ %2" ).arg( settings.portName,
                                                          QString::number( settings.baudRate ) );
        auto closeConnection = menu.addAction( closeText );
        QPointer<StreamSession> safeSession = session;
        closeConnection->setEnabled( safeSession && safeSession->isConnectionOpen() );
        connect( closeConnection, &QAction::triggered, this, [ safeSession ] {
            if ( safeSession ) {
                safeSession->closeConnection();
            }
        } );
    }
    menu.addSeparator();
    auto renameTab = menu.addAction( tr( "Rename tab" ) );
    auto resetTabName = menu.addAction( tr( "Reset tab name" ) );

    connect( closeThis, &QAction::triggered, [ tab, this ] { Q_EMIT tabCloseRequested( tab ); } );

    connect( closeOthers, &QAction::triggered, [ tabWidget = widget( tab ), this ] {
        while ( count() != 1 ) {
            for ( int i = 0; i < count(); ++i ) {
                if ( i != indexOf( tabWidget ) ) {
                    Q_EMIT tabCloseRequested( i );
                    break;
                }
            }
        }
    } );

    connect( closeLeft, &QAction::triggered, [ tabWidget = widget( tab ), this ] {
        while ( indexOf( tabWidget ) != 0 ) {
            Q_EMIT tabCloseRequested( 0 );
        }
    } );

    connect( closeRight, &QAction::triggered, [ tab, this ] {
        while ( count() > tab + 1 ) {
            Q_EMIT tabCloseRequested( tab + 1 );
        }
    } );

    connect( closeAll, &QAction::triggered, [ this ] {
        while ( count() ) {
            Q_EMIT tabCloseRequested( 0 );
        }
    } );

    if ( tab == 0 ) {
        closeLeft->setDisabled( true );
    }
    else if ( tab == count() - 1 ) {
        closeRight->setDisabled( true );
    }

    connect( copyFullPath, &QAction::triggered, this,
             [ this, tab ] { sendTextToClipboard( tabToolTip( tab ) ); } );

    connect( openContainingFolder, &QAction::triggered, this,
             [ this, tab ] { showPathInFileExplorer( tabToolTip( tab ) ); } );

    connect( renameTab, &QAction::triggered, this, [ this, tab ] {
        bool isNameEntered = false;
        const auto tabPath = tabPathAt( tab );
        const auto currentName = TabNameMapping::getSynced().tabName( tabPath );
        auto newName = QInputDialog::getText(
            this, "Rename tab", "Tab name", QLineEdit::Normal,
            currentName.isEmpty() ? QFileInfo( tabPath ).fileName() : currentName, &isNameEntered );
        if ( isNameEntered ) {
            TabNameMapping::getSynced().setTabName( tabPath, newName ).save();
            updateTabPresentation( tab );
        }
    } );

    connect( resetTabName, &QAction::triggered, this, [ this, tab ] {
        const auto tabPath = tabPathAt( tab );
        TabNameMapping::getSynced().setTabName( tabPath, "" ).save();
        updateTabPresentation( tab );
    } );

    menu.exec( globalPoint );
}

void TabbedCrawlerWidget::keyPressEvent( QKeyEvent* event )
{
    const auto mod = event->modifiers();
    const auto key = event->key();

    LOG_DEBUG << "TabbedCrawlerWidget::keyPressEvent";

    // Ctrl + tab
    if ( ( mod == Qt::ControlModifier && key == Qt::Key_Tab )
         || ( mod == Qt::ControlModifier && key == Qt::Key_PageDown )
         || ( mod == ( Qt::ControlModifier | Qt::AltModifier | Qt::KeypadModifier )
              && key == Qt::Key_Right ) ) {
        setCurrentIndex( ( currentIndex() + 1 ) % count() );
    }
    // Ctrl + shift + tab
    else if ( ( mod == ( Qt::ControlModifier | Qt::ShiftModifier ) && key == Qt::Key_Tab )
              || ( mod == Qt::ControlModifier && key == Qt::Key_PageUp )
              || ( mod == ( Qt::ControlModifier | Qt::AltModifier | Qt::KeypadModifier )
                   && key == Qt::Key_Left ) ) {
        setCurrentIndex( ( currentIndex() - 1 >= 0 ) ? currentIndex() - 1 : count() - 1 );
    }
    // Ctrl + numbers
    else if ( mod == Qt::ControlModifier && ( key >= Qt::Key_1 && key <= Qt::Key_8 ) ) {
        int newIndex = key - Qt::Key_0;
        if ( newIndex <= count() )
            setCurrentIndex( newIndex - 1 );
    }
    // Ctrl + 9
    else if ( mod == Qt::ControlModifier && key == Qt::Key_9 ) {
        setCurrentIndex( count() - 1 );
    }
    else if ( mod == Qt::ControlModifier && ( key == Qt::Key_Q || key == Qt::Key_W ) ) {
        Q_EMIT tabCloseRequested( currentIndex() );
    }
    else {
        QTabWidget::keyPressEvent( event );
    }
}

void TabbedCrawlerWidget::updateIcon( int index )
{
    auto tabData = myTabBar_.tabData( index ).toMap();

    const QIcon* icon;
    switch ( static_cast<DataStatus>( tabData[ StatusKey ].toInt() ) ) {
    case DataStatus::OLD_DATA:
        icon = &olddata_icon_;
        break;
    case DataStatus::NEW_DATA:
        icon = &newdata_icon_;
        break;
    case DataStatus::NEW_FILTERED_DATA:
        icon = &newfiltered_icon_;
        break;
    default:
        return;
    }

    myTabBar_.setTabIcon( index, *icon );
}

void TabbedCrawlerWidget::setTabDataStatus( int index, DataStatus status )
{
    LOG_DEBUG << "TabbedCrawlerWidget::setTabDataStatus " << index;

    auto tabData = myTabBar_.tabData( index ).toMap();
    tabData[ StatusKey ] = static_cast<int>( status );
    myTabBar_.setTabData( index, tabData );

    updateIcon( index );
}


void TabbedCrawlerWidget::updateTabBarVisibility()
{
    myTabBar_.setVisible( alwaysShowTabBar_ || count() > 1 );
}

void TabbedCrawlerWidget::setTabConnectionState( const QString& fileName, bool connected,
                                                 const SerialCaptureSettings* settings )
{
    for ( int i = 0; i < count(); ++i ) {
        if ( tabPathAt( i ) == fileName ) {
            setTabMetadataValue( i, QStringLiteral( "connected" ), connected );
            if ( settings != nullptr ) {
                setTabMetadataValue( i, QStringLiteral( "portName" ), settings->portName );
                setTabMetadataValue( i, QStringLiteral( "baudRate" ), settings->baudRate );
            }
            else {
                setTabMetadataValue( i, QStringLiteral( "portName" ), {} );
                setTabMetadataValue( i, QStringLiteral( "baudRate" ), {} );
            }
            updateTabPresentation( i );
            break;
        }
    }
}

void TabbedCrawlerWidget::setTabMetadataValue( int index, const QString& key, const QVariant& value )
{
    auto tabData = myTabBar_.tabData( index ).toMap();
    if ( value.isValid() ) {
        tabData.insert( key, value );
    }
    else {
        tabData.remove( key );
    }
    myTabBar_.setTabData( index, tabData );
}

QVariant TabbedCrawlerWidget::tabMetadataValue( int index, const QString& key ) const
{
    if ( index < 0 || index >= count() ) {
        return {};
    }

    return myTabBar_.tabData( index ).toMap().value( key );
}

void TabbedCrawlerWidget::updateTabPresentation( int index )
{
    if ( index < 0 || index >= count() ) {
        return;
    }

    const auto fileName = tabPathAt( index );
    const auto tabLabel = QFileInfo( fileName ).fileName();
    const auto tabName = TabNameMapping::getSynced().tabName( fileName );
    const auto connected = tabMetadataValue( index, QStringLiteral( "connected" ) ).toBool();
    const auto portName = tabMetadataValue( index, QStringLiteral( "portName" ) ).toString();
    const auto baudRate = tabMetadataValue( index, QStringLiteral( "baudRate" ) ).toInt();
    const auto isActionsPort = tabMetadataValue( index, QStringLiteral( "actionsPort" ) ).toBool();
    const auto hasActiveScript = tabMetadataValue( index, QStringLiteral( "scriptActive" ) ).toBool();
    QString suffix;
    if ( isActionsPort ) {
        suffix += QStringLiteral( " (actions)" );
    }
    if ( hasActiveScript ) {
        suffix += QStringLiteral( " (py)" );
    }

    myTabBar_.setTabText( index, ( tabName.isEmpty() ? tabLabel : tabName ) + suffix );

    auto toolTip = QDir::toNativeSeparators( fileName );
    if ( !portName.isEmpty() ) {
        toolTip += QStringLiteral( " (%1 @ %2)" ).arg( portName ).arg( baudRate );
        if ( isActionsPort ) {
            toolTip += QStringLiteral( " (actions)" );
        }
    }
    if ( hasActiveScript ) {
        toolTip += QStringLiteral( " [python script running]" );
    }
    myTabBar_.setTabToolTip( index, toolTip );

    if ( connected ) {
        myTabBar_.setTabIcon( index, connectionIcon_ );
    }
    else {
        updateIcon( index );
    }
    myTabBar_.setTabTextColor( index, connected ? openStreamTabColor_ : defaultTabTextColor_ );
}

void TabbedCrawlerWidget::setAlwaysShowTabBar( bool alwaysShow )
{
    alwaysShowTabBar_ = alwaysShow;
    updateTabBarVisibility();
}






