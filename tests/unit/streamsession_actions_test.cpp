#include <catch2/catch.hpp>

#include <memory>

#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include "../streamsession_testaccess.h"
#include "actionruntime.h"
#include "actionsmanager.h"

namespace {
class ScopedActionsState {
public:
    ScopedActionsState()
        : actions_( ActionsManager::instance().actions() )
        , responses_( ActionsManager::instance().responses() )
        , automatic_( ActionsManager::instance().autoResponsesEnabled() )
    {
    }

    ~ScopedActionsState()
    {
        const auto restored
            = ActionsManager::instance().importFromDefinitions( actions_, responses_ );
        CHECK( restored.ok );
        ActionsManager::instance().setAutoResponsesEnabled( automatic_ );
    }

private:
    QVector<ActionDefinition> actions_;
    QVector<ResponseDefinition> responses_;
    bool automatic_;
};

SerialCaptureSettings settingsFor( const QString& portName, bool enabled = true )
{
    SerialCaptureSettings settings;
    settings.portName = portName;
    settings.filePath = QStringLiteral( "logical-actions-test.log" );
    settings.useForActions = enabled;
    return settings;
}

ActionDefinition testAction( int id )
{
    ActionDefinition action;
    action.id = id;
    action.name = QStringLiteral( "Logical action %1" ).arg( id );
    action.sequence.value = QStringLiteral( "PING" );
    return action;
}

ResponseDefinition testResponse( int id )
{
    ResponseDefinition response;
    response.id = id;
    response.name = QStringLiteral( "Logical response %1" ).arg( id );
    response.match.value = QStringLiteral( "READY" );
    return response;
}

bool waitForActionTimers()
{
    // Negative assertions need event-loop progress, not an immediately true
    // zero-count predicate. This timer is later than every queued action in
    // these fixtures (at most 40 ms), with room for slow CI event delivery.
    QTimer sentinel;
    sentinel.setSingleShot( true );
    QSignalSpy elapsed( &sentinel, &QTimer::timeout );
    sentinel.start( 150 );
    return QTest::qWaitFor( [&elapsed] { return !elapsed.isEmpty(); }, 5000 );
}
} // namespace

TEST_CASE( "Actions setting is authoritative idempotent and persisted per session",
           "[serial][session][actions]" )
{
    const auto initiallyEnabled = GENERATE( false, true );
    StreamSession session(
        settingsFor( QStringLiteral( "TEST_ACTIONS_STATE" ), initiallyEnabled ) );
    QSignalSpy changes( &session, &StreamSession::actionsEnabledChanged );
    const auto initialGeneration = session.actionsGeneration();
    CHECK( session.actionsEnabled() == initiallyEnabled );
    session.setActionsEnabled( initiallyEnabled );
    CHECK( changes.isEmpty() );
    CHECK( session.actionsGeneration() == initialGeneration );

    session.setActionsEnabled( !initiallyEnabled );
    REQUIRE( changes.size() == 1 );
    CHECK( changes.front().front().toBool() == !initiallyEnabled );
    CHECK( session.actionsGeneration() == initialGeneration + 1 );
    CHECK( session.captureSettings().useForActions == !initiallyEnabled );
    const auto restored = deserializeSerialCaptureSettings(
        serializeSerialCaptureSettings( session.captureSettings() ) );
    REQUIRE( restored.has_value() );
    CHECK( restored->useForActions == !initiallyEnabled );

    session.setActionsEnabled( !initiallyEnabled );
    CHECK( changes.size() == 1 );
    StreamSessionTestAccess::openConnection( session );
    session.closeConnection();
    CHECK( session.actionsGeneration() == initialGeneration + 2 );
    CHECK( session.actionsEnabled() == !initiallyEnabled );
    session.closeConnection();
    CHECK( session.actionsGeneration() == initialGeneration + 2 );
    CHECK( changes.size() == 1 );
}

TEST_CASE( "New tabs initialize automatic responses from their Actions choice",
           "[serial][session][actions][auto-responses]" )
{
    const auto actionsEnabled = GENERATE( false, true );
    StreamSession session( settingsFor( "TEST_INITIAL_ACTIONS", actionsEnabled ) );
    CHECK( session.actionsEnabled() == actionsEnabled );
    CHECK( session.autoResponsesEnabled() == actionsEnabled );
}

