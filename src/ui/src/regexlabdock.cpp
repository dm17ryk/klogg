// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "regexlabdock.h"
#include "analysiscommands.h"
#include "analysisstatus.h"
#include "analysistextedit.h"
#include "log.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QInputMethodEvent>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

namespace {
constexpr int TextUnits = 2 * 1024 * 1024;
template <typename T>
T* named( T* widget, const char* name, const QString& label )
{
    widget->setObjectName( QLatin1String( name ) );
    widget->setAccessibleName( label );
    return widget;
}
CommanderResult rejected( const QString& reason )
{
    LOG_DEBUG << "Regex Lab rejected reason=" << reason;
    return { CommanderResultCode::InvalidRequest, reason, { { "reason", reason } } };
}
bool terminalState( const QString& state )
{
    return state == "completed" || state == "failed" || state == "cancelled";
}
} // namespace

RegexLabDock::RegexLabDock( QWidget* parent, Executor executor, Sources sources, Navigate navigate )
    : QDockWidget( tr( "Regex Lab" ), parent )
    , execute_( std::move( executor ) )
    , sources_( std::move( sources ) )
    , navigate_( std::move( navigate ) )
{
    setObjectName( "regexLabDock" );
    setAccessibleName( tr( "Regex Lab" ) );
    setFeatures( DockWidgetClosable | DockWidgetMovable | DockWidgetFloatable );
    auto* body = new QWidget( this );
    auto* layout = new QVBoxLayout( body );
    auto* form = new QFormLayout;
    pattern_ = named( new QLineEdit( body ), "regexLabPattern", tr( "Pattern" ) );
    pattern_->setMaxLength( 65536 );
    pattern_->setPlaceholderText( tr( "Regular expression or CILogg Boolean expression" ) );
    form->addRow( tr( "&Pattern" ), pattern_ );
    source_ = named( new QComboBox( body ), "regexLabSource", tr( "Analysis source" ) );
    source_->addItem( tr( "Current tab at Run" ), QString{} );
    form->addRow( tr( "&Source" ), source_ );
    scope_ = named( new QComboBox( body ), "regexLabScope", tr( "Analysis scope" ) );
    for ( const auto& item : std::vector<std::pair<QString, QString>>{
              { tr( "Sample (1000 lines / 4 MiB)" ), "sample" },
              { tr( "Selected physical rows" ), "selection" },
              { tr( "Completed search matches" ), "matches" },
              { tr( "Whole file snapshot" ), "whole_file" },
              { tr( "Pasted text" ), "text" } } )
        scope_->addItem( item.first, item.second );
    form->addRow( tr( "S&cope" ), scope_ );
    matchMode_ = named( new QComboBox( body ), "regexLabMatchMode", tr( "Match mode" ) );
    matchMode_->addItem( tr( "Contains" ), "contains" );
    matchMode_->addItem( tr( "Whole word" ), "whole_word" );
    matchMode_->addItem( tr( "Whole line" ), "whole_line" );
    form->addRow( tr( "&Match mode" ), matchMode_ );
    layout->addLayout( form );
    auto* flags = new QGridLayout;
    case_ = named( new QCheckBox( tr( "Case sensitive" ), body ), "regexLabCaseSensitive",
                   tr( "Case sensitive" ) );
    case_->setChecked( true );
    boolean_ = named( new QCheckBox( tr( "Boolean" ), body ), "regexLabBoolean",
                      tr( "Boolean expression" ) );
    plain_ = named( new QCheckBox( tr( "Literal text" ), body ), "regexLabPlainText",
                    tr( "Literal text" ) );
    exclude_ = named( new QCheckBox( tr( "Exclude" ), body ), "regexLabExclude",
                      tr( "Exclude combined matches" ) );
    unmatched_ = named( new QCheckBox( tr( "Include nonmatching rows" ), body ),
                        "regexLabIncludeUnmatched", tr( "Include nonmatching rows" ) );
    int flagIndex = 0;
    for ( auto* flag : { case_, boolean_, plain_, exclude_, unmatched_ } ) {
        flags->addWidget( flag, flagIndex / 2, flagIndex % 2 );
        ++flagIndex;
    }
    layout->addLayout( flags );
    auto* input = named( new LabTextEdit( body ), "regexLabText", tr( "Pasted log text" ) );
    text_ = input;
    text_->setPlaceholderText(
        tr( "Paste log text here, then choose Pasted text scope. Maximum 4 MiB of UTF-16 text." ) );
    text_->setMaximumHeight( 120 );
    text_->setLineWrapMode( QPlainTextEdit::NoWrap );
    text_->hide();
    layout->addWidget( text_ );
    auto* budgets = new QFormLayout;
    batchBudget_ = named( new QSpinBox( body ), "regexLabBatchBudget",
                          tr( "Worker budget in milliseconds" ) );
    batchBudget_->setRange( 1, 30000 );
    batchBudget_->setValue( 2000 );
    jobBudget_
        = named( new QSpinBox( body ), "regexLabJobBudget", tr( "Job budget in milliseconds" ) );
    jobBudget_->setRange( 1, 600000 );
    jobBudget_->setValue( 60000 );
    budgets->addRow( tr( "Worker budget (ms)" ), batchBudget_ );
    budgets->addRow( tr( "Job budget (ms)" ), jobBudget_ );
    layout->addLayout( budgets );
    auto* buttons = new QHBoxLayout;
    run_ = named( new QPushButton( tr( "Run" ), body ), "regexLabRun", tr( "Run analysis" ) );
    cancel_ = named( new QPushButton( tr( "Cancel" ), body ), "regexLabCancel",
                     tr( "Cancel analysis" ) );
    apply_ = named( new QPushButton( tr( "Apply to current search" ), body ), "regexLabApply",
                    tr( "Apply validated pattern to current search" ) );
    buttons->addWidget( run_ );
    buttons->addWidget( cancel_ );
    buttons->addWidget( apply_ );
    layout->addLayout( buttons );
    status_ = named(
        new QLabel( tr( "Choose a pattern and Run. The default scope is a bounded sample." ),
                    body ),
        "regexLabStatus", tr( "Analysis status" ) );
    status_->setWordWrap( true );
    status_->setTextFormat( Qt::PlainText );
    status_->setTextInteractionFlags( Qt::TextSelectableByMouse );
    layout->addWidget( status_ );
    auto* results = new QSplitter( Qt::Vertical, body );
    table_ = named( new QTableWidget( 0, 4, results ), "regexLabResults",
                    tr( "Analysis result page" ) );
    table_->setHorizontalHeaderLabels(
        { tr( "Source line" ), tr( "Combined" ), tr( "Occurrences" ), tr( "Raw text" ) } );
    table_->setSelectionBehavior( QAbstractItemView::SelectRows );
    table_->setSelectionMode( QAbstractItemView::SingleSelection );
    table_->setEditTriggers( QAbstractItemView::NoEditTriggers );
    table_->horizontalHeader()->setStretchLastSection( true );
    table_->verticalHeader()->hide();
    details_ = named( new QPlainTextEdit( results ), "regexLabCaptures",
                      tr( "Capture values and zero-based UTF-16 spans" ) );
    details_->setReadOnly( true );
    details_->setPlaceholderText(
        tr( "Select a row for each subpattern's captures and spans. null means an unmatched "
            "optional group; an empty string means a matched empty group." ) );
    layout->addWidget( results, 1 );
    auto* paging = new QHBoxLayout;
    auto* first = named( new QPushButton( tr( "First page" ), body ), "regexLabFirstPage",
                         tr( "First result page" ) );
    next_ = named( new QPushButton( tr( "Next page" ), body ), "regexLabNextPage",
                   tr( "Next result page" ) );
    paging->addWidget( first );
    paging->addWidget( next_ );
    paging->addStretch();
    layout->addLayout( paging );
    setWidget( body );
    cancel_->setEnabled( false );
    apply_->setEnabled( false );
    next_->setEnabled( false );
    input->overflow = [ this ] {
        status_->setText( tr( "Paste rejected: input exceeds 4 MiB of UTF-16 text." ) );
    };
    connect( pattern_, &QLineEdit::textChanged, this, [ this ] { draftChanged(); } );
    connect( text_, &QPlainTextEdit::textChanged, this, [ this ] { draftChanged(); } );
    for ( auto* flag : { case_, boolean_, plain_, exclude_, unmatched_ } )
        connect( flag, &QCheckBox::toggled, this, [ this ] { draftChanged(); } );
    for ( auto* combo : { source_, scope_, matchMode_ } )
        connect( combo, qOverload<int>( &QComboBox::currentIndexChanged ), this,
                 [ this ] { draftChanged(); } );
    for ( auto* spin : { batchBudget_, jobBudget_ } )
        connect( spin, qOverload<int>( &QSpinBox::valueChanged ), this,
                 [ this ] { draftChanged(); } );
    connect( run_, &QPushButton::clicked, this, [ this ] { showFailure( run() ); } );
    connect( cancel_, &QPushButton::clicked, this,
             [ this ] { showFailure( command( CommanderAction::CancelAnalysisJob ) ); } );
    connect( apply_, &QPushButton::clicked, this, [ this ] { showFailure( apply() ); } );
    connect( first, &QPushButton::clicked, this, [ this ] { showFailure( page( {} ) ); } );
    connect( next_, &QPushButton::clicked, this, [ this ] { showFailure( page( nextCursor_ ) ); } );
    connect( table_, &QTableWidget::currentCellChanged, this,
             [ this ]( int row, int, int, int ) { showDetails( row ); } );
    connect( table_, &QTableWidget::cellActivated, this,
             [ this ]( int row, int ) { showFailure( activate( row ) ); } );
    timer_ = new QTimer( this );
    timer_->setInterval( 200 );
    connect( timer_, &QTimer::timeout, this, [ this ] { refresh(); } );
    timer_->start();
    refreshSources();
}

