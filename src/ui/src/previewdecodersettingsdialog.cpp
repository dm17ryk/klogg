// SPDX-License-Identifier: GPL-3.0-or-later
#include "previewdecodersettingsdialog.h"
#include "log.h"
#include "previewdecodersettings.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
void identity( QWidget* widget, const QString& name, const QString& label )
{
    widget->setObjectName( name );
    widget->setAccessibleName( label );
}
QVariantMap routes( QTableWidget* table, bool protocol, QString* error )
{
    QVariantMap result;
    for ( int row = 0; row < table->rowCount(); ++row ) {
        const auto text = [ table, row ]( int column ) {
            const auto* item = table->item( row, column );
            return item ? item->text() : QString();
        };
        const auto name = text( 0 );
        if ( name.isEmpty() || result.contains( name ) ) {
            *error = QObject::tr( "Each route needs a unique, nonempty name." );
            return {};
        }
        QVariantMap route{ { "dissector", text( 1 ) }, { "protocol", text( 2 ) } };
        if ( protocol && !text( 3 ).isEmpty() )
            route[ "inputEncoding" ] = text( 3 );
        result[ name ] = route;
    }
    return result;
}
void populate( QTableWidget* table, const QVariantMap& values, bool protocol )
{
    table->setRowCount( 0 );
    for ( auto it = values.cbegin(); it != values.cend(); ++it ) {
        const auto route = it.value().toMap();
        const int row = table->rowCount();
        table->insertRow( row );
        const QStringList cells{ it.key(), route.value( "dissector" ).toString(),
                                 route.value( "protocol" ).toString(),
                                 route.value( "inputEncoding" ).toString() };
        for ( int column = 0; column < ( protocol ? 4 : 3 ); ++column )
            table->setItem( row, column, new QTableWidgetItem( cells[ column ] ) );
    }
}
} // namespace

PreviewDecoderSettingsDialog::PreviewDecoderSettingsDialog( QWidget* parent )
    : QDialog( parent )
{
    identity( this, "previewDecoderSettingsDialog", tr( "TShark / Lua decoder settings" ) );
    setWindowTitle( tr( "TShark / Lua preview decoder" ) );
    setModal( false );
    resize( 800, 700 );
    auto* layout = new QVBoxLayout( this );
    enabled_ = new QCheckBox( tr( "Enable TShark first for automatic previews" ), this );
    identity( enabled_, "decoderEnabled", enabled_->text() );
    approval_ = new QCheckBox(
        tr( "I trust and approve execution of the selected external Lua scripts" ), this );
    identity( approval_, "decoderAllowExternalLua", approval_->text() );
    layout->addWidget( enabled_ );
    layout->addWidget( approval_ );
    auto* form = new QFormLayout;
    const auto tool = [ this, form ]( const QString& label, const QString& name ) {
        auto* row = new QWidget( this );
        auto* horizontal = new QHBoxLayout( row );
        horizontal->setContentsMargins( 0, 0, 0, 0 );
        auto* edit = new QLineEdit( row );
        identity( edit, name, label );
        auto* browse = new QPushButton( tr( "Browse…" ), row );
        horizontal->addWidget( edit, 1 );
        horizontal->addWidget( browse );
        connect( browse, &QPushButton::clicked, this, [ this, edit, label ] {
            const auto path = QFileDialog::getOpenFileName( this, label, edit->text() );
            if ( !path.isEmpty() )
                edit->setText( path );
        } );
        form->addRow( label, row );
        return edit;
    };
    tshark_ = tool( tr( "TShark executable:" ), "decoderTshark" );
    text2pcap_ = tool( tr( "text2pcap executable:" ), "decoderText2pcap" );
    auto* discover = new QPushButton( tr( "Suggest tools from PATH" ), this );
    connect( discover, &QPushButton::clicked, this, [ this ] {
        // Discovery only suggests paths. Applying the dialog is explicit approval.
        tshark_->setText( QStandardPaths::findExecutable( "tshark" ) );
        text2pcap_->setText( QStandardPaths::findExecutable( "text2pcap" ) );
        LOG_DEBUG << "Preview decoder tool discovery completed";
    } );
    form->addRow( discover );
    dissector_ = new QLineEdit( this );
    identity( dissector_, "decoderDefaultDissector", tr( "Default dissector" ) );
    dissector_->setPlaceholderText( tr( "Empty: use mapped routes only" ) );
    form->addRow( tr( "Default named dissector:" ), dissector_ );
    encoding_ = new QComboBox( this );
    identity( encoding_, "decoderInputEncoding", tr( "Default input encoding" ) );
    encoding_->addItems( { "utf8", "hex", "base64" } );
    form->addRow( tr( "Direct input encoding:" ), encoding_ );
    layout->addLayout( form );
    scripts_ = new QListWidget( this );
    identity( scripts_, "decoderLuaScripts", tr( "Approved Lua files" ) );
    scripts_->setMaximumHeight( 100 );
    layout->addWidget( new QLabel(
        tr( "Selected external Lua files (executed only on a routed preview):" ), this ) );
    layout->addWidget( scripts_ );
    auto* files = new QHBoxLayout;
    auto* add = new QPushButton( tr( "Add Lua…" ), this );
    auto* remove = new QPushButton( tr( "Remove selected" ), this );
    files->addWidget( add );
    files->addWidget( remove );
    files->addStretch();
    layout->addLayout( files );
    connect( add, &QPushButton::clicked, this, [ this ] {
        const auto paths = QFileDialog::getOpenFileNames(
            this, tr( "Select trusted Lua files" ), {}, tr( "Lua files (*.lua);;All files (*)" ) );
        for ( const auto& path : paths ) {
            if ( scripts_->findItems( path, Qt::MatchExactly ).isEmpty() )
                scripts_->addItem( path );
        }
    } );
    connect( remove, &QPushButton::clicked, this,
             [ this ] { delete scripts_->takeItem( scripts_->currentRow() ); } );
    auto* tabs = new QTabWidget( this );
    const auto routeTable = [ this, tabs ]( const QString& name, const QString& title,
                                            bool protocol ) {
        auto* page = new QWidget( tabs );
        auto* box = new QVBoxLayout( page );
        auto* table = new QTableWidget( 0, protocol ? 4 : 3, page );
        identity( table, name, title );
        QStringList headers{ protocol ? tr( "Assembly protocol" ) : tr( "Preview definition" ),
                             tr( "Named dissector" ), tr( "PDML protocol (optional)" ) };
        if ( protocol )
            headers << tr( "Encoding (utf8/hex/base64)" );
        table->setHorizontalHeaderLabels( headers );
        table->horizontalHeader()->setSectionResizeMode( QHeaderView::Stretch );
        box->addWidget( table );
        auto* row = new QHBoxLayout;
        auto* addRoute = new QPushButton( tr( "Add route" ), page );
        auto* removeRoute = new QPushButton( tr( "Remove route" ), page );
        row->addWidget( addRoute );
        row->addWidget( removeRoute );
        row->addStretch();
        box->addLayout( row );
        connect( addRoute, &QPushButton::clicked, this,
                 [ table ] { table->insertRow( table->rowCount() ); } );
        connect( removeRoute, &QPushButton::clicked, this, [ table ] {
            if ( table->currentRow() >= 0 )
                table->removeRow( table->currentRow() );
        } );
        tabs->addTab( page, title );
        return table;
    };
    previewRoutes_ = routeTable( "decoderPreviewRoutes", tr( "Preview routes" ), false );
    protocolRoutes_ = routeTable( "decoderProtocolRoutes", tr( "Assembly protocol routes" ), true );
    layout->addWidget( tabs, 1 );
    error_ = new QLabel( this );
    identity( error_, "decoderValidationError", tr( "Configuration validation" ) );
    error_->setWordWrap( true );
    error_->setTextFormat( Qt::PlainText );
    layout->addWidget( error_ );
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Apply | QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
    layout->addWidget( buttons );
    connect( buttons->button( QDialogButtonBox::Apply ), &QPushButton::clicked, this,
             [ this ] { apply(); } );
    connect( buttons, &QDialogButtonBox::accepted, this, [ this ] {
        if ( apply() )
            accept();
    } );
    connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );
    reload();
}

