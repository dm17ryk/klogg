// SPDX-License-Identifier: GPL-3.0-or-later
#include "previewdecodersettingsdialog.h"
#include "log.h"
#include "previewdecodersettings.h"
#include "previewmanager.h"
#include "previewpackregistry.h"
#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <memory>
#include <utility>

namespace {
const QString PythonPrefix = QStringLiteral( "python:" );
const QStringList Encodings{ QStringLiteral( "utf8" ), QStringLiteral( "hex" ),
                             QStringLiteral( "base64" ) };
constexpr int EncodingColumn = 3;
// `tshark -G protocols` takes about a second; a TShark that hangs is not waited for longer.
constexpr int TsharkListTimeoutMs = 5000;
constexpr qint64 TsharkListOutputLimit = 8 * 1024 * 1024;
// The most warnings listed under the routes; the rest are counted.
constexpr int ShownWarningLimit = 10;
// The dissector a route cell held before an edit, so that its PDML protocol can follow it.
constexpr int PreviousDissectorRole = Qt::UserRole + 1;

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
// Typed text completes against any part of a known value, in any case.
void completeAnywhere( QComboBox* combo )
{
    combo->setInsertPolicy( QComboBox::NoInsert );
    combo->setMaxVisibleItems( 20 );
    auto* completer = combo->completer();
    completer->setCaseSensitivity( Qt::CaseInsensitive );
    completer->setFilterMode( Qt::MatchContains );
    completer->setCompletionMode( QCompleter::PopupCompletion );
}

// A route cell's editor: a drop-down of the cell's known values that still takes typed text, or a
// fixed one for the encoding.
class RouteChoiceDelegate : public QStyledItemDelegate {
public:
    using Values = std::function<QStringList( const QModelIndex& )>;
    RouteChoiceDelegate( Values values, QObject* parent )
        : QStyledItemDelegate( parent )
        , values_( std::move( values ) )
    {
    }
    QWidget* createEditor( QWidget* parent, const QStyleOptionViewItem&,
                           const QModelIndex& index ) const override
    {
        auto* combo = new QComboBox( parent );
        combo->setObjectName( "decoderRouteEditor" );
        combo->setEditable( index.column() != EncodingColumn );
        combo->addItems( values_( index ) );
        if ( combo->isEditable() )
            completeAnywhere( combo );
        // A value picked from the list is written at once, so the row's PDML protocol follows it.
        auto* self = const_cast<RouteChoiceDelegate*>( this );
        connect( combo, &QComboBox::activated, self,
                 [ self, combo ] { Q_EMIT self->commitData( combo ); } );
        return combo;
    }
    void setEditorData( QWidget* editor, const QModelIndex& index ) const override
    {
        auto* combo = static_cast<QComboBox*>( editor );
        const auto text = index.data( Qt::EditRole ).toString();
        if ( combo->isEditable() )
            combo->setEditText( text );
        else
            combo->setCurrentIndex( combo->findText( text ) );
    }
    void setModelData( QWidget* editor, QAbstractItemModel* model,
                       const QModelIndex& index ) const override
    {
        // A fixed drop-down that shows none of its values (an empty encoding: the default) was
        // only visited: the cell keeps its value.
        auto* combo = static_cast<QComboBox*>( editor );
        if ( !combo->isEditable() && combo->currentIndex() < 0 )
            return;
        model->setData( index, combo->currentText(), Qt::EditRole );
    }

private:
    Values values_;
};

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
            route = { { "dissector", target }, { "protocol", text( 2 ) } };
        if ( protocol && !text( EncodingColumn ).isEmpty() )
            route[ "inputEncoding" ] = text( EncodingColumn );
        result[ name ] = route;
    }
    return result;
}
void populate( QTableWidget* table, const QVariantMap& values, bool protocol )
{
    const QSignalBlocker blocker( table );
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
        for ( int column = 0; column < ( protocol ? 4 : 3 ); ++column ) {
            auto* item = new QTableWidgetItem( cells[ column ] );
            if ( column == 1 )
                item->setData( PreviousDissectorRole, target );
            table->setItem( row, column, item );
        }
    }
}