TEST_CASE( "Actions and automatic responses transitions publish consistent state",
           "[serial][session][actions][auto-responses]" )
{
    StreamSession session( settingsFor( "TEST_COUPLED_ACTIONS", false ) );
    QSignalSpy actions( &session, &StreamSession::actionsEnabledChanged );
    QSignalSpy responses( &session, &StreamSession::autoResponsesEnabledChanged );
    const auto checkState = [&] {
        CHECK( ( !session.autoResponsesEnabled() || session.actionsEnabled() ) );
        CHECK( session.captureSettings().useForActions == session.actionsEnabled() );
        CHECK( session.captureSettings().autoResponsesEnabled == session.autoResponsesEnabled() );
    };
    QObject::connect( &session, &StreamSession::actionsEnabledChanged, &session, checkState );
    QObject::connect( &session, &StreamSession::autoResponsesEnabledChanged, &session, checkState );
    const auto manualGeneration = session.actionsGeneration();
    const auto automaticGeneration = session.autoResponsesGeneration();

    session.setActionsEnabled( true );
    CHECK( session.actionsEnabled() );
    CHECK_FALSE( session.autoResponsesEnabled() );
    session.setAutoResponsesEnabled( true );
    CHECK( session.actionsEnabled() );
    CHECK( session.autoResponsesEnabled() );
    session.setActionsEnabled( false );
    CHECK_FALSE( session.actionsEnabled() );
    CHECK_FALSE( session.autoResponsesEnabled() );
    session.setAutoResponsesEnabled( true );
    CHECK( session.actionsEnabled() );
    CHECK( session.autoResponsesEnabled() );
    session.setAutoResponsesEnabled( false );
    CHECK( session.actionsEnabled() );
    CHECK_FALSE( session.autoResponsesEnabled() );
    CHECK( session.actionsGeneration() == manualGeneration + 3 );
    CHECK( session.autoResponsesGeneration() == automaticGeneration + 5 );
    CHECK( actions.count() == 3 );
    CHECK( responses.count() == 4 );

    session.setActionsEnabled( true );
    session.setAutoResponsesEnabled( false );
    CHECK( session.actionsGeneration() == manualGeneration + 3 );
    CHECK( session.autoResponsesGeneration() == automaticGeneration + 5 );
    CHECK( actions.count() == 3 );
    CHECK( responses.count() == 4 );
}

TEST_CASE( "Coupled notifications tolerate reentrant changes and session deletion",
           "[serial][session][actions][auto-responses]" )
{
    SECTION( "A reentrant Actions observer supersedes the pending response notification" )
    {
        StreamSession session( settingsFor( "TEST_REENTRANT_FLAGS", false ) );
        QSignalSpy responses( &session, &StreamSession::autoResponsesEnabledChanged );
        QObject::connect( &session, &StreamSession::actionsEnabledChanged, &session,
                          [&]( bool enabled ) {
                              if ( enabled ) {
                                  CHECK( session.autoResponsesEnabled() );
                                  session.setAutoResponsesEnabled( false );
                              }
                          } );
        session.setAutoResponsesEnabled( true );
        CHECK( session.actionsEnabled() );
        CHECK_FALSE( session.autoResponsesEnabled() );
        REQUIRE( responses.count() == 1 );
        CHECK_FALSE( responses.front().front().toBool() );
    }
    SECTION( "Reentrant off and on does not duplicate the pending response notification" )
    {
        StreamSession session( settingsFor( "TEST_REENTRANT_ROUNDTRIP", false ) );
        QSignalSpy responses( &session, &StreamSession::autoResponsesEnabledChanged );
        QObject::connect( &session, &StreamSession::actionsEnabledChanged, &session,
                          [&]( bool enabled ) {
                              if ( enabled ) {
                                  session.setAutoResponsesEnabled( false );
                                  session.setAutoResponsesEnabled( true );
                              }
                          } );
        session.setAutoResponsesEnabled( true );
        CHECK( session.actionsEnabled() );
        CHECK( session.autoResponsesEnabled() );
        REQUIRE( responses.count() == 2 );
        CHECK_FALSE( responses.at( 0 ).front().toBool() );
        CHECK( responses.at( 1 ).front().toBool() );
    }
    SECTION( "An Actions observer may delete the session before the second notification" )
    {
        auto session = std::make_unique<StreamSession>( settingsFor( "TEST_DELETED_FLAGS", false ) );
        QSignalSpy responses( session.get(), &StreamSession::autoResponsesEnabledChanged );
        QObject::connect( session.get(), &StreamSession::actionsEnabledChanged,
                          [&] { session.reset(); } );
        session->setAutoResponsesEnabled( true );
        CHECK_FALSE( session );
        CHECK( responses.isEmpty() );
    }
}

