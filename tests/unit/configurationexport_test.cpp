#include <catch2/catch.hpp>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>

#include "actionsmanager.h"
#include "actionsrepository.h"
#include "configuration.h"
#include "configurationexportdialog.h"
#include "persistentinfo.h"
#include "previewmanager.h"
#include "previewrepository.h"

namespace {
struct ConfigurationRestoreGuard {
    QMap<QString, QVariant> settings;
    QVector<ActionDefinition> actions = ActionsManager::instance().actions();
    QVector<ResponseDefinition> responses = ActionsManager::instance().responses();
    QVector<PreviewDefinition> previews = PreviewManager::instance().all();
    QMap<QString, PreviewFieldSpec> blocks = PreviewManager::instance().blocks();
    ConfigurationRestoreGuard()
    {
        auto& source = PersistentInfo::getSettings( app_settings{} );
        for ( const auto& key : source.allKeys() ) {
            settings.insert( key, source.value( key ) );
        }
    }
    ~ConfigurationRestoreGuard()
    {
        auto& destination = PersistentInfo::getSettings( app_settings{} );
        destination.clear();
        for ( auto it = settings.cbegin(); it != settings.cend(); ++it ) {
            destination.setValue( it.key(), it.value() );
        }
        destination.sync();
        ActionsRepository{}.save( actions, responses );
        PreviewRepository{}.save( previews, blocks );
        ActionsManager::instance().loadFromRepository();
        PreviewManager::instance().loadFromRepository();
        Configuration::getSynced();
        HighlighterSetCollection::getSynced();
        PredefinedFiltersCollection::getSynced();
    }
};

void seedConfiguration()
{
    ActionDefinition first;
    first.id = 1;
    first.name = "First action";
    first.sequence.value = "hello";
    first.enabled = false;
    first.hidden = true;
    ActionDefinition second = first;
    second.id = 2;
    second.name = "Second action";
    second.sequence.value = "bye";
    ResponseDefinition response;
    response.id = 3;
    response.name = "Reply";
    response.match.value = "ready";
    response.response.hasActionId = true;
    response.response.actionId = 2;
    REQUIRE( ActionsRepository{}.save( { first, second }, { response } ) );
    ActionsManager::instance().loadFromRepository();

    PreviewDefinition preview;
    preview.name = "Example preview";
    preview.regex = "^hello$";
    preview.enabled = false;
    preview.hasEnabled = true;
    PreviewFieldSpec field;
    field.name = "payload";
    field.width.isSet = true;
    field.width.isLiteral = true;
    field.width.literalValue = 0;
    field.type = PreviewBufferType::String;
    preview.fields.append( field );
    REQUIRE( PreviewRepository{}.save( { preview }, {} ) );
    PreviewManager::instance().loadFromRepository();

    auto& highlights = HighlighterSetCollection::getSynced();
    auto set = HighlighterSet::createNewSet( "Errors" );
    set.highlighters().append( Highlighter( "error", true, false, Qt::red, Qt::white ) );
    highlights.setHighlighterSets( { set } );
    highlights.deactivateAll();
    highlights.activateSet( set.id() );
    highlights.save();
    auto& filters = PredefinedFiltersCollection::getSynced();
    filters.setFilters(
        { { "one", "Warnings", "warning", true }, { "two", "Text", "literal", false } } );
    filters.save();
    Configuration::getSynced().setShowTabsBarByDefault( false );
    Configuration::get().save();
}

SessionInfo testSession()
{
    SessionInfo session;
    session.add( "window-1" );
    session.setGeometry( "window-1", QByteArray( "geometry" ) );
    session.setOpenFiles( "window-1", { { "relative.log", 17, "view-context", "stream-context",
                                          "script-context" } } );
    session.setGlobalScriptContext( "global-context" );
    return session;
}
} // namespace

