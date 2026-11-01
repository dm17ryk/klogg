// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QString>
#include <QVariantMap>
#include <functional>

class QObject;
class QProcess;
struct PreviewEvaluationRequest {
    QString text;
    QString previewName;
    QByteArray definitions;
    int timeoutMs = 3000;
    // Set by the native caller; never accepted from a Commander/MCP request.
    QString executable;
};

class PreviewService {
public:
    using Completion = std::function<void( QVariantMap )>;
    static QByteArray definitions();
    static QProcess* evaluate( QObject* owner, PreviewEvaluationRequest request, Completion done );
    static QVariantMap evaluateAndWait( PreviewEvaluationRequest request,
                                        QObject* cancellationOwner = nullptr );
    static void cancel( QProcess* process );
    static QString tooltipHtml( const QVariantMap& result );
    static int workerMain( int argc, char* argv[] );
};
