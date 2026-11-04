// SPDX-License-Identifier: GPL-3.0-or-later
#include "previewdecodersettings.h"
#include "previewmanager.h"
#include "previewservice.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QCoreApplication>
#include <QSpinBox>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>
#include <QTreeWidget>
#include <catch2/catch.hpp>
#ifndef Q_OS_MACOS
#include "mainwindow.h"
#include "sessioninfo.h"
#include <QAction>
#include <QMenu>
#include <QToolBar>
#endif
#include "previewdecodersettingsdialog.h"
#include "previewmessagetab.h"

TEST_CASE( "Preview exposes independent decoder modes and cancellation", "[ui][previewdecoderui]" )
{
    PreviewMessageTab tab( "01020304", "Auto", 1 );
    auto* mode = tab.findChild<QComboBox*>( "previewDecoderMode" );
    REQUIRE( mode );
    CHECK( mode->findData( "auto" ) >= 0 );
    CHECK( mode->findData( "json" ) >= 0 );
    CHECK( mode->findData( "tshark" ) >= 0 );
    REQUIRE( tab.findChild<QPushButton*>( "previewRun" ) );
    REQUIRE( tab.findChild<QPushButton*>( "previewCancel" ) );
    REQUIRE( tab.findChild<QTreeWidget*>( "previewFields" ) );
    mode->setCurrentIndex( mode->findData( "tshark" ) );
    tab.findChild<QPushButton*>( "previewRun" )->click();
    QTest::mouseClick( tab.findChild<QPushButton*>( "previewCancel" ), Qt::LeftButton );
    CHECK( tab.findChild<QLabel*>( "previewStatus" )
               ->text()
               .contains( "cancel", Qt::CaseInsensitive ) );
}

namespace {
struct RestoreDecoder {
    QVariantMap config = PreviewDecoderSettings::instance().configuration();
    ~RestoreDecoder()
    {
        PreviewDecoderSettings::instance().setConfiguration( config );
    }
};
struct RestorePreviews {
    QByteArray bytes = PreviewRepository::serialize( PreviewManager::instance().all(),
                                                     PreviewManager::instance().blocks() );
    ~RestorePreviews()
    {
        auto& manager = PreviewManager::instance();
        manager.clearAll();
        QTemporaryFile file;
        if ( file.open() && file.write( bytes ) == bytes.size() && file.flush() )
            manager.importFromFile( file.fileName() );
    }
};
void loadCarrier()
{
    QTemporaryFile file;
    REQUIRE( file.open() );
    const QByteArray definition
        = R"({"version":1,"previews":[{"name":"Carrier","regex":"^prefix:(?<bytes>.*)$","bufferCapture":"bytes","type":"hexString","fields":[{"name":"Byte","type":"uint8","offset":0}]}]})";
    REQUIRE( file.write( definition ) == definition.size() );
    REQUIRE( file.flush() );
    PreviewManager::instance().clearAll();
    REQUIRE( PreviewManager::instance().importFromFile( file.fileName() ).ok );
}
} // namespace

TEST_CASE( "Preview dialog rejects invalid settings atomically", "[ui][previewdecoderui]" )
{
    RestoreDecoder restore;
    const auto revision = PreviewDecoderSettings::instance().revision();
    PreviewDecoderSettingsDialog dialog;
    dialog.findChild<QLineEdit*>( "decoderDefaultDissector" )->setText( "bad\n" );
    dialog.findChild<QDialogButtonBox*>()->button( QDialogButtonBox::Apply )->click();
    CHECK( PreviewDecoderSettings::instance().revision() == revision );
    CHECK( dialog.findChild<QLabel*>( "decoderValidationError" )
               ->text()
               .contains( "invalid_dissector" ) );
}

