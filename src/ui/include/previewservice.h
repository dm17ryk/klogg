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
    static QString tooltipHtml( const QVariantMap& result );
    // E16b: the Python decoder that decoded `result`, "<pack-id>/<decoder-id>", followed by its
    // pack's name while that pack is enabled; empty for another backend. Plain text.
    static QString pythonDecoderLabel( const QVariantMap& result );
    static int workerMain( int argc, char* argv[] );
};
