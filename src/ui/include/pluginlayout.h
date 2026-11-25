// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPointer>
#include <QWidget>

class QLabel;
class QPushButton;

// The busy overlay of a plugin view container (docs/plugin-sdk-advanced.md section 29.3): a
// child of the container that covers its contents (`covered`, which the view disables while busy)
// with a busy indicator, a text and an optional Cancel button.
class PluginBusyOverlay final : public QWidget {
    Q_OBJECT
public:
    PluginBusyOverlay( QWidget* container, QWidget* covered, const QString& objectName );

    void setText( const QString& text );
    void setCancellable( bool cancellable );
    // Cancel is available again (a new busy period).
    void resetCancel();
    QPushButton* cancelButton() const
    {
        return cancel_;
    }
    QLabel* label() const
    {
        return text_;
    }

Q_SIGNALS:
    void cancelRequested();

protected:
    bool eventFilter( QObject* watched, QEvent* event ) override;
    void paintEvent( QPaintEvent* event ) override;
    void showEvent( QShowEvent* event ) override;

private:
    void follow();

    QPointer<QWidget> covered_;
    QLabel* text_ = nullptr;
    QPushButton* cancel_ = nullptr;
};
