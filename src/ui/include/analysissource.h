// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CILOGG_ANALYSISSOURCE_H
#define CILOGG_ANALYSISSOURCE_H

#include <optional>

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>

// What a tab-bound tool runs on: the focused tab, one tab, a file, or all opened files.
// Text form (CLI, MCP, saved definitions): "focused", "all", "tab:<tab id>", "file:<path>".
struct AnalysisSource {
    enum class Kind { Focused, Tab, File, AllOpen };

    Kind kind = Kind::Focused;
    QString tabId;
    QString filePath;

    static std::optional<AnalysisSource> parse( const QString& text,
                                                QString* errorMessage = nullptr );
    QString toString() const;

    bool operator==( const AnalysisSource& other ) const
    {
        return kind == other.kind && tabId == other.tabId && filePath == other.filePath;
    }
};

// An open tab as the application lists it (see MainWindow::commanderWindowInfo).
struct OpenSource {
    QString tabId;
    QString filePath;
    QString label;
    int windowIndex = 0;
    int tabIndex = 0;
    bool focused = false;

    static QList<OpenSource> fromVariantList( const QVariantList& sources );
};

// The tabs a selection runs on. A file that is not open yet is returned in fileToOpen.
struct AnalysisSourceResolution {
    QStringList tabIds;
    QString fileToOpen;
    // Empty on success; otherwise a stable reason (source_not_found, no_focused_tab,
    // no_open_sources, too_many_sources).
    QString reason;
    QString message;

    bool ok() const
    {
        return reason.isEmpty();
    }
};

// Runs on at most this many tabs at once: analysis jobs are bounded (10 active).
inline constexpr int MaxAnalysisSources = 8;

AnalysisSourceResolution resolveAnalysisSource( const AnalysisSource& source,
                                                const QList<OpenSource>& open,
                                                int maxSources = MaxAnalysisSources );

class QComboBox;
class QWidget;

// The shared source picker of the analysis docks. Item data is the AnalysisSource text:
// "focused", "all", one "tab:<id>" per open tab (every window), chosen "file:<path>" entries,
// a "File…" entry (data "file:") and, if needed, the closed tab still selected.
void populateAnalysisSourceCombo( QComboBox* combo, const QVariantList& sources,
                                  const QString& selected );
// Selects `source` in the combo, adding a file entry when needed. False if it is not listed.
bool selectAnalysisSource( QComboBox* combo, const QString& source );
// Handles the "File…" entry: asks for a file, then selects it, or restores `previous`.
void chooseAnalysisSourceFile( QComboBox* combo, QWidget* parent, const QString& previous );

#endif