TEST_CASE( "Automatic response counters are independent and disabled tabs still observe lines",
           "[serial][session][actions]" )
{
    ScopedActionsState restore;
    auto& manager = ActionsManager::instance();
    const auto response = testResponse( 7101 );
    REQUIRE( manager.importFromDefinitions( {}, { response } ).ok );
    manager.setAutoResponsesEnabled( true );
    StreamSession first( settingsFor( QStringLiteral( "TEST_ACTIONS_FIRST" ) ) );
    StreamSession second( settingsFor( QStringLiteral( "TEST_ACTIONS_SECOND" ) ) );
    StreamSession disabled( settingsFor( QStringLiteral( "TEST_ACTIONS_DISABLED" ), false ) );
    StreamSessionTestAccess::openConnection( first );
    StreamSessionTestAccess::openConnection( second );
    StreamSessionTestAccess::openConnection( disabled );
    QSignalSpy firstMatches( &first, &StreamSession::responseMatched );
    QSignalSpy secondMatches( &second, &StreamSession::responseMatched );
    QSignalSpy disabledMatches( &disabled, &StreamSession::responseMatched );
    QSignalSpy disabledLines( &disabled, &StreamSession::lineObserved );
    StreamSessionTestAccess::feedData( first, QByteArrayLiteral( "REA" ) );
    StreamSessionTestAccess::feedData( first, QByteArrayLiteral( "DY\r\nREADY\n" ) );
    StreamSessionTestAccess::feedData( second, QByteArrayLiteral( "READY\n" ) );
    StreamSessionTestAccess::feedData( disabled, QByteArrayLiteral( "READY\r\n" ) );
    CHECK( first.responseCounter( response.id ) == 2 );
    CHECK( second.responseCounter( response.id ) == 1 );
    CHECK( disabled.responseCounter( response.id ) == 0 );
    CHECK( firstMatches.size() == 2 );
    CHECK( secondMatches.size() == 1 );
    CHECK( disabledMatches.isEmpty() );
    REQUIRE( disabledLines.size() == 1 );
    CHECK( disabledLines.front().front().toByteArray() == QByteArrayLiteral( "READY" ) );

    manager.setAutoResponsesEnabled( false );
    StreamSessionTestAccess::feedData( first, QByteArrayLiteral( "READY\n" ) );
    CHECK( first.responseCounter( response.id ) == 2 );
    disabled.setActionsEnabled( true );
    manager.setAutoResponsesEnabled( true );
    StreamSessionTestAccess::feedData( disabled, QByteArrayLiteral( "READY\n" ) );
    CHECK( disabled.responseCounter( response.id ) == 0 );
    disabled.setAutoResponsesEnabled( true );
    StreamSessionTestAccess::feedData( disabled, QByteArrayLiteral( "READY\n" ) );
    CHECK( disabled.responseCounter( response.id ) == 1 );
    first.setActionsEnabled( false );
    CHECK( first.responseCounter( response.id ) == 2 );
    StreamSessionTestAccess::feedData( second, QByteArrayLiteral( "READY\n" ) );
    CHECK( second.responseCounter( response.id ) == 2 );
}

