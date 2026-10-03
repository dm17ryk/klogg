#include <catch2/catch.hpp>

#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTableView>
#include <QTest>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "uiautomation.h"

TEST_CASE( "UI commands inspect and edit dialogs through the normal Qt signals", "[ui][commander]" )
{
    QWidget owner;
    QDialog dialog( &owner );
    auto* layout = new QVBoxLayout( &dialog );
    auto* field = new QLineEdit( &dialog );
    field->setObjectName( "testField" );
    auto* check = new QCheckBox( &dialog );
    check->setObjectName( "testCheck" );
    auto* button = new QPushButton( &dialog );
    button->setObjectName( "testButton" );
    layout->addWidget( field );
    layout->addWidget( check );
    layout->addWidget( button );
    dialog.show();
    QSignalSpy edits( field, &QLineEdit::textChanged );
    QSignalSpy clicks( button, &QPushButton::clicked );

    CommanderRequest request;
    request.action = CommanderAction::GetUi;
    const auto objects = executeUiAutomation( &owner, request ).payload.value( "objects" ).toList();
    REQUIRE( !objects.isEmpty() );

    request.action = CommanderAction::SetUi;
    request.objectName = "testField";
    request.definitionPayload = { { "text", "Changed by CLI" } };
    REQUIRE( executeUiAutomation( &owner, request ).ok() );
    REQUIRE( field->text() == "Changed by CLI" );
    REQUIRE( edits.count() == 1 );

    request.action = CommanderAction::ActivateUi;
    request.searchText = "Ctrl+A";
    request.definitionPayload.clear();
    REQUIRE( executeUiAutomation( &owner, request ).ok() );
    QTest::qWait( 10 );
    REQUIRE( field->selectedText() == field->text() );
    request.searchText.clear();
    request.action = CommanderAction::SetUi;

    request.definitionPayload = { { "text", "Do not apply" }, { "unsupported", 1 } };
    REQUIRE( executeUiAutomation( &owner, request ).code == CommanderResultCode::InvalidRequest );
    REQUIRE( field->text() == "Changed by CLI" );
    field->setReadOnly( true );
    request.definitionPayload = { { "text", "read-only" } };
    REQUIRE( executeUiAutomation( &owner, request ).code == CommanderResultCode::InvalidRequest );
    REQUIRE( field->text() == "Changed by CLI" );

    request.objectName = "testCheck";
    request.definitionPayload = { { "checked", true } };
    REQUIRE( executeUiAutomation( &owner, request ).ok() );
    REQUIRE( check->isChecked() );

    request.action = CommanderAction::ActivateUi;
    request.objectName = "testButton";
    request.definitionPayload.clear();
    REQUIRE( executeUiAutomation( &owner, request ).payload.value( "queued" ).toBool() );
    REQUIRE( clicks.count() == 0 );
    QTest::qWait( 10 );
    REQUIRE( clicks.count() == 1 );

    button->setEnabled( false );
    REQUIRE( executeUiAutomation( &owner, request ).code == CommanderResultCode::ExecutionFailed );
    request.objectName = "does-not-exist";
    REQUIRE( executeUiAutomation( &owner, request ).code == CommanderResultCode::NotFound );
}

TEST_CASE( "UI paths distinguish duplicate names and password inspection is redacted",
           "[ui][commander]" )
{
    QWidget owner;
    QVBoxLayout layout( &owner );
    QLineEdit first;
    QLineEdit second;
    first.setObjectName( "duplicate" );
    second.setObjectName( "duplicate" );
    second.setText( "secret" );
    second.setEchoMode( QLineEdit::Password );
    layout.addWidget( &first );
    layout.addWidget( &second );
    owner.show();

    CommanderRequest request;
    request.action = CommanderAction::GetUi;
    request.objectName = "duplicate";
    REQUIRE( executeUiAutomation( &owner, request ).code == CommanderResultCode::InvalidRequest );
    request.objectName.clear();
    const auto objects = executeUiAutomation( &owner, request ).payload.value( "objects" ).toList();
    QString firstPath;
    for ( const auto& object : objects ) {
        const auto data = object.toMap();
        if ( data.value( "objectName" ).toString() == "duplicate" ) {
            if ( data.value( "properties" ).toMap().value( "text" ).toString().isEmpty() ) {
                firstPath = data.value( "objectPath" ).toString();
            }
            else {
                REQUIRE( data.value( "properties" ).toMap().value( "text" ) == "<redacted>" );
            }
        }
    }
    REQUIRE( !firstPath.isEmpty() );
    request.action = CommanderAction::SetUi;
    request.objectName = firstPath;
    request.definitionPayload = { { "text", "one" } };
    REQUIRE( executeUiAutomation( &owner, request ).ok() );
    REQUIRE( first.text() == "one" );
    REQUIRE( second.text() == "secret" );
}