RegexLabDock::~RegexLabDock()
{
    // Teardown cannot call back into a partly destroyed MainWindow.
    if ( auto* service = applicationAnalysisService( false ); service && !jobId_.isEmpty() )
        service->release( jobId_ );
}
CommanderResult RegexLabDock::command( CommanderAction action, QVariantMap arguments )
{
    if ( jobId_.isEmpty() && action != CommanderAction::RegexEvaluate )
        return rejected( "job_expired" );
    CommanderRequest request;
    request.action = action;
    if ( action != CommanderAction::RegexEvaluate )
        arguments[ "jobId" ] = jobId_;
    request.analysisArguments = std::move( arguments );
    if ( action == CommanderAction::RegexEvaluate )
        request.tabId = source_->currentData().toString();
    return execute_( request );
}
void RegexLabDock::showFailure( const CommanderResult& result )
{
    if ( !result.ok() )
        status_->setText( result.message.isEmpty() ? result.payload.value( "reason" ).toString()
                                                   : result.message );
}
QVariantMap RegexLabDock::draft( bool fullText ) const
{
    auto limits = extraLimits_;
    limits[ "batchTimeoutMs" ] = batchBudget_->value();
    limits[ "timeoutMs" ] = jobBudget_->value();
    QVariantMap result{ { "pattern", pattern_->text() },
                        { "scope", scope_->currentData() },
                        { "requestRevision", QString::number( revision_ ) },
                        { "limits", limits },
                        { "options", QVariantMap{ { "caseSensitive", case_->isChecked() },
                                                  { "boolean", boolean_->isChecked() },
                                                  { "plainText", plain_->isChecked() },
                                                  { "exclude", exclude_->isChecked() },
                                                  { "includeUnmatched", unmatched_->isChecked() },
                                                  { "matchMode", matchMode_->currentData() } } } };
    if ( scope_->currentData() == "text" ) {
        const auto raw = static_cast<LabTextEdit*>( text_ )->rawText();
        result[ "text" ] = fullText ? raw : raw.left( 4096 );
        if ( !fullText )
            result[ "textPreview" ] = raw.size() > 4096;
    }
    return result;
}
void RegexLabDock::draftChanged()
{
    if ( updating_ )
        return;
    ++revision_;
    apply_->setEnabled( false );
    next_->setEnabled( false );
    source_->setEnabled( scope_->currentData() != "text" );
    text_->setVisible( scope_->currentData() == "text" );
    status_->setText( jobId_.isEmpty()
                          ? tr( "Draft changed. Run to validate and evaluate." )
                          : tr( "Draft changed. Displayed output belongs to the previous revision; "
                                "pending results will not update it." ) );
    LOG_DEBUG << "Regex Lab draft changed revision=" << revision_
              << " scope=" << scope_->currentData().toString();
}
void RegexLabDock::refreshSources()
{
    const auto available = sources_();
    if ( available == sourceCache_ )
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
CommanderResult RegexLabDock::run()
{
    if ( scope_->currentData() == "text"
         && static_cast<LabTextEdit*>( text_ )->rawText().size() > TextUnits )
        return rejected( "invalid_input" );
    if ( !jobId_.isEmpty() )
        command( CommanderAction::ReleaseAnalysisJob );
    jobId_.clear();
    rows_.clear();
    pageCursor_.clear();
    nextCursor_.clear();
    fetchedRecords_ = -1;
    jobStatus_.clear();
    table_->setRowCount( 0 );
    details_->clear();
    apply_->setEnabled( false );
    cancel_->setEnabled( false );
    next_->setEnabled( false );
    const auto result = command( CommanderAction::RegexEvaluate, draft( true ) );
    if ( result.ok() ) {
        jobId_ = result.payload.value( "jobId" ).toString();
        jobRevision_ = revision_;
        LOG_DEBUG << "Regex Lab run id=" << jobId_ << " revision=" << revision_;
        refresh( true );
    }
    return result;
}
void RegexLabDock::refresh( bool requestPage )
{
    if ( refreshing_ )
        return;
    const QScopedValueRollback<bool> guard( refreshing_, true );
    if ( isVisible() )
        refreshSources();
    if ( jobId_.isEmpty() )
        return;
    const auto result = command( CommanderAction::GetAnalysisJob );
    if ( !result.ok() ) {
        rows_.clear();
        table_->setRowCount( 0 );
        details_->clear();
        apply_->setEnabled( false );
        cancel_->setEnabled( false );
        next_->setEnabled( false );
        jobStatus_ = result.payload;
        showFailure( result );
        return;
    }
    jobStatus_ = result.payload;
    const auto state = jobStatus_.value( "state" ).toString();
    cancel_->setEnabled( !terminalState( state ) );
    if ( jobRevision_ != revision_ ) {
        auto message = tr( "Draft changed. Displayed output belongs to the previous revision; "
                           "pending results will not update it." );
        if ( jobStatus_.value( "historical" ).toBool() )
            message += '\n' + tr( "Historical source: navigation unavailable" );
        else if ( jobStatus_.value( "newerDataAvailable" ).toBool() )
            message += '\n' + tr( "Newer data available; Run again to include it" );
        status_->setText( message );
        return;
    }
    apply_->setEnabled( jobStatus_.value( "compiled" ).toBool() );
    QStringList description{ tr( "%1; %2 retained rows; %3 scanned lines" )
                                 .arg( state )
                                 .arg( jobStatus_.value( "resultRecords" ).toInt() )
                                 .arg( jobStatus_.value( "scannedLines" ).toULongLong() ) };
    if ( terminalState( state ) )
        description
            << ( jobStatus_.value( "complete" ).toBool()
                     ? tr( "Complete snapshot results" )
                     : tr( "Incomplete: %1" ).arg( jobStatus_.value( "reason" ).toString() ) );
    if ( jobStatus_.value( "historical" ).toBool() )
        description << tr( "Historical source: navigation unavailable" );
    else if ( jobStatus_.value( "newerDataAvailable" ).toBool() )
        description << tr( "Newer data available; Run again to include it" );
    const auto source = jobStatus_.value( "sourceDescription" ).toMap();
    description
        << tr( "Source: %1" ).arg( source.value( "filePath", tr( "Pasted text" ) ).toString() );
    description << tr( "Backend: %1" ).arg( jobStatus_.value( "captureBackend" ).toString() );
    const auto metrics = jobStatus_.value( "metrics" ).toMap();
    description << analysisWorkerMemoryText( metrics );
    const auto timing = jobStatus_.value( "timing" ).toMap();
    description
        << tr( "ms — compile %1; match %2; startup %3; read %4; delivery %5; backpressure %6" )
               .arg( metrics.value( "compileUs" ).toDouble() / 1000, 0, 'f', 2 )
               .arg( metrics.value( "matchUs" ).toDouble() / 1000, 0, 'f', 2 )
               .arg( metrics.value( "startupUs" ).toDouble() / 1000, 0, 'f', 2 )
               .arg( timing.value( "sourceReadUs" ).toDouble() / 1000, 0, 'f', 2 )
               .arg( timing.value( "resultDeliveryUs" ).toDouble() / 1000, 0, 'f', 2 )
               .arg( timing.value( "backpressureUs" ).toDouble() / 1000, 0, 'f', 2 );
    const auto error = jobStatus_.value( "errorDetails" ).toMap();
    if ( !error.isEmpty() )
        description << QString::fromUtf8(
            QJsonDocument::fromVariant( error ).toJson( QJsonDocument::Compact ) );
    status_->setText( description.join( '\n' ) );
    // Status/history stays live while hidden, but retained pages are delivered
    // only when the consumer can use them and the record count has changed.
    if ( ( requestPage || isVisible() )
         && ( fetchedRecords_ != jobStatus_.value( "resultRecords" ).toInt()
              || fetchedTerminal_ != terminalState( state ) ) ) {
        const auto pageResult = page( pageCursor_ );
        if ( !pageResult.ok() )
            showFailure( pageResult );
    }
}
CommanderResult RegexLabDock::page( const QString& cursor )
{
    if ( jobRevision_ != revision_ )
        return rejected( "draft_changed" );
    const auto result = command( CommanderAction::GetAnalysisResults,
                                 { { "cursor", cursor }, { "pageSize", 200 } } );
    if ( !result.ok() )
        return result;
    pageCursor_ = cursor;
    fetchedRecords_ = jobStatus_.value( "resultRecords" ).toInt();
    fetchedTerminal_ = terminalState( jobStatus_.value( "state" ).toString() );
    nextCursor_ = result.payload.value( "nextCursor" ).toString();
    next_->setEnabled( !nextCursor_.isEmpty() );
    const auto values = result.payload.value( "rows" ).toList();
    if ( rows_ == values )
        return result;
    const auto selected = table_->currentRow();
    rows_ = values;
    const QSignalBlocker blocker( table_ );
    table_->setRowCount( static_cast<int>( rows_.size() ) );
    for ( int index = 0; index < rows_.size(); ++index ) {
        const auto row = rows_.at( index ).toMap();
        int occurrences = 0;
        for ( const auto& subpattern : row.value( "subpatterns" ).toList() )
            occurrences
                += static_cast<int>( subpattern.toMap().value( "matches" ).toList().size() );
        const QStringList fields{ row.value( "lineNumber" ).toString(),
                                  row.value( "matched" ).toBool() ? tr( "Match" )
                                                                  : tr( "No match" ),
                                  QString::number( occurrences ),
                                  row.value( "text" ).toString().left( 1024 ) };
        for ( int column = 0; column < fields.size(); ++column )
            table_->setItem( index, column, new QTableWidgetItem( fields.at( column ) ) );
    }
    if ( selected >= 0 && selected < rows_.size() ) {
        table_->selectRow( selected );
        showDetails( selected );
    }
    else
        details_->clear();
    return result;
}
void RegexLabDock::showDetails( int index )
{
    if ( index < 0 || index >= rows_.size() ) {
        details_->clear();
        return;
    }
    auto row = rows_.at( index ).toMap();
    row.remove( "source" );
    row.remove( "jobId" );
    row.remove( "requestRevision" );
    details_->setPlainText(
        QString::fromUtf8( QJsonDocument::fromVariant( row ).toJson( QJsonDocument::Indented ) ) );
}
CommanderResult RegexLabDock::activate( int index )
{
    if ( index < 0 || index >= rows_.size() )
        return rejected( "invalid_result_index" );
    const auto number = rows_.at( index ).toMap().value( "lineNumber" ).toULongLong();
    if ( !number )
        return rejected( "invalid_location" );
    return navigate_( jobId_, number - 1 );
}
CommanderResult RegexLabDock::apply()
{
    refresh();
    if ( jobRevision_ != revision_ || !jobStatus_.value( "compiled" ).toBool() )
        return rejected( "pattern_not_validated" );
    CommanderRequest request;
    request.action = CommanderAction::Search;
    request.searchText = pattern_->text();
    request.searchUseRegex = !plain_->isChecked();
    request.searchUseBoolean = boolean_->isChecked();
    request.searchCaseSensitive = case_->isChecked();
    request.searchInverseMatch = exclude_->isChecked();
    request.matchMode = matchMode_->currentData().toString();
    // MainWindow preserves the current viewer's context/limit/refresh options for Lab apply.
    return execute_( request );
}
QVariantMap RegexLabDock::state()
{
    refresh( true );
    QVariantList numbers;
    for ( const auto& row : rows_ )
        numbers.push_back( row.toMap().value( "lineNumber" ) );
    return { { "objectName", objectName() },
             { "visible", isVisible() },
             { "floating", isFloating() },
             { "draft", draft( false ) },
             { "sourceTabId", source_->currentData() },
             { "jobId", jobId_ },
             { "jobRevision", QString::number( jobRevision_ ) },
             { "job", jobStatus_ },
             { "draftChanged", !jobId_.isEmpty() && revision_ != jobRevision_ },
             { "page", QVariantMap{ { "cursor", pageCursor_ },
                                    { "nextCursor", nextCursor_ },
                                    { "lineNumbers", numbers },
                                    { "rows", rows_.size() } } } };
}
void RegexLabDock::prepareSelection( const QString& tabId )
{
    refreshSources();
    const auto index = source_->findData( tabId );
    if ( index >= 0 )
        source_->setCurrentIndex( index );
    scope_->setCurrentIndex( scope_->findData( "selection" ) );
    show();
    raise();
    pattern_->setFocus();
}
CommanderResult RegexLabDock::setDefinition( const QVariantMap& definition )
{
    const std::set<QString> strings{ "pattern", "scope", "text", "sourceTabId", "operation" };
    const std::set<QString> booleans{ "visible", "floating" };
    for ( auto it = definition.cbegin(); it != definition.cend(); ++it ) {
        if ( strings.count( it.key() ) ) {
            if ( it.value().userType() != QMetaType::QString )
                return rejected( "invalid_definition" );
        }
        else if ( booleans.count( it.key() ) ) {
            if ( it.value().userType() != QMetaType::Bool )
                return rejected( "invalid_definition" );
        }
        else if ( it.key() == "options" || it.key() == "limits" ) {
            if ( it.value().userType() != QMetaType::QVariantMap )
                return rejected( "invalid_definition" );
        }
        else if ( it.key() != "activateResultIndex" )
            return rejected( "invalid_definition" );
    }
    if ( definition.value( "text" ).toString().size() > TextUnits
         || definition.value( "pattern" ).toString().size() > 65536 )
        return rejected( "input_limit" );
    if ( definition.contains( "scope" ) && scope_->findData( definition.value( "scope" ) ) < 0 )
        return rejected( "invalid_scope" );
    const auto options = definition.value( "options" ).toMap();
    const std::map<QString, QCheckBox*> flags{ { "caseSensitive", case_ },
                                               { "boolean", boolean_ },
                                               { "plainText", plain_ },
                                               { "exclude", exclude_ },
                                               { "includeUnmatched", unmatched_ } };
    for ( auto it = options.cbegin(); it != options.cend(); ++it ) {
        if ( it.key() == "matchMode" ) {
            if ( it.value().userType() != QMetaType::QString
                 || matchMode_->findData( it.value() ) < 0 )
                return rejected( "unsupported_options" );
        }
        else if ( !flags.count( it.key() ) || it.value().userType() != QMetaType::Bool )
            return rejected( "unsupported_options" );
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
        if ( !maximum.count( it.key() ) || it.value().userType() == QMetaType::Bool
             || it.value().userType() == QMetaType::QString )
            return rejected( "invalid_limits" );
        const auto value = it.value().toDouble();
        if ( !std::isfinite( value ) || value != std::floor( value ) || value < 1
             || value > maximum.at( it.key() ) )
            return rejected( "invalid_limits" );
    }
    const auto operation = definition.value( "operation" ).toString();
    if ( !QStringList{ "", "run", "cancel", "release", "first_page", "next_page" }.contains(
             operation ) )
        return rejected( "unsupported_operation" );
    if ( definition.contains( "activateResultIndex" ) ) {
        if ( definition.size() != 1 )
            return rejected( "invalid_definition" );
        const auto value = definition.value( "activateResultIndex" );
        const auto type = value.userType();
        const bool numeric = type == QMetaType::Int || type == QMetaType::UInt
                             || type == QMetaType::LongLong || type == QMetaType::ULongLong
                             || type == QMetaType::Double || type == QMetaType::Float;
        bool converted = false;
        const auto index = value.toDouble( &converted );
        if ( !numeric || !converted || !std::isfinite( index ) || index != std::floor( index )
             || index < 0 || index >= static_cast<double>( rows_.size() ) )
            return rejected( "invalid_result_index" );
    }
    refreshSources();
    if ( definition.contains( "sourceTabId" )
         && source_->findData( definition.value( "sourceTabId" ) ) < 0 )
        return rejected( "source_not_found" );
    {
        const QScopedValueRollback<bool> guard( updating_, true );
        if ( definition.contains( "pattern" ) )
            pattern_->setText( definition.value( "pattern" ).toString() );
        if ( definition.contains( "text" ) )
            static_cast<LabTextEdit*>( text_ )->setRawText( definition.value( "text" ).toString() );
        if ( definition.contains( "scope" ) )
            scope_->setCurrentIndex( scope_->findData( definition.value( "scope" ) ) );
        if ( definition.contains( "sourceTabId" ) )
            source_->setCurrentIndex( source_->findData( definition.value( "sourceTabId" ) ) );
        for ( const auto& flag : flags )
            if ( options.contains( flag.first ) )
                flag.second->setChecked( options.value( flag.first ).toBool() );
        if ( options.contains( "matchMode" ) )
            matchMode_->setCurrentIndex( matchMode_->findData( options.value( "matchMode" ) ) );
        for ( auto it = limits.cbegin(); it != limits.cend(); ++it ) {
            if ( it.key() == "batchTimeoutMs" )
                batchBudget_->setValue( it.value().toInt() );
            else if ( it.key() == "timeoutMs" )
                jobBudget_->setValue( it.value().toInt() );
            else
                extraLimits_[ it.key() ] = it.value();
        }
    }
    if ( definition.contains( "pattern" ) || definition.contains( "text" )
         || definition.contains( "scope" ) || definition.contains( "sourceTabId" )
         || !options.isEmpty() || !limits.isEmpty() )
        draftChanged();
    if ( definition.contains( "floating" ) )
        setFloating( definition.value( "floating" ).toBool() );
    if ( definition.contains( "visible" ) )
        setVisible( definition.value( "visible" ).toBool() );
    CommanderResult result;
    if ( operation == "run" )
        result = run();
    else if ( operation == "cancel" )
        result = command( CommanderAction::CancelAnalysisJob );
    else if ( operation == "release" ) {
        result = command( CommanderAction::ReleaseAnalysisJob );
        if ( result.ok() ) {
            jobId_.clear();
            jobStatus_.clear();
            rows_.clear();
            table_->setRowCount( 0 );
            details_->clear();
            cancel_->setEnabled( false );
            apply_->setEnabled( false );
            next_->setEnabled( false );
        }
    }
    else if ( operation == "first_page" || operation == "next_page" )
        result = operation == "next_page" && nextCursor_.isEmpty()
                     ? rejected( "no_next_page" )
                     : page( operation == "first_page" ? QString{} : nextCursor_ );
    if ( result.ok() && definition.contains( "activateResultIndex" ) )
        result = activate( definition.value( "activateResultIndex" ).toInt() );
    if ( !result.ok() ) {
        showFailure( result );
        return result;
    }
    return commanderSuccess( {}, state() );
}
