// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CILOGG_ANALYSISTEXTEDIT_H
#define CILOGG_ANALYSISTEXTEDIT_H
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <algorithm>
#include <functional>
#include <map>

class LabTextEdit final : public QPlainTextEdit {
public:
    using QPlainTextEdit::QPlainTextEdit;
    std::function<void()> overflow;

    // Qt's toPlainText() normalizes CRLF, paragraph separators and NBSP. Keep
    // imported separator spelling on the following block's character format;
    // Qt then retains it with that paragraph across editing and undo/redo.
    QString rawText() const
    {
        QString result;
        for ( auto block = document()->begin(); block.isValid(); block = block.next() ) {
            if ( block != document()->begin() )
                result += separator( block );
            result += block.text();
        }
        return result;
    }
    void setRawText( const QString& text )
    {
        const QSignalBlocker blocker( this );
        QPlainTextEdit::clear();
        auto cursor = textCursor();
        cursor.setBlockCharFormat( QTextCharFormat{} );
        insertRawText( text );
        document()->clearUndoRedoStacks();
    }

protected:
    void inputMethodEvent( QInputMethodEvent* event ) override
    {
        // Include replacement/selection bounds; the final Run guard also checks
        // exact raw units after Qt has applied inherited separator formatting.
        if ( rawText().size() - selectedRawUnits() + event->commitString().size() > TextUnits ) {
            if ( overflow )
                overflow();
            event->ignore();
            return;
        }
        QPlainTextEdit::inputMethodEvent( event );
    }
    void insertFromMimeData( const QMimeData* mime ) override
    {
        if ( !mime->hasText() ) {
            QPlainTextEdit::insertFromMimeData( mime );
            return;
        }
        const auto text = mime->text();
        if ( rawText().size() - selectedRawUnits() + text.size() > TextUnits ) {
            if ( overflow )
                overflow();
            return;
        }
        insertRawText( text );
    }
    void keyPressEvent( QKeyEvent* event ) override
    {
        const auto editingShortcut
            = event->matches( QKeySequence::Copy ) || event->matches( QKeySequence::Cut )
              || event->matches( QKeySequence::Paste ) || event->matches( QKeySequence::Undo )
              || event->matches( QKeySequence::Redo ) || event->matches( QKeySequence::SelectAll );
        if ( !editingShortcut && event->key() != Qt::Key_Backspace && event->key() != Qt::Key_Delete
             && !event->text().isEmpty()
             && rawText().size() - selectedRawUnits() + event->text().size() > TextUnits ) {
            if ( overflow )
                overflow();
            return;
        }
        QPlainTextEdit::keyPressEvent( event );
    }

private:
    static constexpr int TextUnits = 2 * 1024 * 1024;
    static constexpr int RawSeparator = QTextFormat::UserProperty + 41;
    static QString separator( const QTextBlock& block )
    {
        const auto value = block.charFormat().property( RawSeparator ).toString();
        return value.isEmpty() ? QStringLiteral( "\n" ) : value;
    }
    int selectedRawUnits() const
    {
        const auto cursor = textCursor();
        const auto start = cursor.selectionStart();
        const auto end = cursor.selectionEnd();
        int units = 0;
        for ( auto block = document()->findBlock( start );
              block.isValid() && block.position() < end; block = block.next() ) {
            const auto textEnd = block.position() + block.length() - 1;
            units += std::max( 0, std::min( end, textEnd ) - std::max( start, block.position() ) );
            if ( block.next().isValid() && start <= textEnd && end > textEnd )
                units += static_cast<int>( separator( block.next() ).size() );
        }
        return units;
    }
    void insertRawText( const QString& text )
    {
        QString normalized;
        QStringList separators;
        normalized.reserve( text.size() );
        for ( decltype( text.size() ) index = 0; index < text.size(); ++index ) {
            const auto character = text.at( index );
            if ( character == '\r' || character == '\n'
                 || character == QChar::ParagraphSeparator ) {
                auto ending = character == '\r'
                                  ? QStringLiteral( "\r" )
                                  : ( character == '\n' ? QStringLiteral( "\n" )
                                                        : QStringLiteral( "\u2029" ) );
                if ( character == '\r' && index + 1 < text.size()
                     && text.at( index + 1 ) == '\n' ) {
                    ending = QStringLiteral( "\r\n" );
                    ++index;
                }
                separators.push_back( ending );
                normalized += '\n';
            }
            else
                normalized += character;
        }
        auto cursor = textCursor();
        const auto start = cursor.selectionStart();
        std::map<QString, int> counts;
        for ( const auto& ending : separators )
            ++counts[ ending ];
        auto preferred = QStringLiteral( "\n" );
        int maximum = 0;
        for ( const auto& item : counts )
            if ( item.second > maximum ) {
                preferred = item.first;
                maximum = item.second;
            }
        auto format = cursor.charFormat();
        format.setProperty( RawSeparator, preferred );
        cursor.beginEditBlock();
        cursor.insertText( normalized, format );
        const auto initialBlock = document()->findBlock( start ).blockNumber();
        for ( int index = 0; index < separators.size(); ++index ) {
            if ( separators.at( index ) == preferred )
                continue;
            const auto block = document()->findBlockByNumber( initialBlock + index + 1 );
            if ( !block.isValid() )
                break;
            QTextCharFormat spelling;
            spelling.setProperty( RawSeparator, separators.at( index ) );
            QTextCursor marker( block );
            marker.mergeBlockCharFormat( spelling );
        }
        cursor.endEditBlock();
        setTextCursor( cursor );
    }
};
#endif
