// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "valuecountdock.h"
#include "analysiscommands.h"
#include "analysistextedit.h"
#include "log.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <map>
#include <set>

namespace {
template <typename T>
T* named( T* widget, const char* name, const QString& label )
{
    widget->setObjectName( QLatin1String( name ) );
    widget->setAccessibleName( label );
    return widget;
}
CommanderResult failure( const QString& reason )
{
    LOG_DEBUG << "Value Counts dock rejected reason=" << reason;
    return { CommanderResultCode::InvalidRequest, reason, { { "reason", reason } } };
}
bool terminal( const QString& state )
{
    return state == "completed" || state == "failed" || state == "cancelled";
}
} // namespace

ValueCountDock::ValueCountDock( QWidget* parent, Executor executor, Sources sources )
    : QDockWidget( tr( "Value Counts" ), parent )
    , execute_( std::move( executor ) )
    , sources_( std::move( sources ) )
{
    setObjectName( "valueCountsDock" );
    setAccessibleName( tr( "Value Counts" ) );
    setFeatures( DockWidgetClosable | DockWidgetMovable | DockWidgetFloatable );
    auto* body = new QWidget( this );
    auto* layout = new QVBoxLayout( body );
    auto* form = new QFormLayout;
    pattern_ = named( new QLineEdit( "\\S+", body ), "valueCountsPattern", tr( "Pattern" ) );
    pattern_->setMaxLength( 65536 );
    form->addRow( tr( "&Pattern" ), pattern_ );
    capture_
        = named( new QLineEdit( "0", body ), "valueCountsCapture", tr( "Capture number or name" ) );
    capture_->setMaxLength( 128 );
    form->addRow( tr( "&Capture (0 = whole match)" ), capture_ );
    subpattern_
        = named( new QSpinBox( body ), "valueCountsSubpattern", tr( "Zero-based subpattern" ) );
    subpattern_->setRange( 0, 63 );
    form->addRow( tr( "Boolean subpattern (0-based)" ), subpattern_ );
    source_ = named( new QComboBox( body ), "valueCountsSource", tr( "Source" ) );
    form->addRow( tr( "&Source" ), source_ );
    scope_ = named( new QComboBox( body ), "valueCountsScope", tr( "Scope" ) );
    for ( const auto& item :
          std::map<QString, QString>{ { "sample", tr( "Sample (1000 lines / 4 MiB)" ) },
                                      { "selection", tr( "Selected physical rows" ) },
                                      { "matches", tr( "Completed search matches" ) },
                                      { "whole_file", tr( "Whole file snapshot" ) },
                                      { "text", tr( "Pasted text" ) } } )
        scope_->addItem( item.second, item.first );
    scope_->setCurrentIndex( scope_->findData( "sample" ) );
    form->addRow( tr( "S&cope" ), scope_ );
    matchMode_ = named( new QComboBox( body ), "valueCountsMatchMode", tr( "Match mode" ) );
    matchMode_->addItem( tr( "Contains" ), "contains" );
    matchMode_->addItem( tr( "Whole word" ), "whole_word" );
    matchMode_->addItem( tr( "Whole line" ), "whole_line" );
    form->addRow( tr( "Match mode" ), matchMode_ );
    missing_ = named( new QComboBox( body ), "valueCountsMissing", tr( "Missing capture policy" ) );
    missing_->addItem( tr( "Skip missing captures" ), "skip" );
    missing_->addItem( tr( "Count missing captures separately" ), "include" );
    form->addRow( tr( "Missing values" ), missing_ );
    maxValues_ = named( new QSpinBox( body ), "valueCountsMaxValues", tr( "Unique value limit" ) );
    maxValues_->setRange( 1, 10000 );
    maxValues_->setValue( 10000 );
    form->addRow( tr( "Maximum unique values" ), maxValues_ );
    layout->addLayout( form );
    auto* flags = new QGridLayout;
    matchCase_ = named( new QCheckBox( tr( "Case-sensitive pattern" ), body ),
                        "valueCountsMatchCase", tr( "Case-sensitive pattern" ) );
    matchCase_->setChecked( true );
    boolean_ = named( new QCheckBox( tr( "Boolean expression" ), body ), "valueCountsBoolean",
                      tr( "Boolean expression" ) );
    plain_ = named( new QCheckBox( tr( "Literal pattern" ), body ), "valueCountsPlainText",
                    tr( "Literal pattern" ) );
    fold_ = named( new QCheckBox( tr( "Combine case variants" ), body ), "valueCountsFoldCase",
                   tr( "Combine case variants" ) );
    int index = 0;
    exclude_ = named( new QCheckBox( tr( "Invert combined result" ), body ), "valueCountsExclude",
                      tr( "Invert combined result" ) );
    unmatched_ = named( new QCheckBox( tr( "Include nonmatching rows" ), body ),
                        "valueCountsUnmatched", tr( "Include nonmatching rows" ) );
    for ( auto* flag : { matchCase_, boolean_, plain_, fold_, exclude_, unmatched_ } ) {
        flags->addWidget( flag, index / 2, index % 2 );
        ++index;
        connect( flag, &QCheckBox::toggled, this, [ this ] { draftChanged(); } );
    }
    layout->addLayout( flags );
    text_ = named( new LabTextEdit( body ), "valueCountsText", tr( "Pasted raw log text" ) );
    text_->setMaximumHeight( 100 );
    text_->setLineWrapMode( QPlainTextEdit::NoWrap );
    text_->hide();
    text_->overflow = [ this ] { status_->setText( tr( "Input exceeds 4 MiB of UTF-16 text." ) ); };
    layout->addWidget( text_ );
    auto* buttons = new QGridLayout;
    auto* runButton
        = named( new QPushButton( tr( "Run" ), body ), "valueCountsRun", tr( "Run counts" ) );
    cancel_ = named( new QPushButton( tr( "Cancel" ), body ), "valueCountsCancel",
                     tr( "Cancel counts" ) );
    auto* apply = named( new QPushButton( tr( "Apply selected value" ), body ), "valueCountsApply",
                         tr( "Apply selected literal value to current search" ) );
    auto* open = named( new QPushButton( tr( "Go to first occurrence" ), body ), "valueCountsOpen",
                        tr( "Go to first source occurrence" ) );
    buttons->addWidget( runButton, 0, 0 );
    buttons->addWidget( cancel_, 0, 1 );
    buttons->addWidget( apply, 1, 0 );
    buttons->addWidget( open, 1, 1 );
    layout->addLayout( buttons );
    status_ = named( new QLabel( tr( "Run exact counts on a bounded sample." ), body ),
                     "valueCountsStatus", tr( "Count status" ) );
    status_->setTextFormat( Qt::PlainText );
    status_->setWordWrap( true );
    status_->setTextInteractionFlags( Qt::TextSelectableByMouse );
    layout->addWidget( status_ );
    table_ = named( new QTableWidget( 0, 4, body ), "valueCountsResults",
                    tr( "Value count result page" ) );
    table_->setHorizontalHeaderLabels(
        { tr( "Value" ), tr( "Count" ), tr( "Percent" ), tr( "First line" ) } );
    table_->setEditTriggers( QAbstractItemView::NoEditTriggers );
    table_->setSelectionBehavior( QAbstractItemView::SelectRows );
    table_->setSelectionMode( QAbstractItemView::SingleSelection );
    table_->horizontalHeader()->setStretchLastSection( true );
    layout->addWidget( table_, 1 );
    auto* footer = new QGridLayout;
    auto* first = named( new QPushButton( tr( "First page" ), body ), "valueCountsFirstPage",
                         tr( "First page" ) );
    next_ = named( new QPushButton( tr( "Next page" ), body ), "valueCountsNextPage",
                   tr( "Next page" ) );
    auto* exportButton = named( new QPushButton( tr( "Export CSV" ), body ), "valueCountsExport",
                                tr( "Export counts to CSV" ) );
    partial_ = named( new QCheckBox( tr( "Allow partial export" ), body ),
                      "valueCountsAllowPartial", tr( "Allow incomplete count export" ) );
    footer->addWidget( first, 0, 0 );
    footer->addWidget( next_, 0, 1 );
    footer->addWidget( exportButton, 1, 0 );
    footer->addWidget( partial_, 1, 1 );
    layout->addLayout( footer );
    setWidget( body );
    cancel_->setEnabled( false );
    next_->setEnabled( false );
    for ( auto* edit : { pattern_, capture_ } )
        connect( edit, &QLineEdit::textChanged, this, [ this ] { draftChanged(); } );
    connect( capture_, &QLineEdit::textChanged, this, [ this ] {
        if ( !updating_ )
            captureOverride_.clear();
    } );
    connect( text_, &QPlainTextEdit::textChanged, this, [ this ] { draftChanged(); } );
    for ( auto* spin : { subpattern_, maxValues_ } )
        connect( spin, qOverload<int>( &QSpinBox::valueChanged ), this,
                 [ this ] { draftChanged(); } );
    for ( auto* combo : { source_, scope_, missing_, matchMode_ } )
        connect( combo, qOverload<int>( &QComboBox::currentIndexChanged ), this,
                 [ this ] { draftChanged(); } );
    connect( runButton, &QPushButton::clicked, this, [ this ] { showFailure( run() ); } );
    connect( cancel_, &QPushButton::clicked, this,
             [ this ] { showFailure( command( CommanderAction::CancelAnalysisJob ) ); } );
    connect( apply, &QPushButton::clicked, this,
             [ this ] { showFailure( selected( CommanderAction::ApplyValueCount ) ); } );
    connect( open, &QPushButton::clicked, this,
             [ this ] { showFailure( selected( CommanderAction::OpenValueCount ) ); } );
    connect( table_, &QTableWidget::cellActivated, this, [ this ]( int, int ) {
        showFailure( selected( CommanderAction::ApplyValueCount ) );
    } );
    connect( first, &QPushButton::clicked, this, [ this ] { showFailure( page( {} ) ); } );
    connect( next_, &QPushButton::clicked, this, [ this ] { showFailure( page( cursor_ ) ); } );
    connect( exportButton, &QPushButton::clicked, this, [ this ] {
        const auto path = QFileDialog::getSaveFileName( this, tr( "Export value counts" ), {},
                                                        tr( "CSV (*.csv)" ) );
        if ( !path.isEmpty() )
            showFailure(
                command( CommanderAction::ExportValueCounts,
                         { { "filePath", path }, { "allowPartial", partial_->isChecked() } } ) );
    } );
    auto* timer = new QTimer( this );
    timer->setInterval( 200 );
    connect( timer, &QTimer::timeout, this, [ this ] { refresh(); } );
    timer->start();
    refreshSources();
}

