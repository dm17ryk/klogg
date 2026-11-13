// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysissource.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QSignalBlocker>
#include <QVariantMap>

#include <algorithm>

namespace {
QString tr( const char* text )
{
    return QCoreApplication::translate( "AnalysisSource", text );
}

bool samePath( const QString& left, const QString& right )
{
    const auto normalized = []( const QString& path ) {
        return QDir::cleanPath( QFileInfo( QDir::fromNativeSeparators( path ) ).absoluteFilePath() );
    };
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    return !left.isEmpty() && !right.isEmpty()
           && normalized( left ).compare( normalized( right ), sensitivity ) == 0;
}

AnalysisSourceResolution failure( const QString& reason, const QString& message )
{
    AnalysisSourceResolution result;
    result.reason = reason;
    result.message = message;
    return result;
}
} // namespace

std::optional<AnalysisSource> AnalysisSource::parse( const QString& text, QString* errorMessage )
{
    const auto value = text.trimmed();
    AnalysisSource source;
    if ( value.isEmpty() || value == QLatin1String( "focused" ) ) {
        return source;
    }
    if ( value == QLatin1String( "all" ) ) {
        source.kind = Kind::AllOpen;
        return source;
    }
    if ( value.startsWith( QLatin1String( "tab:" ) ) && value.size() > 4 ) {
        source.kind = Kind::Tab;
        source.tabId = value.mid( 4 );
        return source;
    }
    if ( value.startsWith( QLatin1String( "file:" ) ) && value.size() > 5 ) {
        source.kind = Kind::File;
        source.filePath = value.mid( 5 );
        return source;
    }
    if ( errorMessage ) {
        *errorMessage = tr( "Unknown source \"%1\"; use focused, all, tab:<id> or file:<path>." )
                            .arg( value );
    }
    return std::nullopt;
}

QString AnalysisSource::toString() const
{
    switch ( kind ) {
    case Kind::Focused:
        return QStringLiteral( "focused" );
    case Kind::AllOpen:
        return QStringLiteral( "all" );
    case Kind::Tab:
        return QStringLiteral( "tab:" ) + tabId;
    case Kind::File:
        return QStringLiteral( "file:" ) + filePath;
    }
    return QStringLiteral( "focused" );
}

QList<OpenSource> OpenSource::fromVariantList( const QVariantList& sources )
{
    QList<OpenSource> result;
    for ( const auto& value : sources ) {
        const auto map = value.toMap();
        OpenSource source;
        source.tabId = map.value( QStringLiteral( "tabId" ) ).toString();
        source.filePath = map.value( QStringLiteral( "filePath" ) ).toString();
        source.label = map.value( QStringLiteral( "label" ) ).toString();
        source.windowIndex = map.value( QStringLiteral( "windowIndex" ) ).toInt();
        source.tabIndex = map.value( QStringLiteral( "tabIndex" ) ).toInt();
        source.focused = map.value( QStringLiteral( "focused" ) ).toBool();
        if ( !source.tabId.isEmpty() ) {
            result.append( source );
        }
    }
    return result;
}

AnalysisSourceResolution resolveAnalysisSource( const AnalysisSource& source,
                                                const QList<OpenSource>& open )
{
    AnalysisSourceResolution result;
    switch ( source.kind ) {
    case AnalysisSource::Kind::Focused: {
        for ( const auto& tab : open ) {
            if ( tab.focused ) {
                result.tabIds.append( tab.tabId );
                return result;
            }
        }
        return failure( QStringLiteral( "no_focused_tab" ), tr( "No tab has the focus." ) );
    }
    case AnalysisSource::Kind::Tab:
        for ( const auto& tab : open ) {
            if ( tab.tabId == source.tabId ) {
                result.tabIds.append( tab.tabId );
                return result;
            }
        }
        return failure( QStringLiteral( "source_not_found" ),
                        tr( "The selected tab is closed." ) );
    case AnalysisSource::Kind::File:
        for ( const auto& tab : open ) {
            if ( samePath( tab.filePath, source.filePath ) ) {
                result.tabIds.append( tab.tabId );
                return result;
            }
        }
        result.fileToOpen = source.filePath;
        return result;
    case AnalysisSource::Kind::AllOpen: {
        for ( const auto& tab : open ) {
            if ( !result.tabIds.contains( tab.tabId ) ) {
                result.tabIds.append( tab.tabId );
            }
        }
        if ( result.tabIds.isEmpty() ) {
            return failure( QStringLiteral( "no_open_sources" ), tr( "No file is open." ) );
        }
        return result;
    }
    }
    return failure( QStringLiteral( "source_not_found" ), tr( "Unknown source." ) );
}

bool isListedSource( const QVariantList& sources, const QString& tabId )
{
    return std::any_of( sources.cbegin(), sources.cend(), [ &tabId ]( const QVariant& value ) {
        return value.toMap().value( QStringLiteral( "tabId" ) ).toString() == tabId;
    } );
}

namespace {
const auto FileChooserData = QStringLiteral( "file:" );

void addFileEntry( QComboBox* combo, const QString& source )
{
    const auto path = source.mid( 5 );
    // Chosen files sit just before the "File…" entry.
    const auto chooser = combo->findData( FileChooserData );
    combo->insertItem( chooser < 0 ? combo->count() : chooser,
                       tr( "File: %1" ).arg( QDir::toNativeSeparators( path ) ), source );
}
} // namespace

void populateAnalysisSourceCombo( QComboBox* combo, const QVariantList& sources,
                                  const QString& selected )
{
    QStringList files;
    for ( int index = 0; index < combo->count(); ++index ) {
        const auto data = combo->itemData( index ).toString();
        if ( data.startsWith( FileChooserData ) && data != FileChooserData )
            files.append( data );
    }
    const QSignalBlocker blocker( combo );
    combo->clear();
    combo->addItem( tr( "Focused tab at Run" ), QStringLiteral( "focused" ) );
    combo->addItem( tr( "All open files (%1)" ).arg( sources.size() ), QStringLiteral( "all" ) );
    for ( const auto& value : sources ) {
        const auto source = value.toMap();
        combo->addItem( source.value( QStringLiteral( "label" ) ).toString(),
                        QStringLiteral( "tab:" )
                            + source.value( QStringLiteral( "tabId" ) ).toString() );
    }
    combo->addItem( tr( "File…" ), FileChooserData );
    for ( const auto& file : files )
        addFileEntry( combo, file );
    if ( !selectAnalysisSource( combo, selected ) ) {
        // Keep showing a selected tab that has since closed.
        combo->addItem( tr( "Closed source (%1)" ).arg( selected.mid( 4 ) ), selected );
        combo->setCurrentIndex( combo->count() - 1 );
    }
}

bool selectAnalysisSource( QComboBox* combo, const QString& source )
{
    const auto text = source.isEmpty() ? QStringLiteral( "focused" ) : source;
    auto index = combo->findData( text );
    if ( index < 0 && text.startsWith( FileChooserData ) && text != FileChooserData ) {
        addFileEntry( combo, text );
        index = combo->findData( text );
    }
    if ( index < 0 )
        return false;
    combo->setCurrentIndex( index );
    return true;
}

void chooseAnalysisSourceFile( QComboBox* combo, QWidget* parent, const QString& previous )
{
    const auto path = QFileDialog::getOpenFileName( parent, tr( "Analyze file" ) );
    if ( path.isEmpty() ) {
        selectAnalysisSource( combo, previous );
        return;
    }
    selectAnalysisSource( combo, FileChooserData + QDir::fromNativeSeparators( path ) );
}