TEST_CASE( "Export selection preserves hidden checks and applies bulk changes to visible names",
           "[configurationexport]" )
{
    ConfigurationExportDialog dialog( ConfigurationExportKind::Actions,
                                      { "Alpha", "Beta", "Alphabet" } );
    auto* list = dialog.findChild<QListWidget*>( "exportItems" );
    auto* filter = dialog.findChild<QLineEdit*>( "exportNameFilter" );
    auto* deselect = dialog.findChild<QPushButton*>( "exportDeselectAll" );
    auto* select = dialog.findChild<QPushButton*>( "exportSelectAll" );
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    REQUIRE( list );
    REQUIRE( filter );
    REQUIRE( deselect );
    REQUIRE( select );
    REQUIRE( buttons );
    REQUIRE( dialog.selectedRows() == QVector<int>{ 0, 1, 2 } );
    filter->setText( "ALP" );
    REQUIRE( list->item( 1 )->isHidden() );
    deselect->click();
    REQUIRE( dialog.selectedRows() == QVector<int>{ 1 } );
    filter->clear();
    deselect->click();
    REQUIRE( dialog.selectedRows().isEmpty() );
    REQUIRE_FALSE( buttons->button( QDialogButtonBox::Save )->isEnabled() );
    select->click();
    REQUIRE( dialog.selectedRows().size() == 3 );
    REQUIRE( buttons->button( QDialogButtonBox::Save )->isEnabled() );
    ConfigurationExportDialog empty( ConfigurationExportKind::Previews, {} );
    REQUIRE_FALSE(
        empty.findChild<QDialogButtonBox*>()->button( QDialogButtonBox::Save )->isEnabled() );
}

TEST_CASE(
    "Highlight export exposes separate files and filters export offers selection without search",
    "[configurationexport]" )
{
    ConfigurationExportDialog highlights( ConfigurationExportKind::Highlights, { "Group" } );
    auto* multiple = highlights.findChild<QCheckBox*>( "exportMultipleFiles" );
    REQUIRE( multiple );
    REQUIRE_FALSE( highlights.multipleFiles() );
    multiple->setChecked( true );
    REQUIRE( highlights.multipleFiles() );
    ConfigurationExportDialog filters( ConfigurationExportKind::PredefinedFilters, { "Filter" } );
    REQUIRE( filters.findChild<QLineEdit*>( "exportNameFilter" )->isHidden() );
}

TEST_CASE(
    "Selected actions export keeps metadata and excludes responses with missing dependencies",
    "[configurationexport]" )
{
    ConfigurationRestoreGuard restore;
    seedConfiguration();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    QString error;
    const auto file = directory.filePath( "actions.json" );
    REQUIRE( ConfigurationExport::exportSelected( ConfigurationExportKind::Actions, { 0 }, file,
                                                  &error ) );
    const auto parsed = ActionsConfigParser{}.parseFile( file );
    REQUIRE( parsed.errors.isEmpty() );
    REQUIRE( parsed.actions.size() == 1 );
    REQUIRE( parsed.actions.front().id == 1 );
    REQUIRE_FALSE( parsed.actions.front().enabled );
    REQUIRE( parsed.actions.front().hidden );
    REQUIRE( parsed.responses.isEmpty() );
    REQUIRE( ConfigurationExport::exportSelected( ConfigurationExportKind::Actions, { 0, 1 }, file,
                                                  &error ) );
    REQUIRE( ActionsConfigParser{}.parseFile( file ).responses.size() == 1 );
    REQUIRE_FALSE( ConfigurationExport::exportSelected( ConfigurationExportKind::Actions, { 9 },
                                                        file, &error ) );
    REQUIRE( ActionsConfigParser{}.parseFile( file ).actions.size() == 2 );
}

TEST_CASE(
    "Highlight files preserve group IDs and survive duplicate and invalid filename characters",
    "[configurationexport]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    HighlighterSetCollection collection;
    collection.setHighlighterSets( { HighlighterSet::createNewSet( "../CON:name" ),
                                     HighlighterSet::createNewSet( "../CON:name" ) } );
    QString error;
    QStringList files;
    REQUIRE( ConfigurationExport::exportHighlightFiles( collection, { 0, 1 }, directory.path(),
                                                        &error, &files ) );
    REQUIRE( files.size() == 2 );
    REQUIRE( files.at( 0 ) != files.at( 1 ) );
    QStringList secondExport;
    REQUIRE( ConfigurationExport::exportHighlightFiles( collection, { 0, 1 }, directory.path(),
                                                        &error, &secondExport ) );
    REQUIRE( secondExport.size() == 2 );
    REQUIRE_FALSE( files.contains( secondExport.at( 0 ) ) );
    REQUIRE_FALSE( files.contains( secondExport.at( 1 ) ) );
    for ( int i = 0; i < files.size(); ++i ) {
        REQUIRE( QFileInfo( files.at( i ) ).absoluteDir().path() == directory.path() );
        QSettings settings( files.at( i ), QSettings::IniFormat );
        HighlighterSetCollection loaded;
        loaded.retrieveFromStorage( settings );
        REQUIRE( loaded.highlighterSets().size() == 1 );
        REQUIRE( loaded.highlighterSets().front().id()
                 == collection.highlighterSets().at( i ).id() );
    }
}

