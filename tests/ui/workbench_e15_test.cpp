// E15a: the Plugins and Packs window, its ordered Repositories table, the merged Available list
// and the in-app guide (docs/plugin-repositories.md sections 3.3 and 3.9).
#include "../helpers/fake_git_environment.h"
#include "../helpers/https_catalog_server.h"
#include "mainwindow.h"
#include "pluginmanager.h"
#include "session.h"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSysInfo>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <catch2/catch.hpp>
#include <functional>
#include <kzip.h>

namespace {
using cilogg::plugins::PluginManager;

// Runs `step` on the next modal dialog that `isTarget` accepts, while the code under test waits
// in exec().
void onModal( const std::function<bool( QWidget* )>& isTarget,
              const std::function<void( QWidget* )>& step )
{
    auto* timer = new QTimer;
    timer->setInterval( 20 );
    auto remaining = std::make_shared<int>( 250 );
    QObject::connect( timer, &QTimer::timeout, [ timer, isTarget, step, remaining ] {
        auto* modal = QApplication::activeModalWidget();
        if ( modal && isTarget( modal ) ) {
            timer->stop();
            timer->deleteLater();
            step( modal );
        }
        else if ( --*remaining <= 0 ) {
            timer->stop();
            timer->deleteLater();
        }
    } );
    timer->start();
}

void answerYes()
{
    onModal( []( QWidget* widget ) { return qobject_cast<QMessageBox*>( widget ) != nullptr; },
             []( QWidget* widget ) {
                 auto* box = qobject_cast<QMessageBox*>( widget );
                 box->button( QMessageBox::Yes )->click();
             } );
}

void fillRepositoryDialog( const QString& name, const QString& address,
                           std::shared_ptr<bool> addressReadOnly = {} )
{
    onModal( []( QWidget* widget ) { return widget->objectName() == "pluginRepositoryDialog"; },
             [ name, address, addressReadOnly ]( QWidget* dialog ) {
                 auto* nameEdit = dialog->findChild<QLineEdit*>( "pluginRepositoryName" );
                 auto* addressEdit = dialog->findChild<QLineEdit*>( "pluginRepositoryAddress" );
                 if ( addressReadOnly ) {
                     *addressReadOnly = addressEdit->isReadOnly();
                 }
                 nameEdit->setText( name );
                 if ( !addressEdit->isReadOnly() ) {
                     addressEdit->setText( address );
                 }
                 dialog->findChild<QPushButton*>( "pluginRepositoryAccept" )->click();
             } );
}

QStringList column( QTableWidget* table, int index )
{
    QStringList texts;
    for ( int row = 0; row < table->rowCount(); ++row ) {
        texts.append( table->item( row, index ) ? table->item( row, index )->text() : QString{} );
    }
    return texts;
}

int rowOf( QTableWidget* table, int index, const QString& text )
{
    return int( column( table, index ).indexOf( text ) );
}

QByteArray fixtureZip( const QString& root )
{
    const auto folder = QDir( root ).filePath( "package" );
    REQUIRE( QDir().mkpath( folder ) );
    const auto executable = QFileInfo( QString::fromUtf8( PLUGIN_FIXTURE_PATH ) ).fileName();
    QFile manifest( QDir( folder ).filePath( "plugin.json" ) );
    REQUIRE( manifest.open( QIODevice::WriteOnly ) );
    manifest.write( QJsonDocument( QJsonObject{ { "id", "org.test.e15" },
                                                { "name", "E15 fixture" },
                                                { "version", "1.0.0" },
                                                { "protocol_version", 1 },
                                                { "executable", executable },
                                                { "capabilities", QJsonArray{ "tool" } } } )
                        .toJson() );
    manifest.close();
    REQUIRE( QFile::copy( QString::fromUtf8( PLUGIN_FIXTURE_PATH ),
                          QDir( folder ).filePath( executable ) ) );
    const auto path = QDir( root ).filePath( "package.zip" );
    KZip zip( path );
    REQUIRE( zip.open( QIODevice::WriteOnly ) );
    REQUIRE( zip.addLocalDirectory( folder, "" ) );
    REQUIRE( zip.close() );
    QFile archive( path );
    REQUIRE( archive.open( QIODevice::ReadOnly ) );
    return archive.readAll();
}

QTextBrowser* documentationWindow()
{
    for ( auto* widget : QApplication::topLevelWidgets() ) {
        if ( auto* browser = qobject_cast<QTextBrowser*>( widget );
             browser && browser->objectName() == "documentationBrowser" && browser->isVisible() ) {
            return browser;
        }
    }
    return nullptr;
}

bool documentHasAnchor( QTextDocument* document, const QString& anchor )
{
    for ( auto block = document->begin(); block.isValid(); block = block.next() ) {
        for ( auto it = block.begin(); !it.atEnd(); ++it ) {
            if ( it.fragment().charFormat().anchorNames().contains( anchor ) ) {
                return true;
            }
        }
    }
    return false;
}

void closeDocumentation()
{
    while ( auto* browser = documentationWindow() ) {
        browser->close();
        QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
    }
}

// How to... and Help > Plugin Repositories Guide open the documentation at the guide.
void requireGuide( const std::function<void()>& open )
{
    closeDocumentation();
    open();
    REQUIRE( QTest::qWaitFor( [] { return documentationWindow() != nullptr; }, 5000 ) );
    auto* browser = documentationWindow();
    REQUIRE( browser->property( "documentationAnchor" ).toString()
             == QLatin1String( MainWindow::PluginRepositoriesGuideAnchor ) );
    if ( browser->toPlainText().contains( "Documentation generation disabled" ) ) {
        WARN( "This build has no generated documentation; the anchor is not checked" );
        closeDocumentation();
        return;
    }
    REQUIRE( documentHasAnchor( browser->document(),
                                QLatin1String( MainWindow::PluginRepositoriesGuideAnchor ) ) );
    // The guide is far down the user guide: the window is scrolled to it.
    REQUIRE( QTest::qWaitFor( [ browser ] { return browser->verticalScrollBar()->value() > 0; },
                              5000 ) );
    REQUIRE( browser->toPlainText().contains( "Plugins: packing, repositories and updates" ) );
    closeDocumentation();
}
} // namespace

