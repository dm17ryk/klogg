#include "responseeditdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "previewdecodeutils.h"

namespace {
QString bytesToHexString( const QByteArray& bytes )
{
    QStringList parts;
    parts.reserve( bytes.size() );
    for ( const auto byte : bytes ) {
        parts.push_back( QStringLiteral( "%1" )
                             .arg( static_cast<quint8>( byte ), 2, 16, QLatin1Char( '0' ) )
                             .toUpper() );
    }
    return parts.join( QLatin1Char( ' ' ) );
}

QString actionLabel( const QVector<ActionDefinition>& actions, int actionId )
{
    for ( const auto& action : actions ) {
        if ( action.id == actionId ) {
            return QStringLiteral( "%1 (%2)" ).arg( action.name ).arg( action.id );
        }
    }
    return QObject::tr( "Unknown action (%1)" ).arg( actionId );
}

class ResponseActionStepDialog : public QDialog {
  public:
    explicit ResponseActionStepDialog( const QVector<ActionDefinition>& actions, QWidget* parent = nullptr )
        : QDialog( parent )
        , actions_( actions )
    {
        setWindowTitle( tr( "Linked Action Step" ) );

        filterEdit_ = new QLineEdit( this );
        filterEdit_->setPlaceholderText( tr( "Filter actions" ) );

        actionCombo_ = new QComboBox( this );

        delaySpin_ = new QSpinBox( this );
        delaySpin_->setRange( 0, 3600000 );
        delaySpin_->setSuffix( tr( " ms" ) );

        auto* formLayout = new QFormLayout;
        formLayout->addRow( tr( "Filter" ), filterEdit_ );
        formLayout->addRow( tr( "Action" ), actionCombo_ );
        formLayout->addRow( tr( "Delay" ), delaySpin_ );

        buttons_ = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
        connect( filterEdit_, &QLineEdit::textChanged, this, [this]() {
            reloadActionCombo( currentActionId() );
        } );
        connect( actionCombo_, qOverload<int>( &QComboBox::currentIndexChanged ), this, [this]( int ) {
            updateState();
        } );
        connect( buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept );
        connect( buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject );

        auto* layout = new QVBoxLayout( this );
        layout->addLayout( formLayout );
        layout->addWidget( buttons_ );

        reloadActionCombo();
    }

    void setStep( const ResponseActionStep& step )
    {
        filterEdit_->clear();
        reloadActionCombo( step.actionId );
        delaySpin_->setValue( step.delayMs );
    }

    ResponseActionStep step() const
    {
        return { actionCombo_->currentData().toInt(), delaySpin_->value() };
    }

  private:
    void reloadActionCombo( int preferredActionId = -1 )
    {
        const auto filter = filterEdit_->text().trimmed();
        actionCombo_->clear();

        for ( const auto& action : actions_ ) {
            const auto label = QStringLiteral( "%1 (%2)" ).arg( action.name ).arg( action.id );
            if ( !filter.isEmpty()
                 && !label.contains( filter, Qt::CaseInsensitive )
                 && !action.description.contains( filter, Qt::CaseInsensitive ) ) {
                continue;
            }

            actionCombo_->addItem( label, action.id );
        }

        const auto index = preferredActionId >= 0 ? actionCombo_->findData( preferredActionId ) : 0;
        actionCombo_->setCurrentIndex( index >= 0 ? index : ( actionCombo_->count() > 0 ? 0 : -1 ) );
        updateState();
    }

    int currentActionId() const
    {
        return actionCombo_->currentData().isValid() ? actionCombo_->currentData().toInt() : -1;
    }

    void updateState()
    {
        const bool hasActions = actionCombo_->count() > 0 && currentActionId() >= 0;
        actionCombo_->setEnabled( hasActions );
        if ( auto* okButton = buttons_->button( QDialogButtonBox::Ok ) ) {
            okButton->setEnabled( hasActions );
        }
    }

    QVector<ActionDefinition> actions_;
    QLineEdit* filterEdit_ = nullptr;
    QComboBox* actionCombo_ = nullptr;
    QSpinBox* delaySpin_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};
} // namespace

