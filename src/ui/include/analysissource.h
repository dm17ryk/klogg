// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CILOGG_ANALYSISSOURCE_H
#define CILOGG_ANALYSISSOURCE_H

#include <optional>

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>

// AnalysisSource (the focused/all/tab:<id>/file:<path> selection) is declared in commander.h,
// next to CommanderRequest::source, so the CLI validates it without the UI library.
#include "commander.h"

// An open tab as the application lists it (see MainWindow::commanderWindowInfo).
struct OpenSource {
    QString tabId;
    QString filePath;
    QString label;
    int windowIndex = 0;
    int tabIndex = 0;
    bool focused = false;
    // "file", "url" or "com" (a live serial capture); empty when the lister does not say.
    QString sourceType;

    static QList<OpenSource> fromVariantList( const QVariantList& sources );
};

// The sources a tool can use. resolveAnalysisSource refuses the others with a stable reason and
// the picker lists them disabled, saying why.
struct SourceRules {
    bool allowAll = true;
    bool allowFile = true;
    // Only live COM tabs: "all" means every COM tab, other tabs are refused (not_com_tab).
    bool comOnly = false;

    // Focused tab, a tab or a file (Preview, plugin views): a line or a view names one source.
    static SourceRules oneSource()
    {
        return { false, true, false };
    }
    // Focused tab, a COM tab or every COM tab (Script Runner): a script needs a live session.
    static SourceRules liveComTabs()
    {
        return { true, false, true };
    }
};

// The source list of the pickers from Commander get_info windows (tabs of every window; the
// focused tab is the current tab of the active window).
QVariantList analysisSourcesFromWindowInfo( const QVariantList& windows );

// The tabs a selection runs on. A file that is not open yet is returned in fileToOpen.
struct AnalysisSourceResolution {
    QStringList tabIds;
    QString fileToOpen;
    // Empty on success; otherwise a stable reason (source_not_found, no_focused_tab,
    // no_open_sources, and with SourceRules invalid_source or not_com_tab).
    QString reason;
    QString message;

    bool ok() const
    {
        return reason.isEmpty();
    }
};

// "all" resolves to every open tab: the docks start the jobs as the analysis service admits
// them (see AnalysisRuns).
AnalysisSourceResolution resolveAnalysisSource( const AnalysisSource& source,
                                                const QList<OpenSource>& open,
                                                const SourceRules& rules = {} );

// True if `tabId` is one of the open tabs in `sources` (the dock source list).
bool isListedSource( const QVariantList& sources, const QString& tabId );

class QComboBox;
class QWidget;

// The shared source picker of every tab-bound tool. Item data is the AnalysisSource text:
// "focused", "all", one "tab:<id>" per open tab (every window), chosen "file:<path>" entries,
// a "File…" entry (data "file:") and, if needed, the closed tab still selected. Entries the
// rules refuse, and tabs whose source map has a "disabledReason", are disabled with the reason
// as their tooltip; a refused selection falls back to the focused tab.
void populateAnalysisSourceCombo( QComboBox* combo, const QVariantList& sources,
                                  const QString& selected, const SourceRules& rules = {} );
// Selects `source` in the combo, adding a file entry when needed. False if it is not listed or
// its entry is disabled.
bool selectAnalysisSource( QComboBox* combo, const QString& source );
// Handles the "File…" entry: asks for a file, then selects it, or restores `previous`.
void chooseAnalysisSourceFile( QComboBox* combo, QWidget* parent, const QString& previous );

#include <QComboBox>

#include <functional>

// The shared picker as a widget, for tools without a refresh cycle of their own (plugin actions
// and views, Script Runner, Preview): the list is rebuilt from `sources` when it opens and on
// refresh(); choosing "File…" asks for a file.
class AnalysisSourcePicker : public QComboBox {
    Q_OBJECT

public:
    AnalysisSourcePicker( std::function<QVariantList()> sources, SourceRules rules,
                          QWidget* parent = nullptr );

    // The selected AnalysisSource text ("focused" when nothing is selected).
    QString selection() const;
    // Selects a listed, usable source without emitting selectionChanged.
    bool setSelection( const QString& source );
    void refresh();
    void showPopup() override;

Q_SIGNALS:
    // The user (or UI automation) chose another source.
    void selectionChanged( const QString& source );

private:
    void selected();

    std::function<QVariantList()> sources_;
    SourceRules rules_;
    QVariantList cache_;
    QString previous_ = QStringLiteral( "focused" );
    bool choosing_ = false;
};

#endif