TEST_CASE( "JSON pane uses supervised carrier selection and rejects stale delivery",
           "[ui][previewdecoderui]" )
{
    RestoreDecoder restore;
    RestorePreviews definitions;
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration( {} ) );
    loadCarrier();
    PreviewMessageTab tab( "prefix:0102", "Auto", 1 );
    REQUIRE( QTest::qWaitFor( [ & ] { return !tab.result().isEmpty(); }, 10000 ) );
    CHECK( tab.result().value( "backend" ) == "json_preview" );
    CHECK( tab.result().value( "previewName" ) == "Carrier" );
    CHECK( tab.findChild<QTreeWidget*>( "previewFields" )->columnCount() == 5 );
    tab.findChild<QPushButton*>( "previewRun" )->click();
    tab.findChild<QComboBox*>( "previewDecoderMode" )->setCurrentIndex( 2 );
    QTest::qWait( 100 );
    CHECK( tab.result().isEmpty() );
    CHECK( tab.findChild<QLabel*>( "previewStatus" )->text().contains( "Options changed" ) );
    CHECK( PreviewService::tooltipHtml( { { "backend", "json_preview" },
                                          { "available", false },
                                          { "reason", "no_match" },
                                          { "fallbackReason", "no_applicable_decoder" } } )
               .isEmpty() );
    const auto tooltip = PreviewService::tooltipHtml( { { "backend", "json_preview" },
                                                        { "available", true },
                                                        { "fallbackReason", "<unsafe>" } } );
    CHECK( tooltip.contains( "JSON preview" ) );
    CHECK( tooltip.contains( "&lt;unsafe&gt;" ) );
}

TEST_CASE( "Incomplete assembly is explicit in the manual pane", "[ui][previewdecoderui]" )
{
    PreparedPreview prepared;
    prepared.text = "0102";
    prepared.assembly
        = { { "protocol", "EHCI" }, { "state", "incomplete" }, { "reason", "more_data_required" } };
    PreviewMessageTab tab( prepared, "Auto", 1 );
    CHECK( tab.result().value( "state" ) == "incomplete" );
    CHECK_FALSE( tab.result().value( "complete" ).toBool() );
    CHECK_FALSE( tab.result().value( "available" ).toBool() );
}

