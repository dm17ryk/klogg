// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysissource.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QStandardItemModel>
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
        source.sourceType = map.value( QStringLiteral( "sourceType" ) ).toString();
        if ( !source.tabId.isEmpty() ) {
            result.append( source );
        }
    }
    return result;
}

namespace {
const auto ComSourceType = QStringLiteral( "com" );

AnalysisSourceResolution notComTab()
{
    return failure( QStringLiteral( "not_com_tab" ),
                    tr( "The selected tab is not a live COM tab." ) );
}

QString sourceRuleRefusal( AnalysisSource::Kind kind, const SourceRules& rules )
{
    if ( kind == AnalysisSource::Kind::AllOpen && !rules.allowAll )
        return tr( "This tool works on one source at a time; choose a tab or a file." );
    if ( kind == AnalysisSource::Kind::File && !rules.allowFile )
        return tr( "This tool needs a live COM tab; a file cannot be used." );
    return {};
}
} // namespace

AnalysisSourceResolution resolveAnalysisSource( const AnalysisSource& source,
                                                const QList<OpenSource>& open,
                                                const SourceRules& rules )
{
    if ( const auto refusal = sourceRuleRefusal( source.kind, rules ); !refusal.isEmpty() )
        return failure( QStringLiteral( "invalid_source" ), refusal );
    const auto usable = [ &rules ]( const OpenSource& tab ) {
        return !rules.comOnly || tab.sourceType == ComSourceType;
    };
    AnalysisSourceResolution result;
    switch ( source.kind ) {
    case AnalysisSource::Kind::Focused: {
        for ( const auto& tab : open ) {
            if ( tab.focused ) {
                if ( !usable( tab ) )
                    return notComTab();
                result.tabIds.append( tab.tabId );
                return result;
            }
        }
        return failure( QStringLiteral( "no_focused_tab" ), tr( "No tab has the focus." ) );
    }
    case AnalysisSource::Kind::Tab:
        for ( const auto& tab : open ) {
            if ( tab.tabId == source.tabId ) {
                if ( !usable( tab ) )
                    return notComTab();
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
            if ( usable( tab ) && !result.tabIds.contains( tab.tabId ) ) {
                result.tabIds.append( tab.tabId );
            }
        }
        if ( result.tabIds.isEmpty() ) {
            return failure( QStringLiteral( "no_open_sources" ), rules.comOnly
                                                                     ? tr( "No COM tab is open." )
                                                                     : tr( "No file is open." ) );
        }
        return result;
    }
    }
    return failure( QStringLiteral( "source_not_found" ), tr( "Unknown source." ) );
}

QVariantList analysisSourcesFromWindowInfo( const QVariantList& windows )
{
    QVariantList sources;
    for ( const auto& windowValue : windows ) {
        const auto window = windowValue.toMap();
        const auto windowIndex = window.value( QStringLiteral( "windowIndex" ) ).toInt();
        const bool active = window.value( QStringLiteral( "isActiveWindow" ) ).toBool();
        const auto currentTabId = window.value( QStringLiteral( "currentTabId" ) ).toString();
        for ( const auto& tabValue : window.value( QStringLiteral( "tabs" ) ).toList() ) {
            const auto tab = tabValue.toMap();
            const auto tabId = tab.value( QStringLiteral( "tabId" ) ).toString();
            const auto tabIndex = tab.value( QStringLiteral( "tabIndex" ) ).toInt();
            auto name = tab.value( QStringLiteral( "displayName" ) ).toString();
            if ( name.isEmpty() )
                name = tab.value( QStringLiteral( "filePath" ) ).toString();
            const auto label = windows.size() > 1 ? tr( "Window %1 · %2: %3" )
                                                        .arg( windowIndex + 1 )
                                                        .arg( tabIndex + 1 )
                                                        .arg( name )
                                                  : tr( "%1: %2" ).arg( tabIndex + 1 ).arg( name );
            sources.push_back( QVariantMap{
                { QStringLiteral( "tabId" ), tabId },
                { QStringLiteral( "filePath" ), tab.value( QStringLiteral( "filePath" ) ) },
                { QStringLiteral( "label" ), label },
                { QStringLiteral( "windowIndex" ), windowIndex },
                { QStringLiteral( "tabIndex" ), tabIndex },
                { QStringLiteral( "focused" ),
                  active && !tabId.isEmpty() && tabId == currentTabId },
                { QStringLiteral( "sourceType" ), tab.value( QStringLiteral( "sourceType" ) ) } } );
        }
    }
    return sources;
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

// Disables a picker entry; the tooltip says why the tool cannot use it.
void disableEntry( QComboBox* combo, int index, const QString& reason )
{
    if ( auto* model = qobject_cast<QStandardItemModel*>( combo->model() ) ) {
        if ( auto* item = model->item( index ) ) {
            item->setEnabled( false );
            item->setToolTip( reason );
        }
    }
}

bool entryEnabled( const QComboBox* combo, int index )
{
    const auto* model = qobject_cast<const QStandardItemModel*>( combo->model() );
    const auto* item = model ? model->item( index ) : nullptr;
    return !item || item->isEnabled();
}
} // namespace