ResponseEditDialog::ResponseEditDialog( QWidget* parent )
    : QDialog( parent )
{
    setWindowTitle( tr( "Edit Response" ) );

    nameEdit_ = new QLineEdit( this );
    descriptionEdit_ = new QPlainTextEdit( this );
    descriptionEdit_->setTabChangesFocus( true );
    descriptionEdit_->setMinimumHeight( 80 );

    matchTypeCombo_ = new QComboBox( this );
    matchTypeCombo_->addItem( tr( "String" ), responseMatchTypeToString( ResponseMatchType::String ) );
    matchTypeCombo_->addItem( tr( "Hex String" ), responseMatchTypeToString( ResponseMatchType::HexString ) );
    matchTypeCombo_->addItem( tr( "Regex" ), responseMatchTypeToString( ResponseMatchType::Regex ) );
    matchTypeCombo_->addItem( tr( "Wildcard" ), responseMatchTypeToString( ResponseMatchType::Wildcard ) );

    stringValueEdit_ = new QPlainTextEdit( this );
    stringValueEdit_->setTabChangesFocus( true );
    stringValueEdit_->setMinimumHeight( 70 );

    hexValueEdit_ = new QPlainTextEdit( this );
    hexValueEdit_->setTabChangesFocus( true );
    hexValueEdit_->setMinimumHeight( 70 );

    expressionValueEdit_ = new QPlainTextEdit( this );
    expressionValueEdit_->setTabChangesFocus( true );
    expressionValueEdit_->setMinimumHeight( 100 );

    auto* literalPage = new QWidget( this );
    auto* literalLayout = new QFormLayout( literalPage );
    literalLayout->addRow( tr( "String value" ), stringValueEdit_ );
    literalLayout->addRow( tr( "Hex string value" ), hexValueEdit_ );

    auto* expressionPage = new QWidget( this );
    auto* expressionLayout = new QVBoxLayout( expressionPage );
    expressionLayout->setContentsMargins( 0, 0, 0, 0 );
    expressionLayout->addWidget( expressionValueEdit_ );

    matchEditorStack_ = new QStackedWidget( this );
    matchEditorStack_->addWidget( literalPage );
    matchEditorStack_->addWidget( expressionPage );

    stepsTable_ = new QTableWidget( this );
    stepsTable_->setColumnCount( 2 );
    stepsTable_->setHorizontalHeaderLabels( { tr( "Action" ), tr( "Delay (ms)" ) } );
    stepsTable_->horizontalHeader()->setStretchLastSection( false );
    stepsTable_->horizontalHeader()->setSectionResizeMode( 0, QHeaderView::Stretch );
    stepsTable_->horizontalHeader()->setSectionResizeMode( 1, QHeaderView::ResizeToContents );
    stepsTable_->verticalHeader()->setVisible( false );
    stepsTable_->setSelectionBehavior( QAbstractItemView::SelectRows );
    stepsTable_->setSelectionMode( QAbstractItemView::SingleSelection );
    stepsTable_->setEditTriggers( QAbstractItemView::NoEditTriggers );
    stepsTable_->setMinimumHeight( 140 );

    addStepButton_ = new QPushButton( tr( "Add" ), this );
    editStepButton_ = new QPushButton( tr( "Edit" ), this );
    deleteStepButton_ = new QPushButton( tr( "Delete" ), this );
    moveStepUpButton_ = new QPushButton( tr( "Move Up" ), this );
    moveStepDownButton_ = new QPushButton( tr( "Move Down" ), this );

    auto* stepButtonsLayout = new QVBoxLayout;
    stepButtonsLayout->addWidget( addStepButton_ );
    stepButtonsLayout->addWidget( editStepButton_ );
    stepButtonsLayout->addWidget( deleteStepButton_ );
    stepButtonsLayout->addSpacing( 12 );
    stepButtonsLayout->addWidget( moveStepUpButton_ );
    stepButtonsLayout->addWidget( moveStepDownButton_ );
    stepButtonsLayout->addStretch( 1 );

    auto* stepsLayout = new QHBoxLayout;
    stepsLayout->addWidget( stepsTable_, 1 );
    stepsLayout->addLayout( stepButtonsLayout );

    inlineActionCheck_ = new QCheckBox( tr( "Use inline action" ), this );

    inlineTypeCombo_ = new QComboBox( this );
    inlineTypeCombo_->addItem( tr( "String" ), actionSequenceTypeToString( ActionSequenceType::String ) );
    inlineTypeCombo_->addItem( tr( "Hex String" ), actionSequenceTypeToString( ActionSequenceType::HexString ) );

    inlineValueEdit_ = new QPlainTextEdit( this );
    inlineValueEdit_->setTabChangesFocus( true );
    inlineValueEdit_->setMinimumHeight( 80 );

    commentEdit_ = new QPlainTextEdit( this );
    commentEdit_->setTabChangesFocus( true );
    commentEdit_->setMinimumHeight( 70 );

    linebreakCheck_ = new QCheckBox( tr( "Insert line break" ), this );
    timestampCheck_ = new QCheckBox( tr( "Prefix timestamp" ), this );
    snapshotCheck_ = new QCheckBox( tr( "Request snapshot" ), this );
    stopCommunicationCheck_ = new QCheckBox( tr( "Stop communication" ), this );

    auto* formLayout = new QFormLayout;
    formLayout->addRow( tr( "Name" ), nameEdit_ );
    formLayout->addRow( tr( "Description" ), descriptionEdit_ );
    formLayout->addRow( tr( "Match type" ), matchTypeCombo_ );
    formLayout->addRow( tr( "Match value" ), matchEditorStack_ );
    formLayout->addRow( tr( "Linked actions" ), stepsLayout );
    formLayout->addRow( QString(), inlineActionCheck_ );
    formLayout->addRow( tr( "Inline action type" ), inlineTypeCombo_ );
    formLayout->addRow( tr( "Inline action" ), inlineValueEdit_ );
    formLayout->addRow( tr( "Comment" ), commentEdit_ );
    formLayout->addRow( QString(), linebreakCheck_ );
    formLayout->addRow( QString(), timestampCheck_ );
    formLayout->addRow( QString(), snapshotCheck_ );
    formLayout->addRow( QString(), stopCommunicationCheck_ );

    auto* buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
    connect( buttons, &QDialogButtonBox::accepted, this, &ResponseEditDialog::accept );
    connect( buttons, &QDialogButtonBox::rejected, this, &ResponseEditDialog::reject );

    auto* layout = new QVBoxLayout( this );
    layout->addLayout( formLayout );
    layout->addWidget( buttons );
    setLayout( layout );

    connect( stringValueEdit_, &QPlainTextEdit::textChanged, this, &ResponseEditDialog::syncHexFromString );
    connect( hexValueEdit_, &QPlainTextEdit::textChanged, this, &ResponseEditDialog::syncStringFromHex );
    connect( matchTypeCombo_, qOverload<int>( &QComboBox::currentIndexChanged ),
             this, [this]( int ) { updateMatchEditorMode(); } );
    connect( addStepButton_, &QPushButton::clicked, this, &ResponseEditDialog::addStep );
    connect( editStepButton_, &QPushButton::clicked, this, &ResponseEditDialog::editSelectedStep );
    connect( deleteStepButton_, &QPushButton::clicked, this, &ResponseEditDialog::deleteSelectedStep );
    connect( moveStepUpButton_, &QPushButton::clicked, this, [this]() { moveSelectedStep( -1 ); } );
    connect( moveStepDownButton_, &QPushButton::clicked, this, [this]() { moveSelectedStep( 1 ); } );
    connect( stepsTable_, &QTableWidget::itemDoubleClicked, this, [this]() { editSelectedStep(); } );
    connect( stepsTable_, &QTableWidget::itemSelectionChanged, this, [this]() {
        const auto row = selectedStepRow();
        const bool hasSelection = row >= 0;
        editStepButton_->setEnabled( hasSelection );
        deleteStepButton_->setEnabled( hasSelection );
        moveStepUpButton_->setEnabled( hasSelection && row > 0 );
        moveStepDownButton_->setEnabled( hasSelection && row >= 0 && row < steps_.size() - 1 );
    } );

    refreshStepTable();
}

