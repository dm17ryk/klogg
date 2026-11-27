// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QMap>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <functional>

#include "previewconfig.h"

class QObject;
class QProcess;
struct PreviewEvaluationRequest {
    QString text;
    QString previewName;
    QByteArray definitions;
    int timeoutMs = 3000;
    // Set by the native caller; never accepted from a Commander/MCP request.
    QString executable;
    QString decoder = "auto";
    QString dissector;
    QString inputEncoding;
    QString assemblyProtocol;
    // The largest TShark/Lua result body this request accepts, in KiB (#183): 128 to 4096.
    int resultLimitKiB = 1024;
    QObject* sessionOwner = nullptr;
};

class PreviewService {
public:
    using Completion = std::function<void( QVariantMap )>;
    // The definitions evaluation uses: the user's previews and the enabled packs' previews
    // (PreviewManager::effective()). Empty when they exceed the service's limits.
    static QByteArray definitions();
    // The same serialization and limits for any set of previews and blocks: 128 previews, 4,096
    // blocks, the text, entry and depth bounds and 1 MiB. Empty when a limit is exceeded.
    static QByteArray definitionsFor( const QVector<PreviewDefinition>& previews,
                                      const QMap<QString, PreviewFieldSpec>& blocks,
                                      const QString& revision = {} );
    static QProcess* evaluate( QObject* owner, PreviewEvaluationRequest request, Completion done );
    static QVariantMap evaluateAndWait( PreviewEvaluationRequest request,
                                        QObject* cancellationOwner = nullptr );
    static void cancel( QProcess* process );
    // Requests not finished yet. A cancelled request keeps its place until the event loop has
    // reaped its worker; evaluate() refuses a third one with "preview_busy".
    static int activeRequestCount();
    static QString tooltipHtml( const QVariantMap& result );
    // Which limit cut a limited result, and by how much (#183), e.g. "390 of 768 rows: result
    // limit 1024 KiB, the whole result needs 1650 KiB". Empty when the result names no limit.
    static QString limitText( const QVariantMap& result );
    // E16b: the Python decoder that decoded `result`, "<pack-id>/<decoder-id>", followed by its
    // pack's name while that pack is enabled; empty for another backend. Plain text.
    static QString pythonDecoderLabel( const QVariantMap& result );
    static int workerMain( int argc, char* argv[] );
};