QHash<QString, QStringList>& tsharkProtocolCache()
{
    static QHash<QString, QStringList> cache;
    return cache;
}
// The saved TShark as the protocol list's key (path, size and time), or empty for none.
QString tsharkKey( const QString& path )
{
    const QFileInfo info( path );
    if ( path.isEmpty() || !info.isAbsolute() || !info.isFile() || !info.isExecutable() )
        return {};
    return QStringLiteral( "%1|%2|%3" )
        .arg( info.absoluteFilePath() )
        .arg( info.size() )
        .arg( info.lastModified().toMSecsSinceEpoch() );
}
// Like the preview worker's TShark, this one reads no personal or global plugins or settings, so it
// lists the protocols a preview session knows before its Lua is added.
QProcessEnvironment isolatedEnvironment( const QString& root )
{
    auto environment = QProcessEnvironment::systemEnvironment();
    for ( const auto& name : environment.keys() ) {
        if ( name.startsWith( "WIRESHARK_", Qt::CaseInsensitive )
             || name.startsWith( "LUA_", Qt::CaseInsensitive ) )
            environment.remove( name );
    }
    static const std::pair<const char*, const char*> folders[]
        = { { "WIRESHARK_CONFIG_DIR", "config" }, { "WIRESHARK_PLUGIN_DIR", "plugins" },
            { "APPDATA", "appdata" },             { "HOME", "home" },
            { "USERPROFILE", "home" },            { "XDG_CONFIG_HOME", "xdg-config" },
            { "XDG_DATA_HOME", "xdg-data" },      { "XDG_CACHE_HOME", "xdg-cache" } };
    const QDir folder( root );
    for ( const auto& [ name, relative ] : folders ) {
        folder.mkpath( relative );
        environment.insert( name, QDir::toNativeSeparators( folder.filePath( relative ) ) );
    }
    return environment;
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
    dissector_ = new QComboBox( this );
    dissector_->setEditable( true );
    completeAnywhere( dissector_ );
    identity( dissector_, "decoderDefaultDissector", tr( "Default dissector" ) );
    dissector_->lineEdit()->setPlaceholderText( tr( "Empty: use mapped routes only" ) );
    dissector_->setToolTip( tr( "A dissector a selected Lua file registers with "
                                "Proto(\"<name>\", ...), or a TShark protocol." ) );
    form->addRow( tr( "Default named dissector:" ), dissector_ );
    encoding_ = new QComboBox( this );
    identity( encoding_, "decoderInputEncoding", tr( "Default input encoding" ) );
    encoding_->addItems( Encodings );
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
    identity( add, "decoderAddLua", add->text() );
    auto* remove = new QPushButton( tr( "Remove selected" ), this );
    identity( remove, "decoderRemoveLua", remove->text() );
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
        refreshChoices();
    } );
    connect( remove, &QPushButton::clicked, this, [ this ] {
        delete scripts_->takeItem( scripts_->currentRow() );
        refreshChoices();
    } );
    auto* tabs = new QTabWidget( this );
    previewRoutes_ = routeTable( tabs, "decoderPreviewRoutes", tr( "Preview routes" ), false,
                                 &previewAddPython_ );
    protocolRoutes_ = routeTable( tabs, "decoderProtocolRoutes", tr( "Assembly protocol routes" ),
                                  true, &protocolAddPython_ );
    // The settings and the packs can change while the dialog is open: its menus, descriptions,
    // drop-downs and warnings follow them, also when Python's check of the packs' modules ends
    // (E16b). A change of the settings (another TShark saved elsewhere, or the watched TShark
    // replaced) also reads the TShark list again; an unchanged TShark keeps its list.
    connect( &PreviewDecoderSettings::instance(), &PreviewDecoderSettings::changed, this,
             [ this ] {
                 refreshPythonDecoders();
                 if ( isVisible() )
                     loadTsharkProtocols();
                 // A watched Lua file or a pack may have changed within the file system's time
                 // granularity: their names are read again.
                 previewroutes::forgetLuaNames();
                 refreshChoices();
                 showWarnings( savedWarnings() );
             } );
    connect( &PreviewDecoderSettings::instance(), &PreviewDecoderSettings::pythonChecked, this,
             [ this ] {
                 refreshPythonDecoders();
                 refreshChoices();
                 showWarnings( savedWarnings() );
             } );
    // A preview imported, removed or renamed while the dialog is open is offered at once.
    connect( &PreviewManager::instance(), &PreviewManager::previewsChanged, this,
             [ this ] { refreshChoices(); } );
    layout->addWidget( tabs, 1 );
    choicesStatus_ = new QLabel( this );
    identity( choicesStatus_, "decoderChoicesStatus", tr( "Offered dissectors" ) );
    choicesStatus_->setWordWrap( true );
    choicesStatus_->setTextFormat( Qt::PlainText );
    layout->addWidget( choicesStatus_ );
    auto* warningRow = new QHBoxLayout;
    warningsLabel_ = new QLabel( this );
    identity( warningsLabel_, "decoderRouteWarnings", tr( "Route warnings" ) );
    warningsLabel_->setWordWrap( true );
    warningsLabel_->setTextFormat( Qt::PlainText );
    warningsLabel_->setTextInteractionFlags( Qt::TextSelectableByMouse );
    useDeclared_ = new QPushButton( tr( "Use the declared names" ), this );
    identity( useDeclared_, "decoderUseDeclaredNames", useDeclared_->text() );
    useDeclared_->setToolTip(
        tr( "Replace each name the warnings suggest a dissector for, and apply again." ) );
    warningRow->addWidget( warningsLabel_, 1 );
    warningRow->addWidget( useDeclared_, 0, Qt::AlignTop );
    layout->addLayout( warningRow );
    connect( useDeclared_, &QPushButton::clicked, this, [ this ] { useDeclaredNames(); } );
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
        // Warnings do not block the save. OK keeps the dialog open once to show new ones.
        const auto shown = warningsLabel_->text();
        if ( !apply() )
            return;
        if ( warnings_.isEmpty() || warningsLabel_->text() == shown )
            accept();
        else
            error_->setText( heldOpenNote() );
    } );
    connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );
    reload();
    // A check that found no interpreter is tried again (one may have been installed since).
    PreviewPackRegistry::instance().checkPythonDecoders();
}