void populateAnalysisSourceCombo( QComboBox* combo, const QVariantList& sources,
                                  const QString& selected, const SourceRules& rules )
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
    const auto usableTabs
        = std::count_if( sources.cbegin(), sources.cend(), [ &rules ]( const auto& value ) {
              return !rules.comOnly
                     || value.toMap().value( QStringLiteral( "sourceType" ) ).toString()
                            == ComSourceType;
          } );
    combo->addItem( rules.comOnly ? tr( "All COM tabs (%1)" ).arg( usableTabs )
                                  : tr( "All open files (%1)" ).arg( usableTabs ),
                    QStringLiteral( "all" ) );
    if ( const auto refusal = sourceRuleRefusal( AnalysisSource::Kind::AllOpen, rules );
         !refusal.isEmpty() )
        disableEntry( combo, combo->count() - 1, refusal );
    for ( const auto& value : sources ) {
        const auto source = value.toMap();
        combo->addItem( source.value( QStringLiteral( "label" ) ).toString(),
                        QStringLiteral( "tab:" )
                            + source.value( QStringLiteral( "tabId" ) ).toString() );
        auto reason = source.value( QStringLiteral( "disabledReason" ) ).toString();
        if ( reason.isEmpty() && rules.comOnly
             && source.value( QStringLiteral( "sourceType" ) ).toString() != ComSourceType )
            reason = tr( "Not a live COM tab." );
        if ( !reason.isEmpty() )
            disableEntry( combo, combo->count() - 1, reason );
    }
    combo->addItem( tr( "File…" ), FileChooserData );
    if ( const auto refusal = sourceRuleRefusal( AnalysisSource::Kind::File, rules );
         !refusal.isEmpty() ) {
        disableEntry( combo, combo->count() - 1, refusal );
        files.clear();
    }
    for ( const auto& file : files )
        addFileEntry( combo, file );
    if ( selectAnalysisSource( combo, selected ) )
        return;
    const auto parsed = AnalysisSource::parse( selected );
    if ( parsed && parsed->kind == AnalysisSource::Kind::Tab && combo->findData( selected ) < 0 ) {
        // Keep showing a selected tab that has since closed.
        combo->addItem( tr( "Closed source (%1)" ).arg( selected.mid( 4 ) ), selected );
        combo->setCurrentIndex( combo->count() - 1 );
        return;
    }
    // A source this tool cannot use: start from the focused tab.
    combo->setCurrentIndex( 0 );
}

bool selectAnalysisSource( QComboBox* combo, const QString& source )
{
    const auto text = source.isEmpty() ? QStringLiteral( "focused" ) : source;
    auto index = combo->findData( text );
    const auto chooser = combo->findData( FileChooserData );
    if ( index < 0 && text.startsWith( FileChooserData ) && text != FileChooserData
         && entryEnabled( combo, chooser ) ) {
        addFileEntry( combo, text );
        index = combo->findData( text );
    }
    if ( index < 0 || !entryEnabled( combo, index ) )
        return false;
    combo->setCurrentIndex( index );
    return true;
}

AnalysisSourcePicker::AnalysisSourcePicker( std::function<QVariantList()> sources,
                                            SourceRules rules, QWidget* parent )
    : QComboBox( parent )
    , sources_( std::move( sources ) )
    , rules_( rules )
{
    setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
    setMinimumContentsLength( 16 );
    refresh();
    connect( this, QOverload<int>::of( &QComboBox::currentIndexChanged ), this,
             &AnalysisSourcePicker::selected );
}

QString AnalysisSourcePicker::selection() const
{
    const auto value = currentData().toString();
    return value.isEmpty() ? QStringLiteral( "focused" ) : value;
}

bool AnalysisSourcePicker::setSelection( const QString& source )
{
    refresh();
    const QSignalBlocker blocker( this );
    if ( !selectAnalysisSource( this, source ) )
        return false;
    previous_ = selection();
    return true;
}

void AnalysisSourcePicker::refresh()
{
    const auto available = sources_ ? sources_() : QVariantList{};
    if ( available == cache_ && count() > 0 )
        return;
    cache_ = available;
    populateAnalysisSourceCombo( this, available, previous_, rules_ );
    previous_ = selection();
}

void AnalysisSourcePicker::showPopup()
{
    refresh();
    QComboBox::showPopup();
}

void AnalysisSourcePicker::selected()
{
    if ( choosing_ )
        return;
    if ( currentData().toString() == FileChooserData ) {
        const QScopedValueRollback<bool> guard( choosing_, true );
        chooseAnalysisSourceFile( this, this, previous_ );
    }
    if ( selection() == previous_ )
        return;
    previous_ = selection();
    Q_EMIT selectionChanged( previous_ );
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
