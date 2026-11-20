// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QIcon>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QPointer>
#include <QStyledItemDelegate>
#include <QWidget>
#include <functional>

class QAbstractButton;
class QAbstractItemView;
class QButtonGroup;
class QComboBox;
class QCompleter;
class QGridLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QStandardItemModel;
class QTimer;

// Choices, check boxes, radio buttons, completion and validation of plugin views
// (docs/plugin-sdk-advanced.md section 8).
namespace pluginchoices {
struct Choice {
    enum class Type { Value, Separator, Section };
    Type type = Type::Value;
    QJsonValue value;
    QString label;
    QJsonValue icon;
    QString tooltip;
    bool enabled = true;
};
using IconResolver = std::function<QIcon( const QJsonValue& reference )>;

// A list of plain values or choice objects with separators and sections; at most `maximum`
// entries, values and labels within the view limits.
QList<Choice> parse( const QJsonArray& choices, int maximum );
// Fills `combo` (item data: the value): icons, plain-text tooltips, disabled choices,
// separators and section titles; `radio` draws a radio mark in the open list.
void fillCombo( QComboBox* combo, const QList<Choice>& choices, const IconResolver& icons,
                bool radio );
// An `editable` choice: the user may type any value; typing also filters the list (any part of a
// label, case-insensitive).
void makeEditable( QComboBox* combo );
// `searchable` for a drop-down filled by fillCombo (a PopupFilter on its open list).
void makeSearchable( QComboBox* combo );
// The item of `combo` whose label is `text`, or -1.
int findLabel( const QComboBox* combo, const QString& text );

// `searchable` drop-downs (section 8.2): while the list is open, typing filters it by any part of
// a label, case-insensitively, and the filter shows above the list; Backspace removes a character.
// Separators and section titles hide while a filter is set. The filter clears when the list
// closes. `isChoice` tells choices from separators and sections. With `spaceToggles`, Space stays
// with the list (a check-dropdown toggles with it) and never filters.
class PopupFilter final : public QObject {
    Q_OBJECT
public:
    using IsChoice = std::function<bool( const QModelIndex& index )>;
    PopupFilter( QComboBox* combo, IsChoice isChoice, bool spaceToggles );
    QString text() const
    {
        return filter_;
    }
    // Sets the filter as typing would (tests and automation).
    void setText( const QString& text );
    static constexpr int MaxChars = 128;

protected:
    bool eventFilter( QObject* watched, QEvent* event ) override;

private:
    void apply();
    QPointer<QComboBox> combo_;
    // The open list and its container, kept from the start: the filter never asks the combo box
    // for them, since Qt 6.12 deletes the container while ~QComboBox runs (and its events then
    // reach this filter with the combo box half destroyed).
    QPointer<QAbstractItemView> view_;
    QPointer<QWidget> container_;
    IsChoice isChoice_;
    bool spaceToggles_;
    QString filter_;
    QPointer<QLabel> label_;
};

// Draws a radio mark before each item of a combo box list (indicator: "radio").
class RadioIndicatorDelegate final : public QStyledItemDelegate {
public:
    RadioIndicatorDelegate( QComboBox* combo );
    void paint( QPainter* painter, const QStyleOptionViewItem& option,
                const QModelIndex& index ) const override;
    QSize sizeHint( const QStyleOptionViewItem& option, const QModelIndex& index ) const override;

private:
    QPointer<QComboBox> combo_;
};
} // namespace pluginchoices

// Radio buttons (one value) or check boxes (a list) in a frame, for `radio_group`,
// `check_group` and the inline styles of choice fields. Buttons are named
// `<objectBase>_choice_<n>`, with `<objectBase>_all` and `<objectBase>_none` links.
class ChoiceGroup final : public QWidget {
    Q_OBJECT
public:
    ChoiceGroup( bool exclusive, const QString& objectBase, QWidget* parent );
    void setTitle( const QString& title );
    void setChoices( const QList<pluginchoices::Choice>& choices,
                     const pluginchoices::IconResolver& icons );
    // `vertical` (default) or `horizontal`, in `columns` columns (1 to 12; 0: automatic).
    void setShape( const QString& orientation, int columns );
    void setAllNone( bool shown );
    // Shows only the choices whose label contains `text` (searchable inline lists).
    void filter( const QString& text );
    bool exclusive() const
    {
        return exclusive_;
    }
    // The chosen value (null when none) or the list of checked values.
    QJsonValue value() const;
    void setValue( const QJsonValue& value );

Q_SIGNALS:
    void changed();

private:
    void relayout();
    bool exclusive_;
    QString objectBase_;
    QGroupBox* frame_;
    QGridLayout* grid_;
    QButtonGroup* group_;
    QWidget* links_;
    QList<QWidget*> items_; // buttons, separators and section titles in order
    QList<QAbstractButton*> buttons_;
    QString orientation_ = QStringLiteral( "vertical" );
    QString filter_;
    int columns_ = 0;
    bool setting_ = false;
};

// Plugin completion (`field/complete`) for a line edit: typing asks the provider (debounced);
// the latest answer is offered as a list, labels shown and values inserted.
class FieldCompleter final : public QObject {
    Q_OBJECT
public:
    using Provider = std::function<void( const QString& text, int cursor,
                                         std::function<void( const QJsonArray& items )> done )>;
    FieldCompleter( QLineEdit* edit, Provider provider, pluginchoices::IconResolver icons );
    // The labels last offered (tests and automation).
    QStringList offered() const;
    static constexpr int DebounceMs = 150;
    static constexpr int MaxItems = 100;

private:
    void ask();
    QPointer<QLineEdit> edit_;
    Provider provider_;
    pluginchoices::IconResolver icons_;
    QCompleter* completer_ = nullptr;
    QStandardItemModel* model_ = nullptr;
    QTimer* debounce_ = nullptr;
    quint64 generation_ = 0;
};

namespace pluginfields {
// A `pattern` longer than this checks nothing; a value longer than MaxPatternText is not matched.
constexpr int MaxPatternChars = 256;
constexpr int MaxPatternText = 1024;
// The validation message of a field (section 8.6), empty when valid: the plugin's `error`, then
// `required`, `min_length`/`max_length`, `pattern` (the whole text) and `min`/`max`. A pattern
// that does not compile checks nothing; one that takes too long to match (PCRE2's match limit)
// counts as no match.
QString validationError( const QJsonObject& props, const QJsonValue& value );
// Whether a field has a rule (by value: `required: false` or an empty `pattern` is none).
bool validates( const QJsonObject& props );
} // namespace pluginfields