PreviewDecoderSettingsDialog::~PreviewDecoderSettingsDialog()
{
    // A TShark still listing its protocols ends before the dialog's members do: its finished
    // signal must not reach a dialog that is half destroyed.
    if ( tsharkProcess_ ) {
        tsharkProcess_->disconnect( this );
        delete tsharkProcess_.data();
    }
}

QTableWidget* PreviewDecoderSettingsDialog::routeTable( QTabWidget* tabs, const QString& name,
                                                        const QString& title, bool protocol,
                                                        QPushButton** addPython )
{
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
    table->horizontalHeaderItem( 0 )->setToolTip(
        protocol ? tr( "A protocol message assembly gives messages." )
                 : tr( "A preview definition: one of yours or of an enabled pack." ) );
    table->horizontalHeaderItem( 1 )->setToolTip(
        tr( "The name a selected Lua file registers with Proto(\"<name>\", ...), not the Lua "
            "file's name; a TShark protocol; or an enabled pack's Python decoder." ) );
    table->horizontalHeaderItem( 2 )->setToolTip(
        tr( "The PDML protocol to show; empty shows every decoded protocol. It follows the "
            "dissector until you choose another." ) );
    table->horizontalHeader()->setSectionResizeMode( QHeaderView::Stretch );
    table->setItemDelegate( new RouteChoiceDelegate(
        [ this, table ]( const QModelIndex& index ) {
            return cellChoices( table, index.row(), index.column() );
        },
        table ) );
    table->setEditTriggers( QAbstractItemView::CurrentChanged | QAbstractItemView::DoubleClicked
                            | QAbstractItemView::SelectedClicked
                            | QAbstractItemView::EditKeyPressed
                            | QAbstractItemView::AnyKeyPressed );
    connect( table, &QTableWidget::itemChanged, this, [ this, table ]( QTableWidgetItem* item ) {
        followDissector( table, item );
        if ( item->column() == 1 )
            describeTarget( item );
    } );
    box->addWidget( table );
    auto* row = new QHBoxLayout;
    auto* addRoute = new QPushButton( tr( "Add route" ), page );
    identity( addRoute, protocol ? "decoderAddProtocolRoute" : "decoderAddPreviewRoute",
              addRoute->text() );
    auto* removeRoute = new QPushButton( tr( "Remove route" ), page );
    identity( removeRoute, protocol ? "decoderRemoveProtocolRoute" : "decoderRemovePreviewRoute",
              removeRoute->text() );
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
    connect( addRoute, &QPushButton::clicked, this, [ this, table, protocol ] {
        const int added = addRouteRow( table, protocol );
        // The new route opens on its drop-down (a visible table already did on the move).
        table->setCurrentCell( added, 0 );
        if ( !table->indexWidget( table->model()->index( added, 0 ) ) )
            table->editItem( table->item( added, 0 ) );
    } );
    connect( removeRoute, &QPushButton::clicked, this, [ table ] {
        if ( table->currentRow() >= 0 )
            table->removeRow( table->currentRow() );
    } );
    connect( decoders, &QMenu::triggered, this, [ this, table, protocol ]( QAction* action ) {
        // The new route waits for the preview or protocol it is for.
        const int added = addRouteRow( table, protocol, action->data().toString() );
        table->setCurrentCell( added, 0 );
    } );
    tabs->addTab( page, title );
    return table;
}