TEST_CASE( "The Plugins and Packs window manages ordered repositories and installs from them",
           "[ui][workbench][E15]" )
{
#ifndef CILOGG_TEST_HTTPS_SERVER
    WARN( "This Qt build has no TLS server" );
    return;
#else
    testhttps::CatalogServer server;
    if ( !server.listening() ) {
        WARN( "No TLS backend is available" );
        return;
    }
    QTemporaryDir temp;
    const auto package = fixtureZip( temp.path() );
    server.route( "/tools/org.test.e15-1.0.0.zip", { 200, package, "application/zip", 0 } );
    const QJsonObject entry{
        { "id", "org.test.e15" },
        { "name", "E15 fixture" },
        { "description", "Counts lines" },
        { "version", "1.0.0" },
        { "protocol_version", 1 },
        { "os", "any" },
        { "arch", "any" },
        { "url", server.url( "/tools/org.test.e15-1.0.0.zip" ) },
        { "sha256", QString::fromLatin1(
                        QCryptographicHash::hash( package, QCryptographicHash::Sha256 ).toHex() ) }
    };
    server.route( "/tools/plugins.json",
                  { 200,
                    QJsonDocument(
                        QJsonObject{ { "schema_version", 1 }, { "plugins", QJsonArray{ entry } } } )
                        .toJson(),
                    "application/json", 0 } );
    server.route( "/empty/plugins.json",
                  { 200, R"({"schema_version": 1, "plugins": []})", "application/json", 0 } );
    testhttps::TrustingNetwork network;
    PluginManager manager( QDir( temp.path() ).filePath( "data" ),
                           QDir( temp.path() ).filePath( "none" ), nullptr, &network );
    QStringList operations;
    const auto executor = [ &manager, &operations ]( const CommanderRequest& request ) {
        operations.append( request.pluginOperation );
        const auto result
            = manager.manage( request.pluginOperation, request.pluginId, request.pluginArguments );
        return result.ok() ? commanderSuccess( {}, result.payload )
                           : commanderFailure( CommanderResultCode::ExecutionFailed, result.error );
    };
    auto session = std::make_shared<Session>();
    MainWindow window( WindowSession{ session, "PluginRepositoriesE15", 0 } );
    window.setPluginManager( manager, executor );
    window.show();

    // One window; Manage Plugins opens Installed and Browse Plugins opens Available.
    window.findChild<QAction*>( "managePluginsAction" )->trigger();
    auto* dialog = window.findChild<QDialog*>( "pluginsAndPacksDialog" );
    REQUIRE( dialog );
    auto* tabs = dialog->findChild<QTabWidget*>( "pluginsAndPacksTabs" );
    REQUIRE( tabs->count() == 3 );
    REQUIRE( tabs->tabText( 0 ) == "Installed" );
    REQUIRE( tabs->tabText( 1 ) == "Available" );
    REQUIRE( tabs->tabText( 2 ) == "Repositories" );
    REQUIRE( tabs->currentIndex() == 0 );
    window.findChild<QAction*>( "browsePluginsAction" )->trigger();
    REQUIRE( window.findChildren<QDialog*>( "pluginsAndPacksDialog" ).size() == 1 );
    REQUIRE( tabs->currentIndex() == 1 );
    // The URL edit box is gone.
    REQUIRE( dialog->findChild<QLineEdit*>( "pluginCatalogUrl" ) == nullptr );

    // Repositories: the migrated default first; Add, Move Up, Enable, Edit and Remove.
    tabs->setCurrentIndex( 2 );
    auto* repositories = dialog->findChild<QTableWidget*>( "pluginRepositoriesTable" );
    REQUIRE( repositories->columnCount() == 7 );
    REQUIRE( column( repositories, 2 ) == QStringList{ "CILogg examples (GitHub)" } );
    REQUIRE( column( repositories, 0 ) == QStringList{ "1" } );
    REQUIRE( column( repositories, 4 ) == QStringList{ "None" } );
    // Sign In and Sign Out apply to Azure DevOps repositories only.
    repositories->selectRow( 0 );
    REQUIRE_FALSE( dialog->findChild<QPushButton*>( "pluginRepositorySignIn" )->isEnabled() );
    REQUIRE_FALSE( dialog->findChild<QPushButton*>( "pluginRepositorySignOut" )->isEnabled() );
    for ( const auto& [ name, path ] :
          { std::make_pair( QString( "Empty" ), QString( "/empty/plugins.json" ) ),
            std::make_pair( QString( "Tools" ), QString( "/tools/plugins.json" ) ) } ) {
        fillRepositoryDialog( name, server.url( path ) );
        dialog->findChild<QPushButton*>( "pluginRepositoryAdd" )->click();
    }
    REQUIRE( column( repositories, 2 )
             == QStringList{ "CILogg examples (GitHub)", "Empty", "Tools" } );
    // An address that is not HTTPS is refused in the dialog, which stays open until cancelled.
    onModal(
        []( QWidget* widget ) { return widget->objectName() == "pluginRepositoryDialog"; },
        []( QWidget* added ) {
            added->findChild<QLineEdit*>( "pluginRepositoryName" )->setText( "Plain" );
            added->findChild<QLineEdit*>( "pluginRepositoryAddress" )
                ->setText( "http://example.test/plugins.json" );
            onModal(
                []( QWidget* widget ) { return qobject_cast<QMessageBox*>( widget ) != nullptr; },
                [ added ]( QWidget* box ) {
                    qobject_cast<QMessageBox*>( box )->accept();
                    QTimer::singleShot( 50, added, [ added ] {
                        added->findChild<QPushButton*>( "pluginRepositoryCancel" )->click();
                    } );
                } );
            added->findChild<QPushButton*>( "pluginRepositoryAccept" )->click();
        } );
    dialog->findChild<QPushButton*>( "pluginRepositoryAdd" )->click();
    REQUIRE( repositories->rowCount() == 3 );
    // Disable the default (GitHub) repository: the Enabled check box.
    repositories->item( 0, 1 )->setCheckState( Qt::Unchecked );
    REQUIRE( QTest::qWaitFor(
        [ & ] { return manager.repositories()[ 0 ].toMap()[ "enabled" ] == false; }, 3000 ) );
    // Move Tools above Empty; the order persists in the plugin settings.
    repositories->selectRow( 2 );
    dialog->findChild<QPushButton*>( "pluginRepositoryUp" )->click();
    REQUIRE( column( repositories, 2 )
             == QStringList{ "CILogg examples (GitHub)", "Tools", "Empty" } );
    REQUIRE( manager.repositories()[ 1 ].toMap()[ "id" ] == "tools" );
    REQUIRE( repositories->currentRow() == 1 );
    REQUIRE( operations.contains( "repository_move" ) );
    // Edit renames; the address is read-only.
    auto readOnly = std::make_shared<bool>( false );
    fillRepositoryDialog( "Team tools", {}, readOnly );
    dialog->findChild<QPushButton*>( "pluginRepositoryEdit" )->click();
    REQUIRE( *readOnly );
    REQUIRE( column( repositories, 2 )
             == QStringList{ "CILogg examples (GitHub)", "Team tools", "Empty" } );
    REQUIRE( manager.repositories()[ 1 ].toMap()[ "url" ] == server.url( "/tools/plugins.json" ) );

    // Available: one job fetches the enabled repositories; groups follow the order.
    tabs->setCurrentIndex( 1 );
    auto* available = dialog->findChild<QTableWidget*>( "pluginCatalogTable" );
    auto* status = dialog->findChild<QLabel*>( "pluginCatalogStatus" );
    REQUIRE( available->columnCount() == 6 );
    dialog->findChild<QPushButton*>( "fetchPluginCatalog" )->click();
    REQUIRE( QTest::qWaitFor(
        [ & ] { return status->text().contains( "1 package from 2 repositories" ); }, 15000 ) );
    REQUIRE_FALSE( status->text().contains( "completed" ) );
    const auto names = column( available, 0 );
    REQUIRE( names.size() == 3 );
    REQUIRE( names[ 0 ].startsWith( "Team tools" ) );
    REQUIRE( names[ 0 ].contains( "1 package" ) );
    REQUIRE( names[ 1 ] == "E15 fixture" );
    REQUIRE( names[ 2 ].startsWith( "Empty" ) );
    REQUIRE( names[ 2 ].contains( "No plugins in this repository" ) );
    REQUIRE( available->item( 1, 1 )->text() == "Plugins" );
    REQUIRE( available->item( 1, 3 )->text() == "Team tools" );
    REQUIRE( available->item( 1, 4 )->text().isEmpty() );
    // The Repositories table shows the same results.
    REQUIRE( rowOf( repositories, 5, "No plugins in this repository" ) == 2 );
    REQUIRE( repositories->item( 1, 5 )->text() == "1 package" );
    REQUIRE( repositories->item( 1, 6 )->text() != "Never" );
    // The Kind filter: no filter packs here, one process plugin.
    auto* kind = dialog->findChild<QComboBox*>( "pluginKindFilter" );
    REQUIRE( kind->count() == 7 );
    kind->setCurrentIndex( kind->findText( "Filters" ) );
    REQUIRE( rowOf( available, 0, "E15 fixture" ) < 0 );
    kind->setCurrentIndex( kind->findText( "Plugins" ) );
    REQUIRE( rowOf( available, 0, "E15 fixture" ) == 1 );
    // Install from the selected row.
    available->selectRow( 1 );
    dialog->findChild<QPushButton*>( "installCatalogPlugin" )->click();
    REQUIRE( QTest::qWaitFor(
        [ & ] { return status->text().contains( "Installed E15 fixture 1.0.0" ); }, 15000 ) );
    REQUIRE( QTest::qWaitFor(
        [ & ] { return available->item( 1, 4 ) && available->item( 1, 4 )->text() == "1.0.0"; },
        3000 ) );
    // Installed: kind, version and source.
    tabs->setCurrentIndex( 0 );
    auto* installed = dialog->findChild<QTableWidget*>( "pluginManagerTable" );
    REQUIRE( QTest::qWaitFor( [ & ] { return rowOf( installed, 0, "E15 fixture" ) >= 0; }, 3000 ) );
    const auto row = rowOf( installed, 0, "E15 fixture" );
    REQUIRE( installed->item( row, 1 )->text() == "Plugins" );
    REQUIRE( installed->item( row, 2 )->text() == "1.0.0" );
    REQUIRE( installed->item( row, 3 )->text() == "Team tools" );
    REQUIRE( dialog->findChild<QPushButton*>( "pluginManage_update" ) );

    // Remove asks first; the plugin stays installed and its source becomes unavailable.
    tabs->setCurrentIndex( 2 );
    repositories->selectRow( 1 );
    answerYes();
    dialog->findChild<QPushButton*>( "pluginRepositoryRemove" )->click();
    REQUIRE( column( repositories, 2 ) == QStringList{ "CILogg examples (GitHub)", "Empty" } );
    REQUIRE( column( repositories, 0 ) == QStringList{ "1", "2" } );
    tabs->setCurrentIndex( 0 );
    REQUIRE( QTest::qWaitFor(
        [ & ] {
            const auto at = rowOf( installed, 0, "E15 fixture" );
            return at >= 0 && installed->item( at, 3 )->text().contains( "source unavailable" );
        },
        3000 ) );

    // How to... and Help > Plugin Repositories Guide open the guide.
    tabs->setCurrentIndex( 2 );
    requireGuide(
        [ & ] { dialog->findChild<QPushButton*>( "pluginRepositoriesHowTo" )->click(); } );
    auto* guide = window.findChild<QAction*>( "pluginRepositoriesGuideAction" );
    REQUIRE( guide );
    REQUIRE( window.findChild<QMenu*>( "helpMenu" )->actions().contains( guide ) );
    requireGuide( [ guide ] { guide->trigger(); } );
    dialog->close();
#endif
}