TEST_CASE( "Real TShark Lua preview renders direct and mapped fields",
           "[.][previewdecoderengineui]" )
{
    REQUIRE_FALSE( qEnvironmentVariable( "CILOGG_TSHARK" ).isEmpty() );
    REQUIRE_FALSE( qEnvironmentVariable( "CILOGG_TEXT2PCAP" ).isEmpty() );
    const auto lua = qEnvironmentVariable( "CILOGG_PREVIEW_PUBLIC_LUA" );
    REQUIRE_FALSE( lua.isEmpty() );
    RestoreDecoder restore;
    RestorePreviews definitions;
    loadCarrier();
    const QVariantMap route{ { "dissector", "cilogg_md01_probe" } };
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration(
        { { "enabled", true },
          { "allowExternalLua", true },
          { "tshark", qEnvironmentVariable( "CILOGG_TSHARK" ) },
          { "text2pcap", qEnvironmentVariable( "CILOGG_TEXT2PCAP" ) },
          { "lua", QVariantList{ lua } },
          { "inputEncoding", "hex" },
          { "previewRoutes", QVariantMap{ { "Carrier", route } } },
          { "protocolRoutes", QVariantMap{ { "EHCI", route } } } } ) );
    PreparedPreview prepared;
    prepared.text = "01020304";
    prepared.assembly = { { "protocol", "EHCI" }, { "state", "complete" } };
    PreviewMessageTab direct( prepared, "Auto", 1 );
    direct.resize( 1100, 650 );
    direct.show();
    REQUIRE( QTest::qWaitFor( [ & ] { return !direct.result().isEmpty(); }, 15000 ) );
    INFO( direct.findChild<QLabel*>( "previewStatus" )->text().toStdString() );
    REQUIRE( direct.result().value( "available" ).toBool() );
    CHECK( direct.result().value( "backend" ) == "tshark_lua" );
    auto* tree = direct.findChild<QTreeWidget*>( "previewFields" );
    REQUIRE( tree->topLevelItemCount() > 0 );
    CHECK( tree->headerItem()->text( 3 ).contains( "decoded buffer" ) );
    QVector<QTreeWidgetItem*> ordered;
    std::function<void( QTreeWidgetItem* )> append = [ & ]( QTreeWidgetItem* parent ) {
        for ( int i = 0; i < parent->childCount(); ++i ) {
            auto* child = parent->child( i );
            ordered.push_back( child );
            append( child );
        }
    };
    append( tree->invisibleRootItem() );
    const auto fields = direct.result().value( "fields" ).toList();
    REQUIRE( ordered.size() == fields.size() );
    int repeated = 0;
    for ( int i = 0; i < ordered.size(); ++i ) {
        const auto field = fields[ i ].toMap();
        const auto columns = field.value( "columns" ).toList();
        for ( int column = 0; column < 5; ++column )
            CHECK( ordered[ i ]->text( column ) == columns[ column ].toString() );
        CHECK_FALSE( ordered[ i ]->data( 0, Qt::UserRole ).isValid() );
        if ( columns[ 0 ] == "cilogg_md01_probe.item" )
            ++repeated;
    }
    CHECK( repeated == 4 );
    direct.findChild<QComboBox*>( "previewDecoderMode" )->setCurrentIndex( 1 );
    direct.findChild<QLineEdit*>( "previewDissector" )->setText( "cilogg_md01_probe" );
    auto* encoding = direct.findChild<QComboBox*>( "previewInputEncoding" );
    encoding->setCurrentIndex( encoding->findData( "hex" ) );
    direct.findChild<QPushButton*>( "previewRun" )->click();
    REQUIRE( QTest::qWaitFor( [ & ] { return !direct.result().isEmpty(); }, 15000 ) );
    REQUIRE( direct.result().value( "available" ).toBool() );
    CHECK( direct.result().value( "backend" ) == "tshark_lua" );
    CHECK( PreviewDecoderSettings::instance()
               .configuration()
               .value( "defaultDissector" )
               .toString()
               .isEmpty() );
    const auto capture = qEnvironmentVariable( "CILOGG_PREVIEW_PANE_SCREENSHOT" );
    if ( !capture.isEmpty() )
        REQUIRE( direct.grab().save( capture ) );
    PreviewMessageTab mapped( "prefix:01020304", "Carrier", 2 );
    REQUIRE( QTest::qWaitFor( [ & ] { return !mapped.result().isEmpty(); }, 15000 ) );
    REQUIRE( mapped.result().value( "available" ).toBool() );
    CHECK( mapped.result().value( "backend" ) == "tshark_lua" );
    auto* modes = mapped.findChild<QComboBox*>( "previewDecoderMode" );
    modes->setCurrentIndex( modes->findData( "json" ) );
    mapped.findChild<QPushButton*>( "previewRun" )->click();
    REQUIRE( QTest::qWaitFor( [ & ] { return !mapped.result().isEmpty(); }, 10000 ) );
    CHECK( mapped.result().value( "backend" ) == "json_preview" );
}