int PreviewDecoderSettingsDialog::addRouteRow( QTableWidget* table, bool protocol,
                                               const QString& target )
{
    const int added = table->rowCount();
    {
        const QSignalBlocker blocker( table );
        table->insertRow( added );
        for ( int column = 0; column < table->columnCount(); ++column ) {
            auto* item = new QTableWidgetItem(
                column == 1 ? target
                            : ( ( protocol && column == EncodingColumn ) ? encoding_->currentText()
                                                                         : QString() ) );
            if ( column == 1 )
                item->setData( PreviousDissectorRole, target );
            table->setItem( added, column, item );
        }
    }
    describeTarget( table->item( added, 1 ) );
    return added;
}

QStringList PreviewDecoderSettingsDialog::cellChoices( const QTableWidget* table, int row,
                                                       int column ) const
{
    switch ( column ) {
    case 0:
        return table == protocolRoutes_ ? choices_.assemblyProtocols : choices_.previews;
    case 1:
        return choices_.routeTargets();
    case 2: {
        // The row's TShark dissector first: for a Lua dissector it is also the PDML protocol.
        auto names = choices_.tsharkDissectors();
        const auto* target = table->item( row, 1 );
        const auto dissector = target ? target->text().trimmed() : QString();
        if ( !dissector.isEmpty() && !dissector.startsWith( PythonPrefix ) ) {
            names.removeOne( dissector );
            names.prepend( dissector );
        }
        return names;
    }
    default:
        return Encodings;
    }
}

void PreviewDecoderSettingsDialog::followDissector( QTableWidget* table, QTableWidgetItem* item )
{
    if ( item->column() != 1 )
        return;
    const auto previous = item->data( PreviousDissectorRole ).toString();
    const auto current = item->text().trimmed();
    if ( previous == current )
        return;
    const QSignalBlocker blocker( table );
    item->setData( PreviousDissectorRole, current );
    auto* pdml = table->item( item->row(), 2 );
    if ( !pdml ) {
        pdml = new QTableWidgetItem;
        table->setItem( item->row(), 2, pdml );
    }
    // A Python decoder takes no PDML protocol, not even one the user chose: the settings would
    // refuse the route.
    if ( current.startsWith( PythonPrefix ) ) {
        pdml->setText( {} );
        return;
    }
    const auto selected = pdml->text().trimmed();
    if ( !selected.isEmpty() && selected != previous )
        return;
    // A dissector a Lua file declares is also the PDML protocol it adds to the tree (Wireshark
    // registers both under its lower-case name); for any other, every decoded protocol shows.
    pdml->setText( choices_.luaNames().contains( current ) ? current : QString() );
}

