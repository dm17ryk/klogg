#include "uiautomation.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QItemSelectionModel>
#include <QJsonValue>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QMetaProperty>
#include <QMouseEvent>
#include <QPersistentModelIndex>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScopeGuard>
#include <QSet>
#include <QTextEdit>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>

#include "log.h"

namespace {
int combinedKey( const QKeySequence& keys )
{
#if QT_VERSION >= QT_VERSION_CHECK( 6, 0, 0 )
    return keys[ 0 ].toCombined();
#else
    return keys[ 0 ];
#endif
}
const QSet<QByteArray> editableProperties{ "text",         "plainText",     "checked",      "value",
                                           "currentIndex", "currentText",   "date",         "time",
                                           "dateTime",     "currentFont",   "currentColor", "color",
                                           "font",         "cursorPosition" };

struct UiObject {
    QPointer<QObject> object;
    QString path;
};

void collectObjects( QObject* object, const QString& path, QList<UiObject>& objects )
{
    objects.push_back( { object, path } );
    const auto children = object->children();
    for ( qsizetype index = 0; index < children.size(); ++index ) {
        collectObjects( children.at( index ), path + '/' + QString::number( index ), objects );
    }
}

QVariantMap describeObject( const UiObject& entry )
{
    auto* object = entry.object.data();
    QVariantMap result{ { "objectName", object->objectName() },
                        { "objectPath", entry.path },
                        { "className", object->metaObject()->className() } };
    if ( const auto* widget = qobject_cast<QWidget*>( object ) ) {
        result.insert( "enabled", widget->isEnabled() );
        result.insert( "visible", widget->isVisible() );
        result.insert( "accessibleName", widget->accessibleName() );
        result.insert( "windowTitle", widget->windowTitle() );
        result.insert( "width", widget->width() );
        result.insert( "height", widget->height() );
    }
    if ( const auto* action = qobject_cast<QAction*>( object ) ) {
        result.insert( "enabled", action->isEnabled() );
        result.insert( "text", action->text() );
        result.insert( "checked", action->isChecked() );
    }
    QVariantMap properties;
    QStringList writable;
    const auto* meta = object->metaObject();
    for ( int index = 0; index < meta->propertyCount(); ++index ) {
        const auto property = meta->property( index );
        if ( !editableProperties.contains( property.name() ) ) {
            continue;
        }
        // Passwords must never be disclosed by a UI inspection.
        if ( const auto* line = qobject_cast<QLineEdit*>( object );
             line != nullptr && line->echoMode() != QLineEdit::Normal
             && QByteArray( property.name() ) == "text" ) {
            properties.insert( "text", QStringLiteral( "<redacted>" ) );
        }
        else {
            properties.insert( property.name(), property.read( object ) );
        }
        if ( property.isWritable() ) {
            writable.push_back( property.name() );
        }
    }
    result.insert( "properties", properties );
    result.insert( "writableProperties", writable );
    result.insert( "activatable", qobject_cast<QAbstractButton*>( object ) != nullptr
                                      || qobject_cast<QAction*>( object ) != nullptr
                                      || qobject_cast<QAbstractItemView*>( object ) != nullptr );
    if ( const auto* view = qobject_cast<QAbstractItemView*>( object );
         view != nullptr && view->model() != nullptr ) {
        result.insert( "rowCount", view->model()->rowCount( view->rootIndex() ) );
        result.insert( "columnCount", view->model()->columnCount( view->rootIndex() ) );
        result.insert( "currentRow", view->currentIndex().row() );
        result.insert( "currentColumn", view->currentIndex().column() );
        QVariantList items;
        QVariantList checkStates;
        const auto parent
            = view->currentIndex().isValid() ? view->currentIndex().parent() : view->rootIndex();
        const int rowCount = view->model()->rowCount( parent );
        const int columnCount = view->model()->columnCount( parent );
        // Bound discovery output even for a table backed by a very large log.
        for ( int row = 0; row < std::min( rowCount, 100 ); ++row ) {
            QVariantList cells;
            QVariantList checks;
            for ( int column = 0; column < std::min( columnCount, 20 ); ++column ) {
                cells.push_back(
                    view->model()->data( view->model()->index( row, column, parent ) ) );
                checks.push_back( view->model()->data( view->model()->index( row, column, parent ),
                                                       Qt::CheckStateRole ) );
            }
            items.push_back( cells );
            checkStates.push_back( checks );
        }
        result.insert( "items", items );
        result.insert( "checkStates", checkStates );
        result.insert( "itemsTruncated", rowCount > 100 || columnCount > 20 );
    }
    return result;
}

CommanderResult setObject( const UiObject& entry, const QVariantMap& properties )
{
    auto* object = entry.object.data();
    if ( properties.isEmpty() ) {
        return commanderFailure( CommanderResultCode::InvalidRequest,
                                 QStringLiteral( "set_ui requires a nonempty property object." ) );
    }
    // Validate the complete request before applying any property, so an unknown field
    // never leaves the dialog half edited. Setters still run their normal Qt signals.
    QList<QPair<QMetaProperty, QVariant>> writes;
    QPersistentModelIndex modelIndex;
    QPointer<QAbstractItemView> view = qobject_cast<QAbstractItemView*>( object );
    QPointer<QAbstractItemModel> model = view ? view->model() : nullptr;
    const auto itemNumber = []( const QVariant& value, int& number ) {
        bool ok = false;
        const double parsed = value.toDouble( &ok );
        if ( !ok || !std::isfinite( parsed ) || parsed < 0
             || parsed > std::numeric_limits<int>::max() || std::trunc( parsed ) != parsed ) {
            return false;
        }
        number = static_cast<int>( parsed );
        return true;
    };
    for ( auto it = properties.cbegin(); it != properties.cend(); ++it ) {
        if ( it.key() == "indexPath" || it.key() == "column" || it.key() == "editText"
             || it.key() == "checkState" ) {
            if ( view == nullptr || view->model() == nullptr
                 || !properties.contains( "indexPath" ) ) {
                return commanderFailure(
                    CommanderResultCode::InvalidRequest,
                    QStringLiteral( "Item editing requires a view and indexPath." ) );
            }
            continue;
        }
        const auto name = it.key().toUtf8();
        const auto index = object->metaObject()->indexOfProperty( name.constData() );
        if ( !editableProperties.contains( name ) || index < 0 ) {
            LOG_DEBUG << "UI property rejected (unsupported): " << it.key().toStdString();
            return commanderFailure(
                CommanderResultCode::InvalidRequest,
                QStringLiteral( "Unsupported UI property: %1" ).arg( it.key() ) );
        }
        const auto property = object->metaObject()->property( index );
        const auto* line = qobject_cast<QLineEdit*>( object );
        const auto* plain = qobject_cast<QPlainTextEdit*>( object );
        const auto* rich = qobject_cast<QTextEdit*>( object );
        if ( ( name == "text" && line && line->isReadOnly() )
             || ( name == "plainText"
                  && ( ( plain && plain->isReadOnly() ) || ( rich && rich->isReadOnly() ) ) ) ) {
            LOG_DEBUG << "UI property rejected: text control is read-only";
            return commanderFailure( CommanderResultCode::InvalidRequest,
                                     QStringLiteral( "The text control is read-only." ) );
        }
        auto value = it.value();
        bool converted = false;
        if ( property.isWritable() ) {
#if QT_VERSION >= QT_VERSION_CHECK( 6, 0, 0 )
            converted = value.convert( property.metaType() );
#else
            converted = value.convert( property.userType() );
#endif
        }
        if ( !converted ) {
            LOG_DEBUG << "UI property rejected (type/read-only): " << it.key().toStdString();
            return commanderFailure(
                CommanderResultCode::InvalidRequest,
                QStringLiteral( "Invalid value or read-only property: %1" ).arg( it.key() ) );
        }
        writes.push_back( { property, value } );
    }
    if ( properties.contains( "indexPath" ) ) {
        const auto rows = properties.value( "indexPath" ).toList();
        if ( rows.isEmpty() ) {
            return commanderFailure(
                CommanderResultCode::InvalidRequest,
                QStringLiteral( "indexPath must be a nonempty array of row indices." ) );
        }
        modelIndex = view->rootIndex();
        for ( qsizetype depth = 0; depth < rows.size(); ++depth ) {
            int row = 0;
            int column = 0;
            if ( !itemNumber( rows.at( depth ), row )
                 || ( depth == rows.size() - 1
                      && !itemNumber( properties.value( "column", 0 ), column ) ) ) {
                return commanderFailure( CommanderResultCode::InvalidRequest,
                                         QStringLiteral( "Invalid item row or column." ) );
            }
            modelIndex = model->index( row, column, modelIndex );
            if ( !modelIndex.isValid() ) {
                return commanderFailure( CommanderResultCode::NotFound,
                                         QStringLiteral( "Requested model item was not found." ) );
            }
        }
        if ( properties.contains( "editText" )
             && !( view->model()->flags( modelIndex ) & Qt::ItemIsEditable ) ) {
            return commanderFailure( CommanderResultCode::InvalidRequest,
                                     QStringLiteral( "Requested model item is read-only." ) );
        }
        if ( properties.contains( "checkState" ) ) {
            int state = 0;
            if ( !itemNumber( properties.value( "checkState" ), state ) || state > Qt::Checked
                 || !( view->model()->flags( modelIndex ) & Qt::ItemIsUserCheckable ) ) {
                return commanderFailure(
                    CommanderResultCode::InvalidRequest,
                    QStringLiteral( "Invalid check state or item is not checkable." ) );
            }
        }
    }
    for ( const auto& write : writes ) {
        if ( entry.object.isNull() ) {
            return commanderFailure(
                CommanderResultCode::ExecutionFailed,
                QStringLiteral( "UI object was destroyed by a property change." ) );
        }
        LOG_DEBUG << "UI property write: path=" << entry.path.toStdString()
                  << " property=" << write.first.name();
        if ( !write.first.write( entry.object.data(), write.second ) ) {
            return commanderFailure(
                CommanderResultCode::ExecutionFailed,
                QStringLiteral( "Qt rejected property: %1" ).arg( write.first.name() ) );
        }
    }
    if ( properties.contains( "indexPath" ) ) {
        if ( !view || !model || view->model() != model || !modelIndex.isValid() ) {
            return commanderFailure(
                CommanderResultCode::ExecutionFailed,
                QStringLiteral( "A property change invalidated the model item." ) );
        }
        view->setCurrentIndex( modelIndex );
        if ( !view || !model || view->model() != model || !modelIndex.isValid() ) {
            return commanderFailure( CommanderResultCode::ExecutionFailed,
                                     QStringLiteral( "Selection invalidated the model item." ) );
        }
        view->scrollTo( modelIndex );
        if ( properties.contains( "checkState" )
             && !model->setData( modelIndex, properties.value( "checkState" ),
                                 Qt::CheckStateRole ) ) {
            return commanderFailure( CommanderResultCode::ExecutionFailed,
                                     QStringLiteral( "The model rejected the check state." ) );
        }
        if ( properties.contains( "editText" )
             && ( !model || !modelIndex.isValid()
                  || !model->setData( modelIndex, properties.value( "editText" ),
                                      Qt::EditRole ) ) ) {
            return commanderFailure( CommanderResultCode::ExecutionFailed,
                                     QStringLiteral( "The model rejected the item edit." ) );
        }
    }
    return entry.object.isNull()
               ? commanderFailure(
                     CommanderResultCode::ExecutionFailed,
                     QStringLiteral( "UI object was destroyed by a property change." ) )
               : commanderSuccess( {}, describeObject( entry ) );
}
} // namespace

