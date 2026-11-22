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

// Explains which part of a search selected a line of the filtered view: the predefined filters
// the search was built from, the alternatives of a combined pattern, or the matched text.
// A labeler is immutable once built; the filtered view labels lines on a background thread.
class MatchLabeler {
  public:
    MatchLabeler() = default;

    // `filters` are the predefined filters the search text was built from. Pass them only while
    // the search text still equals their combination; otherwise the pattern itself is split.
    // Lines of a search built from filters are labelled with the filter names, even for one.
    // `knownFilters` are all predefined filters: a quoted term of a boolean search whose pattern
    // is one of them is labelled like that filter, so a search restored from the session or the
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

    // The names of the terms that match `line`, joined with ", ". A typed search with a single
    // term reports the matched text instead. A predefined filter made of several top-level
    // alternatives reports the alternatives that matched, after the filter name when the filter
    // has one of its own. Empty when nothing matches.
    QString label( const QString& line ) const;

    // How an alternative is shown in a label: a lone capturing group "(x)" is shown as "x";
    // anchors, lookarounds, inline option settings and ".*" at its ends are left out, and what is
    // left is unescaped when it is plain text, so "(^(?!.*\+CME).*ERROR)" is shown as "ERROR"
    // and "(\[TX\])" as "[TX]".
    static QString alternativeDisplayName( const QString& alternative );

    // A column width, in characters, that fits typical labels.
    int preferredColumnChars() const;

    // Labels are cut here; the view elides them further to its column and shows the label in a
    // tooltip.
    static constexpr int MaxLabelLength = 256;

private:
    // A top-level alternative of a predefined filter, compiled on its own.
    struct Part {
        QString name;
        QRegularExpression expression;
    };

    struct Term {
        QString name;
        QRegularExpression expression;
        // Alternatives of a predefined filter with several of them; empty otherwise.
        std::vector<Part> parts;
        // The label starts with `name` before the matched parts (a filter with a name of its
        // own, not just its pattern).
        bool namePrefix = false;
    };

    // What a term is built from, before its expressions are compiled.
    struct TermSpec {
        QString name;
        RegularExpressionPattern pattern;
        QList<RegularExpressionPattern> partPatterns;
        QStringList partNames;
        bool namePrefix = false;
    };

    QString termLabel( const Term& term, const QString& line ) const;

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
    // The terms are predefined filters: lines are labelled with their names.
    bool namesFilters_ = false;
};

#endif