void PreviewDecoderSettingsDialog::refreshChoices()
{
    QStringList lua;
    for ( int i = 0; i < scripts_->count(); ++i )
        lua << scripts_->item( i )->text();
    auto choices = previewroutes::collect( lua );
    choices.tshark = choices_.tshark;
    choices.tsharkLoaded = choices_.tsharkLoaded;
    choices_ = std::move( choices );
    showChoicesStatus();
    // The default dissector is TShark's: no Python decoder. Refilling it keeps what is being
    // typed there, with its cursor and selection.
    const auto names = choices_.tsharkDissectors();
    QStringList current;
    for ( int i = 0; i < dissector_->count(); ++i )
        current << dissector_->itemText( i );
    if ( current == names )
        return;
    auto* edit = dissector_->lineEdit();
    const auto text = edit->text();
    const auto cursor = edit->cursorPosition();
    const auto selectionStart = edit->selectionStart();
    const auto selectionLength = edit->selectedText().size();
    const QSignalBlocker blocker( dissector_ );
    dissector_->clear();
    dissector_->addItems( names );
    dissector_->setEditText( text );
    if ( selectionStart >= 0 )
        edit->setSelection( selectionStart, static_cast<int>( selectionLength ) );
    else
        edit->setCursorPosition( cursor );
}

QVector<previewroutes::Warning> PreviewDecoderSettingsDialog::savedWarnings() const
{
    // The saved routes are checked against the saved Lua files, whatever the list shows now
    // (a file added or removed but not applied yet), with the TShark list as far as it is.
    const auto config = PreviewDecoderSettings::instance().configuration();
    auto choices = previewroutes::collect( config.value( "lua" ).toStringList() );
    choices.tshark = choices_.tshark;
    choices.tsharkLoaded = choices_.tsharkLoaded;
    choices.tsharkPending = !tsharkProcess_.isNull();
    choices.tsharkDisabled = !config.value( "enabled" ).toBool();
    // Why the list could not be read (timed out, ...) is part of the warnings that need it.
    choices.tsharkFailure = tsharkFailure_;
    return previewroutes::check( config, choices );
}

void PreviewDecoderSettingsDialog::showChoicesStatus()
{
    const auto pythonCount = choices_.python.size();
    const auto python = pythonCount == 1 ? tr( "1 Python decoder" )
                                         : tr( "%1 Python decoders" ).arg( pythonCount );
    QString tshark;
    if ( choices_.tsharkLoaded )
        tshark = tr( "%1 TShark protocols" ).arg( choices_.tshark.size() );
    else if ( tsharkProcess_ )
        tshark = tr( "TShark protocols loading…" );
    else if ( !tsharkFailure_.isEmpty() )
        tshark = tr( "no TShark protocols (%1)" ).arg( tsharkFailure_ );
    else
        tshark = tr( "no TShark protocols (enable TShark and apply its executable to list "
                     "them)" );
    choicesStatus_->setText( tr( "Offered dissectors: %1 from Lua files, %2, %3." )
                                 .arg( QString::number( choices_.luaNames().size() ), python,
                                       tshark ) );
}

void PreviewDecoderSettingsDialog::showEvent( QShowEvent* event )
{
    QDialog::showEvent( event );
    // A list that failed (a cold start past the timeout) is read again on the next show.
    loadTsharkProtocols( true );
    // While it loads, the names only its list could settle wait for it.
    showWarnings( savedWarnings() );
}

QString PreviewDecoderSettingsDialog::heldOpenNote() const
{
    return tr( "Settings applied, with the warnings below. OK again closes the dialog." );
}