void ResponseEditDialog::setResponse( const ResponseDefinition& response,
                                      const QVector<ActionDefinition>& actions )
{
    actions_ = actions;
    response_ = response;
    populateFromResponse( response );
}

ResponseDefinition ResponseEditDialog::response() const
{
    return response_;
}

void ResponseEditDialog::accept()
{
    response_.name = nameEdit_->text().trimmed();
    response_.description = descriptionEdit_->toPlainText().trimmed();
    bool ok = false;
    response_.match.type = responseMatchTypeFromString(
        matchTypeCombo_->currentData().toString(), &ok );
    if ( !ok ) {
        response_.match.type = ResponseMatchType::String;
    }

    if ( response_.match.type == ResponseMatchType::String ) {
        response_.match.value = stringValueEdit_->toPlainText();
        response_.match.compiled = QRegularExpression();
    }
    else if ( response_.match.type == ResponseMatchType::HexString ) {
        response_.match.value = hexValueEdit_->toPlainText().trimmed();
        response_.match.compiled = QRegularExpression();
    }
    else {
        response_.match.value = expressionValueEdit_->toPlainText().trimmed();
        if ( response_.match.type == ResponseMatchType::Regex ) {
            response_.match.compiled = QRegularExpression( response_.match.value );
        }
        else {
            response_.match.compiled = QRegularExpression(
                QRegularExpression::wildcardToRegularExpression( response_.match.value ),
                QRegularExpression::CaseInsensitiveOption );
        }
    }

    response_.response.steps = steps_;
    response_.response.hasActionId = !steps_.isEmpty();
    response_.response.actionId = response_.response.hasActionId ? steps_.front().actionId : -1;
    response_.response.hasInlineAction = inlineActionCheck_->isChecked();
    bool inlineOk = false;
    response_.response.inlineAction.type = actionSequenceTypeFromString(
        inlineTypeCombo_->currentData().toString(), &inlineOk );
    if ( !inlineOk ) {
        response_.response.inlineAction.type = ActionSequenceType::String;
    }
    response_.response.inlineAction.value = inlineValueEdit_->toPlainText();
    response_.response.comment = commentEdit_->toPlainText().trimmed();
    response_.response.linebreak = linebreakCheck_->isChecked();
    response_.response.timestamp = timestampCheck_->isChecked();
    response_.response.snapshot = snapshotCheck_->isChecked();
    response_.response.stopCommunication = stopCommunicationCheck_->isChecked();

    if ( !response_.response.hasInlineAction ) {
        response_.response.inlineAction = {};
    }

    QString errorMessage;
    if ( !validateResponseDefinition( response_, &errorMessage ) ) {
        QMessageBox::warning( this, tr( "Edit Response" ), errorMessage );
        return;
    }

    QDialog::accept();
}

