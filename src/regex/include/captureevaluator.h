// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CILOGG_CAPTURE_EVALUATOR_H
#define CILOGG_CAPTURE_EVALUATOR_H

#include <QVariantMap>
#include <memory>

// Used only inside a supervised worker. Never compile arbitrary drafts in the GUI.
class CaptureEvaluator {
public:
    explicit CaptureEvaluator( const QVariantMap& options );
    ~CaptureEvaluator();
    QVariantMap compilation() const;
    QVariantMap evaluate( const QVariantList& lines );

private:
    struct Private;
    std::unique_ptr<Private> d_;
};

// Internal, versioned length-prefixed protocol on stdin/stdout; diagnostics use stderr.
int runAnalysisWorker( int argc, char* argv[] );
#endif
