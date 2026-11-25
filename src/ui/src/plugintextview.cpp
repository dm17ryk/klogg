// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugintextview.h"

#include "pluginfeatures.h"
#include <QEvent>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPointer>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <utility>

using cilogg::plugins::MaxTextViewChars;

PluginTextBrowser::PluginTextBrowser( QWidget* parent )
    : QTextBrowser( parent )
{
    // Links only send an event; the host never follows them.
    setOpenLinks( false );
    setOpenExternalLinks( false );
    setUndoRedoEnabled( false );
}

QVariant PluginTextBrowser::loadResource( int type, const QUrl& name )
{
    // Never the base implementation: it reads files relative to the search paths.
    if ( type != QTextDocument::ImageResource || !imageFor ) {
        return {};
    }
    const auto image = imageFor( name.toString() );
    return image.isNull() ? QVariant{} : QVariant( image );
}

PluginTextView::PluginTextView( const QString& objectName, QWidget* parent )
    : QWidget( parent )
{
    auto* layout = new QVBoxLayout( this );
    layout->setContentsMargins( 0, 0, 0, 0 );
    layout->setSpacing( 2 );
    find_ = new QLineEdit( this );
    find_->setObjectName( objectName + "_find" );
    find_->setPlaceholderText( tr( "Find" ) );
    find_->setAccessibleName( tr( "Find" ) );
    find_->setClearButtonEnabled( true );
    find_->setMaxLength( 1024 );
    find_->setVisible( false );
    find_->installEventFilter( this );
    layout->addWidget( find_ );
    browser_ = new PluginTextBrowser( this );
    browser_->setObjectName( objectName );
    browser_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    layout->addWidget( browser_, 1 );
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    connect( browser_, &QTextBrowser::anchorClicked, this, [ this ]( const QUrl& url ) {
        Q_EMIT linkActivated( url.toString().left( 4096 ) );
    } );
    // Ctrl+F is claimed by the text itself (ShortcutOverride), so CILogg's own Find shortcut
    // neither fires nor makes the key ambiguous while the text has the focus.
    browser_->installEventFilter( this );
    connect( find_, &QLineEdit::textChanged, this, [ this ] {
        // Search from the start of the current match, so typing extends it in place.
        auto cursor = browser_->textCursor();
        cursor.setPosition( cursor.selectionStart() );
        browser_->setTextCursor( cursor );
        findNext( false );
    } );
}

QString PluginTextView::tail( const QString& text, int maximum )
{
    if ( text.size() <= maximum ) {
        return text;
    }
    const auto start = text.size() - maximum;
    const auto newline = text.indexOf( QLatin1Char( '\n' ), start - 1 );
    return newline >= 0 && newline + 1 < text.size() ? text.mid( newline + 1 ) : text.mid( start );
}

QString PluginTextView::text() const
{
    return markdown_ ? source_ : browser_->toPlainText();
}

void PluginTextView::setMarkdown( bool markdown )
{
    if ( markdown == markdown_ ) {
        return;
    }
    const auto current = text();
    markdown_ = markdown;
    browser_->document()->setMaximumBlockCount( markdown_ ? 0 : maxLines_ );
    setText( current );
}

void PluginTextView::setText( const QString& text )
{
    renderPending_ = false; // rendered now; a pending Markdown render must not overwrite it
    if ( markdown_ ) {
        source_ = tail( text, MaxTextViewChars );
        render();
    }
    else {
        source_.clear();
        browser_->setPlainText( tail( text, MaxTextViewChars ) );
    }
    followEnd();
}

void PluginTextView::append( const QString& text )
{
    if ( text.isEmpty() ) {
        return;
    }
    if ( markdown_ ) {
        // Markdown is rendered again from its whole source: once per event loop turn, however
        // many appends arrive in it.
        source_ = tail( source_ + text, MaxTextViewChars );
        if ( !std::exchange( renderPending_, true ) ) {
            QTimer::singleShot( 0, this, [ this ] {
                if ( std::exchange( renderPending_, false ) && markdown_ ) {
                    render();
                    followEnd();
                }
            } );
        }
        return;
    }
    else {
        // A cursor of its own: the user's selection and scroll position stay.
        QTextCursor cursor( browser_->document() );
        cursor.movePosition( QTextCursor::End );
        cursor.insertText( text );
        trimPlain();
    }
    followEnd();
}

