// SPDX-License-Identifier: GPL-3.0-or-later
#include "previewdecodersettingsdialog.h"
#include "log.h"
#include "previewdecodersettings.h"
#include "previewpackregistry.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
const QString PythonPrefix = QStringLiteral( "python:" );
// A tooltip from text (a pack's name, a typed decoder): escaped inside rich text, so markup in it
// is shown as written; empty for no tooltip.
QString plainTip( const QString& text )
{
    return text.isEmpty() ? QString{}
                          : QStringLiteral( "<span>%1</span>" ).arg( text.toHtmlEscaped() );
}
// E16b: why Python's check does not offer a pack's module as a decoder (PreviewDecoderPacks::
// pythonNotes), as the end of a sentence.
QString notOffered( const QVariantMap& note )
{
    const auto reason = note.value( "reason" ).toString();
    if ( reason == QLatin1String( "syntax_error" ) ) {
        const auto line = note.value( "line" ).toInt();
        const auto detail = note.value( "detail" ).toString();
        return line > 0 ? QObject::tr( "Python cannot compile it (line %1: %2), so a route to it "
                                       "fails to decode" )
                              .arg( line )
                              .arg( detail )
                        : QObject::tr( "Python cannot compile it (%1), so a route to it fails to "
                                       "decode" )
                              .arg( detail );
    }
    if ( reason == QLatin1String( "async_decode" ) )
        return QObject::tr( "its decode() is an async def, which the preview calls without "
                            "awaiting" );
    if ( reason == QLatin1String( "not_callable" ) )
        return QObject::tr( "its decode is a class, a module or a value, not a function" );
    if ( reason == QLatin1String( "too_large" ) )
        return QObject::tr( "it is larger than 1 MiB" );
    if ( reason == QLatin1String( "too_complex" ) )
        return QObject::tr( "it is nested too deeply for Python to compile" );
    if ( reason == QLatin1String( "unreadable" ) )
        return QObject::tr( "it cannot be read" );
    if ( reason == QLatin1String( "timeout" ) )
        return QObject::tr( "the check of the packs' modules ran out of time" );
    return QObject::tr( "Python finds no decode() function at its top level" );
}
// Why the packs' modules are not checked (PreviewDecoderPacks::pythonCheck), as a sentence.
QString checkState( const QString& state )
{
    if ( state == QLatin1String( "checking" ) )
        return QObject::tr( "CILogg is checking the enabled packs' Python modules." );
    if ( state == QLatin1String( "python_unavailable" ) )
        return QObject::tr( "CILogg found no Python interpreter, so it cannot tell which of the "
                            "packs' Python modules are decoders. Install Python 3 on PATH or set "
                            "CILOGG_SCRIPT_PYTHON, then open this window again." );
    return QObject::tr( "CILogg could not check the packs' Python modules (%1); open this window "
                        "again to retry." )
        .arg( state );
}
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
        // A pack's Python decoder is written `python:<pack-id>/<decoder-id>` (MD03b); it takes
        // no PDML protocol, so one given is kept for the settings to refuse.
        const auto target = text( 1 ).trimmed();
        QVariantMap route;
        if ( target.startsWith( PythonPrefix ) ) {
            route[ "python" ] = target.mid( PythonPrefix.size() );
            if ( !text( 2 ).isEmpty() )
                route[ "protocol" ] = text( 2 );
        }
        else
            route = { { "dissector", text( 1 ) }, { "protocol", text( 2 ) } };
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
        const auto target = route.contains( "python" )
                                ? PythonPrefix + route.value( "python" ).toString()
                                : route.value( "dissector" ).toString();
        const QStringList cells{ it.key(), target, route.value( "protocol" ).toString(),
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
    assemblyHosts_ = new QLineEdit( this );
    identity( assemblyHosts_, "decoderAssemblyHttpHosts", tr( "HTTP hosts for message assembly" ) );
    assemblyHosts_->setPlaceholderText( tr( "Empty: any Host header; comma-separated list" ) );
    form->addRow( tr( "Assembly HTTP hosts:" ), assemblyHosts_ );
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
                                            bool protocol, QPushButton** addPython ) {
        auto* page = new QWidget( tabs );
        auto* box = new QVBoxLayout( page );
        auto* table = new QTableWidget( 0, protocol ? 4 : 3, page );
        identity( table, name, title );
        QStringList headers{ protocol ? tr( "Assembly protocol" ) : tr( "Preview definition" ),
                             tr( "Named dissector or python:<pack>/<decoder>" ),
                             tr( "PDML protocol (optional)" ) };
        if ( protocol )
            headers << tr( "Encoding (utf8/hex/base64)" );
        table->setHorizontalHeaderLabels( headers );
        table->horizontalHeader()->setSectionResizeMode( QHeaderView::Stretch );
        box->addWidget( table );
        auto* row = new QHBoxLayout;
        auto* addRoute = new QPushButton( tr( "Add route" ), page );
        auto* removeRoute = new QPushButton( tr( "Remove route" ), page );
        // E16b: a route to a Python decoder of an enabled pack, chosen by the pack's name.
        auto* python = new QPushButton( tr( "Add Python decoder" ), page );
        identity( python, name + QStringLiteral( "AddPython" ),
                  tr( "Add a route to a configuration pack's Python decoder" ) );
        auto* decoders = new QMenu( python );
        decoders->setToolTipsVisible( true );
        python->setMenu( decoders );
        *addPython = python;
        row->addWidget( addRoute );
        row->addWidget( removeRoute );
        row->addWidget( python );
        row->addStretch();
        box->addLayout( row );
        connect( addRoute, &QPushButton::clicked, this,
                 [ table ] { table->insertRow( table->rowCount() ); } );
        connect( removeRoute, &QPushButton::clicked, this, [ table ] {
            if ( table->currentRow() >= 0 )
                table->removeRow( table->currentRow() );
        } );
        connect( decoders, &QMenu::triggered, this, [ this, table, protocol ]( QAction* action ) {
            // The new route waits for the preview or protocol it is for.
            const int added = table->rowCount();
            table->insertRow( added );
            for ( int column = 0; column < ( protocol ? 4 : 3 ); ++column )
                table->setItem(
                    added, column,
                    new QTableWidgetItem( column == 1 ? action->data().toString() : QString{} ) );
            describeTarget( table->item( added, 1 ) );
            table->setCurrentCell( added, 0 );
        } );
        connect( table, &QTableWidget::itemChanged, this, [ this ]( QTableWidgetItem* item ) {
            if ( item->column() == 1 )
                describeTarget( item );
        } );
        tabs->addTab( page, title );
        return table;
    };
    previewRoutes_
        = routeTable( "decoderPreviewRoutes", tr( "Preview routes" ), false, &previewAddPython_ );
    protocolRoutes_ = routeTable( "decoderProtocolRoutes", tr( "Assembly protocol routes" ), true,
                                  &protocolAddPython_ );
    // The packs can change while the dialog is open: its menus and descriptions follow them.
    connect( &PreviewDecoderSettings::instance(), &PreviewDecoderSettings::changed, this,
             [ this ] { refreshPythonDecoders(); } );
    // E16b: so do they when Python's check of the packs' modules ends.
    connect( &PreviewDecoderSettings::instance(), &PreviewDecoderSettings::pythonChecked, this,
             [ this ] { refreshPythonDecoders(); } );
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
    // A check that found no interpreter is tried again (one may have been installed since).
    PreviewPackRegistry::instance().checkPythonDecoders();
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
    assemblyHosts_->setText( config.value( "assemblyHttpHosts" ).toStringList().join( ", " ) );
    scripts_->clear();
    for ( const auto& value : config.value( "lua" ).toList() )
        scripts_->addItem( value.toString() );
    populate( previewRoutes_, config.value( "previewRoutes" ).toMap(), false );
    populate( protocolRoutes_, config.value( "protocolRoutes" ).toMap(), true );
    refreshPythonDecoders();
}
void PreviewDecoderSettingsDialog::refreshPythonDecoders()
{
    const auto packs = PreviewDecoderSettings::instance().packs();
    // Packs' names need not be unique: one shared by two packs is followed by the pack's ID.
    QHash<QString, int> names;
    for ( const auto& value : packs.python ) {
        const auto packId = value.toMap().value( "id" ).toString();
        names[ packs.pythonNames.value( packId, packId ).toString() ] += 1;
    }
    // The modules Python's check does not offer for a reason a pack's author would want to see
    // (a helper without decode() is no news).
    QStringList refused;
    for ( const auto& value : packs.python ) {
        const auto packId = value.toMap().value( "id" ).toString();
        const auto notes = packs.pythonNotes.value( packId ).toMap();
        for ( auto note = notes.cbegin(); note != notes.cend(); ++note ) {
            if ( note.value().toMap().value( "reason" ) != QLatin1String( "no_decode" ) )
                refused << tr( "%1/%2: %3" )
                               .arg( packId, note.key(), notOffered( note.value().toMap() ) );
        }
    }
    constexpr int RefusedShown = 8;
    if ( refused.size() > RefusedShown ) {
        const auto more = refused.size() - RefusedShown;
        refused = refused.mid( 0, RefusedShown );
        refused << tr( "… and %n more", nullptr, static_cast<int>( more ) );
    }
    for ( auto* button : { previewAddPython_, protocolAddPython_ } ) {
        auto* menu = button->menu();
        menu->clear();
        for ( const auto& value : packs.python ) {
            const auto pack = value.toMap();
            const auto packId = pack.value( "id" ).toString();
            const auto name = packs.pythonNames.value( packId, packId ).toString();
            // Only the modules where decode() may be a function: the others are helpers the pack
            // lists, or modules Python refuses.
            const auto entries = packs.pythonEntries.value( packId ).toStringList();
            for ( const auto& listed : pack.value( "decoders" ).toList() ) {
                const auto decoder = listed.toMap();
                const auto decoderId = decoder.value( "id" ).toString();
                if ( !entries.contains( decoderId ) )
                    continue;
                // A pack's name is text: an ampersand is shown, not a shortcut, and markup in it
                // is not rendered.
                auto shown = names.value( name ) > 1 ? tr( "%1 (%2)" ).arg( name, packId ) : name;
                shown.replace( QLatin1Char( '&' ), QStringLiteral( "&&" ) );
                const auto target = PythonPrefix + packId + QLatin1Char( '/' ) + decoderId;
                auto* action = menu->addAction( tr( "%1: %2" ).arg( shown, decoderId ) );
                action->setData( target );
                action->setToolTip(
                    plainTip( tr( "%1: %2 in the pack \"%3\"" )
                                  .arg( target, decoder.value( "module" ).toString(), name ) ) );
            }
        }
        const bool any = !menu->isEmpty();
        button->setEnabled( any );
        QStringList tip{ any ? tr( "Route a preview or protocol to a Python decoder of an enabled "
                                   "configuration pack." )
                         : packs.python.isEmpty()
                             ? tr( "No enabled configuration pack has Python decoders." )
                             : tr( "No module of the enabled configuration packs is a Python "
                                   "decoder." ) };
        if ( packs.pythonCheck != QLatin1String( "done" ) )
            tip << checkState( packs.pythonCheck );
        if ( !refused.isEmpty() )
            tip << tr( "Not offered:" ) << refused;
        button->setToolTip( plainTip( tip.join( QLatin1Char( '\n' ) ) )
                                .replace( QLatin1String( "\n" ), QLatin1String( "<br>" ) ) );
    }
    for ( auto* table : { previewRoutes_, protocolRoutes_ } ) {
        for ( int row = 0; row < table->rowCount(); ++row )
            describeTarget( table->item( row, 1 ) );
    }
}
void PreviewDecoderSettingsDialog::describeTarget( QTableWidgetItem* item ) const
{
    if ( !item )
        return;
    const auto target = item->text().trimmed();
    QString tip;
    if ( target.startsWith( PythonPrefix ) ) {
        const auto decoder = target.mid( PythonPrefix.size() );
        const auto& settings = PreviewDecoderSettings::instance();
        const auto pack = settings.pythonDecoderPack( decoder );
        const auto slash = decoder.indexOf( QLatin1Char( '/' ) );
        const auto decoderId = decoder.mid( slash + 1 );
        const auto packs = settings.packs();
        const auto packId = decoder.left( slash );
        const auto note = packs.pythonNotes.value( packId ).toMap().value( decoderId ).toMap();
        if ( pack.isEmpty() )
            tip = tr( "Python decoder %1: its pack is not enabled (or has no such decoder), so "
                      "the route does not apply." )
                      .arg( decoder );
        else if ( packs.pythonEntries.value( packId ).toStringList().contains( decoderId ) )
            tip = tr( "Python decoder \"%1\" of the pack \"%2\"." ).arg( decoderId, pack );
        else if ( !note.isEmpty() || packs.pythonCheck == QLatin1String( "done" ) )
            // Python's parser read the module; a decode() bound by running code (a star import,
            // exec) is beyond what it reads.
            tip = tr( "Python module \"%1\" of the pack \"%2\" is not offered as a decoder: %3." )
                      .arg( decoderId, pack, notOffered( note ) );
        else
            tip = tr( "Python module \"%1\" of the pack \"%2\": %3" )
                      .arg( decoderId, pack, checkState( packs.pythonCheck ) );
    }
    // The pack's name and the typed decoder are text, never markup.
    tip = plainTip( tip );
    // Setting a tooltip changes the item, which calls this again: only a new text is set.
    if ( item->toolTip() != tip )
        item->setToolTip( tip );
}
bool PreviewDecoderSettingsDialog::apply()
{
    QString error;
    const auto preview = routes( previewRoutes_, false, &error );
    const auto protocol = routes( protocolRoutes_, true, &error );
    QVariantList scripts;
    for ( int i = 0; i < scripts_->count(); ++i )
        scripts << scripts_->item( i )->text();
    QVariantList hosts;
    for ( const auto& host : assemblyHosts_->text().split( ',', Qt::SkipEmptyParts ) ) {
        if ( !host.trimmed().isEmpty() )
            hosts << host.trimmed();
    }
    const QVariantMap config{ { "enabled", enabled_->isChecked() },
                              { "allowExternalLua", approval_->isChecked() },
                              { "tshark", tshark_->text() },
                              { "text2pcap", text2pcap_->text() },
                              { "lua", scripts },
                              { "defaultDissector", dissector_->text() },
                              { "inputEncoding", encoding_->currentText() },
                              { "previewRoutes", preview },
                              { "protocolRoutes", protocol },
                              { "assemblyHttpHosts", hosts } };
    const bool ok
        = error.isEmpty() && PreviewDecoderSettings::instance().setConfiguration( config, &error );
    error_->setText( ok ? tr( "Settings applied." )
                        : tr( "Settings were not applied: %1" ).arg( error ) );
    LOG_DEBUG << "Preview decoder dialog apply success=" << ok << " reason=" << error
              << " scripts=" << scripts.size() << " routes=" << preview.size() + protocol.size();
    return ok;
}
