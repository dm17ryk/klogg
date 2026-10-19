// Copyright (C) 2021 Anton Filimonov and other contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CILOGG_BOOLEAN_PARSER_H
#define CILOGG_BOOLEAN_PARSER_H
#include "containers.h"
#include "regularexpressionpattern.h"

// Replaces quoted leaves with stable evaluator variables, preserving search grammar.
klogg::vector<RegularExpressionPattern> parseBooleanExpressions( QString& expression,
                                                                 bool caseSensitive, bool plainText,
                                                                 MatchMode matchMode );
#endif