ValueCountDock::~ValueCountDock()
{
    if ( auto* service = applicationAnalysisService( false ); service && !id_.isEmpty() )
        service->release( id_ );
}
void ValueCountDock::showFailure( const CommanderResult& result )
{
    if ( !result.ok() )
        status_->setText( result.message.isEmpty() ? result.payload.value( "reason" ).toString()
                                                   : result.message );
}
void ValueCountDock::draftChanged()
{
    if ( updating_ )
        return;
    ++revision_;
    text_->setVisible( scope_->currentData() == "text" );
    source_->setEnabled( scope_->currentData() != "text" );
    next_->setEnabled( false );
    status_->setText(
        tr( "Draft changed; Run to count this revision. Previous output is historical." ) );
    LOG_DEBUG << "Value Counts draft changed revision=" << revision_;
}
QVariantMap ValueCountDock::draft() const
{
    bool numeric = false;
    const auto number = capture_->text().toInt( &numeric );
    auto options = extraOptions_;
    options[ "caseSensitive" ] = matchCase_->isChecked();
    options[ "boolean" ] = boolean_->isChecked();
    options[ "plainText" ] = plain_->isChecked();
    options[ "exclude" ] = exclude_->isChecked();
    options[ "includeUnmatched" ] = unmatched_->isChecked();
    options[ "matchMode" ] = matchMode_->currentData();
    QVariantMap result{ { "pattern", pattern_->text() },
                        { "scope", scope_->currentData() },
                        { "options", options },
                        { "limits", limits_ },
                        { "requestRevision", QString::number( revision_ ) },
                        { "counts",
                          QVariantMap{ { "capture", captureOverride_.isValid() ? captureOverride_
                                                    : numeric ? QVariant( number )
                                                              : QVariant( capture_->text() ) },
                                       { "subpattern", subpattern_->value() },
                                       { "caseSensitive", !fold_->isChecked() },
                                       { "missing", missing_->currentData() },
                                       { "maxValues", maxValues_->value() } } } };
    if ( scope_->currentData() == "text" )
        result[ "text" ] = text_->rawText();
    return result;
}
CommanderResult ValueCountDock::command( CommanderAction action, QVariantMap arguments )
{
    CommanderRequest request;
    request.action = action;
    if ( action == CommanderAction::ValueCounts )
        request.tabId = source_->currentData().toString();
    else {
        if ( id_.isEmpty() )
            return failure( "job_expired" );
        arguments[ "jobId" ] = id_;
    }
    request.analysisArguments = std::move( arguments );
    return execute_( request );
}
CommanderResult ValueCountDock::run()
{
    if ( text_->rawText().size() > 2 * 1024 * 1024 )
        return failure( "input_limit" );
    if ( !id_.isEmpty() )
        command( CommanderAction::ReleaseAnalysisJob );
    id_.clear();
    rows_.clear();
    job_.clear();
    cursor_.clear();
    fetched_ = false;
    table_->setRowCount( 0 );
    cancel_->setEnabled( false );
    next_->setEnabled( false );
    const auto result = command( CommanderAction::ValueCounts, draft() );
    if ( result.ok() ) {
        id_ = result.payload.value( "jobId" ).toString();
        jobRevision_ = revision_;
        LOG_DEBUG << "Value Counts run id=" << id_ << " revision=" << revision_;
        refresh();
    }
    return result;
}
void ValueCountDock::refreshSources()
{
    const auto available = sources_();
    if ( available == sourceCache_ && source_->count() > 0 )
        return;
    sourceCache_ = available;
    const auto selected = source_->currentData().toString();
    const QSignalBlocker blocker( source_ );
    source_->clear();
    source_->addItem( tr( "Current tab at Run" ), QString{} );
    for ( const auto& value : available ) {
        const auto item = value.toMap();
        source_->addItem( item.value( "label" ).toString(), item.value( "tabId" ) );
    }
    auto index = source_->findData( selected );
    if ( index < 0 ) {
        source_->addItem( tr( "Closed source (%1)" ).arg( selected ), selected );
        index = source_->count() - 1;
    }
    source_->setCurrentIndex( index );
}
void ValueCountDock::refresh()
{
    if ( refreshing_ )
        return;
    const QScopedValueRollback<bool> guard( refreshing_, true );
    if ( isVisible() )
        refreshSources();
    if ( id_.isEmpty() )
        return;
    const auto result = command( CommanderAction::GetAnalysisJob );
    if ( !result.ok() ) {
        rows_.clear();
        table_->setRowCount( 0 );
        cancel_->setEnabled( false );
        next_->setEnabled( false );
        showFailure( result );
        return;
    }
    job_ = result.payload;
    const auto state = job_.value( "state" ).toString();
    cancel_->setEnabled( !terminal( state ) );
    if ( revision_ != jobRevision_ )
        return;
    const auto aggregate = job_.value( "aggregation" ).toMap();
    QString text
        = tr( "%1 — %2 unique values; %3 counted occurrences; %4 missing skipped.\n"
              "Percent denominator: counted occurrences. %5" )
              .arg( state )
              .arg( aggregate.value( "uniqueValues" ).toInt() )
              .arg( aggregate.value( "countedOccurrences" ).toString() )
              .arg( aggregate.value( "skippedMissing" ).toString() )
              .arg( job_.value( "complete" ).toBool()
                        ? tr( "Complete" )
                        : tr( "Incomplete: %1" ).arg( job_.value( "reason" ).toString() ) );
    if ( job_.value( "historical" ).toBool() )
        text += '\n' + tr( "Historical source; navigation unavailable" );
    else if ( job_.value( "newerDataAvailable" ).toBool() )
        text += '\n' + tr( "Newer data available; Run again to include it" );
    text += '\n'
            + tr( "Backend: %1; scanned lines: %2" )
                  .arg( job_.value( "captureBackend" ).toString(),
                        job_.value( "scannedLines" ).toString() );
    status_->setText( text );
    if ( terminal( state ) && !fetched_ )
        showFailure( page( {} ) );
}
CommanderResult ValueCountDock::page( const QString& cursor )
{
    if ( revision_ != jobRevision_ )
        return failure( "draft_changed" );
    const auto result = command( CommanderAction::GetAnalysisResults,
                                 { { "cursor", cursor }, { "pageSize", 200 } } );
    if ( !result.ok() )
        return result;
    rows_ = result.payload.value( "rows" ).toList();
    cursor_ = result.payload.value( "nextCursor" ).toString();
    next_->setEnabled( !cursor_.isEmpty() );
    table_->setRowCount( static_cast<int>( rows_.size() ) );
    for ( int index = 0; index < rows_.size(); ++index ) {
        const auto row = rows_.at( index ).toMap();
        auto value = row.value( "value" ).toString();
        if ( row.value( "missing" ).toBool() )
            value = tr( "[missing]" );
        else if ( value.isEmpty() )
            value = tr( "[empty string]" );
        const QStringList cells{ value.left( 1024 ), row.value( "count" ).toString(),
                                 QString::number( row.value( "percentage" ).toDouble(), 'f', 3 )
                                     + '%',
                                 row.value( "lineNumber" ).toString() };
        for ( int column = 0; column < cells.size(); ++column )
            table_->setItem( index, column, new QTableWidgetItem( cells.at( column ) ) );
    }
    fetched_ = terminal( result.payload.value( "state" ).toString() );
    return result;
}
CommanderResult ValueCountDock::selected( CommanderAction action )
{
    if ( revision_ != jobRevision_ )
        return failure( "draft_changed" );
    const auto index = table_->currentRow();
    if ( index < 0 || index >= rows_.size() )
        return failure( "invalid_result_index" );
    return command( action, { { "rowIndex", rows_.at( index ).toMap().value( "rowIndex" ) } } );
}
QVariantMap ValueCountDock::state()
{
    refresh();
    auto definition = draft();
    if ( definition.value( "text" ).toString().size() > 4096 ) {
        definition[ "text" ] = definition.value( "text" ).toString().left( 4096 );
        definition[ "textPreview" ] = true;
    }
    return { { "objectName", objectName() },
             { "visible", isVisible() },
             { "floating", isFloating() },
             { "draft", definition },
             { "sourceTabId", source_->currentData() },
             { "jobId", id_ },
             { "job", job_ },
             { "draftChanged", !id_.isEmpty() && revision_ != jobRevision_ },
             { "page", QVariantMap{ { "rows", rows_ }, { "nextCursor", cursor_ } } } };
}