CommanderResult executeUiAutomation( QWidget* owner, const CommanderRequest& request,
                                     const QList<QWidget*>& extraRoots )
{
    LOG_DEBUG << "UI command: action=" << commanderActionToString( request.action ).toStdString()
              << " selector=" << request.objectName.toStdString();
    if ( request.objectName == "@clipboard" ) {
        if ( request.action == CommanderAction::GetUi ) {
            return commanderSuccess( {}, { { "text", QApplication::clipboard()->text() } } );
        }
        if ( request.action == CommanderAction::SetUi && request.definitionPayload.size() == 1
             && request.definitionPayload.contains( "text" ) ) {
            QApplication::clipboard()->setText(
                request.definitionPayload.value( "text" ).toString() );
            LOG_DEBUG << "UI command updated clipboard text";
            return commanderSuccess();
        }
        return commanderFailure(
            CommanderResultCode::InvalidRequest,
            QStringLiteral( "Clipboard supports get_ui or set_ui with text." ) );
    }
    QList<QWidget*> roots{ owner };
    for ( auto* extra : extraRoots ) {
        if ( extra != nullptr && extra->isVisible() && !roots.contains( extra ) ) {
            roots.push_back( extra );
        }
    }
    // Dialogs and popup windows may have their own top-level widget, including
    // modal dialogs. Only include descendants of this window or its auxiliary roots.
    const auto topLevels = QApplication::topLevelWidgets();
    // The application-wide runners are unparented and shared by every main window.
    for ( auto* top : topLevels ) {
        if ( top->isVisible() && !roots.contains( top )
             && ( top->inherits( "ScriptRunnerWindow" ) || top->inherits( "ScenarioRunnerWindow" )
                  || top->inherits( "LabQueueWindow" ) ) ) {
            roots.push_back( top );
        }
    }
    for ( auto* top : topLevels ) {
        if ( !top->isVisible() || roots.contains( top ) ) {
            continue;
        }
        for ( auto* parent = top->parent(); parent != nullptr; parent = parent->parent() ) {
            if ( roots.contains( qobject_cast<QWidget*>( parent ) ) ) {
                roots.push_back( top );
                break;
            }
        }
    }
    QList<UiObject> objects;
    // Each top-level address forms a stable selector during that widget's lifetime.
    // Child indices also distinguish the many unnamed controls in Qt dialogs.
    for ( auto* root : roots ) {
        collectObjects( root,
                        QStringLiteral( "ui:%1" ).arg( reinterpret_cast<quintptr>( root ), 0, 16 ),
                        objects );
    }
    QSet<QObject*> visited;
    QList<UiObject> matches;
    QVariantList descriptions;
    for ( const auto& entry : objects ) {
        auto* object = entry.object.data();
        if ( object == nullptr || visited.contains( object ) ) {
            continue;
        }
        visited.insert( object );
        if ( request.objectName.isEmpty() ) {
            if ( qobject_cast<QWidget*>( object ) || qobject_cast<QAction*>( object ) ) {
                descriptions.push_back( describeObject( entry ) );
            }
        }
        else if ( entry.path == request.objectName || object->objectName() == request.objectName ) {
            matches.push_back( entry );
        }
    }
    if ( request.action == CommanderAction::GetUi && request.objectName.isEmpty() ) {
        LOG_DEBUG << "UI inspection returned " << descriptions.size() << " controls";
        return commanderSuccess( {}, { { "objects", descriptions } } );
    }
    if ( matches.size() != 1 ) {
        LOG_DEBUG << "UI selector rejected: matches=" << matches.size();
        return commanderFailure(
            matches.isEmpty() ? CommanderResultCode::NotFound : CommanderResultCode::InvalidRequest,
            matches.isEmpty()
                ? QStringLiteral( "UI object was not found." )
                : QStringLiteral( "Ambiguous object name; use objectPath from get_ui." ) );
    }
    const auto entry = matches.front();
    auto* object = entry.object.data();
    if ( request.action == CommanderAction::GetUi ) {
        return commanderSuccess( {}, describeObject( entry ) );
    }
    auto* widget = qobject_cast<QWidget*>( object );
    auto* action = qobject_cast<QAction*>( object );
    if ( ( widget != nullptr && ( !widget->isEnabled() || !widget->isVisible() ) )
         || ( action != nullptr && !action->isEnabled() ) ) {
        LOG_DEBUG << "UI mutation rejected: control is disabled or hidden";
        return commanderFailure( CommanderResultCode::ExecutionFailed,
                                 QStringLiteral( "UI object is disabled or hidden." ) );
    }
    if ( auto* modal = QApplication::activeModalWidget(); modal != nullptr && widget != nullptr
                                                          && widget != modal
                                                          && !modal->isAncestorOf( widget ) ) {
        return commanderFailure( CommanderResultCode::ExecutionFailed,
                                 QStringLiteral( "A modal dialog blocks this control." ) );
    }
    if ( auto* modal = QApplication::activeModalWidget(); modal != nullptr && action != nullptr ) {
        auto* parent = action->parent();
        while ( parent != nullptr && parent != modal ) {
            parent = parent->parent();
        }
        if ( parent != modal ) {
            LOG_DEBUG << "UI activation rejected: a modal dialog blocks the action";
            return commanderFailure( CommanderResultCode::ExecutionFailed,
                                     QStringLiteral( "A modal dialog blocks this action." ) );
        }
    }
    if ( request.action == CommanderAction::SetUi ) {
        return setObject( entry, request.definitionPayload );
    }
    auto* button = qobject_cast<QAbstractButton*>( object );
    auto* view = qobject_cast<QAbstractItemView*>( object );
    if ( !request.definitionPayload.isEmpty() ) {
        const auto& gesture = request.definitionPayload;
        const auto kind = gesture.value( "event" ).toString();
        const QSet<QString> fields{
            "event", "x", "y", "toX", "toY", "button", "modifiers", "delta"
        };
        const QStringList events{ "click", "double_click", "drag", "wheel" };
        if ( !widget || !request.searchText.isEmpty() || !events.contains( kind ) ) {
            return commanderFailure(
                CommanderResultCode::InvalidRequest,
                "Mouse activation requires a widget and event click/double_click/drag/wheel." );
        }
        for ( auto it = gesture.begin(); it != gesture.end(); ++it ) {
            if ( !fields.contains( it.key() ) ) {
                return commanderFailure( CommanderResultCode::InvalidRequest,
                                         "Unknown mouse field: " + it.key() );
            }
            if ( QStringList{ "x", "y", "toX", "toY", "delta" }.contains( it.key() ) ) {
                const auto json = QJsonValue::fromVariant( it.value() );
                const auto number = json.toDouble();
                if ( !json.isDouble() || !std::isfinite( number ) || number != std::floor( number )
                     || number < -100000 || number > 100000 ) {
                    return commanderFailure( CommanderResultCode::InvalidRequest,
                                             "Mouse coordinates/delta must be bounded integers." );
                }
            }
        }
        const auto buttonName = gesture.value( "button", "left" ).toString();
        if ( !QStringList{ "left", "right", "middle" }.contains( buttonName ) ) {
            return commanderFailure( CommanderResultCode::InvalidRequest,
                                     "Mouse button must be left, right or middle." );
        }
        if ( gesture.contains( "modifiers" ) ) {
            if ( gesture.value( "modifiers" ).userType() != QMetaType::QVariantList ) {
                return commanderFailure( CommanderResultCode::InvalidRequest,
                                         "modifiers must be an array." );
            }
            for ( const auto& modifier : gesture.value( "modifiers" ).toList() ) {
                if ( !QStringList{ "Ctrl", "Alt", "Shift", "Meta" }.contains(
                         modifier.toString() ) ) {
                    return commanderFailure( CommanderResultCode::InvalidRequest,
                                             "Unknown mouse modifier." );
                }
            }
        }
        const QPoint start( gesture.value( "x", widget->rect().center().x() ).toInt(),
                            gesture.value( "y", widget->rect().center().y() ).toInt() );
        if ( !widget->rect().contains( start )
             || ( kind == "drag"
                  && ( !gesture.contains( "toX" ) || !gesture.contains( "toY" ) ) ) ) {
            return commanderFailure(
                CommanderResultCode::InvalidRequest,
                "Mouse start must be within the widget; drag requires toX and toY." );
        }
    }
    else if ( !request.searchText.isEmpty() ) {
        const auto keys
            = QKeySequence::fromString( request.searchText, QKeySequence::PortableText );
        if ( widget == nullptr || keys.count() != 1
             || ( combinedKey( keys ) & ~int( Qt::KeyboardModifierMask ) ) == Qt::Key_unknown ) {
            return commanderFailure(
                CommanderResultCode::InvalidRequest,
                QStringLiteral( "Key activation requires a widget and one valid key sequence." ) );
        }
    }
    else if ( action == nullptr && button == nullptr
              && ( view == nullptr || !view->currentIndex().isValid() ) ) {
        return commanderFailure(
            CommanderResultCode::InvalidRequest,
            QStringLiteral( "Object is not a button, action, or selected model item." ) );
    }
    LOG_DEBUG << "UI activation queued: " << entry.path.toStdString();
    queueUiActivation( object, request.searchText, request.definitionPayload );
    return commanderSuccess( {}, { { "queued", true }, { "objectPath", entry.path } } );
}

