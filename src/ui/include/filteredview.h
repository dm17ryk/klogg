/*
 * Copyright (C) 2009, 2010, 2012 Nicolas Bonnefon and other contributors
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

#ifndef FILTEREDVIEW_H
#define FILTEREDVIEW_H

#include "abstractlogview.h"

#include "logfiltereddata.h"
#include "matchlabeler.h"

#include <memory>

#include <QHash>
#include <QKeyEvent>

class QAction;

// Class implementing the filtered (bottom) view widget.
class FilteredView : public AbstractLogView {
    Q_OBJECT
public:
    FilteredView( LogFilteredData* newLogData, const QuickFindPattern* const quickFindPattern,
                  QWidget* parent = nullptr );

    // What is visible in the view.
    using Visibility = LogFilteredData::Visibility;
    void setVisibility( Visibility visi );
    Visibility visibility() const;

    // Names the filter that selected each line in the "Matched by" column. A null or empty
    // labeler hides the column.
    void setMatchLabeler( std::shared_ptr<const MatchLabeler> labeler );
    void setMatchedByColumnVisible( bool visible );
    bool isMatchedByColumnVisible() const
    {
        return matchedByVisible_;
    }
    // The "Matched by" text of the filtered line at `index`.
    QString matchedByText( LineNumber index ) const;

Q_SIGNALS:
    void matchedByColumnToggled( bool visible );
    void clearResultsRequested();

protected:
    LogFilteredData::LineType lineType( LineNumber lineNumber ) const override;
    bool groupSeparatorBefore( LineNumber lineNumber ) const override;

    // Number of the filtered line relative to the unfiltered source
    LineNumber displayLineNumber( LineNumber lineNumber ) const override;
    LineNumber lineIndex( LineNumber lineNumber ) const override;
    LineNumber maxDisplayLineNumber() const override;

    int annotationColumnChars() const override;
    QString lineAnnotation( LineNumber lineNumber, const QString& lineText ) const override;

    void doRegisterShortcuts() override;

private:
    static constexpr int MaxCachedAnnotations = 4096;

    LogFilteredData* logFilteredData_;
    std::shared_ptr<const MatchLabeler> matchLabeler_;
    bool matchedByVisible_ = true;
    QAction* matchedByAction_ = nullptr;
    QAction* clearResultsAction_ = nullptr;
    // Labels of visible lines by source line number; reset with the labeler. The text length is
    // kept so a line that was still being written is labelled again once it grows.
    struct CachedAnnotation {
        qsizetype textSize = 0;
        QString label;
    };
    mutable QHash<quint64, CachedAnnotation> annotationCache_;
};

#endif