#ifndef Q_OS_MACOS
TEST_CASE( "Decoder commands share trusted settings and visible menu toolbar dialog",
           "[ui][previewdecoderui][previewdecoderwindow]" )
{
    RestoreDecoder restore;
    auto session = std::make_shared<Session>();
    auto& sessions = SessionInfo::getSynced();
    const QString id = "DecoderSettingsIntegration";
    sessions.add( id );
    sessions.save();
    MainWindow window( WindowSession{ session, id, 0 } );
    auto* action = window.findChild<QAction*>( "previewDecoderSettingsAction" );
    REQUIRE( action );
    CHECK_FALSE( action->icon().isNull() );
    CHECK_FALSE( action->toolTip().isEmpty() );
    bool menu = false;
    bool toolbar = false;
    for ( auto* candidate : window.findChildren<QMenu*>() )
        menu = menu || candidate->actions().contains( action );
    for ( auto* candidate : window.findChildren<QToolBar*>() )
        toolbar = toolbar || candidate->actions().contains( action );
    CHECK( menu );
    CHECK( toolbar );
    CommanderRequest request;
    request.action = CommanderAction::SetPreviewDecoder;
    request.analysisArguments = { { "enabled", false }, { "defaultDissector", "data" } };
    auto result = window.executeCommanderRequest( request );
    REQUIRE( result.ok() );
    const auto config = result.payload.value( "configuration" );
    const auto revision = result.payload.value( "revision" );
    CHECK( result.payload.value( "status" ) == "disabled" );
    request.analysisArguments = { { "enabled", "true" } };
    CHECK_FALSE( window.executeCommanderRequest( request ).ok() );
    request.action = CommanderAction::GetPreviewDecoder;
    request.analysisArguments.clear();
    result = window.executeCommanderRequest( request );
    CHECK( result.payload.value( "configuration" ) == config );
    CHECK( result.payload.value( "revision" ) == revision );
    request.action = CommanderAction::ShowPreviewDecoderSettings;
    REQUIRE( window.executeCommanderRequest( request ).ok() );
    auto* dialog
        = window.findChild<PreviewDecoderSettingsDialog*>( "previewDecoderSettingsDialog" );
    REQUIRE( dialog );
    CHECK( dialog->isVisible() );
    CHECK_FALSE( dialog->isModal() );
    CHECK( window.executeCommanderRequest( request ).ok() );
    CHECK( window.findChildren<PreviewDecoderSettingsDialog*>().size() == 1 );
    dialog->hide();
    request.action = CommanderAction::PreviewMessage;
    for ( const auto& args :
          { QVariantMap{ { "text", "0102" }, { "decoder", true } },
            QVariantMap{ { "text", "0102" }, { "inputEncoding", "HEX" } },
            QVariantMap{ { "text", "0102" }, { "dissector", "data\n" } },
            QVariantMap{ { "text", "0102" }, { "decoder", "json" }, { "dissector", "data" } },
            QVariantMap{ { "text", "0102" }, { "timeoutMs", true } },
            QVariantMap{ { "text", "0102" }, { "lua", QVariantList{} } },
            QVariantMap{ { "text", "0102" }, { "requestRevision", 2 } } } ) {
        request.analysisArguments = args;
        CHECK( window.executeCommanderRequest( request ).code
               == CommanderResultCode::InvalidRequest );
    }
    request.analysisArguments = { { "text", "0102" },
                                  { "decoder", "tshark" },
                                  { "requestRevision", "9007199254740993" } };
    result = window.executeCommanderRequest( request );
    CHECK( result.code == CommanderResultCode::ExecutionFailed );
    CHECK( result.payload.value( "reason" ) == "decoder_disabled" );
    CHECK( result.payload.value( "requestRevision" ) == "9007199254740993" );
    request.analysisArguments
        = { { "text", QString( 70000, QChar( '\n' ) ) }, { "decoder", "json" } };
    result = window.executeCommanderRequest( request );
    CHECK( result.code == CommanderResultCode::ExecutionFailed );
    CHECK( result.payload.value( "reason" ) == "input_limit" );
    CHECK( result.payload.value( "inputTextOmitted" ).toBool() );
    CHECK_FALSE( result.payload.contains( "inputText" ) );
    CHECK( QJsonDocument::fromVariant( result.payload ).toJson( QJsonDocument::Compact ).size()
           < 4096 );
}
#endif

TEST_CASE( "Decoder settings use structured controls", "[ui][previewdecoderui]" )
{
    PreviewDecoderSettingsDialog dialog;
    CHECK_FALSE( dialog.isModal() );
    CHECK( dialog.findChild<QWidget*>( "decoderEnabled" ) );
    CHECK( dialog.findChild<QWidget*>( "decoderAllowExternalLua" ) );
    CHECK( dialog.findChild<QWidget*>( "decoderPreviewRoutes" ) );
    CHECK( dialog.findChild<QWidget*>( "decoderProtocolRoutes" ) );
}