TEST_CASE( "Per-tab automatic response switches preserve manual actions and persisted state",
           "[serial][session][actions][auto-responses]" )
{
    ScopedActionsState restore;
    auto& manager = ActionsManager::instance();
    const auto response = testResponse( 7120 );
    REQUIRE( manager.importFromDefinitions( {}, { response } ).ok );
    manager.setAutoResponsesEnabled( true );
    StreamSession first( settingsFor( "TEST_AUTO_FIRST" ) );
    StreamSession second( settingsFor( "TEST_AUTO_SECOND" ) );
    StreamSessionTestAccess::openConnection( first );
    StreamSessionTestAccess::openConnection( second );
    QSignalSpy changes( &first, &StreamSession::autoResponsesEnabledChanged );
    QSignalSpy observed( &first, &StreamSession::lineObserved );
    const auto manualGeneration = first.actionsGeneration();
    const auto automaticGeneration = first.autoResponsesGeneration();
    CHECK( first.autoResponsesEnabled() );
    first.setAutoResponsesEnabled( true );
    CHECK( changes.isEmpty() );
    first.setAutoResponsesEnabled( false );
    CHECK( changes.size() == 1 );
    CHECK( first.actionsEnabled() );
    CHECK( first.actionsGeneration() == manualGeneration );
    CHECK( first.autoResponsesGeneration() == automaticGeneration + 1 );
    first.setAutoResponsesEnabled( false );
    CHECK( changes.size() == 1 );
    StreamSessionTestAccess::feedData( first, "READY\n" );
    StreamSessionTestAccess::feedData( second, "READY\n" );
    CHECK( observed.size() == 1 );
    CHECK( first.responseCounter( response.id ) == 0 );
    CHECK( second.responseCounter( response.id ) == 1 );
    const auto saved = serializeSerialCaptureSettings( first.captureSettings() );
    const auto restored = deserializeSerialCaptureSettings( saved );
    REQUIRE( restored );
    CHECK_FALSE( restored->autoResponsesEnabled );
    const auto legacy
        = deserializeSerialCaptureSettings( "{\"portName\":\"LEGACY\",\"useForActions\":true}" );
    REQUIRE( legacy );
    CHECK( legacy->autoResponsesEnabled );
    first.setAutoResponsesEnabled( true );
    StreamSessionTestAccess::feedData( first, "READY\n" );
    CHECK( first.responseCounter( response.id ) == 1 );
}

TEST_CASE( "Per-tab response changes invalidate buffered lineage and automatic timers only",
           "[serial][session][actions][auto-responses]" )
{
    ScopedActionsState restore;
    auto& manager = ActionsManager::instance();
    const auto response = testResponse( 7121 );
    REQUIRE( manager.importFromDefinitions( {}, { response } ).ok );
    manager.setAutoResponsesEnabled( true );
    StreamSession session( settingsFor( "TEST_AUTO_LINEAGE" ) );
    StreamSessionTestAccess::openConnection( session );
    QSignalSpy observed( &session, &StreamSession::lineObserved );
    StreamSessionTestAccess::feedData( session, "REA" );
    session.setAutoResponsesEnabled( false );
    session.setAutoResponsesEnabled( true );
    StreamSessionTestAccess::feedData( session, "DY\nREADY\n" );
    CHECK( observed.size() == 2 );
    CHECK( session.responseCounter( response.id ) == 1 );

    auto manual = testAction( 7122 );
    manual.parameters.delay = 30;
    auto automatic = testAction( 7123 );
    automatic.parameters.delay = 10;
    automatic.parameters.repeatCount = 2;
    automatic.parameters.repeatInterval = 10;
    QSignalSpy sent( &session, &StreamSession::actionSent );
    REQUIRE( sendActionDefinition( &session, manual, {}, -1, nullptr, false ) );
    REQUIRE( sendActionDefinition( &session, automatic, {}, -1, nullptr, true ) );
    session.setAutoResponsesEnabled( false );
    session.setAutoResponsesEnabled( true );
    REQUIRE( waitForActionTimers() );
    REQUIRE( sent.size() == 1 );
    CHECK( sent.front().front().toInt() == manual.id );
}