void PreviewDecoderSettingsDialog::loadTsharkProtocols( bool retry )
{
    // Only the saved TShark of an enabled decoder runs, as for a preview: applying the settings
    // approves their executables.
    const auto config = PreviewDecoderSettings::instance().configuration();
    const auto key = config.value( "enabled" ).toBool()
                         ? tsharkKey( config.value( "tshark" ).toString() )
                         : QString{};
    if ( key == tsharkKey_
         && ( choices_.tsharkLoaded || tsharkProcess_
              || ( !tsharkFailure_.isEmpty() && !retry ) ) )
        return;
    if ( tsharkProcess_ ) {
        tsharkProcess_->disconnect( this );
        tsharkProcess_->kill();
        tsharkProcess_->deleteLater();
        tsharkProcess_ = nullptr;
    }
    tsharkKey_ = key;
    tsharkFailure_.clear();
    choices_.tshark.clear();
    choices_.tsharkLoaded = false;
    const auto cached = tsharkProtocolCache().constFind( key );
    if ( key.isEmpty() || cached != tsharkProtocolCache().cend() ) {
        choices_.tsharkLoaded = !key.isEmpty();
        if ( choices_.tsharkLoaded )
            choices_.tshark = *cached;
        refreshChoices();
        return;
    }
    auto folder = std::make_shared<QTemporaryDir>();
    if ( !folder->isValid() ) {
        tsharkFailure_ = tr( "no temporary folder" );
        showChoicesStatus();
        return;
    }
    auto* process = new QProcess( this );
    tsharkProcess_ = process;
    // The folder stays until the process is gone.
    connect( process, &QObject::destroyed, [ folder ] { Q_UNUSED( folder ) } );
    process->setProcessEnvironment( isolatedEnvironment( folder->path() ) );
    process->setWorkingDirectory( folder->path() );
    process->setStandardInputFile( QProcess::nullDevice() );
    process->setStandardErrorFile( QProcess::nullDevice() );
    connect( process, &QProcess::readyReadStandardOutput, this, [ process ] {
        if ( process->bytesAvailable() > TsharkListOutputLimit ) {
            process->setProperty( "failure", tr( "too much output" ) );
            process->kill();
        }
    } );
    connect( process, &QProcess::finished, this,
             [ this, process, key ]( int code, QProcess::ExitStatus status ) {
                 auto failure = process->property( "failure" ).toString();
                 if ( failure.isEmpty() && ( status != QProcess::NormalExit || code != 0 ) )
                     failure = tr( "exit code %1" ).arg( code );
                 tsharkProtocolsLoaded( process, key, failure );
             } );
    connect( process, &QProcess::errorOccurred, this,
             [ this, process, key ]( QProcess::ProcessError error ) {
                 if ( error == QProcess::FailedToStart )
                     tsharkProtocolsLoaded( process, key, tr( "TShark did not start" ) );
             } );
    QTimer::singleShot( TsharkListTimeoutMs, process, [ process ] {
        process->setProperty( "failure", tr( "timed out" ) );
        process->kill();
    } );
    LOG_INFO << "Preview decoder settings read the TShark protocol list";
    process->start( config.value( "tshark" ).toString(), { "-G", "protocols" },
                    QIODevice::ReadOnly );
    showChoicesStatus();
}

void PreviewDecoderSettingsDialog::tsharkProtocolsLoaded( QProcess* process, const QString& key,
                                                          const QString& failure )
{
    process->deleteLater();
    if ( process != tsharkProcess_ )
        return;
    tsharkProcess_ = nullptr;
    const auto names = failure.isEmpty()
                           ? previewroutes::tsharkProtocolNames( process->readAllStandardOutput() )
                           : QStringList{};
    tsharkFailure_ = failure.isEmpty() && names.isEmpty() ? tr( "none listed" ) : failure;
    if ( tsharkFailure_.isEmpty() ) {
        tsharkProtocolCache().insert( key, names );
        choices_.tshark = names;
        choices_.tsharkLoaded = true;
    }
    LOG_INFO << "Preview decoder settings TShark protocols=" << names.size()
             << " failure=" << tsharkFailure_;
    refreshChoices();
    // The saved routes are checked again with the list (also in a closed dialog: it shows them
    // when it opens); when none remain, OK's note goes too.
    showWarnings( savedWarnings() );
    if ( warnings_.isEmpty() && error_->text() == heldOpenNote() )
        error_->setText( tr( "Settings applied." ) );
}