TEST_CASE( "Automatic assembly route forwards the visible budget across decoder modes",
           "[ui][previewdecoderui][previewbudget]" )
{
    RestoreDecoder restore;
    RestorePreviews definitions;
    const auto savedPython = qgetenv( "CILOGG_SCRIPT_PYTHON" );
    const auto savedMode = qgetenv( "CILOGG_PREVIEW_FIXTURE_MODE" );
    const auto savedPid = qgetenv( "CILOGG_PREVIEW_FIXTURE_PID" );
    struct RestoreEnv {
        QByteArray python, mode, pid;
        ~RestoreEnv()
        {
            for ( const auto& item : { qMakePair( "CILOGG_SCRIPT_PYTHON", python ),
                                       qMakePair( "CILOGG_PREVIEW_FIXTURE_MODE", mode ),
                                       qMakePair( "CILOGG_PREVIEW_FIXTURE_PID", pid ) } ) {
                if ( item.second.isNull() )
                    qunsetenv( item.first );
                else
                    qputenv( item.first, item.second );
            }
        }
    } environment{ savedPython, savedMode, savedPid };
    const auto fixture = QString::fromUtf8( PREVIEW_DECODER_FIXTURE_PATH );
    REQUIRE( QFile::exists( fixture ) );
    qputenv( "CILOGG_SCRIPT_PYTHON", fixture.toUtf8() );
    qputenv( "CILOGG_PREVIEW_FIXTURE_MODE", "budget" );
    qputenv( "CILOGG_PREVIEW_FIXTURE_PID",
             QByteArray::number( QCoreApplication::applicationPid() ) );
    PreviewManager::instance().clearAll();
    REQUIRE( PreviewDecoderSettings::instance().setConfiguration(
        { { "enabled", true },
          { "tshark", QCoreApplication::applicationFilePath() },
          { "text2pcap", QCoreApplication::applicationFilePath() },
          { "protocolRoutes",
            QVariantMap{ { "EHCI", QVariantMap{ { "dissector", "data" },
                                                { "inputEncoding", "hex" } } } } } } ) );
    PreparedPreview prepared;
    prepared.text = "01020304";
    prepared.assembly = { { "protocol", "EHCI" }, { "state", "complete" } };
    PreviewMessageTab tab( prepared, "Auto", 1 );
    auto* mode = tab.findChild<QComboBox*>( "previewDecoderMode" );
    auto* timeout = tab.findChild<QSpinBox*>( "previewTimeoutMs" );
    REQUIRE( mode );
    REQUIRE( timeout );
    CHECK( timeout->value() == 3000 );
    CHECK( timeout->maximum() == 10000 );
    CHECK( timeout->isEnabled() );
    mode->setCurrentIndex( mode->findData( "tshark" ) );
    timeout->setValue( 10000 );
    mode->setCurrentIndex( mode->findData( "json" ) );
    CHECK( timeout->isEnabled() );
    CHECK( timeout->value() == 10000 );
    mode->setCurrentIndex( mode->findData( "auto" ) );
    CHECK( timeout->isEnabled() );
    CHECK_FALSE( tab.findChild<QLineEdit*>( "previewDissector" )->isEnabled() );
    CHECK_FALSE( tab.findChild<QComboBox*>( "previewInputEncoding" )->isEnabled() );
    tab.findChild<QPushButton*>( "previewRun" )->click();
    REQUIRE( QTest::qWaitFor( [ & ] { return !tab.result().isEmpty(); }, 10000 ) );
    INFO( tab.result().value( "reason" ).toString().toStdString() );
    REQUIRE( tab.result().value( "available" ).toBool() );
    CHECK( tab.result().value( "backend" ) == "tshark_lua" );
    CHECK( tab.result().value( "assembly" ).toMap().value( "protocol" ) == "EHCI" );
    CHECK_FALSE( tab.result().contains( "fallbackReason" ) );
    const auto fields = tab.result().value( "fields" ).toList();
    REQUIRE( fields.size() == 1 );
    const auto forwarded = fields.front().toMap().value( "columns" ).toList().at( 1 ).toInt();
    // The worker receives the displayed total minus selection time, never the old 3000.
    CHECK( forwarded > 3000 );
    CHECK( forwarded <= timeout->value() );
    CHECK( forwarded + tab.result().value( "elapsedMs" ).toLongLong() >= timeout->value() );
}