void queueUiActivation( QObject* object, const QString& keySequence, const QVariantMap& gesture )
{
    QTimer::singleShot( 0, object, [ guard = QPointer<QObject>( object ), keySequence, gesture ] {
        // Commands opening file/font/color dialogs need Qt controls to appear in
        // get_ui. Restore the user's native-dialog preference when activation ends.
        const auto wasNonNative = QApplication::testAttribute( Qt::AA_DontUseNativeDialogs );
        QApplication::setAttribute( Qt::AA_DontUseNativeDialogs, true );
        const auto restoreDialogs = qScopeGuard( [ wasNonNative ] {
            QApplication::setAttribute( Qt::AA_DontUseNativeDialogs, wasNonNative );
        } );
        if ( !gesture.isEmpty() ) {
            QPointer<QWidget> target( qobject_cast<QWidget*>( guard.data() ) );
            if ( !target || !target->isVisible() || !target->isEnabled() ) {
                return;
            }
            target->setFocus( Qt::OtherFocusReason );
            if ( !target ) {
                return;
            }
            const auto kind = gesture.value( "event" ).toString();
            const auto buttonName = gesture.value( "button", "left" ).toString();
            const auto button = buttonName == "right"    ? Qt::RightButton
                                : buttonName == "middle" ? Qt::MiddleButton
                                                         : Qt::LeftButton;
            Qt::KeyboardModifiers modifiers;
            for ( const auto& modifier : gesture.value( "modifiers" ).toList() ) {
                const auto name = modifier.toString();
                modifiers |= name == "Ctrl"    ? Qt::ControlModifier
                             : name == "Alt"   ? Qt::AltModifier
                             : name == "Shift" ? Qt::ShiftModifier
                                               : Qt::MetaModifier;
            }
            const QPoint start( gesture.value( "x", target->rect().center().x() ).toInt(),
                                gesture.value( "y", target->rect().center().y() ).toInt() );
            const auto mouse = [ & ]( QEvent::Type type, const QPoint& point,
                                      Qt::MouseButton changed, Qt::MouseButtons buttons ) {
                if ( !target ) {
                    return;
                }
                QMouseEvent event( type, point, target->mapToGlobal( point ), changed, buttons,
                                   modifiers );
                QApplication::sendEvent( target.data(), &event );
            };
            LOG_DEBUG << "UI mouse gesture: " << kind.toStdString() << " at " << start.x() << ','
                      << start.y();
            if ( kind == "wheel" ) {
                QWheelEvent event( start, target->mapToGlobal( start ), {},
                                   QPoint( 0, gesture.value( "delta", 120 ).toInt() ), Qt::NoButton,
                                   modifiers, Qt::NoScrollPhase, false );
                QApplication::sendEvent( target.data(), &event );
                return;
            }
            mouse( QEvent::MouseButtonPress, start, button, button );
            QPoint end = start;
            if ( kind == "drag" ) {
                end = QPoint( gesture.value( "toX" ).toInt(), gesture.value( "toY" ).toInt() );
                for ( int step = 1; step <= 5; ++step ) {
                    mouse( QEvent::MouseMove, start + ( end - start ) * step / 5, Qt::NoButton,
                           button );
                }
            }
            mouse( QEvent::MouseButtonRelease, end, button, Qt::NoButton );
            if ( kind == "double_click" ) {
                mouse( QEvent::MouseButtonDblClick, start, button, button );
                mouse( QEvent::MouseButtonRelease, start, button, Qt::NoButton );
            }
            if ( kind == "click" && button == Qt::RightButton && target ) {
                QContextMenuEvent event( QContextMenuEvent::Mouse, start,
                                         target->mapToGlobal( start ), modifiers );
                QApplication::sendEvent( target.data(), &event );
            }
            return;
        }
        if ( !keySequence.isEmpty() ) {
            auto* target = qobject_cast<QWidget*>( guard.data() );
            if ( target == nullptr ) {
                return;
            }
            if ( target->focusProxy() != nullptr ) {
                target = target->focusProxy();
            }
            QPointer<QWidget> targetGuard( target );
            target->setFocus( Qt::OtherFocusReason );
            if ( !targetGuard ) {
                return;
            }
            const auto combination = combinedKey(
                QKeySequence::fromString( keySequence, QKeySequence::PortableText ) );
            const auto key = combination & ~int( Qt::KeyboardModifierMask );
            const auto modifiers
                = Qt::KeyboardModifiers( combination & int( Qt::KeyboardModifierMask ) );
            LOG_DEBUG << "UI key activation: " << keySequence.toStdString();
            if ( key == Qt::Key_Menu ) {
                QContextMenuEvent event( QContextMenuEvent::Keyboard, target->rect().center(),
                                         target->mapToGlobal( target->rect().center() ) );
                QApplication::sendEvent( target, &event );
                return;
            }
            QKeyEvent press( QEvent::KeyPress, key, modifiers );
            QKeyEvent release( QEvent::KeyRelease, key, modifiers );
            QApplication::sendEvent( target, &press );
            if ( !targetGuard.isNull() ) {
                QApplication::sendEvent( targetGuard.data(), &release );
            }
            return;
        }
        if ( auto* selectedAction = qobject_cast<QAction*>( guard.data() ) ) {
            selectedAction->trigger();
        }
        else if ( auto* selectedButton = qobject_cast<QAbstractButton*>( guard.data() ) ) {
            selectedButton->click();
        }
        else if ( auto* selectedView = qobject_cast<QAbstractItemView*>( guard.data() ) ) {
            QMetaObject::invokeMethod( selectedView, "doubleClicked", Qt::DirectConnection,
                                       Q_ARG( QModelIndex, selectedView->currentIndex() ) );
        }
    } );
}
