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
class MatchLabeler {
  public:
    MatchLabeler() = default;

    // `filters` are the predefined filters the search text was built from. Pass them only while
    // the search text still equals their combination; otherwise the pattern itself is split.
    static MatchLabeler fromSearch( const RegularExpressionPattern& search,
                                    const QList<PredefinedFilter>& filters = {} );

    // Top-level `|` alternatives of a regular expression; escapes, character classes and groups
    // are kept intact.
    static QStringList splitTopLevelAlternatives( const QString& regex );

    bool isEmpty() const
    {
        return terms_.empty();
    }

    // The names of the terms that match `line`, joined with ", ". A search with a single term
    // reports the matched text instead. Empty when nothing matches.
    QString label( const QString& line ) const;

    // A column width, in characters, that fits typical labels.
    int preferredColumnChars() const;

    static constexpr int MaxLabelLength = 64;

  private:
    struct Term {
        QString name;
        QRegularExpression expression;
    };

    static MatchLabeler fromTerms( const QList<RegularExpressionPattern>& patterns,
                                   const QStringList& names );

    std::vector<Term> terms_;
};

#endif