TEST_CASE( "Project round trip restores all definitions preferences session and registered exports",
           "[configurationexport][project]" )
{
    ConfigurationRestoreGuard restore;
    seedConfiguration();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    QString error;
    const auto path = directory.filePath( "example.cilogproj" );
    const auto exportPath = directory.filePath( "selected.conf" );
    REQUIRE( ConfigurationExport::exportSelected( ConfigurationExportKind::PredefinedFilters, { 1 },
                                                  exportPath, &error ) );
    REQUIRE( ConfigurationExport::saveProject( path, testSession(), { { "window-1", 0 } },
                                               { { exportPath, "filters" } }, &error ) );
    ConfigurationProject loaded;
    REQUIRE( ConfigurationExport::readProject( path, &loaded, &error ) );
    REQUIRE( loaded.actions.actions.size() == 2 );
    REQUIRE( loaded.actions.responses.size() == 1 );
    REQUIRE( loaded.previews.previews.size() == 1 );
    REQUIRE_FALSE( loaded.previews.previews.front().enabled );
    REQUIRE( loaded.highlights.highlighterSets().front().name() == "Errors" );
    REQUIRE( loaded.highlights.activeSetIds().size() == 1 );
    REQUIRE( loaded.filters.getFilters().size() == 2 );
    REQUIRE( loaded.preferences.value( "view.showTabsBarByDefault" ).toBool() == false );
    REQUIRE( loaded.session.openFiles( "window-1" ).front().topLine == 17 );
    REQUIRE( loaded.session.openFiles( "window-1" ).front().scriptContext == "script-context" );
    REQUIRE( loaded.session.globalScriptContext() == "global-context" );
    REQUIRE( loaded.activeTabs.value( "window-1" ) == 0 );
    REQUIRE( loaded.exports.size() == 1 );
    REQUIRE( QFileInfo( loaded.exports.firstKey() ).exists() );
    REQUIRE( ConfigurationExport::applyProjectConfiguration( loaded, &error ) );
    REQUIRE( ActionsManager::instance().actions().size() == 2 );
    REQUIRE_FALSE( Configuration::get().showTabsBarByDefault() );
    const auto activeHighlightId = loaded.highlights.activeSetIds().front();
    REQUIRE( HighlighterSetCollection::get().activeSetIds().contains( activeHighlightId ) );
}

TEST_CASE( "Project snapshots use relative references and failed save preserves previous manifest",
           "[configurationexport][project]" )
{
    ConfigurationRestoreGuard restore;
    seedConfiguration();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    QString error;
    const auto path = directory.filePath( "original.cilogproj" );
    REQUIRE( ConfigurationExport::saveProject( path, testSession(), {}, {}, &error ) );
    QFile manifest( path );
    REQUIRE( manifest.open( QIODevice::ReadOnly ) );
    const auto original = manifest.readAll();
    manifest.close();
    const auto root = QJsonDocument::fromJson( original ).object();
    for ( const auto& reference : root.value( "configurations" ).toObject() ) {
        REQUIRE( QDir::isRelativePath( reference.toString() ) );
    }
    REQUIRE_FALSE( ConfigurationExport::saveProject(
        path, testSession(), {}, { { directory.filePath( "missing.conf" ), "filters" } },
        &error ) );
    REQUIRE( manifest.open( QIODevice::ReadOnly ) );
    REQUIRE( manifest.readAll() == original );
    manifest.close();
    ConfigurationProject project;
    REQUIRE( ConfigurationExport::readProject( path, &project, &error ) );
    const auto preferences
        = root.value( "configurations" ).toObject().value( "preferences" ).toString();
    REQUIRE( QFile::remove( directory.filePath( preferences ) ) );
    REQUIRE_FALSE( ConfigurationExport::readProject( path, &project, &error ) );
    REQUIRE( ActionsManager::instance().actions().size() == 2 );
    REQUIRE( project.session.windows().size() == 1 );
}

TEST_CASE(
    "Project loading rejects invalid versions and escaping references without modifying settings",
    "[configurationexport][project]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    QString error;
    const auto path = directory.filePath( "invalid.cilogproj" );
    REQUIRE( ConfigurationExport::writeFile( path, "{\"version\":99}", &error ) );
    ConfigurationProject project;
    REQUIRE_FALSE( ConfigurationExport::readProject( path, &project, &error ) );
    REQUIRE( project.session.windows().isEmpty() );
    REQUIRE( ConfigurationExport::writeFile(
        path,
        R"({"format":"CILoggProject","version":1,"activeTabs":{},"exports":[],"configurations":{"actions":"../actions.json"}})",
        &error ) );
    REQUIRE_FALSE( ConfigurationExport::readProject( path, &project, &error ) );
    REQUIRE( error.contains( "reference" ) );
}

