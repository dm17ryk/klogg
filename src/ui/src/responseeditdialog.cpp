#include "responseeditdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QVBoxLayout>

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

    matchValueEdit_ = new QPlainTextEdit( this );
    matchValueEdit_->setTabChangesFocus( true );
    matchValueEdit_->setMinimumHeight( 100 );

    actionCombo_ = new QComboBox( this );
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
    formLayout->addRow( tr( "Match value" ), matchValueEdit_ );
    formLayout->addRow( tr( "Linked action" ), actionCombo_ );
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
}

void ResponseEditDialog::setResponse( const ResponseDefinition& response,
                                      const QVector<ActionDefinition>& actions )
{
    actions_ = actions;
    populateActionCombo( actions );
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
    response_.match.value = matchValueEdit_->toPlainText().trimmed();
    if ( response_.match.type == ResponseMatchType::Regex ) {
        response_.match.compiled = QRegularExpression( response_.match.value );
    }
    else if ( response_.match.type == ResponseMatchType::Wildcard ) {
        response_.match.compiled = QRegularExpression(
            QRegularExpression::wildcardToRegularExpression( response_.match.value ),
            QRegularExpression::CaseInsensitiveOption );
    }
    else {
        response_.match.compiled = QRegularExpression();
    }

    response_.response.hasInlineAction = inlineActionCheck_->isChecked();
    response_.response.hasActionId = !response_.response.hasInlineAction
                                     && actionCombo_->currentData().isValid()
                                     && actionCombo_->currentData().toInt() >= 0;
    response_.response.actionId = response_.response.hasActionId ? actionCombo_->currentData().toInt() : -1;
    bool inlineOk = false;
    response_.response.inlineAction.type = actionSequenceTypeFromString(
        inlineTypeCombo_->currentData().toString(), &inlineOk );
    if ( !inlineOk ) {
        response_.response.inlineAction.type = ActionSequenceType::String;
    }
    response_.response.inlineAction.value = inlineValueEdit_->toPlainText().trimmed();
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

void ResponseEditDialog::populateActionCombo( const QVector<ActionDefinition>& actions )
{
    actionCombo_->clear();
    actionCombo_->addItem( tr( "No linked action" ), -1 );
    for ( const auto& action : actions ) {
        actionCombo_->addItem( QStringLiteral( "%1 (%2)" ).arg( action.name ).arg( action.id ),
                               action.id );
    }
}

void ResponseEditDialog::populateFromResponse( const ResponseDefinition& response )
{
    nameEdit_->setText( response.name );
    descriptionEdit_->setPlainText( response.description );
    const auto matchIndex
        = matchTypeCombo_->findData( responseMatchTypeToString( response.match.type ) );
    matchTypeCombo_->setCurrentIndex( matchIndex >= 0 ? matchIndex : 0 );
    matchValueEdit_->setPlainText( response.match.value );

    const auto actionIndex = actionCombo_->findData( response.response.actionId );
    actionCombo_->setCurrentIndex( actionIndex >= 0 ? actionIndex : 0 );
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
}