TEST_CASE( "Buffered response lines preserve text without inheriting newer Actions eligibility",
           "[serial][session][actions][response-lineage]" )
{
    ScopedActionsState restore;
    const auto transition = GENERATE( 0, 1, 2 );
    const auto fragmented = GENERATE( false, true );
    CAPTURE( transition, fragmented );
    auto& manager = ActionsManager::instance();
    const auto response = testResponse( 7112 );
    REQUIRE( manager.importFromDefinitions( {}, { response } ).ok );
    manager.setAutoResponsesEnabled( transition != 2 );
    StreamSession session(
        settingsFor( QStringLiteral( "TEST_PARTIAL_ACTIONS" ), transition != 0 ) );
    StreamSessionTestAccess::openConnection( session );
    QSignalSpy observed( &session, &StreamSession::lineObserved );
    QSignalSpy matched( &session, &StreamSession::responseMatched );
    StreamSessionTestAccess::feedData( session, QByteArrayLiteral( "R" ) );
    StreamSessionTestAccess::feedData( session, fragmented ? QByteArrayLiteral( "E" )
                                                           : QByteArrayLiteral( "EA" ) );
    if ( transition == 1 ) {
        session.setActionsEnabled( false );
    }
    session.setActionsEnabled( true );
    session.setAutoResponsesEnabled( true );
    manager.setAutoResponsesEnabled( true );
    if ( fragmented ) {
        StreamSessionTestAccess::feedData( session, QByteArrayLiteral( "A" ) );
    }
    StreamSessionTestAccess::feedData( session, QByteArrayLiteral( "DY\r\nREADY\nRE" ) );
    REQUIRE( observed.size() == 2 );
    CHECK( observed.at( 0 ).front().toByteArray() == QByteArrayLiteral( "READY" ) );
    CHECK( observed.at( 1 ).front().toByteArray() == QByteArrayLiteral( "READY" ) );
    CHECK( matched.size() == 1 );
    CHECK( session.responseCounter( response.id ) == 1 );

    // The remaining partial line belongs to the completing chunk, rather than
    // the stale first line or the generation in which its newline later arrives.
    session.setActionsEnabled( false );
    session.setActionsEnabled( true );
    session.setAutoResponsesEnabled( true );
    StreamSessionTestAccess::feedData( session, QByteArrayLiteral( "ADY\nREADY\n" ) );
    REQUIRE( observed.size() == 4 );
    CHECK( observed.at( 2 ).front().toByteArray() == QByteArrayLiteral( "READY" ) );
    CHECK( observed.at( 3 ).front().toByteArray() == QByteArrayLiteral( "READY" ) );
    CHECK( matched.size() == 2 );
    CHECK( session.responseCounter( response.id ) == 2 );
}

TEST_CASE( "A globally disabled middle chunk makes only its partial response line ineligible",
           "[serial][session][actions][response-lineage]" )
{
    ScopedActionsState restore;
    auto& manager = ActionsManager::instance();
    const auto response = testResponse( 7113 );
    REQUIRE( manager.importFromDefinitions( {}, { response } ).ok );
    manager.setAutoResponsesEnabled( true );
    StreamSession session( settingsFor( QStringLiteral( "TEST_PARTIAL_GLOBAL_ACTIONS" ) ) );
    StreamSessionTestAccess::openConnection( session );
    QSignalSpy observed( &session, &StreamSession::lineObserved );
    StreamSessionTestAccess::feedData( session, QByteArrayLiteral( "R" ) );
    manager.setAutoResponsesEnabled( false );
    StreamSessionTestAccess::feedData( session, QByteArrayLiteral( "EA" ) );
    manager.setAutoResponsesEnabled( true );
    StreamSessionTestAccess::feedData( session, QByteArrayLiteral( "DY\nREADY\n" ) );
    REQUIRE( observed.size() == 2 );
    CHECK( observed.at( 0 ).front().toByteArray() == QByteArrayLiteral( "READY" ) );
    CHECK( observed.at( 1 ).front().toByteArray() == QByteArrayLiteral( "READY" ) );
    CHECK( session.responseCounter( response.id ) == 1 );
}

TEST_CASE( "Reentrant line input retains the lineage and order of already buffered lines",
           "[serial][session][actions][response-lineage]" )
{
    ScopedActionsState restore;
    auto& manager = ActionsManager::instance();
    const auto response = testResponse( 7114 );
    REQUIRE( manager.importFromDefinitions( {}, { response } ).ok );
    manager.setAutoResponsesEnabled( true );
    StreamSession session( settingsFor( QStringLiteral( "TEST_REENTRANT_PARTIAL_ACTIONS" ) ) );
    StreamSessionTestAccess::openConnection( session );
    QSignalSpy observed( &session, &StreamSession::lineObserved );
    bool completed = false;
    QObject::connect( &session, &StreamSession::lineObserved, &session, [ & ] {
        if ( completed ) {
            return;
        }
        completed = true;
        session.setActionsEnabled( false );
        session.setActionsEnabled( true );
        session.setAutoResponsesEnabled( true );
        StreamSessionTestAccess::feedData( session, QByteArrayLiteral( "ADY\nREADY\n" ) );
    } );
    StreamSessionTestAccess::feedData( session, QByteArrayLiteral( "READY\nREADY\nRE" ) );
    REQUIRE( completed );
    REQUIRE( observed.size() == 4 );
    for ( const auto& line : observed ) {
        CHECK( line.front().toByteArray() == QByteArrayLiteral( "READY" ) );
    }
    CHECK( session.responseCounter( response.id ) == 1 );
    StreamSessionTestAccess::feedData( session, QByteArrayLiteral( "READY\n" ) );
    CHECK( session.responseCounter( response.id ) == 2 );
}