void PluginTextView::trimPlain()
{
    auto* document = browser_->document();
    const auto excess = document->characterCount() - 1 - MaxTextViewChars;
    if ( excess <= 0 ) {
        return;
    }
    // Whole lines go, from the start, as far as the excess reaches.
    auto end = excess;
    const auto block = document->findBlock( excess );
    if ( block.isValid() && block.position() != excess ) {
        const auto next = block.next();
        end = next.isValid() ? next.position() : excess;
    }
    QTextCursor cursor( document );
    cursor.setPosition( 0 );
    cursor.setPosition( end, QTextCursor::KeepAnchor );
    cursor.removeSelectedText();
}

void PluginTextView::render()
{
    // Markdown is rendered again from its source; the reading position stays unless following.
    auto* bar = browser_->verticalScrollBar();
    const auto position = bar->value();
#if QT_VERSION >= QT_VERSION_CHECK( 5, 14, 0 )
#if QT_CONFIG( textmarkdownreader )
    browser_->document()->setMarkdown(
        source_, QTextDocument::MarkdownFeatures( QTextDocument::MarkdownDialectCommonMark
                                                  | QTextDocument::MarkdownNoHTML ) );
#else
    browser_->setPlainText( source_ );
#endif
#else
    browser_->setPlainText( source_ );
#endif
    bar->setValue( position );
}

void PluginTextView::followEnd()
{
    if ( !follow_ ) {
        return;
    }
    // The scroll range is updated after the layout: now and once more when queued.
    browser_->verticalScrollBar()->setValue( browser_->verticalScrollBar()->maximum() );
    QTimer::singleShot( 0, browser_, [ browser = QPointer<PluginTextBrowser>( browser_ ) ] {
        if ( browser ) {
            browser->verticalScrollBar()->setValue( browser->verticalScrollBar()->maximum() );
        }
    } );
}

void PluginTextView::setMonospace( bool monospace )
{
    browser_->setFont( monospace ? QFontDatabase::systemFont( QFontDatabase::FixedFont ) : QFont() );
}

void PluginTextView::setWrap( bool wrap )
{
    browser_->setLineWrapMode( wrap ? QTextEdit::WidgetWidth : QTextEdit::NoWrap );
}

void PluginTextView::setFollow( bool follow )
{
    // Turning it on goes to the end; a root that sends `follow` again leaves the reader's place
    // (only new text scrolls).
    const bool starts = follow && !follow_;
    follow_ = follow;
    if ( starts ) {
        followEnd();
    }
}

void PluginTextView::setMaxLines( int lines )
{
    maxLines_ = qBound( 0, lines, cilogg::plugins::MaxTextViewLines );
    // Plain text only: the oldest blocks go when the document passes the limit.
    browser_->document()->setMaximumBlockCount( markdown_ ? 0 : maxLines_ );
}

void PluginTextView::showFind()
{
    find_->setVisible( true );
    find_->setFocus( Qt::ShortcutFocusReason );
    find_->selectAll();
}

bool PluginTextView::findNext( bool backward )
{
    const auto needle = find_->text();
    if ( needle.isEmpty() ) {
        return false;
    }
    QTextDocument::FindFlags flags;
    if ( backward ) {
        flags |= QTextDocument::FindBackward;
    }
    if ( browser_->find( needle, flags ) ) {
        return true;
    }
    // Wrap around once.
    auto cursor = browser_->textCursor();
    cursor.movePosition( backward ? QTextCursor::End : QTextCursor::Start );
    browser_->setTextCursor( cursor );
    return browser_->find( needle, flags );
}

bool PluginTextView::eventFilter( QObject* watched, QEvent* event )
{
    // Ctrl+F belongs to the text and its find bar while either has the focus (again in the bar:
    // its text is selected).
    if ( ( watched == browser_ || watched == find_ )
         && ( event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress )
         && static_cast<QKeyEvent*>( event )->matches( QKeySequence::Find ) ) {
        event->accept();
        if ( event->type() == QEvent::KeyPress ) {
            showFind();
        }
        return true;
    }
    if ( watched == find_ && event->type() == QEvent::KeyPress ) {
        const auto* key = static_cast<QKeyEvent*>( event );
        if ( key->key() == Qt::Key_Escape ) {
            find_->setVisible( false );
            browser_->setFocus( Qt::OtherFocusReason );
            return true;
        }
        if ( key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter ) {
            findNext( key->modifiers().testFlag( Qt::ShiftModifier ) );
            return true;
        }
    }
    return QWidget::eventFilter( watched, event );
}
