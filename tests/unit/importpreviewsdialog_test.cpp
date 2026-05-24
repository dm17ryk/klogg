#include <catch2/catch.hpp>

#include <QItemSelectionModel>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryFile>
#include <QTest>
#include <QToolButton>

#include "importpreviewsdialog.h"
#include "previewmanager.h"

namespace {

constexpr auto kImportPreviewConfig = R"json(
{
  "$schema": "../schemas/klogg-previews.schema.json",
  "version": 1,
  "previews": [
    {
      "name": "First",
      "regex": "^first$",
      "format": "fields",
      "fields": [
        { "name": "payload", "width": 0, "type": "string", "format": "string" }
      ]
    },
    {
      "name": "Second",
      "regex": "^second$",
      "format": "fields",
      "fields": [
        { "name": "payload", "width": 0, "type": "string", "format": "string" }
      ]
    },
    {
      "name": "Third",
      "regex": "^third$",
      "format": "fields",
      "fields": [
        { "name": "payload", "width": 0, "type": "string", "format": "string" }
      ]
    }
  ]
}
)json";

void loadImportPreviewConfig()
{
    auto& manager = PreviewManager::instance();
    REQUIRE( manager.clearAll() );

    QTemporaryFile file;
    REQUIRE( file.open() );
    const auto bytes = QByteArray::fromRawData(
        kImportPreviewConfig, static_cast<int>( strlen( kImportPreviewConfig ) ) );
    REQUIRE( file.write( bytes ) == bytes.size() );
    REQUIRE( file.flush() );

    const auto result = manager.importFromFile( file.fileName() );
    REQUIRE( result.ok );
}

QStringList previewNames()
{
    QStringList names;
    const auto& previews = PreviewManager::instance().all();
    names.reserve( previews.size() );
    for ( const auto& preview : previews ) {
        names.push_back( preview.name );
    }
    return names;
}

} // namespace

TEST_CASE( "Import previews dialog moves selected previews up and down", "[previewimport]" )
{
    loadImportPreviewConfig();

    ImportPreviewsDialog dialog;
    dialog.show();
    REQUIRE( QTest::qWaitForWindowExposed( &dialog ) );

    auto* table = dialog.findChild<QTableView*>( "importPreviewsTable" );
    auto* moveUpButton = dialog.findChild<QToolButton*>( "movePreviewUpButton" );
    auto* moveDownButton = dialog.findChild<QToolButton*>( "movePreviewDownButton" );
    REQUIRE( table );
    REQUIRE( moveUpButton );
    REQUIRE( moveDownButton );

    table->selectRow( 1 );
    table->setCurrentIndex( table->model()->index( 1, 0 ) );
    REQUIRE( moveUpButton->isEnabled() );
    REQUIRE( moveDownButton->isEnabled() );

    QTest::mouseClick( moveUpButton, Qt::LeftButton );
    REQUIRE( previewNames() == QStringList{ "Second", "First", "Third" } );
    REQUIRE( table->selectionModel()->currentIndex().row() == 0 );
    REQUIRE_FALSE( moveUpButton->isEnabled() );
    REQUIRE( moveDownButton->isEnabled() );

    QTest::mouseClick( moveDownButton, Qt::LeftButton );
    REQUIRE( previewNames() == QStringList{ "First", "Second", "Third" } );
    REQUIRE( table->selectionModel()->currentIndex().row() == 1 );
    REQUIRE( moveUpButton->isEnabled() );
    REQUIRE( moveDownButton->isEnabled() );

    table->selectRow( 2 );
    table->setCurrentIndex( table->model()->index( 2, 0 ) );
    REQUIRE( moveUpButton->isEnabled() );
    REQUIRE_FALSE( moveDownButton->isEnabled() );
}
