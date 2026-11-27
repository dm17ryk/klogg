/*
 * Copyright (C) 2009, 2010 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2019 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef highlighterSet_H
#define highlighterSet_H

#include <QColor>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QRegularExpression>
#include <QVector>
#include <memory>
#include <optional>
#include <qcolor.h>
#include <qregularexpression.h>

#include "containers.h"
#include "highlightedmatch.h"
#include "persistable.h"
#include "regularexpression.h"
#include "regularexpressionpattern.h"

struct HighlightColor {
    QColor foreColor;
    QColor backColor;
};

// Represents a filter, i.e. a regexp and the colors matching text
// should be rendered in.
class Highlighter {
  public:
    // Construct an uninitialized Highlighter (when reading from a config file)
    Highlighter() = default;
    Highlighter( const QString& pattern, bool ignoreCase, bool onlyMatch, const QColor& foreColor,
                 const QColor& backColor );

    bool matchLine( const QString& line, klogg::vector<HighlightedMatch>& matches ) const;

    // Accessor functions
    QString pattern() const;
    void setPattern( const QString& pattern );
    bool ignoreCase() const;
    void setIgnoreCase( bool ignoreCase );

    bool highlightOnlyMatch() const;
    void setHighlightOnlyMatch( bool onlyMatch );

    bool useRegex() const;
    void setUseRegex( bool useRegex );

    bool variateColors() const;
    void setVariateColors( bool variateColors );

    // How far (in percent) "Variate colors" may darken or lighten a match's colors. Values outside
    // 0 to MaxColorVariance, from a hand-edited or imported file, are clamped (#170): they would
    // give the colour distribution bounds in the wrong order or overflow them. Configuration packs
    // refuse values past the same limit (MaxColorVariance in src/plugins/src/packcanonical.cpp).
    static constexpr int MaxColorVariance = 100;
    int colorVariance() const;
    void setColorVariance( int colorVariance );

    const QColor& foreColor() const;
    void setForeColor( const QColor& foreColor );
    const QColor& backColor() const;
    void setBackColor( const QColor& backColor );

    // Reads/writes the current config in the QSettings object passed
    void saveToStorage( QSettings& settings ) const;
    void retrieveFromStorage( QSettings& settings );

    RegularExpressionPattern expressionPattern() const;

    void compile() const;

  private:
    std::pair<QColor, QColor> vairateColors( const QString& match ) const;

  private:
    QRegularExpression regexp_;
    
    mutable std::optional<QRegularExpression> optimizedRegexp_;

    bool useRegex_ = true;
    bool highlightOnlyMatch_ = false;
    bool variateColors_ = false;
    int colorVariance_ = 15;

    HighlightColor color_;
};

enum class HighlighterMatchType { NoMatch, WordMatch, LineMatch };

// Represents an ordered set of filters to be applied to each line displayed.
class HighlighterSet {
  public:
    static const char* persistableName()
    {
        return "HighlighterSet";
    }

    static HighlighterSet createNewSet( const QString& name );

    HighlighterSet() = default;

    QString name() const;
    QString id() const;
    const QList<Highlighter>& highlighters() const;
    QList<Highlighter>& highlighters();

    // Returns whether the passed line matches a filter of the set,
    // if so, it returns the fore/back colors the line should use.
    HighlighterMatchType matchLine( const QString& line,
                                    HighlightedMatchRanges& matches ) const;

    bool isEmpty() const;

    // Reads/writes the current config in the QSettings object passed
    void saveToStorage( QSettings& settings ) const;
    void retrieveFromStorage( QSettings& settings );

    void compile() const;
    void invalidateCompiled() const;

  private:
    explicit HighlighterSet( const QString& name );

  private:
    static constexpr int HighlighterSet_VERSION = 3;
    static constexpr int FilterSet_VERSION = 2;

    QString name_;
    QString id_;
    QList<Highlighter> highlighterList_;

    // To simplify this class interface, HighlightersDialog can access our
    // internal structure directly.
    friend class HighlighterSetEdit;
    friend class HighlighterSetCollection;

    mutable std::shared_ptr<MultiRegularExpression> compiledExpression_;
    mutable QVector<int> compiledPatternToHighlighterIndex_;
    mutable QString compiledPatternSignature_;
};

struct QuickHighlighter {
    QString name;
    HighlightColor color;
    bool useInCycle;
};

// A highlighter set as the lists offer it (E16c2): the user's own (packId empty) or a set of an
// enabled configuration pack (docs/plugin-repositories.md 4.5), with its pack's ID and name.
struct ListedHighlighterSet {
    HighlighterSet set;
    QString packId;
    QString packName;
};

// The most highlighter sets the lists offer, the user's and the enabled packs' together: as many
// as a configuration import takes. Enabling a pack past it is refused; when the user's own sets
// grow past it, the packs' sets give way (as their previews and filters do).
constexpr int MaxListedHighlighterSets = 4096;

// The highlighter sets of the enabled configuration packs, set by PreviewPackRegistry. They are
// kept apart from the user's sets: never saved, exported or put in a project snapshot. Which of
// them are active is the user's state (HighlighterSetCollection's active sets, by id).
class PackHighlighters final : public QObject {
    Q_OBJECT
  public:
    static PackHighlighters& instance();
    // The packs' sets (packs in ID order, each in its own order), with packId and packName.
    void setSets( const QList<ListedHighlighterSet>& sets );
    const QList<ListedHighlighterSet>& sets() const
    {
        return sets_;
    }

  Q_SIGNALS:
    void changed();

  private:
    PackHighlighters() = default;
    QList<ListedHighlighterSet> sets_;
};

class HighlighterSetCollection final : public Persistable<HighlighterSetCollection> {
  public:
    static const char* persistableName()
    {
        return "HighlighterSetCollection";
    }

    // The user's own sets (saved, exported, put in project snapshots).
    const QList<HighlighterSet>& highlighterSets() const;
    QList<HighlighterSet>& highlighterSets();
    void setHighlighterSets( const QList<HighlighterSet>& highlighters );

    // The sets the lists offer: the user's, then the enabled packs' whose id the user has not
    // taken (E16c2), unless the packs' sets give way past MaxListedHighlighterSets.
    QList<ListedHighlighterSet> listedSets() const;
    // Applies a change of the packs' sets to the active highlighting.
    void refreshPackSets();

    const HighlighterSet& currentActiveSet() const;

    bool hasSet( const QString& setId ) const;
    bool hasSetByName( const QString& name ) const;

    QStringList activeSetIds() const;
    void activateSet( const QString& setId );
    void deactivateSet( const QString& setId );
    void deactivateAll();

    QList<QuickHighlighter> quickHighlighters() const;
    void setQuickHighlighters( const QList<QuickHighlighter>& quickHighlighters );

    // Reads/writes the current config in the QSettings object passed
    void saveToStorage( QSettings& settings ) const;
    void retrieveFromStorage( QSettings& settings );

  private:
    static constexpr int HighlighterSetCollection_VERSION = 2;

    void updateCombinedSet();

  private:
    QList<HighlighterSet> highlighters_;
    QStringList activeSets_;
    HighlighterSet combinedActiveSet_;

    QList<QuickHighlighter> quickHighlighters_;

    // To simplify this class interface, HighlightersDialog can access our
    // internal structure directly.
    friend class HighlightersDialog;
};

#endif