CommanderResult ValueCountDock::setDefinition( const QVariantMap& definition )
{
    const std::set<QString> strings{ "pattern",     "scope",     "text",
                                     "sourceTabId", "operation", "filePath" };
    const std::set<QString> flags{ "visible", "floating", "allowPartial" };
    for ( auto it = definition.cbegin(); it != definition.cend(); ++it ) {
        if ( strings.count( it.key() ) ) {
            if ( it.value().userType() != QMetaType::QString )
                return failure( "invalid_definition" );
        }
        else if ( flags.count( it.key() ) ) {
            if ( it.value().userType() != QMetaType::Bool )
                return failure( "invalid_definition" );
        }
        else if ( it.key() == "options" || it.key() == "counts" || it.key() == "limits" ) {
            if ( it.value().userType() != QMetaType::QVariantMap )
                return failure( "invalid_definition" );
        }
        else if ( it.key() != "rowIndex" )
            return failure( "invalid_definition" );
    }
    const auto operation = definition.value( "operation" ).toString();
    if ( !QStringList{ "", "run", "cancel", "release", "first_page", "next_page", "apply", "open",
                       "export" }
              .contains( operation ) )
        return failure( "unsupported_operation" );
    if ( definition.value( "pattern" ).toString().size() > 65536
         || definition.value( "text" ).toString().size() > 2 * 1024 * 1024 )
        return failure( "input_limit" );
    if ( definition.contains( "scope" ) && scope_->findData( definition.value( "scope" ) ) < 0 )
        return failure( "invalid_scope" );
    auto counts = draft().value( "counts" ).toMap();
    const auto patch = definition.value( "counts" ).toMap();
    for ( auto it = patch.cbegin(); it != patch.cend(); ++it )
        counts[ it.key() ] = it.value();
    const auto countOptions = parseValueCountOptions( counts );
    if ( !countOptions )
        return failure( "invalid_count_options" );
    const auto options = definition.value( "options" ).toMap();
    for ( auto it = options.cbegin(); it != options.cend(); ++it ) {
        if ( it.key() == "matchMode" ) {
            if ( it.value().userType() != QMetaType::QString
                 || matchMode_->findData( it.value() ) < 0 )
                return failure( "unsupported_options" );
        }
        else if ( !std::set<QString>{ "caseSensitive", "boolean", "plainText", "exclude",
                                      "includeUnmatched" }
                       .count( it.key() )
                  || it.value().userType() != QMetaType::Bool )
            return failure( "unsupported_options" );
    }
    const auto limits = definition.value( "limits" ).toMap();
    const std::map<QString, int> maximum{ { "batchLines", 256 },
                                          { "batchBytes", 1024 * 1024 },
                                          { "recordBytes", 1024 * 1024 },
                                          { "pendingBytes", 4 * 1024 * 1024 },
                                          { "resultBytes", 16 * 1024 * 1024 },
                                          { "resultRecords", 10000 },
                                          { "batchTimeoutMs", 30000 },
                                          { "timeoutMs", 600000 } };
    for ( auto it = limits.cbegin(); it != limits.cend(); ++it ) {
        const auto number = it.value().toDouble();
        if ( !maximum.count( it.key() ) || it.value().userType() == QMetaType::Bool
             || it.value().userType() == QMetaType::QString || !std::isfinite( number )
             || number != std::floor( number ) || number < 1 || number > maximum.at( it.key() ) )
            return failure( "invalid_limits" );
    }
    if ( definition.contains( "rowIndex" ) ) {
        const auto value = definition.value( "rowIndex" );
        bool converted = false;
        const auto number = value.toDouble( &converted );
        if ( value.userType() == QMetaType::Bool || value.userType() == QMetaType::QString
             || !converted || !std::isfinite( number ) || number != std::floor( number )
             || number < 0 || number > 10000 )
            return failure( "invalid_result_index" );
    }
    refreshSources();
    if ( definition.contains( "sourceTabId" )
         && source_->findData( definition.value( "sourceTabId" ) ) < 0 )
        return failure( "source_not_found" );
    const auto hasDraft = definition.contains( "pattern" ) || definition.contains( "scope" )
                          || definition.contains( "text" ) || definition.contains( "sourceTabId" )
                          || definition.contains( "counts" ) || definition.contains( "options" )
                          || definition.contains( "limits" );
    {
        const QScopedValueRollback<bool> guard( updating_, true );
        if ( definition.contains( "pattern" ) )
            pattern_->setText( definition.value( "pattern" ).toString() );
        if ( definition.contains( "scope" ) )
            scope_->setCurrentIndex( scope_->findData( definition.value( "scope" ) ) );
        if ( definition.contains( "sourceTabId" ) )
            source_->setCurrentIndex( source_->findData( definition.value( "sourceTabId" ) ) );
        if ( definition.contains( "text" ) )
            text_->setRawText( definition.value( "text" ).toString() );
        capture_->setText( countOptions->capture.toString() );
        captureOverride_ = countOptions->capture;
        subpattern_->setValue( countOptions->subpattern );
        maxValues_->setValue( countOptions->maxValues );
        fold_->setChecked( !countOptions->caseSensitive );
        missing_->setCurrentIndex(
            missing_->findData( countOptions->includeMissing ? "include" : "skip" ) );
        for ( auto it = options.cbegin(); it != options.cend(); ++it )
            extraOptions_[ it.key() ] = it.value();
        if ( options.contains( "caseSensitive" ) )
            matchCase_->setChecked( options.value( "caseSensitive" ).toBool() );
        if ( options.contains( "boolean" ) )
            boolean_->setChecked( options.value( "boolean" ).toBool() );
        if ( options.contains( "plainText" ) )
            plain_->setChecked( options.value( "plainText" ).toBool() );
        if ( options.contains( "exclude" ) )
            exclude_->setChecked( options.value( "exclude" ).toBool() );
        if ( options.contains( "includeUnmatched" ) )
            unmatched_->setChecked( options.value( "includeUnmatched" ).toBool() );
        if ( options.contains( "matchMode" ) )
            matchMode_->setCurrentIndex( matchMode_->findData( options.value( "matchMode" ) ) );
        for ( auto it = limits.cbegin(); it != limits.cend(); ++it )
            limits_[ it.key() ] = it.value();
    }
    if ( hasDraft )
        draftChanged();
    if ( definition.contains( "floating" ) )
        setFloating( definition.value( "floating" ).toBool() );
    if ( definition.contains( "visible" ) )
        setVisible( definition.value( "visible" ).toBool() );
    if ( operation == "run" )
        return run();
    if ( operation == "cancel" )
        return command( CommanderAction::CancelAnalysisJob );
    if ( operation == "release" ) {
        const auto result = command( CommanderAction::ReleaseAnalysisJob );
        id_.clear();
        rows_.clear();
        job_.clear();
        cursor_.clear();
        cancel_->setEnabled( false );
        next_->setEnabled( false );
        table_->setRowCount( 0 );
        return result;
    }
    if ( operation == "first_page" )
        return page( {} );
    if ( operation == "next_page" )
        return page( cursor_ );
    if ( operation == "apply" || operation == "open" ) {
        if ( revision_ != jobRevision_ )
            return failure( "draft_changed" );
        if ( definition.contains( "rowIndex" ) )
            return command( operation == "apply" ? CommanderAction::ApplyValueCount
                                                 : CommanderAction::OpenValueCount,
                            { { "rowIndex", definition.value( "rowIndex" ) } } );
        return selected( operation == "apply" ? CommanderAction::ApplyValueCount
                                              : CommanderAction::OpenValueCount );
    }
    if ( operation == "export" )
        return command( CommanderAction::ExportValueCounts,
                        { { "filePath", definition.value( "filePath" ) },
                          { "allowPartial", definition.value( "allowPartial", false ) } } );
    return { CommanderResultCode::Success, {}, state() };
}