void PreviewDecoderSettingsDialog::reload()
{
    const auto config = PreviewDecoderSettings::instance().configuration();
    enabled_->setChecked( config.value( "enabled" ).toBool() );
    approval_->setChecked( config.value( "allowExternalLua" ).toBool() );
    tshark_->setText( config.value( "tshark" ).toString() );
    text2pcap_->setText( config.value( "text2pcap" ).toString() );
    encoding_->setCurrentText( config.value( "inputEncoding" ).toString() );
    assemblyHosts_->setText( config.value( "assemblyHttpHosts" ).toStringList().join( ", " ) );
    scripts_->clear();
    for ( const auto& value : config.value( "lua" ).toList() )
        scripts_->addItem( value.toString() );
    // Opening reads the Lua files again: one may have changed within the file system's time
    // granularity since they were last read.
    previewroutes::forgetLuaNames();
    refreshChoices();
    dissector_->setEditText( config.value( "defaultDissector" ).toString() );
    populate( previewRoutes_, config.value( "previewRoutes" ).toMap(), false );
    populate( protocolRoutes_, config.value( "protocolRoutes" ).toMap(), true );
    refreshPythonDecoders();
    // Settings reloaded into an open dialog may name another TShark.
    if ( isVisible() )
        loadTsharkProtocols();
    // The saved routes are checked when the dialog opens, so a warning that came after it was
    // closed (a TShark list that arrived late) is shown again.
    showWarnings( savedWarnings() );
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
    // An open route editor holds a value its cell does not have yet.
    for ( auto* table : { previewRoutes_, protocolRoutes_ } ) {
        const auto index = table->currentIndex();
        if ( auto* editor = table->indexWidget( index ) )
            table->itemDelegate()->setModelData( editor, table->model(), index );
    }
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
                              { "defaultDissector", dissector_->currentText() },
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
    if ( !ok )
        return false;
    // The TShark just applied lists its protocols (an unchanged one keeps its list), so no
    // warning is checked against the previous TShark's.
    if ( isVisible() )
        loadTsharkProtocols();
    refreshChoices();
    showWarnings( savedWarnings() );
    return true;
}

void PreviewDecoderSettingsDialog::showWarnings( const QVector<previewroutes::Warning>& warnings )
{
    warnings_ = warnings;
    QStringList lines;
    bool suggestions = false;
    for ( const auto& warning : warnings ) {
        if ( lines.size() < ShownWarningLimit )
            lines << tr( "%1: %2" ).arg( warning.field, warning.text );
        suggestions = suggestions || !warning.suggestion.isEmpty();
    }
    if ( warnings.size() > ShownWarningLimit )
        lines << tr( "…and %1 more." ).arg( warnings.size() - ShownWarningLimit );
    warningsLabel_->setText( lines.join( '\n' ) );
    warningsLabel_->setVisible( !lines.isEmpty() );
    useDeclared_->setVisible( suggestions );
    if ( !warnings.isEmpty() ) {
        LOG_INFO << "Preview decoder settings route warnings=" << warnings.size()
                 << " suggestions=" << suggestions;
    }
}

void PreviewDecoderSettingsDialog::useDeclaredNames()
{
    QHash<QString, QString> replacements;
    for ( const auto& warning : std::as_const( warnings_ ) ) {
        if ( !warning.suggestion.isEmpty() )
            replacements.insert( warning.name, warning.suggestion );
    }
    // Each replaced dissector takes its PDML protocol along (followDissector). Apply saves a
    // route's dissector without the spaces around it, so the warnings name it so.
    for ( auto* table : { previewRoutes_, protocolRoutes_ } ) {
        for ( int row = 0; row < table->rowCount(); ++row ) {
            auto* item = table->item( row, 1 );
            const auto name = item ? item->text().trimmed() : QString();
            if ( replacements.contains( name ) )
                item->setText( replacements.value( name ) );
        }
    }
    if ( replacements.contains( dissector_->currentText() ) )
        dissector_->setEditText( replacements.value( dissector_->currentText() ) );
    LOG_INFO << "Preview decoder settings use declared dissector names replacements="
             << replacements.size();
    apply();
}
