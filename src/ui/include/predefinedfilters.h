/*
 * Copyright (C) 2009, 2010, 2011, 2013, 2014, 2015 Nicolas Bonnefon
 * and other contributors
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
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
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

#ifndef PREDEFINEDFILTERS_H_
#define PREDEFINEDFILTERS_H_

#include <QList>
#include <QObject>
#include <QString>

#include "persistable.h"

struct PredefinedFilter {
    QString id;
    QString name;
    QString pattern;
    bool useRegex;
};

// A filter as the filter lists offer it (E16c): the user's own (packId empty) or a filter of an
// enabled configuration pack (docs/plugin-repositories.md 4.5), with its pack's ID and name.
struct ListedFilter {
    PredefinedFilter filter;
    QString packId;
    QString packName;
};

// Represents collection of filters read from settings file.
class PredefinedFiltersCollection final : public Persistable<PredefinedFiltersCollection> {
  public:
    using Collection = QList<PredefinedFilter>;

    static const char* persistableName()
    {
        return "PredefinedFiltersCollection";
    }

    Collection getSyncedFilters();
    Collection getFilters() const;
    void setFilters( const Collection& filters );

    void retrieveFromStorage( QSettings& settings );
    void saveToStorage( QSettings& settings ) const;
    void saveToStorage( const Collection& filters );

  private:
    static constexpr int PredefinedFiltersCollection_VERSION = 3;

    Collection filters_;
};

QString createPredefinedFilterId();

// The predefined filters of the enabled configuration packs (docs/plugin-repositories.md 4.5),
// set by PreviewPackRegistry. They are kept apart from the user's filters: never saved, exported
// or put in a project snapshot.
class PackFilters final : public QObject {
    Q_OBJECT
public:
    static PackFilters& instance();
    // The packs' filters (packs in ID order, each in its own order), with packId and packName.
    void setFilters( const QList<ListedFilter>& filters );
    const QList<ListedFilter>& filters() const
    {
        return filters_;
    }

Q_SIGNALS:
    void changed();

private:
    PackFilters() = default;
    QList<ListedFilter> filters_;
};

// The most predefined filters the lists offer, the user's and the enabled packs' together: as many
// as a configuration import takes. Enabling a pack past it is refused; the user's own filters are
// not limited, and when they grow past it the packs' filters give way (as pack previews do).
constexpr int MaxListedFilters = 4096;

// Whether the enabled packs' filters give way: the user's filters and the packs' that are listed
// (not replaced by one of the user's) would be more than MaxListedFilters.
bool packFiltersGiveWay( const QList<PredefinedFilter>& own );

// The filters the filter lists offer: the user's own, then the enabled packs' (a pack filter
// whose id the user has is replaced by the user's), unless the packs' filters give way.
QList<ListedFilter> effectivePredefinedFilters( const QList<PredefinedFilter>& own );

Q_DECLARE_METATYPE(PredefinedFilter)

#endif
