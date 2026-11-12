// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QVariantMap>
#include <QWidget>
#include <functional>
#include <memory>

class QScrollArea;
class QVBoxLayout;

// Host dialogs used by view fields and buttons. Tests replace them so no modal exec() runs.
// `objectName` names the dialog; PluginView::cancelDialogs() rejects open dialogs by that name.
struct PluginViewDialogs {
    std::function<bool( QWidget* parent, const QString& objectName, const QString& title,
                        const QString& text, const QString& okLabel )>
        confirm;
    std::function<QString( QWidget* parent, const QString& objectName, const QString& title,
                           const QString& filter, const QString& suggestedName )>
        saveFile;
    std::function<QString( QWidget* parent, const QString& objectName, const QString& title,
                           const QString& filter )>
        openFile;
    std::function<QString( QWidget* parent, const QString& objectName, const QString& title )>
        directory;
    static PluginViewDialogs native();
};

// Renders one plugin view element tree (docs/plugin-views.md section 2.3) with native widgets.
// Patches update widgets in place, so scroll position, selection, focus and typed text survive.
class PluginView final : public QWidget {
    Q_OBJECT
public:
    static constexpr int MaxElements = 512;
    static constexpr int MaxTableCells = 50000;
    static constexpr int MaxTableColumns = 64;
    static constexpr int MaxTextChars = 4096;        // labels, titles, cells, tooltips
    static constexpr int MaxFieldValueChars = 65536; // one field value
    static constexpr int MaxInputsBytes = 256 * 1024;
    static constexpr int MaxSelectedIds = 1000;
    static constexpr int MaxWarnings = 64;

    PluginView( const QString& pluginId, const QString& viewId, PluginViewDialogs dialogs,
                QWidget* parent = nullptr );
    ~PluginView() override;

    // A full tree. When the ids and types match the current tree it is applied in place.
    void setRoot( const QJsonObject& root );
    // `{id, set:{...}}` entries plus table row operations.
    void applyPatch( const QJsonArray& patch );
    // All field values by element id, bounded (see MaxFieldValueChars and MaxInputsBytes).
    QJsonObject inputs( bool* truncated = nullptr ) const;
    QWidget* element( const QString& id ) const;
    int elementCount() const
    {
        return static_cast<int>( nodes_.size() );
    }
    int renderCount() const
    {
        return renders_;
    }
    bool hasRoot() const
    {
        return !root_.isNull();
    }
    void setDialogs( PluginViewDialogs dialogs );
    // Dialogs are parented to a widget that outlives the view (the CILogg window).
    void setDialogParent( QWidget* parent )
    {
        dialogParent_ = parent;
    }
    // Rejects this view's open confirmation and file dialogs.
    void cancelDialogs();
    void setTextDebounceMs( int milliseconds )
    {
        debounceMs_ = milliseconds;
    }
    static QString objectNameFor( const QString& pluginId, const QString& viewId,
                                  const QString& elementId = {} );
    static QString limitText( const QString& text, int maximum = MaxTextChars );

Q_SIGNALS:
    // `element_id`, `type` (changed|clicked|selected), `value`, `inputs`, and `truncated` when a
    // value or the inputs were shortened.
    void eventRaised( const QJsonObject& event );
    // A button with `action`: arguments carry `inputs` and, after a save dialog, `save_path`.
    void actionRequested( const QString& action, const QVariantMap& arguments );

private:
    struct Node;
    QWidget* build( const QJsonObject& element, QWidget* parent, const QString& path );
    QWidget* buildField( Node& node, const QJsonObject& element, QWidget* parent );
    QWidget* buildTable( Node& node, const QJsonObject& element, QWidget* parent );
    void apply( Node& node, const QJsonObject& props, bool fromRoot );
    void applyTableOperation( Node& node, const QJsonObject& entry );
    bool sameShape( const QJsonObject& element ) const;
    void applyInPlace( const QJsonObject& element, const QString& path );
    void clear();
    void emitEvent( const QString& id, const QString& type, const QJsonValue& value,
                    const QJsonObject& extra = {} );
    void emitSelection( const QString& id );
    void pressButton( const QString& id );
    // Static: it runs after modal dialogs, when `this` may already be deleted.
    static bool stillCurrent( const QPointer<PluginView>& guard, const std::weak_ptr<Node>& node,
                              quint64 generation );
    void browse( const QString& id, const QString& kind );
    void warnOnce( const QString& key, const QString& text );
    QString nameFor( const QString& elementId ) const;
    QWidget* dialogParent();
    QJsonValue fieldValue( const Node& node ) const;
    void setFieldValue( Node& node, const QJsonValue& value );

    QString pluginId_;
    QString viewId_;
    PluginViewDialogs dialogs_;
    QPointer<QWidget> dialogParent_;
    QScrollArea* scroll_ = nullptr;
    QPointer<QWidget> root_;
    QHash<QString, std::shared_ptr<Node>> nodes_;
    QHash<QString, QString> paths_; // tree position -> element id (also for id-less elements)
    QStringList order_;
    QSet<QString> warned_;
    QSet<QString> activeDialogs_; // object names of this view's open host dialogs
    quint64 generation_ = 0;
    int renders_ = 0;
    int debounceMs_ = 300;
    bool applying_ = false;
    bool limitReached_ = false;
    bool warningsSuppressed_ = false;
};