TEST_CASE( "An empty merged list says so and links to the guide", "[ui][workbench][E15]" )
{
#ifndef CILOGG_TEST_HTTPS_SERVER
    WARN( "This Qt build has no TLS server" );
    return;
#else
    testhttps::CatalogServer server;
    if ( !server.listening() ) {
        WARN( "No TLS backend is available" );
        return;
    }
    server.route( "/empty/plugins.json",
                  { 200, R"({"schema_version": 1, "plugins": []})", "application/json", 0 } );
    QTemporaryDir temp;
    testhttps::TrustingNetwork network;
    PluginManager manager( QDir( temp.path() ).filePath( "data" ),
                           QDir( temp.path() ).filePath( "none" ), nullptr, &network );
    REQUIRE( manager.manage( "repository_remove", {}, { { "repository", "default" } } ).ok() );
    REQUIRE( manager
                 .manage( "repository_add", {},
                          { { "name", "Empty" }, { "url", server.url( "/empty/plugins.json" ) } } )
                 .ok() );
    const auto executor = [ &manager ]( const CommanderRequest& request ) {
        const auto result
            = manager.manage( request.pluginOperation, request.pluginId, request.pluginArguments );
        return result.ok() ? commanderSuccess( {}, result.payload )
                           : commanderFailure( CommanderResultCode::ExecutionFailed, result.error );
    };
    auto session = std::make_shared<Session>();
    MainWindow window( WindowSession{ session, "PluginRepositoriesEmpty", 0 } );
    window.setPluginManager( manager, executor );
    window.show();
    window.findChild<QAction*>( "browsePluginsAction" )->trigger();
    auto* dialog = window.findChild<QDialog*>( "pluginsAndPacksDialog" );
    auto* status = dialog->findChild<QLabel*>( "pluginCatalogStatus" );
    dialog->findChild<QPushButton*>( "fetchPluginCatalog" )->click();
    REQUIRE( QTest::qWaitFor(
        [ & ] { return status->text().contains( "No plugins in the enabled repositories" ); },
        15000 ) );
    REQUIRE( status->text().contains( "href" ) );
    auto* available = dialog->findChild<QTableWidget*>( "pluginCatalogTable" );
    REQUIRE( available->rowCount() == 1 );
    REQUIRE( available->item( 0, 0 )->text().contains( "No plugins in this repository" ) );
    requireGuide( [ status ] { Q_EMIT status->linkActivated( "guide" ); } );
    dialog->close();
#endif
}

