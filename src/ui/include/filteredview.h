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

#include <atomic>
#include <cstddef>
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
    ~FilteredView() override;

    // What is visible in the view.
    using Visibility = LogFilteredData::Visibility;
    void setVisibility( Visibility visi );
    Visibility visibility() const;

    // Names the filter that selected each line in the "Matched by" column. A null or empty
    // labeler leaves only the "mark" labels of marked lines.
    void setMatchLabeler( std::shared_ptr<const MatchLabeler> labeler );
    void setMatchedByColumnVisible( bool visible );
    bool isMatchedByColumnVisible() const
    {
        return matchedByVisible_;
    }
    // The "Matched by" text of the filtered line at `index`. Labels are computed in the
    // background: this is empty until the label of the line has arrived (asking queues it).
    QString matchedByText( LineNumber index ) const;

    // Width of the "Matched by" column in characters; 0 hides it.
    int annotationColumnChars() const override;

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

    QString lineAnnotation( LineNumber lineNumber, const QString& lineText ) const override;

    void doRegisterShortcuts() override;

private:
    static constexpr int MaxCachedAnnotations = 4096;
    // Labels being computed at once; more lines are asked for again on a later paint.
    static constexpr int MaxPendingAnnotations = 256;

    // Queues the label of a line on the labelling thread; storeAnnotation() receives it.
    void requestAnnotation( quint64 sourceLine, const QString& lineText,
                            std::size_t textHash ) const;
    void storeAnnotation( quint64 labelerGeneration, quint64 sourceLine, std::size_t textHash,
                          qsizetype textSize, const QString& label );

    LogFilteredData* logFilteredData_;
    std::shared_ptr<const MatchLabeler> matchLabeler_;
    // Changes with the labeler and when the view goes away: queued labels of an older labeler
    // are skipped and their late answers dropped.
    std::shared_ptr<std::atomic<quint64>> labelerGeneration_;
    bool matchedByVisible_ = true;
    QAction* matchedByAction_ = nullptr;
    QAction* clearResultsAction_ = nullptr;
    // Labels by source line number; reset with the labeler. A label belongs to the text it was
    // computed from, so a line rewritten in place (or still being written) is labelled again.
    struct CachedAnnotation {
        std::size_t textHash = 0;
        qsizetype textSize = 0;
        QString label;
    };
    mutable QHash<quint64, CachedAnnotation> annotationCache_;
    // Lines whose label is being computed, with the hash of the text sent.
    mutable QHash<quint64, std::size_t> pendingAnnotations_;
};

#endif
