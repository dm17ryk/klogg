// SPDX-License-Identifier: GPL-3.0-or-later
// A trusted test interpreter substitute; no external engine is launched.
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QtEndian>
#include <cstdio>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif
namespace {
QByteArray exact( QFile& input, int count )
{
    QByteArray result;
    while ( result.size() < count ) {
        const auto part = input.read( count - result.size() );
        if ( part.isEmpty() )
            return {};
        result += part;
    }
    return result;
}
QByteArray frame( const QJsonObject& value )
{
    const auto bytes = QJsonDocument( value ).toJson( QJsonDocument::Compact );
    char header[ 4 ];
    qToBigEndian<quint32>( static_cast<quint32>( bytes.size() ), header );
    return QByteArray( header, 4 ) + bytes;
}
// A fake TShark for the decoder settings dialog (issue #180): `-G folders` reports the folders the
// environment gives (or a global plugin folder outside it, CILOGG_FAKE_TSHARK_MODE=unisolated),
// `-G protocols` a short list (slowly: "slow", or failing: "fail"). Each call is appended to
// CILOGG_FAKE_TSHARK_LOG.
int fakeTshark( const QByteArray& report )
{
    const auto mode = qEnvironmentVariable( "CILOGG_FAKE_TSHARK_MODE" );
    const auto log = qEnvironmentVariable( "CILOGG_FAKE_TSHARK_LOG" );
    if ( !log.isEmpty() ) {
        QFile file( log );
        if ( file.open( QIODevice::Append ) )
            file.write( report + '\n' );
    }
    QByteArray output;
    if ( report == "folders" ) {
        const auto config = qEnvironmentVariable( "WIRESHARK_CONFIG_DIR" );
        const auto plugins = mode == "unisolated" ? QStringLiteral( "/usr/lib/wireshark/plugins" )
                                                  : qEnvironmentVariable( "WIRESHARK_PLUGIN_DIR" );
        output = QStringLiteral( "Temp:\t/tmp\n"
                                 "Personal configuration:\t%1\n"
                                 "Personal Plugins:\t%1/plugins/4.6\n"
                                 "Global Plugins:\t%2/4.6\n"
                                 "Personal Lua Plugins:\t%1/plugins\n"
                                 "Global Lua Plugins:\t%2\n" )
                     .arg( config, plugins )
                     .toUtf8();
    }
    else if ( report == "protocols" ) {
        if ( mode == "fail" )
            return 3;
        if ( mode == "slow" )
            QThread::msleep( 1500 );
        output = "Data\tData\tdata\tT\tT\tF\n"
                 "Fake protocol\tFAKE\tfake_tshark_proto\tT\tT\tT\n";
    }
    else
        return 1;
    std::fwrite( output.constData(), 1, static_cast<size_t>( output.size() ), stdout );
    std::fflush( stdout );
    return 0;
}
} // namespace
int main( int argc, char** argv )
{
#ifdef Q_OS_WIN
    _setmode( _fileno( stdin ), _O_BINARY );
    _setmode( _fileno( stdout ), _O_BINARY );
#endif
    QCoreApplication app( argc, argv );
    if ( argc >= 3 && QByteArray( argv[ 1 ] ) == "-G" )
        return fakeTshark( QByteArray( argv[ 2 ] ) );
    // Opt-in cold-start simulation for supervision tests; keep normal fixtures fast.
    if ( qEnvironmentVariableIsSet( "CILOGG_PREVIEW_FIXTURE_COLD_START" ) )
        QThread::msleep( 1200 );
    QFile input, output;
    if ( !input.open( stdin, QIODevice::ReadOnly | QIODevice::Unbuffered )
         || !output.open( stdout, QIODevice::WriteOnly ) )
        return 2;
    while ( true ) {
        const auto header = exact( input, 4 );
        if ( header.size() != 4 )
            return 0;
        const auto size = qFromBigEndian<quint32>( header.constData() );
        if ( size > 256 * 1024 )
            return 2;
        const auto request
            = QJsonDocument::fromJson( exact( input, static_cast<int>( size ) ) ).object();
        const auto mode = qEnvironmentVariable( "CILOGG_PREVIEW_FIXTURE_MODE" );
        if ( mode == "crash" )
            return 42;
        if ( mode == "stall" ) {
            QThread::sleep( 30 );
            return 0;
        }
        if ( mode == "stderr" ) {
            const auto flood = QByteArray( 65537, 'x' );
            std::fwrite( flood.constData(), 1, static_cast<size_t>( flood.size() ), stderr );
            std::fflush( stderr );
            QThread::sleep( 30 );
            return 0;
        }
        QJsonObject attributes{ { "name", "test.item" },
                                { "show", "" },
                                { "value", "01" },
                                { "pos", "9007199254740993" },
                                { "size", "1" } };
        if ( mode == "budget" )
            attributes[ "show" ] = QString::number( request.value( "timeoutMs" ).toInt() );
        // The result limit the host asked for, in bytes (#183).
        if ( mode == "result_limit" )
            attributes[ "show" ] = QString::number( request.value( "responseLimit" ).toInt() );
        // The dissector and protocol the host resolved for this request (#182).
        if ( mode == "profile" )
            attributes[ "show" ] = request.value( "profile" ).toObject().value( "dissector" ).toString()
                                   + "|"
                                   + request.value( "profile" ).toObject().value( "protocol" ).toString();
        QJsonObject field{ { "id", "0/0" },
                           { "depth", 0 },
                           { "positionOrigin", "engine_data_source_unresolved" },
                           { "columns", QJsonArray{ "test.item", attributes.value( "show" ), "01",
                                                    "9007199254740993", "1" } },
                           { "attributes", attributes } };
        if ( mode == "attributes" ) {
            attributes[ "show" ] = 1;
            field[ "attributes" ] = attributes;
        }
        if ( mode == "depth" )
            field[ "depth" ] = 33;
        // "rows" fills the row limit (2048); "count" passes it (#183). The "*_rows" modes are a
        // tree cut at it.
        const bool rowCut = mode == "limited_rows" || mode == "limit_rows";
        // A tree cut at a PDML limit (#198); the "pdml_*" variants contradict their result.
        const bool pdmlCut = mode == "pdml_items" || mode == "pdml_bytes" || mode == "pdml_read"
                             || mode == "pdml_maximum" || mode == "pdml_reason"
                             || mode == "pdml_bytes_rows";
        QJsonArray fields;
        for ( int i = 0;
              i < ( mode == "count" ? 2049 : mode == "rows" || rowCut || pdmlCut ? 2048 : 1 ); ++i )
            fields.append( field );
        // A text-only subtree and a Lua value string as the worker renders them (#181); "forged"
        // claims a display text the attributes do not hold.
        if ( mode == "display" || mode == "forged" ) {
            const QJsonObject text{ { "name", "_ws.lua.text" }, { "showname", "Header: two bytes" },
                                    { "show", "" },             { "value", "" },
                                    { "pos", "0" },             { "size", "2" } };
            const QJsonObject status{ { "name", "probe.status" }, { "showname", "Status: Battery OK (0)" },
                                      { "show", "0" },            { "value", "00" },
                                      { "pos", "0" },             { "size", "1" } };
            const QString value = mode == "forged" ? "Forged" : "Battery OK (0)";
            fields = QJsonArray{
                QJsonObject{ { "id", "0/0" },
                             { "depth", 0 },
                             { "positionOrigin", "engine_data_source_unresolved" },
                             { "columns", QJsonArray{ "Header: two bytes", "", "", "0", "2" } },
                             { "attributes", text } },
                QJsonObject{ { "id", "0/0/0" },
                             { "depth", 1 },
                             { "positionOrigin", "engine_data_source_unresolved" },
                             { "columns", QJsonArray{ "probe.status", value, "00", "0", "1" } },
                             { "attributes", status } }
            };
            // Wireshark's truncation mark appended by the dissector to the value is display text.
            const auto appended
                = QStringLiteral( "Battery OK (0) [" ) + QChar( 0x2026 ) + QLatin1Char( ']' );
            const QJsonObject more{ { "name", "probe.more" }, { "showname", "More: " + appended },
                                    { "show", "0" },          { "value", "00" },
                                    { "pos", "0" },           { "size", "1" } };
            fields.append( QJsonObject{ { "id", "0/0/1" },
                                        { "depth", 1 },
                                        { "positionOrigin", "engine_data_source_unresolved" },
                                        { "columns", QJsonArray{ "probe.more", appended, "00", "0", "1" } },
                                        { "attributes", more } } );
            // A value Wireshark cut off ("…" after a prefix of show) shows the whole show value,
            // also as Raw when there are no raw bytes; bare legacy text has neither.
            const QJsonObject cut{ { "name", "probe.long" },
                                   { "showname", QStringLiteral( "Long: abcde" ) + QChar( 0x2026 ) },
                                   { "show", "abcdefghij" },
                                   { "pos", "0" },
                                   { "size", "1" } };
            fields.append( QJsonObject{
                { "id", "0/0/2" },
                { "depth", 1 },
                { "positionOrigin", "engine_data_source_unresolved" },
                { "columns", QJsonArray{ "probe.long", "abcdefghij", "abcdefghij", "0", "1" } },
                { "attributes", cut } } );
            const QJsonObject bare{
                { "name", "text" }, { "show", "Bare legacy text" }, { "pos", "0" }, { "size", "0" }
            };
            fields.append( QJsonObject{
                { "id", "0/1" },
                { "depth", 0 },
                { "positionOrigin", "engine_data_source_unresolved" },
                { "columns", QJsonArray{ "Bare legacy text", "", "", "0", "0" } },
                { "attributes", bare } } );
        }
        QJsonObject response{
            { "version", 1 },
            { "id", request.value( "id" ) },
            { "backend", "tshark_lua" },
            { "state", "completed" },
            { "reason", "" },
            { "available", true },
            { "complete", true },
            { "limited", false },
            { "previewName", "test" },
            { "offsetSpace", "engine_data_source_unresolved" },
            { "messageCompleteness", "not_reported" },
            { "messageIntegrity", "not_reported" },
            { "scriptSha256", QJsonArray{} },
            { "fields", fields },
            // Intentionally unrelated to this fixture process: must remain metadata only.
            { "enginePid", qEnvironmentVariable( "CILOGG_PREVIEW_FIXTURE_PID" ).toDouble() }
        };
        if ( mode == "version" )
            response[ "version" ] = true;
        if ( mode == "id" )
            response[ "id" ] = "wrong";
        if ( mode == "metadata" )
            response.remove( "enginePid" );
        if ( mode == "flags" )
            response[ "complete" ] = false;
        if ( mode == "hash" )
            response[ "scriptSha256" ] = QJsonArray{ "not-a-hash" };
        if ( mode == "hash_newline" )
            response[ "scriptSha256" ] = QJsonArray{ QString( 64, 'a' ) + "\n" };
        // A result cut off at its byte limit names it (#183); "bad_limit" miscounts the rows,
        // "unnamed_limit" names none, "limit_reason" gives the row limit's reason and
        // "limit_needed" says the whole result fits.
        if ( mode == "limited_bytes" || mode == "bad_limit" || mode == "unnamed_limit"
             || mode == "limit_reason" || mode == "limit_needed" ) {
            response[ "complete" ] = false;
            response[ "limited" ] = true;
            response[ "reason" ] = mode == "limit_reason" ? "rendered_field_limit" : "response_limit";
            if ( mode != "unnamed_limit" )
                response[ "limit" ]
                    = QJsonObject{ { "name", "bytes" },
                                   { "maximum", 131072 },
                                   { "shownRows", mode == "bad_limit" ? 2 : 1 },
                                   { "totalRows", 769 },
                                   { "neededBytes", mode == "limit_needed" ? 131072 : 268000 } };
        }
        // A tree cut at the row limit; "limit_rows" claims the whole tree was shown.
        if ( rowCut ) {
            response[ "complete" ] = false;
            response[ "limited" ] = true;
            response[ "reason" ] = "rendered_field_limit";
            response[ "limit" ] = QJsonObject{ { "name", "rows" },
                                               { "maximum", 2048 },
                                               { "shownRows", 2048 },
                                               { "totalRows", mode == "limit_rows" ? 2048 : 2400 } };
        }
        // A tree past the worker's PDML limits shows its first rows (#198). "pdml_read" claims
        // fewer rows read than shown, "pdml_maximum" another item limit, "pdml_reason" the row
        // limit's reason.
        if ( pdmlCut ) {
            // "pdml_bytes_rows": more rows read than fit in the items the worker reads.
            const bool bytes = mode == "pdml_bytes" || mode == "pdml_bytes_rows";
            response[ "complete" ] = false;
            response[ "limited" ] = true;
            response[ "reason" ] = mode == "pdml_reason" ? "rendered_field_limit"
                                   : bytes               ? "pdml_byte_limit"
                                                         : "pdml_item_limit";
            response[ "limit" ] = QJsonObject{
                { "name", bytes ? "pdmlBytes" : "pdmlItems" },
                { "maximum", bytes ? 4194304 : mode == "pdml_maximum" ? 4096 : 8192 },
                { "shownRows", 2048 },
                { "readRows", mode == "pdml_read"         ? 1000
                              : mode == "pdml_bytes_rows" ? 9000
                                                          : 7900 }
            };
        }
        // A byte cut of a tree cut at a PDML limit names that limit; "pdml_tree" names another.
        // "pdml_tree_rows" counts more rows read than the item limit allows.
        if ( mode == "pdml_then_bytes" || mode == "pdml_tree" || mode == "pdml_tree_rows" ) {
            response[ "complete" ] = false;
            response[ "limited" ] = true;
            response[ "reason" ] = "response_limit";
            response[ "limit" ] = QJsonObject{ { "name", "bytes" },
                                               { "maximum", 131072 },
                                               { "shownRows", 1 },
                                               { "totalRows", mode == "pdml_tree_rows" ? 9000 : 7900 },
                                               { "neededBytes", 2700000 },
                                               { "tree", mode == "pdml_tree" ? "rows" : "pdmlItems" } };
        }
        // Limits that contradict themselves or their result (review of #192, #198): a byte
        // maximum below what a request may choose, a row cut at another maximum, a limit on a
        // failure, an extra key and a missing one.
        if ( mode == "limit_low" || mode == "limit_extra" || mode == "limit_missing" ) {
            response[ "complete" ] = false;
            response[ "limited" ] = true;
            response[ "reason" ] = "response_limit";
            QJsonObject limit{ { "name", "bytes" },
                               { "maximum", mode == "limit_low" ? 65536 : 131072 },
                               { "shownRows", 1 },
                               { "totalRows", 769 },
                               { "neededBytes", 268000 } };
            if ( mode == "limit_extra" )
                limit[ "readRows" ] = 769;
            if ( mode == "limit_missing" )
                limit.remove( "neededBytes" );
            response[ "limit" ] = limit;
        }
        if ( mode == "limit_row_max" ) {
            response[ "complete" ] = false;
            response[ "limited" ] = true;
            response[ "reason" ] = "rendered_field_limit";
            response[ "limit" ] = QJsonObject{
                { "name", "rows" }, { "maximum", 1 }, { "shownRows", 1 }, { "totalRows", 2 }
            };
        }
        if ( mode == "limit_failed" ) {
            response[ "state" ] = "failed";
            response[ "reason" ] = "response_limit";
            response[ "available" ] = false;
            response[ "complete" ] = false;
            response[ "limited" ] = true;
            response[ "fields" ] = QJsonArray{};
            response[ "limit" ] = QJsonObject{ { "name", "bytes" },
                                               { "maximum", 131072 },
                                               { "shownRows", 0 },
                                               { "totalRows", 769 },
                                               { "neededBytes", 268000 } };
        }
        // A tree whose PDML read holds no row fails at that limit.
        if ( mode == "pdml_items_failed" ) {
            response[ "state" ] = "failed";
            response[ "reason" ] = "pdml_item_limit";
            response[ "available" ] = false;
            response[ "complete" ] = false;
            response[ "limited" ] = true;
            response[ "fields" ] = QJsonArray{};
        }
        if ( mode == "reported_timeout" || mode == "lua_failure" ) {
            response[ "state" ] = "failed";
            response[ "reason" ] = mode == "lua_failure" ? "lua_failure" : "timeout";
            response[ "available" ] = false;
            response[ "complete" ] = false;
            response[ "fields" ] = QJsonArray{};
        }
        auto bytes = frame( response );
        if ( mode == "surplus" )
            bytes += bytes;
        if ( mode == "large" ) {
            char prefix[ 4 ];
            qToBigEndian<quint32>( 4 * 1024 * 1024 + 1, prefix );
            bytes = QByteArray( prefix, 4 );
        }
        if ( output.write( bytes ) != bytes.size() || !output.flush() )
            return 3;
    }
}