TEST_CASE( "Azure DevOps repositories are added, signed in and signed out in the window",
           "[ui][workbench][E15]" )
{
#ifndef CILOGG_TEST_HTTPS_SERVER
    WARN( "This Qt build has no TLS server" );
    return;
#else
    testhttps::CatalogServer server;
    if ( !server.listening() ) {
        WARN( "No TLS backend is available" );
        return;
    }
    const QByteArray catalogPath = "/contoso/Tools/_apis/git/repositories/catalog/items";
    server.route( catalogPath,
                  { 200, R"({"schema_version": 1, "plugins": []})", "application/json", 0 } );
    testgit::FakeGit git( "bearer", "ui-token" );
    QTemporaryDir temp;
    const auto data = QDir( temp.path() ).filePath( "data" );
    REQUIRE( QDir().mkpath( data ) );
    {
        QSettings settings( QDir( data ).filePath( "settings.ini" ), QSettings::IniFormat );
        settings.setValue( "gitExecutable", testgit::FakeGit::path() );
    }
    testhttps::TrustingNetwork network;
    network.hostToLocalPort.insert( "dev.azure.com", server.port() );
    PluginManager manager( data, QDir( temp.path() ).filePath( "none" ), nullptr, &network );
    auto store = std::make_unique<cilogg::plugins::MemoryPatStore>();
    auto* tokens = store.get();
    manager.setPatStore( std::move( store ) );
    QStringList origins;
    const auto executor = [ &manager, &origins ]( const CommanderRequest& request ) {
        origins.append( request.origin );
        const auto result = manager.manage( request.pluginOperation, request.pluginId,
                                            request.pluginArguments, request.origin );
        return result.ok() ? commanderSuccess( {}, result.payload )
                           : commanderFailure( CommanderResultCode::ExecutionFailed, result.error );
    };
    auto session = std::make_shared<Session>();
    MainWindow window( WindowSession{ session, "PluginRepositoriesE15b", 0 } );
    window.setPluginManager( manager, executor );
    window.show();
    window.findChild<QAction*>( "managePluginsAction" )->trigger();
    auto* dialog = window.findChild<QDialog*>( "pluginsAndPacksDialog" );
    REQUIRE( dialog );
    dialog->findChild<QTabWidget*>( "pluginsAndPacksTabs" )->setCurrentIndex( 2 );
    auto* repositories = dialog->findChild<QTableWidget*>( "pluginRepositoriesTable" );
    auto* status = dialog->findChild<QLabel*>( "pluginRepositoriesStatus" );

    // Add: sign-in Azure DevOps with a file link; the sign-in is tested at once, interactively.
    auto fieldsShown = std::make_shared<bool>( false );
    onModal( []( QWidget* widget ) { return widget->objectName() == "pluginRepositoryDialog"; },
             [ fieldsShown ]( QWidget* added ) {
                 auto* signIn = added->findChild<QComboBox*>( "pluginRepositorySignIn" );
                 signIn->setCurrentIndex( signIn->findData( "azure-devops" ) );
                 *fieldsShown = added->findChild<QLineEdit*>( "pluginRepositoryBranch" )->isVisibleTo( added );
                 added->findChild<QLineEdit*>( "pluginRepositoryName" )->setText( "Team" );
                 added->findChild<QLineEdit*>( "pluginRepositoryAddress" )
                     ->setText( "https://dev.azure.com/contoso/Tools/_git/catalog"
                                "?path=/plugins.json&version=GBcatalog" );
                 added->findChild<QPushButton*>( "pluginRepositoryAccept" )->click();
             } );
    dialog->findChild<QPushButton*>( "pluginRepositoryAdd" )->click();
    REQUIRE( *fieldsShown );
    REQUIRE( QTest::qWaitFor( [ & ] { return status->text() == "Signed in to \"Team\"."; },
                              15000 ) );
    const auto row = rowOf( repositories, 2, "Team" );
    REQUIRE( row == 1 );
    REQUIRE( repositories->item( row, 3 )->text()
             == "https://dev.azure.com/contoso/Tools/_git/catalog" );
    REQUIRE( repositories->item( row, 4 )->text() == "Azure DevOps (Git Credential Manager)" );
    REQUIRE( repositories->item( row, 5 )->text() == "No plugins in this repository" );
    REQUIRE( git.calls().value( 0 ).value( "GCM_INTERACTIVE" ).toString() == "<unset>" );
    REQUIRE( origins.contains( "gui" ) );
    REQUIRE_FALSE( origins.contains( QString{} ) );
    repositories->selectRow( row );
    REQUIRE( dialog->findChild<QPushButton*>( "pluginRepositorySignIn" )->isEnabled() );
    REQUIRE( dialog->findChild<QPushButton*>( "pluginRepositorySignOut" )->isEnabled() );

    // Edit switches to a personal access token; the address fields stay read-only.
    auto addressReadOnly = std::make_shared<bool>( false );
    onModal( []( QWidget* widget ) { return widget->objectName() == "pluginRepositoryDialog"; },
             [ addressReadOnly ]( QWidget* edited ) {
                 *addressReadOnly
                     = edited->findChild<QLineEdit*>( "pluginRepositoryAddress" )->isReadOnly()
                       && edited->findChild<QLineEdit*>( "pluginRepositoryBranch" )->isReadOnly()
                       && !edited->findChild<QComboBox*>( "pluginRepositorySignIn" )->isEnabled();
                 auto* source = edited->findChild<QComboBox*>( "pluginRepositoryCredentialSource" );
                 source->setCurrentIndex( source->findData( "personal-access-token" ) );
                 edited->findChild<QPushButton*>( "pluginRepositoryAccept" )->click();
             } );
    dialog->findChild<QPushButton*>( "pluginRepositoryEdit" )->click();
    REQUIRE( *addressReadOnly );
    REQUIRE( manager.repositories()[ row ].toMap()[ "credentialSource" ]
             == "personal-access-token" );
    REQUIRE( repositories->item( row, 4 )->text() == "Azure DevOps (personal access token)" );

    // Sign In asks for the token in a password field and keeps it only in the store.
    auto passwordField = std::make_shared<bool>( false );
    onModal( []( QWidget* widget ) { return widget->objectName() == "pluginRepositoryTokenDialog"; },
             [ passwordField ]( QWidget* prompt ) {
                 auto* token = prompt->findChild<QLineEdit*>( "pluginRepositoryToken" );
                 *passwordField = token->echoMode() == QLineEdit::Password;
                 token->setText( "ui-pat" );
                 prompt->findChild<QPushButton*>( "pluginRepositoryTokenAccept" )->click();
             } );
    repositories->selectRow( row );
    dialog->findChild<QPushButton*>( "pluginRepositorySignIn" )->click();
    REQUIRE( QTest::qWaitFor( [ & ] { return status->text().startsWith( "Signed in to \"Team\"" ); },
                              15000 ) );
    REQUIRE( *passwordField );
    const auto id = manager.repositories()[ row ].toMap()[ "id" ].toString();
    const auto key = manager.tokenKey( id );
    REQUIRE( tokens->contains( key ) );
    REQUIRE( server.requests.last().headers.value( "authorization" )
             == "Basic " + QByteArray( ":ui-pat" ).toBase64() );
    {
        // Closed again at once: on Windows an open handle makes the next settings save fail.
        QFile settingsFile( QDir( data ).filePath( "settings.ini" ) );
        REQUIRE( settingsFile.open( QIODevice::ReadOnly ) );
        REQUIRE_FALSE( settingsFile.readAll().contains( "ui-pat" ) );
    }

    // Sign Out deletes it.
    repositories->selectRow( row );
    dialog->findChild<QPushButton*>( "pluginRepositorySignOut" )->click();
    REQUIRE( QTest::qWaitFor( [ & ] { return status->text() == "Signed out of \"Team\"."; },
                              5000 ) );
    REQUIRE_FALSE( tokens->contains( key ) );
    REQUIRE( repositories->item( row, 5 )->text() == "Sign-in needed" );
    dialog->close();
#endif
}