TEST_CASE( "Reentrant data observers cannot make an older completing chunk eligible",
           "[serial][session][actions][response-lineage]" )
{
    ScopedActionsState restore;
    auto& manager = ActionsManager::instance();
    const auto response = testResponse( 7115 );
    REQUIRE( manager.importFromDefinitions( {}, { response } ).ok );
    manager.setAutoResponsesEnabled( true );
    StreamSession session( settingsFor( QStringLiteral( "TEST_REENTRANT_DATA_ACTIONS" ) ) );
    StreamSessionTestAccess::openConnection( session );
    QSignalSpy observed( &session, &StreamSession::lineObserved );
    bool completed = false;
    QObject::connect( &session, &StreamSession::dataObserved, &session, [ & ] {
        if ( completed ) {
            return;
        }
        completed = true;
        session.setActionsEnabled( false );
        session.setActionsEnabled( true );
        session.setAutoResponsesEnabled( true );
        StreamSessionTestAccess::feedData( session, QByteArrayLiteral( "RE" ) );
    } );
    StreamSessionTestAccess::feedData( session, QByteArrayLiteral( "ADY\nREADY\n" ) );
    REQUIRE( completed );
    REQUIRE( observed.size() == 2 );
    CHECK( observed.at( 0 ).front().toByteArray() == QByteArrayLiteral( "READY" ) );
    CHECK( observed.at( 1 ).front().toByteArray() == QByteArrayLiteral( "READY" ) );
    CHECK( session.responseCounter( response.id ) == 0 );
    StreamSessionTestAccess::feedData( session, QByteArrayLiteral( "READY\n" ) );
    CHECK( session.responseCounter( response.id ) == 1 );
}

TEST_CASE( "Delayed actions validate the tab generation and automatic eligibility",
           "[serial][session][actions][actionruntime]" )
{
    ScopedActionsState restore;
    const auto automatic = GENERATE( false, true );
    const auto change = GENERATE( 0, 1, 2, 3, 4 );
    CAPTURE( automatic, change );
    auto& manager = ActionsManager::instance();
    manager.setAutoResponsesEnabled( true );
    StreamSession session( settingsFor( QStringLiteral( "TEST_DELAYED_ACTIONS" ) ) );
    StreamSessionTestAccess::openConnection( session );
    auto action = testAction( 7102 );
    action.parameters.delay = 10;
    action.parameters.repeatCount = 3;
    action.parameters.repeatInterval = 10;
    QSignalSpy dispatched( &session, &StreamSession::actionSent );
    QSignalSpy transmitted( &session, &StreamSession::dataTransmitted );
    QString error;
    REQUIRE( sendActionDefinition( &session, action, {}, -1, &error, automatic ) );
    REQUIRE( error.isEmpty() );
    if ( change == 1 ) {
        session.setActionsEnabled( false );
        session.setActionsEnabled( true );
    }
    else if ( change == 2 ) {
        session.closeConnection();
        StreamSessionTestAccess::openConnection( session );
    }
    else if ( change == 3 ) {
        manager.setAutoResponsesEnabled( false );
    }
    if ( change == 4 ) {
        session.setAutoResponsesEnabled( false );
        session.setAutoResponsesEnabled( true );
    }
    const bool cancelled
        = change == 1 || change == 2 || ( automatic && ( change == 3 || change == 4 ) );
    if ( cancelled ) {
        REQUIRE( waitForActionTimers() );
    }
    else {
        REQUIRE( QTest::qWaitFor( [&dispatched] { return dispatched.size() >= 3; }, 5000 ) );
    }
    CHECK( dispatched.size() == ( cancelled ? 0 : 3 ) );
    // An actionSent signal proves logical dispatch only; there is no transport worker.
    CHECK( transmitted.isEmpty() );
    CHECK( session.isConnectionOpen() );
}