void PreviewDecoderSettingsDialog::reload()
{
    const auto config = PreviewDecoderSettings::instance().configuration();
    enabled_->setChecked( config.value( "enabled" ).toBool() );
    approval_->setChecked( config.value( "allowExternalLua" ).toBool() );
    tshark_->setText( config.value( "tshark" ).toString() );
    text2pcap_->setText( config.value( "text2pcap" ).toString() );
    dissector_->setText( config.value( "defaultDissector" ).toString() );
    encoding_->setCurrentText( config.value( "inputEncoding" ).toString() );
    scripts_->clear();
    for ( const auto& value : config.value( "lua" ).toList() )
        scripts_->addItem( value.toString() );
    populate( previewRoutes_, config.value( "previewRoutes" ).toMap(), false );
    populate( protocolRoutes_, config.value( "protocolRoutes" ).toMap(), true );
}
bool PreviewDecoderSettingsDialog::apply()
{
    QString error;
    const auto preview = routes( previewRoutes_, false, &error );
    const auto protocol = routes( protocolRoutes_, true, &error );
    QVariantList scripts;
    for ( int i = 0; i < scripts_->count(); ++i )
        scripts << scripts_->item( i )->text();
    const QVariantMap config{ { "enabled", enabled_->isChecked() },
                              { "allowExternalLua", approval_->isChecked() },
                              { "tshark", tshark_->text() },
                              { "text2pcap", text2pcap_->text() },
                              { "lua", scripts },
                              { "defaultDissector", dissector_->text() },
                              { "inputEncoding", encoding_->currentText() },
                              { "previewRoutes", preview },
                              { "protocolRoutes", protocol } };
    const bool ok
        = error.isEmpty() && PreviewDecoderSettings::instance().setConfiguration( config, &error );
    error_->setText( ok ? tr( "Settings applied." )
                        : tr( "Settings were not applied: %1" ).arg( error ) );
    LOG_DEBUG << "Preview decoder dialog apply success=" << ok << " reason=" << error
              << " scripts=" << scripts.size() << " routes=" << preview.size() + protocol.size();
    return ok;
}