TEST_CASE( "UI commands select and edit model cells without bypassing the model",
           "[ui][commander]" )
{
    QWidget owner;
    QVBoxLayout layout( &owner );
    QTableView table;
    QStandardItemModel model( 2, 2 );
    table.setObjectName( "testTable" );
    table.setModel( &model );
    layout.addWidget( &table );
    owner.show();
    CommanderRequest request;
    request.action = CommanderAction::SetUi;
    request.objectName = "testTable";
    request.definitionPayload
        = { { "indexPath", QVariantList{ 1 } }, { "column", 1 }, { "editText", "edited" } };
    REQUIRE( executeUiAutomation( &owner, request ).ok() );
    REQUIRE( table.currentIndex() == model.index( 1, 1 ) );
    REQUIRE( model.data( model.index( 1, 1 ) ).toString() == "edited" );
    model.item( 1, 1 )->setCheckable( true );
    request.definitionPayload = { { "indexPath", QVariantList{ 1 } },
                                  { "column", 1 },
                                  { "checkState", int( Qt::Checked ) } };
    REQUIRE( executeUiAutomation( &owner, request ).ok() );
    REQUIRE( model.item( 1, 1 )->checkState() == Qt::Checked );
    request.definitionPayload = { { "indexPath", QVariantList{ 1.5 } }, { "column", 1 } };
    REQUIRE( executeUiAutomation( &owner, request ).code == CommanderResultCode::InvalidRequest );
    request.definitionPayload = { { "indexPath", QVariantList{ 20 } }, { "editText", "invalid" } };
    REQUIRE( executeUiAutomation( &owner, request ).code == CommanderResultCode::NotFound );
    REQUIRE( model.data( model.index( 1, 1 ) ).toString() == "edited" );
}

TEST_CASE( "Queued UI actions emit triggered and toggle exactly once", "[ui][commander]" )
{
    QWidget owner;
    QAction action( &owner );
    action.setObjectName( "testAction" );
    action.setCheckable( true );
    QSignalSpy triggers( &action, &QAction::triggered );
    CommanderRequest request;
    request.action = CommanderAction::ActivateUi;
    request.objectName = "testAction";
    REQUIRE( executeUiAutomation( &owner, request ).ok() );
    REQUIRE_FALSE( action.isChecked() );
    QTest::qWait( 10 );
    REQUIRE( action.isChecked() );
    REQUIRE( triggers.count() == 1 );
}

namespace {
class GestureWidget : public QWidget {
public:
    int presses = 0;
    int releases = 0;
    int moves = 0;
    int doubles = 0;
    int wheelDelta = 0;
    void mousePressEvent( QMouseEvent* ) override
    {
        ++presses;
    }
    void mouseReleaseEvent( QMouseEvent* ) override
    {
        ++releases;
    }
    void mouseMoveEvent( QMouseEvent* ) override
    {
        ++moves;
    }
    void mouseDoubleClickEvent( QMouseEvent* ) override
    {
        ++doubles;
    }
    void wheelEvent( QWheelEvent* event ) override
    {
        wheelDelta += event->angleDelta().y();
    }
};
} // namespace
TEST_CASE( "CLI mouse gestures reach custom rendered widgets and reject invalid coordinates",
           "[ui][commander]" )
{
    GestureWidget owner;
    owner.setObjectName( "gestureWidget" );
    owner.resize( 200, 200 );
    owner.show();
    CommanderRequest request;
    request.action = CommanderAction::ActivateUi;
    request.objectName = "gestureWidget";
    request.definitionPayload
        = { { "event", "drag" }, { "x", 10 }, { "y", 10 }, { "toX", 150 }, { "toY", 100 } };
    REQUIRE( executeUiAutomation( &owner, request ).ok() );
    QTest::qWait( 10 );
    REQUIRE( owner.presses == 1 );
    REQUIRE( owner.moves == 5 );
    REQUIRE( owner.releases == 1 );
    request.definitionPayload = { { "event", "wheel" }, { "delta", -120 } };
    REQUIRE( executeUiAutomation( &owner, request ).ok() );
    QTest::qWait( 10 );
    REQUIRE( owner.wheelDelta == -120 );
    request.definitionPayload = { { "event", "double_click" } };
    REQUIRE( executeUiAutomation( &owner, request ).ok() );
    QTest::qWait( 10 );
    REQUIRE( owner.doubles == 1 );
    request.definitionPayload = { { "event", "click" }, { "x", -1 } };
    REQUIRE( executeUiAutomation( &owner, request ).code == CommanderResultCode::InvalidRequest );
    request.definitionPayload = { { "event", "drag" } };
    REQUIRE( executeUiAutomation( &owner, request ).code == CommanderResultCode::InvalidRequest );
}