TEST_CASE( "Export snapshots retain definition identity after live actions change",
           "[configurationexport]" )
{
    ConfigurationRestoreGuard restore;
    seedConfiguration();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    ConfigurationProject snapshot;
    QString error;
    REQUIRE( ConfigurationExport::captureConfiguration( &snapshot, &error ) );
    REQUIRE( ActionsManager::instance().deleteAction( 1, &error ) );
    REQUIRE( ActionsManager::instance().actions().front().id == 2 );
    const auto path = directory.filePath( "snapshot.json" );
    REQUIRE( ConfigurationExport::exportSelected( ConfigurationExportKind::Actions, { 0 }, path,
                                                  &error, nullptr, &snapshot ) );
    const auto parsed = ActionsConfigParser{}.parseFile( path );
    REQUIRE( parsed.errors.isEmpty() );
    REQUIRE( parsed.actions.front().id == 1 );
    REQUIRE( parsed.actions.front().name == "First action" );
}

TEST_CASE( "Project references survive moving the complete project folder",
           "[configurationexport][project]" )
{
    ConfigurationRestoreGuard restore;
    seedConfiguration();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    QDir parent( directory.path() );
    REQUIRE( parent.mkdir( "original" ) );
    QString error;
    REQUIRE( ConfigurationExport::saveProject( parent.filePath( "original/project.cilogproj" ),
                                               testSession(), {}, {}, &error ) );
    REQUIRE( parent.rename( "original", "moved" ) );
    ConfigurationProject project;
    REQUIRE( ConfigurationExport::readProject( parent.filePath( "moved/project.cilogproj" ),
                                               &project, &error ) );
    REQUIRE( project.actions.actions.size() == 2 );
    REQUIRE( project.filters.getFilters().size() == 2 );
}

TEST_CASE( "Project validation rejects unsupported nested settings before changing its destination",
           "[configurationexport][project]" )
{
    ConfigurationRestoreGuard restore;
    seedConfiguration();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "project.cilogproj" );
    QString error;
    REQUIRE( ConfigurationExport::saveProject( path, testSession(), {}, {}, &error ) );
    ConfigurationProject project;
    REQUIRE( ConfigurationExport::readProject( path, &project, &error ) );
    QFile manifest( path );
    REQUIRE( manifest.open( QIODevice::ReadOnly ) );
    const auto refs = QJsonDocument::fromJson( manifest.readAll() )
                          .object()
                          .value( "configurations" )
                          .toObject();
    manifest.close();
    QSettings session( directory.filePath( refs.value( "session" ).toString() ),
                       QSettings::IniFormat );
    session.setValue( "Window/windows/1/OpenFiles/version", 99 );
    session.sync();
    REQUIRE_FALSE( ConfigurationExport::readProject( path, &project, &error ) );
    REQUIRE( project.session.openFiles( "window-1" ).size() == 1 );
    REQUIRE( ActionsManager::instance().actions().size() == 2 );
    session.setValue( "Window/windows/1/OpenFiles/version", 2 );
    session.sync();
    QSettings highlights( directory.filePath( refs.value( "highlights" ).toString() ),
                          QSettings::IniFormat );
    highlights.setValue( "HighlighterSetCollection/sets/1/HighlighterSet/version", 99 );
    highlights.sync();
    REQUIRE_FALSE( ConfigurationExport::readProject( path, &project, &error ) );
    REQUIRE( project.highlights.highlighterSets().front().name() == "Errors" );
}

TEST_CASE( "Project save rejects unsupported collection sizes and removes failed staging folders",
           "[configurationexport][project]" )
{
    ConfigurationRestoreGuard restore;
    seedConfiguration();
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    PredefinedFiltersCollection::Collection filters;
    for ( int i = 0; i < 4097; ++i ) {
        filters.append( { QString::number( i ), QString::number( i ), "pattern", false } );
    }
    auto& collection = PredefinedFiltersCollection::getSynced();
    collection.setFilters( filters );
    collection.save();
    QString error;
    const auto path = directory.filePath( "too-large.cilogproj" );
    REQUIRE_FALSE( ConfigurationExport::saveProject( path, testSession(), {}, {}, &error ) );
    REQUIRE_FALSE( QFileInfo::exists( path ) );
    REQUIRE( QDir( directory.path() ).entryList( QDir::Dirs | QDir::NoDotAndDotDot ).isEmpty() );
}