void ResponseEditDialog::populateFromResponse( const ResponseDefinition& response )
{
    nameEdit_->setText( response.name );
    descriptionEdit_->setPlainText( response.description );
    const auto matchIndex
        = matchTypeCombo_->findData( responseMatchTypeToString( response.match.type ) );
    matchTypeCombo_->setCurrentIndex( matchIndex >= 0 ? matchIndex : 0 );
    setLiteralEditors( response.match );
    expressionValueEdit_->setPlainText( response.match.value );

    steps_ = response.response.steps;
    refreshStepTable();

    inlineActionCheck_->setChecked( response.response.hasInlineAction );
    const auto inlineIndex = inlineTypeCombo_->findData(
        actionSequenceTypeToString( response.response.inlineAction.type ) );
    inlineTypeCombo_->setCurrentIndex( inlineIndex >= 0 ? inlineIndex : 0 );
    inlineValueEdit_->setPlainText( response.response.inlineAction.value );
    commentEdit_->setPlainText( response.response.comment );
    linebreakCheck_->setChecked( response.response.linebreak );
    timestampCheck_->setChecked( response.response.timestamp );
    snapshotCheck_->setChecked( response.response.snapshot );
    stopCommunicationCheck_->setChecked( response.response.stopCommunication );
    updateMatchEditorMode();
}

void ResponseEditDialog::setLiteralEditors( const ResponseMatchDefinition& match )
{
    syncingLiteralEditors_ = true;
    const QSignalBlocker stringBlocker( stringValueEdit_ );
    const QSignalBlocker hexBlocker( hexValueEdit_ );

    if ( match.type == ResponseMatchType::HexString ) {
        const auto decoded = decodeHexStringToBytes( match.value );
        stringValueEdit_->setPlainText( decoded.ok ? QString::fromLatin1( decoded.bytes ) : QString() );
        hexValueEdit_->setPlainText( decoded.ok ? bytesToHexString( decoded.bytes ) : match.value );
    }
    else {
        stringValueEdit_->setPlainText( match.value );
        hexValueEdit_->setPlainText( bytesToHexString( match.value.toLatin1() ) );
    }

    syncingLiteralEditors_ = false;
}

void ResponseEditDialog::syncHexFromString()
{
    if ( syncingLiteralEditors_ ) {
        return;
    }

    syncingLiteralEditors_ = true;
    const QSignalBlocker blocker( hexValueEdit_ );
    hexValueEdit_->setPlainText( bytesToHexString( stringValueEdit_->toPlainText().toLatin1() ) );
    syncingLiteralEditors_ = false;
}

void ResponseEditDialog::syncStringFromHex()
{
    if ( syncingLiteralEditors_ ) {
        return;
    }

    const auto decoded = decodeHexStringToBytes( hexValueEdit_->toPlainText() );
    if ( !decoded.ok ) {
        return;
    }

    syncingLiteralEditors_ = true;
    const QSignalBlocker blocker( stringValueEdit_ );
    stringValueEdit_->setPlainText( QString::fromLatin1( decoded.bytes ) );
    syncingLiteralEditors_ = false;
}

