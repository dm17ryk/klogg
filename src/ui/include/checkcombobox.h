// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QComboBox>
#include <QIcon>
#include <QJsonArray>
#include <functional>

class QStandardItem;
class QStandardItemModel;

// A dropdown whose items are checkboxes (the plugin views `multichoice` editor and field kind).
// The popup stays open while items are toggled; Esc restores the set it opened with, or the set
// last given to setCheckedValues() while it was open.
// Choices, labels and values are capped like a `choice` field's (PluginView limits).
class CheckComboBox : public QComboBox {
    Q_OBJECT
  public:
    explicit CheckComboBox( QWidget* parent = nullptr );
    // Checked values that are not in the new list are dropped, without emitting checkedChanged.
    // Plain values or choice objects: icons through `icons`, tooltips, disabled entries,
    // separators and section titles (docs/plugin-sdk-advanced.md section 8.1).
    void setChoices( const QJsonArray& choices,
                     const std::function<QIcon( const QJsonValue& )>& icons = {} );
    // Emits checkedChanged only when the checked set changes. While the popup is open, the new set
    // is also what Esc restores.
    void setCheckedValues( const QJsonArray& values );
    QJsonArray checkedValues() const;
    QString summary() const;
    // E17c `searchable`: typing filters the open list (pluginchoices::PopupFilter); Space still
    // toggles.
    void makeSearchable();
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
