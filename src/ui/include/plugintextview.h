// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QImage>
#include <QTextBrowser>
#include <QWidget>
#include <functional>

class QLineEdit;

// The read-only text of a `text_view` element: Markdown images come only from icon references
// (the `imageFor` hook) and nothing is ever loaded from files or the network.
class PluginTextBrowser final : public QTextBrowser {
    Q_OBJECT
public:
    explicit PluginTextBrowser( QWidget* parent = nullptr );
    // An icon reference as an image; a null image shows nothing.
    std::function<QImage( const QString& reference )> imageFor;

    QVariant loadResource( int type, const QUrl& name ) override;
};

// A plugin `text_view` (docs/plugin-sdk-advanced.md section 29.2): plain text or Markdown with
// HTML off, appended text, a line limit, following new text and a Ctrl+F find bar.
class PluginTextView final : public QWidget {
    Q_OBJECT
public:
    explicit PluginTextView( const QString& objectName, QWidget* parent = nullptr );

    PluginTextBrowser* browser() const
    {
        return browser_;
    }
    QLineEdit* findBar() const
    {
        return find_;
    }
    // The text as the plugin sent it (Markdown source), bounded by MaxTextViewChars.
    QString text() const;
    bool markdown() const
    {
        return markdown_;
    }
    void setMarkdown( bool markdown );
    void setText( const QString& text );
    // Adds text at the end; the oldest lines go past the limits.
    void append( const QString& text );
    void setMonospace( bool monospace );
    void setWrap( bool wrap );
    void setFollow( bool follow );
    void setMaxLines( int lines );
    // Shows the find bar (Ctrl+F) and finds the next or previous match of its text.
    void showFind();
    bool findNext( bool backward );

    // The tail of `text` within `maximum` characters, starting at a line where possible.
    static QString tail( const QString& text, int maximum );

Q_SIGNALS:
    void linkActivated( const QString& href );

protected:
    bool eventFilter( QObject* watched, QEvent* event ) override;

private:
    void render();
    void trimPlain();
    void followEnd();

    PluginTextBrowser* browser_ = nullptr;
    QLineEdit* find_ = nullptr;
    QString source_; // the Markdown source
    bool markdown_ = false;
    bool follow_ = false;
    int maxLines_ = 0;
};
