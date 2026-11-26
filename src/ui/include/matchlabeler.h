// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CILOGG_MATCHLABELER_H
#define CILOGG_MATCHLABELER_H

#include <vector>

#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include "predefinedfilters.h"
#include "regularexpressionpattern.h"

// Explains which part of a search selected a line of the filtered view: the text that matched,
// and which predefined filter, alternative of a combined pattern or search term matched it.
// A labeler is immutable once built; the filtered view labels lines on a background thread.
class MatchLabeler {
  public:
    MatchLabeler() = default;

    // A term or alternative of the search that matches a line.
    struct Match {
        // What matched: "Modem: WiFi" for the alternative "WiFi" of the predefined filter
        // "Modem", the alternative alone for a filter without a name of its own or a typed
        // search, or the predefined filter or the search term.
        QString name;
        // The text it matched on the line. Anchors, lookarounds and ".*" at the ends of the
        // alternative are left out, so "^(?!.*X).*ERROR" gives "ERROR". When it matched nothing
        // or the whole line, the alternative itself is shown (see alternativeDisplayName).
        QString text;
    };

    // What the "Matched by" cell of a line shows.
    struct Annotation {
        // The text of each match, joined with ", "; a text matched twice is listed once.
        QString text;
        // Rich text: a table of the matches, the name of each with its text.
        QString toolTip;
    };

    // `filters` are the predefined filters the search text was built from. Pass them only while
    // the search text still equals their combination; otherwise the pattern itself is split.
    // Matches of a search built from filters are named after the filters, even for one.
    // `knownFilters` are all predefined filters: a quoted term of a boolean search whose pattern
    // is one of them is named like that filter, so a search restored from the session or the
    // history ("p1" or "p2") still names its filters and the alternatives that matched.
    static MatchLabeler fromSearch( const RegularExpressionPattern& search,
                                    const QList<PredefinedFilter>& filters = {},
                                    const QList<PredefinedFilter>& knownFilters = {} );

    // Top-level `|` alternatives of a regular expression; escapes, character classes and groups
    // are kept intact.
    static QStringList splitTopLevelAlternatives( const QString& regex );

    // The top-level inline option settings of `regex`, such as "(?i)", in order. PCRE keeps such
    // a setting in force for the alternatives after the one that contains it, so they are put
    // in front of each later alternative when the alternatives are compiled one by one.
    static QString topLevelOptionSettings( const QString& regex );

    bool isEmpty() const
    {
        return terms_.empty();
    }

    // The terms that match `line`, in the order of the search. A predefined filter or quoted
    // term made of several top-level alternatives gives one match per alternative that matched.
    std::vector<Match> matches( const QString& line ) const;

    // The cell text and tooltip of `line`; both are empty when nothing matches.
    Annotation annotate( const QString& line ) const;

    // The cell text alone: annotate( line ).text.
    QString label( const QString& line ) const;

    // How an alternative is shown in a label: a lone capturing group "(x)" is shown as "x";
    // anchors, lookarounds, inline option settings and ".*" at its ends are left out, and what is
    // left is unescaped when it is plain text, so "(^(?!.*\+CME).*ERROR)" is shown as "ERROR"
    // and "(\[TX\])" as "[TX]".
    static QString alternativeDisplayName( const QString& alternative );

    // A column width, in characters, that fits typical labels.
    int preferredColumnChars() const;

    // Labels, and the texts in a tooltip, are cut here; the view elides a label further to its
    // column.
    static constexpr int MaxLabelLength = 256;
    // Rows of a tooltip; more matches end it with a count of the rest.
    static constexpr int MaxToolTipRows = 16;

private:
    // A top-level alternative of a predefined filter, compiled on its own. Its `expression`
    // captures the text that matters (see Match::text) when it can.
    struct Part {
        QString name;
        QRegularExpression expression;
    };

    struct Term {
        QString name;
        QRegularExpression expression;
        // Alternatives of a predefined filter with several of them; empty otherwise.
        std::vector<Part> parts;
        // A match of an alternative is named "<name>: <alternative>" (a filter with a name of
        // its own, not just its pattern).
        bool namePrefix = false;
        // How the pattern is shown when its match says nothing (empty or the whole line).
        QString shownPattern;
    };

    // What a term is built from, before its expressions are compiled.
    struct TermSpec {
        QString name;
        RegularExpressionPattern pattern;
        QList<RegularExpressionPattern> partPatterns;
        QStringList partNames;
        bool namePrefix = false;
    };

    struct Alternative {
        QString text;
        // Top-level option settings inside `text`, in order.
        QString optionSettings;
    };

    static std::vector<Alternative> scanTopLevelAlternatives( const QString& regex );
    static MatchLabeler fromTerms( const QList<RegularExpressionPattern>& patterns,
                                   const QStringList& names );
    static MatchLabeler fromSpecs( const std::vector<TermSpec>& specs );

    std::vector<Term> terms_;
};

#endif