TEST_CASE( "Explicit manual sends remain available on an unmarked tab",
           "[serial][session][actions][actionruntime]" )
{
    ScopedActionsState restore;
    ActionsManager::instance().setAutoResponsesEnabled( false );
    StreamSession session( settingsFor( QStringLiteral( "TEST_MANUAL_ACTIONS" ), false ) );
    StreamSessionTestAccess::openConnection( session );
    auto action = testAction( 7103 );
    QSignalSpy dispatched( &session, &StreamSession::actionSent );
    QString error;
    REQUIRE( sendActionDefinition( &session, action, {}, -1, &error ) );
    CHECK( dispatched.size() == 1 );
    action.parameters.delay = 10;
    REQUIRE( sendActionDefinition( &session, action, {}, -1, &error ) );
    REQUIRE( QTest::qWaitFor( [&dispatched] { return dispatched.size() >= 2; }, 5000 ) );
    CHECK( dispatched.size() == 2 );
    CHECK_FALSE( sendActionDefinition( &session, action, {}, -1, &error, true ) );
    CHECK_FALSE( error.isEmpty() );
    CHECK( dispatched.size() == 2 );
}

TEST_CASE( "Disabling a tab cancels remaining repeats after an immediate dispatch",
           "[serial][session][actions][actionruntime]" )
{
    ScopedActionsState restore;
    ActionsManager::instance().setAutoResponsesEnabled( true );
    const auto automatic = GENERATE( false, true );
    StreamSession session( settingsFor( QStringLiteral( "TEST_ACTION_REPEATS" ) ) );
    StreamSessionTestAccess::openConnection( session );
    auto action = testAction( 7104 );
    action.parameters.repeatCount = 3;
    action.parameters.repeatInterval = 10;
    QSignalSpy dispatched( &session, &StreamSession::actionSent );
    REQUIRE( sendActionDefinition( &session, action, {}, -1, nullptr, automatic ) );
    REQUIRE( dispatched.size() == 1 );
    session.setActionsEnabled( false );
    session.setActionsEnabled( true );
    REQUIRE( waitForActionTimers() );
    CHECK( dispatched.size() == 1 );
}

TEST_CASE( "Delayed response steps and finalization cannot revive after tab changes",
           "[serial][session][actions][actionruntime]" )
{
    ScopedActionsState restore;
    const auto change = GENERATE( 0, 1, 2, 3, 4 );
    const auto finalize = GENERATE( false, true );
    CAPTURE( change, finalize );
    auto& manager = ActionsManager::instance();
    manager.setAutoResponsesEnabled( true );
    auto action = testAction( 7105 );
    // Exercise linked dispatch and finalization separately: a send delayed beyond
    // finalization cannot depend on the delivery order of equal-deadline timers.
    action.parameters.delay = finalize ? 30 : 0;
    REQUIRE( manager.importFromDefinitions( { action }, {} ).ok );
    auto response = testResponse( 7106 );
    response.response.steps = { { action.id, 10 } };
    response.response.stopCommunication = finalize;
    StreamSession session( settingsFor( QStringLiteral( "TEST_RESPONSE_FINALIZATION" ) ) );
    StreamSessionTestAccess::openConnection( session );
    QSignalSpy dispatched( &session, &StreamSession::actionSent );
    QSignalSpy closed( &session, &StreamSession::connectionClosed );
    QString error;
    REQUIRE( executeResponseDefinition( &session, response, {}, &error ) );
    REQUIRE( error.isEmpty() );
    if ( change == 1 ) {
        session.setActionsEnabled( false );
        session.setActionsEnabled( true );
    }
    else if ( change == 2 ) {
        session.closeConnection();
        StreamSessionTestAccess::openConnection( session );
        closed.clear();
    }
    else if ( change == 3 ) {
        manager.setAutoResponsesEnabled( false );
    }
    if ( change == 4 ) {
        session.setAutoResponsesEnabled( false );
        session.setAutoResponsesEnabled( true );
    }
    if ( change == 0 ) {
        REQUIRE( QTest::qWaitFor(
            [&] { return finalize ? !closed.isEmpty() : !dispatched.isEmpty(); }, 5000 ) );
    }
    if ( change != 0 || finalize ) {
        REQUIRE( waitForActionTimers() );
    }
    CHECK( dispatched.size() == ( change == 0 && !finalize ? 1 : 0 ) );
    CHECK( closed.size() == ( change == 0 && finalize ? 1 : 0 ) );
    CHECK( session.isConnectionOpen() == ( change != 0 || !finalize ) );
}

