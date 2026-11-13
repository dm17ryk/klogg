// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QComboBox>
#include <QJsonArray>

class QStandardItem;
class QStandardItemModel;

// A dropdown whose items are checkboxes (the plugin views `multichoice` editor and field kind).
// The popup stays open while items are toggled; Esc restores the set it opened with.
class CheckComboBox : public QComboBox {
    Q_OBJECT
  public:
    explicit CheckComboBox( QWidget* parent = nullptr );
    // Checked values that are not in the new list are dropped, without emitting checkedChanged.
    void setChoices( const QJsonArray& choices );
    void setCheckedValues( const QJsonArray& values );
    QJsonArray checkedValues() const;
    QString summary() const;
    void showPopup() override;
    void hidePopup() override;

  Q_SIGNALS:
    void checkedChanged();
    void popupClosed( bool accepted );

  protected:
    bool eventFilter( QObject* watched, QEvent* event ) override;
    void paintEvent( QPaintEvent* event ) override;

  private:
    void toggle( QStandardItem* item );
    void finishClose();

    QStandardItemModel* items_;
    QJsonArray openedWith_;
    bool keepOpen_ = false;
    bool cancelled_ = false;
    bool populating_ = false;
    bool open_ = false;
    bool pressedInView_ = false;
};
