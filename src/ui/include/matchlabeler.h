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
    static MatchLabeler fromSearch( const RegularExpressionPattern& search,
                                    const QList<PredefinedFilter>& filters = {} );

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
    // term reports the matched text instead. Empty when nothing matches.
    QString label( const QString& line ) const;

    // A column width, in characters, that fits typical labels.
    int preferredColumnChars() const;

    static constexpr int MaxLabelLength = 64;

  private:
    struct Term {
        QString name;
        QRegularExpression expression;
    };

    struct Alternative {
        QString text;
        // Top-level option settings inside `text`, in order.
        QString optionSettings;
    };

    static std::vector<Alternative> scanTopLevelAlternatives( const QString& regex );
    static MatchLabeler fromTerms( const QList<RegularExpressionPattern>& patterns,
                                   const QStringList& names );

    std::vector<Term> terms_;
    // The terms are predefined filters: lines are labelled with their names.
    bool namesFilters_ = false;
};

#endif