TEST_CASE( "Synchronous observers can invalidate automatic work before subsequent effects",
           "[serial][session][actions][actionruntime]" )
{
    ScopedActionsState restore;
    const auto observer = GENERATE( 0, 1, 2, 3 );
    const auto automaticToggle = GENERATE( false, true );
    CAPTURE( observer );
    auto& manager = ActionsManager::instance();
    manager.setAutoResponsesEnabled( true );
    const auto first = testAction( 7107 );
    const auto second = testAction( 7108 );
    auto response = testResponse( 7109 );
    response.response.steps = { { first.id, 0 }, { second.id, 0 } };
    response.response.stopCommunication = true;
    REQUIRE( manager.importFromDefinitions( { first, second }, { response } ).ok );
    StreamSession session( settingsFor( QStringLiteral( "TEST_ACTION_OBSERVERS" ) ) );
    StreamSessionTestAccess::openConnection( session );
    const auto invalidate = [ &session, automaticToggle ] {
        if ( automaticToggle ) {
            session.setAutoResponsesEnabled( false );
            session.setAutoResponsesEnabled( true );
        }
        else {
            session.setActionsEnabled( false );
            session.setActionsEnabled( true );
        }
    };
    if ( observer == 0 ) {
        QObject::connect( &session, &StreamSession::lineObserved, &session, invalidate );
    }
    else if ( observer == 1 ) {
        QObject::connect( &session, &StreamSession::responseMatched, &session, invalidate );
    }
    else if ( observer == 2 ) {
        QObject::connect( &session, &StreamSession::actionSent, &session, invalidate );
    }
    else {
        QObject::connect( &session, &StreamSession::dataObserved, &session, invalidate );
    }
    QSignalSpy dispatched( &session, &StreamSession::actionSent );
    QSignalSpy closed( &session, &StreamSession::connectionClosed );
    QSignalSpy lines( &session, &StreamSession::lineObserved );
    StreamSessionTestAccess::feedData( session, QByteArrayLiteral( "READY\n" ) );
    CHECK( lines.size() == 1 );
    CHECK( session.responseCounter( response.id ) == ( observer == 0 || observer == 3 ? 0 : 1 ) );
    CHECK( dispatched.size() == ( observer == 2 ? 1 : 0 ) );
    CHECK( closed.isEmpty() );
    CHECK( session.isConnectionOpen() );
}

TEST_CASE( "Destroyed action sessions leave no queued logical dispatch",
           "[serial][session][actions][actionruntime]" )
{
    ScopedActionsState restore;
    ActionsManager::instance().setAutoResponsesEnabled( true );
    auto session = std::make_unique<StreamSession>(
        settingsFor( QStringLiteral( "TEST_DESTROYED_ACTIONS" ) ) );
    StreamSessionTestAccess::openConnection( *session );
    QSignalSpy dispatched( session.get(), &StreamSession::actionSent );
    auto action = testAction( 7110 );
    action.parameters.delay = 10;
    REQUIRE( sendActionDefinition( session.get(), action, {}, -1, nullptr, true ) );
    session.reset();
    REQUIRE( waitForActionTimers() );
    CHECK( dispatched.isEmpty() );
}

TEST_CASE( "An observation handler may destroy its session safely",
           "[serial][session][actions][actionruntime]" )
{
    const auto sendObserver = GENERATE( false, true );
    auto session = std::make_unique<StreamSession>(
        settingsFor( QStringLiteral( "TEST_DESTROYED_OBSERVER" ) ) );
    StreamSessionTestAccess::openConnection( *session );
    const QPointer<StreamSession> guard = session.get();
    if ( sendObserver ) {
        QObject::connect( session.get(), &StreamSession::actionSent, session.get(),
                          [ &session ] { session.reset(); } );
        const auto action = testAction( 7111 );
        CHECK_FALSE( sendActionDefinition( session.get(), action ) );
    }
    else {
        QObject::connect( session.get(), &StreamSession::lineObserved, session.get(),
                          [ &session ] { session.reset(); } );
        StreamSessionTestAccess::feedData( *session, QByteArrayLiteral( "READY\n" ) );
    }
    CHECK( guard.isNull() );
}