void ResponseEditDialog::updateMatchEditorMode()
{
    bool ok = false;
    const auto type = responseMatchTypeFromString( matchTypeCombo_->currentData().toString(), &ok );
    if ( !ok ) {
        matchEditorStack_->setCurrentIndex( 0 );
        return;
    }

    const bool literalMode = type == ResponseMatchType::String || type == ResponseMatchType::HexString;
    if ( literalMode ) {
        ResponseMatchDefinition match;
        match.type = type;
        match.value = type == ResponseMatchType::HexString ? hexValueEdit_->toPlainText().trimmed()
                                                           : stringValueEdit_->toPlainText();
        if ( stringValueEdit_->toPlainText().isEmpty() && hexValueEdit_->toPlainText().isEmpty()
             && !expressionValueEdit_->toPlainText().isEmpty() ) {
            match.value = expressionValueEdit_->toPlainText();
            match.type = ResponseMatchType::String;
        }
        setLiteralEditors( match );
    }
    else {
        if ( expressionValueEdit_->toPlainText().isEmpty() ) {
            expressionValueEdit_->setPlainText( stringValueEdit_->toPlainText() );
        }
    }
    matchEditorStack_->setCurrentIndex( literalMode ? 0 : 1 );
}

void ResponseEditDialog::refreshStepTable()
{
    stepsTable_->setRowCount( steps_.size() );
    for ( int row = 0; row < steps_.size(); ++row ) {
        const auto& step = steps_.at( row );
        auto* actionItem = new QTableWidgetItem( actionLabel( actions_, step.actionId ) );
        actionItem->setData( Qt::UserRole, step.actionId );
        auto* delayItem = new QTableWidgetItem( QString::number( step.delayMs ) );
        delayItem->setData( Qt::UserRole, step.delayMs );
        stepsTable_->setItem( row, 0, actionItem );
        stepsTable_->setItem( row, 1, delayItem );
    }

    const bool hasSelection = selectedStepRow() >= 0;
    editStepButton_->setEnabled( hasSelection );
    deleteStepButton_->setEnabled( hasSelection );
    moveStepUpButton_->setEnabled( hasSelection && selectedStepRow() > 0 );
    moveStepDownButton_->setEnabled( hasSelection && selectedStepRow() >= 0
                                     && selectedStepRow() < steps_.size() - 1 );
}

int ResponseEditDialog::selectedStepRow() const
{
    const auto ranges = stepsTable_->selectedRanges();
    if ( ranges.isEmpty() ) {
        return -1;
    }
    return ranges.constFirst().topRow();
}

void ResponseEditDialog::addStep()
{
    if ( actions_.isEmpty() ) {
        QMessageBox::warning( this, tr( "Edit Response" ),
                              tr( "Create at least one action before linking response steps." ) );
        return;
    }

    ResponseActionStepDialog dialog( actions_, this );
    if ( dialog.exec() != QDialog::Accepted ) {
        return;
    }

    steps_.push_back( dialog.step() );
    refreshStepTable();
    stepsTable_->selectRow( steps_.size() - 1 );
}

void ResponseEditDialog::editSelectedStep()
{
    const auto row = selectedStepRow();
    if ( row < 0 || row >= steps_.size() ) {
        return;
    }

    ResponseActionStepDialog dialog( actions_, this );
    dialog.setStep( steps_.at( row ) );
    if ( dialog.exec() != QDialog::Accepted ) {
        return;
    }

    steps_[ row ] = dialog.step();
    refreshStepTable();
    stepsTable_->selectRow( row );
}

void ResponseEditDialog::deleteSelectedStep()
{
    const auto row = selectedStepRow();
    if ( row < 0 || row >= steps_.size() ) {
        return;
    }

    steps_.removeAt( row );
    refreshStepTable();
    if ( row < steps_.size() ) {
        stepsTable_->selectRow( row );
    }
    else if ( !steps_.isEmpty() ) {
        stepsTable_->selectRow( steps_.size() - 1 );
    }
}

void ResponseEditDialog::moveSelectedStep( int offset )
{
    const auto row = selectedStepRow();
    if ( row < 0 || row >= steps_.size() ) {
        return;
    }

    const auto targetRow = qBound( 0, row + offset, steps_.size() - 1 );
    if ( targetRow == row ) {
        return;
    }

    steps_.move( row, targetRow );
    refreshStepTable();
    stepsTable_->selectRow( targetRow );
}
