// SPDX-License-Identifier: GPL-3.0-or-later
#include "previewassembly.h"
#include "log.h"
#include <QMap>
#include <QRegularExpression>
#include <QSet>

namespace {
constexpr int MaxRows = 1024;
constexpr int MaxBodyBytes = 64 * 1024;
constexpr int MaxContextBytes = 1024 * 1024;
struct Operation {
    QString flow;
    QStringList lines;
    QVector<int> rows;
};
struct Frame {
    PreviewAssembly result;
    QByteArray body;
};
QString trimRecordEnd( QString line )
{
    while ( line.endsWith( '\r' ) )
        line.chop( 1 );
    return line;
}
} // namespace

QVariantMap PreviewAssembly::metadata() const
{
    QVariantList rows;
    for ( const auto line : physicalLines )
        rows.push_back( QString::number( line + 1 ) );
    return { { "state", state },
             { "reason", reason },
             { "protocol", protocol },
             { "flow", flow },
             { "physicalLinesOneBased", rows },
             { "expectedBytes", expectedBytes },
             { "receivedBytes", receivedBytes },
             { "excludedPrefixRows", excludedPrefixRows },
             { "checksumVerified", false },
             { "origin", "decoded_log_text_utf8" },
             { "deliveryVerified", false } };
}

PreviewAssembly assemblePreviewMessage( const QStringList& lines, int targetIndex,
                                        quint64 firstPhysicalLine )
{
    PreviewAssembly unavailable;
    if ( lines.size() > MaxRows ) {
        unavailable.state = "limited";
        unavailable.reason = "context_limit";
        return unavailable;
    }
    if ( targetIndex < 0 || targetIndex >= lines.size() )
        return unavailable;
    int bytes = 0;
    for ( const auto& line : lines ) {
        if ( line.size() > MaxContextBytes || line.toUtf8().size() > MaxContextBytes - bytes ) {
            unavailable.state = "limited";
            unavailable.reason = "context_limit";
            return unavailable;
        }
        bytes += static_cast<int>( line.toUtf8().size() );
    }
    static const QRegularExpression envelope(
        R"(^[0-9]{2}/[0-9]{2}/[0-9]{4} [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3} \[(RX|TX)\] - (.*)$)" );
    static const QRegularExpression diagnostic(
        R"(^#([0-9]+)\s+[0-9]+\s+(?:TRACE|DEBUG|DETAILS|INFO|WARNING|WARN|ERROR|CRITICAL):(.*)$)" );
    static const QRegularExpression send( R"(^NET Send message command: ([0-9]{1,5})$)" );
    static const QRegularExpression httpRequest( R"(^PUT \S+ HTTP/1\.[01]$)" );
    static const QRegularExpression length( R"(\bLen\(([0-9]{1,7})\))" );
    static const QRegularExpression port( R"(\bPort\(([0-9]{1,5})\))" );
    static const QRegularExpression connect( R"(^NET TCP connect Lport:([0-9]{1,5}) Rport:)" );
    static const QRegularExpression contentLength( R"(^Content-Length:\s*([0-9]{1,8})\s*$)",
                                                   QRegularExpression::CaseInsensitiveOption );
    static const QRegularExpression trailer( R"(^CRC[0-9A-Fa-f]{4}$)" );
    QVector<Frame> frames;
    QMap<QString, int> active;
    QMap<QString, Operation> operations;
    QStringList pending;
    QVector<int> pendingRows;
    QString pendingDirection;
    const auto stop = [ & ]( const QString& flow, const QString& reason ) {
        const auto found = active.find( flow );
        if ( found == active.end() )
            return;
        auto& result = frames[ found.value() ].result;
        result.reason = reason;
        LOG_DEBUG << "Preview assembly stopped flow=" << flow << " reason=" << reason
                  << " received=" << result.receivedBytes << " expected=" << result.expectedBytes;
        active.erase( found );
    };
    for ( int row = 0; row < lines.size(); ++row ) {
        QString text = trimRecordEnd( lines[ row ] );
        const auto wrapper = envelope.match( text );
        const QString direction = wrapper.hasMatch() ? wrapper.captured( 1 ) : "raw";
        if ( wrapper.hasMatch() )
            text = wrapper.captured( 2 );
        const auto info = diagnostic.match( text );
        if ( !info.hasMatch() ) {
            if ( direction != pendingDirection || httpRequest.match( text ).hasMatch() ) {
                pending.clear();
                pendingRows.clear();
            }
            pendingDirection = direction;
            pending << text;
            pendingRows << row;
            continue;
        }
        const auto task = direction + ":" + info.captured( 1 );
        const auto message = info.captured( 2 );
        const auto sending = send.match( message );
        const auto connecting = connect.match( message );
        if ( connecting.hasMatch() )
            stop( task + ":" + connecting.captured( 1 ), "connection_changed" );
        if ( sending.hasMatch() ) {
            while ( !pending.isEmpty() && pending.front().isEmpty() ) {
                pending.removeFirst();
                pendingRows.removeFirst();
            }
            while ( !pending.isEmpty() && pending.back().isEmpty() ) {
                pending.removeLast();
                pendingRows.removeLast();
            }
            operations[ task ]
                = { task + ":" + sending.captured( 1 ),
                    direction == pendingDirection ? pending : QStringList{}, pendingRows };
        }
        else if ( message.startsWith( "NET SendTCPData " ) && operations.contains( task ) ) {
            const auto operation = operations.take( task );
            const auto count = length.match( message );
            const auto dataPort = port.match( message );
            if ( !count.hasMatch()
                 || ( dataPort.hasMatch()
                      && operation.flow != task + ":" + dataPort.captured( 1 ) ) ) {
                stop( operation.flow, "fragment_identity_mismatch" );
                pending.clear();
                pendingRows.clear();
                continue;
            }
            const int declared = count.captured( 1 ).toInt();
            QStringList payload = operation.lines;
            QVector<int> payloadRows = operation.rows;
            QByteArray header;
            bool isHttp = !payload.isEmpty() && httpRequest.match( payload.front() ).hasMatch();
            if ( isHttp ) {
                stop( operation.flow, "new_message" );
                int body = -1;
                qint64 expected = -1;
                bool host = false;
                for ( int i = 1; i < payload.size(); ++i ) {
                    if ( payload[ i ].isEmpty() ) {
                        body = i + 1;
                        break;
                    }
                    const auto field = contentLength.match( payload[ i ] );
                    if ( field.hasMatch() ) {
                        if ( expected >= 0 ) {
                            expected = -1;
                            break;
                        }
                        expected = field.captured( 1 ).toLongLong();
                    }
                    host = host
                           || payload[ i ].compare( "Host: CARE-PANELINFO", Qt::CaseInsensitive )
                                  == 0;
                }
                if ( host && expected > 0 && body > 0 && body < payload.size()
                     && payload[ body ].startsWith( "EHCI" ) ) {
                    if ( frames.size() >= 64 ) {
                        unavailable.state = "limited";
                        unavailable.reason = "message_count_limit";
                        return unavailable;
                    }
                    Frame frame;
                    frame.result.state = "incomplete";
                    frame.result.protocol = "EHCI";
                    frame.result.flow = operation.flow;
                    frame.result.expectedBytes = expected;
                    for ( int i = body; i < payloadRows.size(); ++i )
                        frame.result.physicalLines
                            << firstPhysicalLine + static_cast<quint64>( payloadRows[ i ] );
                    if ( expected > MaxBodyBytes ) {
                        frame.result.state = "limited";
                        frame.result.reason = "message_limit";
                    }
                    frames.push_back( frame );
                    if ( expected <= MaxBodyBytes )
                        active[ operation.flow ] = static_cast<int>( frames.size() ) - 1;
                    header = ( payload.mid( 0, body - 1 ).join( "\r\n" ) + "\r\n\r\n" ).toUtf8();
                    payload = payload.mid( body );
                    payloadRows = payloadRows.mid( body );
                    // Rows are appended only after byte-count validation below.
                    if ( expected <= MaxBodyBytes )
                        frames.back().result.physicalLines.clear();
                }
            }
            if ( active.contains( operation.flow ) && !payload.isEmpty() ) {
                auto& frame = frames[ active.value( operation.flow ) ];
                QMap<QByteArray, int> candidates;
                bool ambiguousRows = false;
                QVector<QByteArray> encoded;
                for ( const auto& part : payload )
                    encoded << part.toUtf8();
                for ( const auto& separator : { QString(), QString( "\n" ), QString( "\r\n" ) } ) {
                    const auto between = separator.toUtf8();
                    qint64 size = header.size();
                    for ( int first = static_cast<int>( encoded.size() ) - 1; first >= 0;
                          --first ) {
                        size += encoded[ first ].size();
                        if ( first + 1 < encoded.size() )
                            size += between.size();
                        if ( size > declared )
                            break;
                        if ( size != declared || ( !header.isEmpty() && first != 0 ) )
                            continue;
                        QByteArray candidate;
                        for ( int part = first; part < encoded.size(); ++part ) {
                            if ( part != first )
                                candidate += between;
                            candidate += encoded[ part ];
                        }
                        if ( candidates.contains( candidate )
                             && candidates.value( candidate ) != first )
                            ambiguousRows = true;
                        candidates[ candidate ] = first;
                    }
                }
                const int first = candidates.size() == 1 && !ambiguousRows
                                      ? candidates.constBegin().value()
                                      : 0;
                for ( const auto physical : payloadRows.mid( first ) )
                    frame.result.physicalLines
                        << firstPhysicalLine + static_cast<quint64>( physical );
                if ( candidates.size() != 1 || ambiguousRows ) {
                    stop( operation.flow, candidates.isEmpty() ? "fragment_length_mismatch"
                                                               : "ambiguous_fragment" );
                }
                else if ( frame.body.size() + candidates.constBegin().key().size()
                          > MaxBodyBytes ) {
                    frame.result.state = "limited";
                    stop( operation.flow, "message_limit" );
                }
                else {
                    frame.result.excludedPrefixRows += first;
                    if ( first > 0 ) {
                        LOG_DEBUG << "Preview assembly excluded prefix flow=" << operation.flow
                                  << " rows=" << first << " declared_bytes=" << declared;
                    }
                    frame.body += candidates.constBegin().key();
                    frame.result.receivedBytes = frame.body.size();
                    frame.result.text = QString::fromUtf8( frame.body );
                    LOG_DEBUG << "Preview assembly fragment flow=" << operation.flow
                              << " rows=" << payloadRows.size() << " received=" << frame.body.size()
                              << " expected=" << frame.result.expectedBytes;
                    if ( frame.result.receivedBytes > frame.result.expectedBytes ) {
                        stop( operation.flow, "message_overrun" );
                    }
                    else if ( frame.result.receivedBytes == frame.result.expectedBytes ) {
                        if ( frame.body.startsWith( "EHCI" )
                             && trailer.match( QString::fromLatin1( frame.body.right( 7 ) ) )
                                    .hasMatch() ) {
                            frame.result.state = "complete";
                            LOG_DEBUG << "Preview assembly complete flow=" << operation.flow
                                      << " bytes=" << frame.body.size();
                            active.remove( operation.flow );
                        }
                        else
                            stop( operation.flow, "invalid_message_trailer" );
                    }
                }
            }
        }
        pending.clear();
        pendingRows.clear();
    }
    const auto target = firstPhysicalLine + static_cast<quint64>( targetIndex );
    for ( const auto& frame : frames ) {
        if ( frame.result.physicalLines.contains( target ) )
            return frame.result;
    }
    return unavailable;
}
