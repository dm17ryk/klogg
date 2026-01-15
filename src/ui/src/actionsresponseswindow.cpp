#include "actionsresponseswindow.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QScrollBar>
#include <QScreen>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTableView>
#include <QVBoxLayout>
#include <functional>

#include "actionstablemodel.h"
#include "actionsmanager.h"
#include "previewdecodeutils.h"
#include "responsestablemodel.h"

namespace {
class ActionsFilterProxyModel : public QSortFilterProxyModel {
  public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setFilterText( const QString& text )
    {
        filterText_ = text.trimmed();
        invalidateFilter();
    }

  protected:
    bool filterAcceptsRow( int sourceRow, const QModelIndex& sourceParent ) const override
    {
        if ( filterText_.isEmpty() ) {
            return true;
        }
        const auto nameIndex = sourceModel()->index( sourceRow, 1, sourceParent );
        const auto seqIndex = sourceModel()->index( sourceRow, 2, sourceParent );
        const auto name = sourceModel()->data( nameIndex, Qt::DisplayRole ).toString();
        const auto sequence = sourceModel()->data( seqIndex, Qt::DisplayRole ).toString();
        return name.contains( filterText_, Qt::CaseInsensitive )
               || sequence.contains( filterText_, Qt::CaseInsensitive );
    }

  private:
    QString filterText_;
};

enum class ResponseFilterType { Text, Hex, Regex };

bool looksLikeHex( const QString& text )
{
    auto trimmed = text.trimmed();
    if ( trimmed.startsWith( "0x", Qt::CaseInsensitive ) ) {
        trimmed = trimmed.mid( 2 );
    }
    if ( trimmed.isEmpty() ) {
        return false;
    }
    QString normalized;
    normalized.reserve( trimmed.size() );
    for ( const auto ch : trimmed ) {
        if ( ch.isSpace() || ch == '_' ) {
            continue;
        }
        if ( !ch.isDigit() && ( ch.toLower() < 'a' || ch.toLower() > 'f' ) ) {
            return false;
        }
        normalized.append( ch );
    }
    return normalized.size() >= 2 && ( normalized.size() % 2 ) == 0;
}

bool looksLikeRegex( const QString& text )
{
    if ( text.startsWith( "re:", Qt::CaseInsensitive ) ) {
        return true;
    }
    static const QRegularExpression regexMeta( R"([.^$|()?*+\[\]\\])" );
    return regexMeta.match( text ).hasMatch();
}

class ResponsesFilterProxyModel : public QSortFilterProxyModel {
  public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setFilterText( const QString& text )
    {
        filterText_ = text.trimmed();
        filterType_ = ResponseFilterType::Text;
        filterValid_ = true;
        hexBytes_.clear();
        regex_.setPattern( {} );

        if ( filterText_.isEmpty() ) {
            invalidateFilter();
            return;
        }

        if ( looksLikeHex( filterText_ ) ) {
            filterType_ = ResponseFilterType::Hex;
            const auto decoded = decodeHexStringToBytes( filterText_ );
            filterValid_ = decoded.ok;
            if ( decoded.ok ) {
                hexBytes_ = decoded.bytes;
            }
        }
        else if ( looksLikeRegex( filterText_ ) ) {
            filterType_ = ResponseFilterType::Regex;
            QString pattern = filterText_;
            if ( pattern.startsWith( "re:", Qt::CaseInsensitive ) ) {
                pattern = pattern.mid( 3 ).trimmed();
            }
            regex_ = QRegularExpression( pattern );
            filterValid_ = regex_.isValid();
        }

        invalidateFilter();
    }

  protected:
    bool filterAcceptsRow( int sourceRow, const QModelIndex& sourceParent ) const override
    {
        if ( filterText_.isEmpty() ) {
            return true;
        }
        if ( !filterValid_ ) {
            return false;
        }

        const auto valueIndex = sourceModel()->index( sourceRow, 2, sourceParent );
        const auto matchValue
            = sourceModel()->data( valueIndex, ResponsesTableModel::MatchValueRole )
                  .toString();
        const auto matchTypeValue
            = sourceModel()->data( valueIndex, ResponsesTableModel::MatchTypeRole )
                  .toInt();
        const auto matchType = static_cast<ResponseMatchType>( matchTypeValue );

        if ( filterType_ == ResponseFilterType::Hex ) {
            if ( matchType != ResponseMatchType::HexString ) {
                return false;
            }
            const auto decoded = decodeHexStringToBytes( matchValue );
            if ( !decoded.ok ) {
                return false;
            }
            return decoded.bytes.contains( hexBytes_ );
        }

        if ( filterType_ == ResponseFilterType::Regex ) {
            return regex_.match( matchValue ).hasMatch();
        }

        return matchValue.contains( filterText_, Qt::CaseInsensitive );
    }

  private:
    QString filterText_;
    ResponseFilterType filterType_ = ResponseFilterType::Text;
    bool filterValid_ = true;
    QByteArray hexBytes_;
    QRegularExpression regex_;
};

int tableWidthHint( QTableView* table )
{
    if ( !table || !table->model() ) {
        return 0;
    }
    int width = table->frameWidth() * 2;
    if ( table->verticalHeader()->isVisible() ) {
        width += table->verticalHeader()->width();
    }
    for ( int column = 0; column < table->model()->columnCount(); ++column ) {
        width += table->columnWidth( column );
    }
    if ( table->verticalScrollBar() ) {
        width += table->verticalScrollBar()->sizeHint().width();
    }
    return width;
}

void capColumnWidth( QTableView* table, int column, int maxWidth )
{
    if ( !table || !table->model() ) {
        return;
    }
    if ( column < 0 || column >= table->model()->columnCount() ) {
        return;
    }
    if ( table->columnWidth( column ) > maxWidth ) {
        table->setColumnWidth( column, maxWidth );
    }
}

int clampToScreenWidth( const QWidget* widget, int width )
{
    const auto* screen = widget ? widget->screen() : nullptr;
    if ( !screen ) {
        return width;
    }
    const int maxWidth = static_cast<int>( screen->availableGeometry().width() * 0.7 );
    if ( maxWidth <= 0 ) {
        return width;
    }
    return qMin( width, maxWidth );
}

int tableHeightForRows( QTableView* table, int rows )
{
    if ( !table ) {
        return 0;
    }
    const int rowHeight = table->verticalHeader()->defaultSectionSize();
    int height = table->frameWidth() * 2 + table->horizontalHeader()->height();
    height += rowHeight * rows;
    if ( table->horizontalScrollBar() ) {
        height += table->horizontalScrollBar()->sizeHint().height();
    }
    return height;
}

class ActionSendDelegate : public QStyledItemDelegate {
  public:
    explicit ActionSendDelegate( std::function<void( const QModelIndex& )> callback,
                                 QObject* parent = nullptr )
        : QStyledItemDelegate( parent )
        , callback_( std::move( callback ) )
    {
    }

  protected:
    void paint( QPainter* painter,
                const QStyleOptionViewItem& option,
                const QModelIndex& index ) const override
    {
        QStyleOptionButton buttonOption;
        buttonOption.rect = option.rect.adjusted( 4, 2, -4, -2 );
        buttonOption.text = QObject::tr( "Send" );
        buttonOption.state = QStyle::State_Enabled;
        const auto enabled = index.data( ActionsTableModel::SendEnabledRole ).toBool();
        if ( !enabled ) {
            buttonOption.state &= ~QStyle::State_Enabled;
        }
        if ( option.state & QStyle::State_MouseOver ) {
            buttonOption.state |= QStyle::State_MouseOver;
        }
        if ( option.state & QStyle::State_Selected ) {
            buttonOption.state |= QStyle::State_HasFocus;
        }
        if ( pressedIndex_ == index ) {
            buttonOption.state |= QStyle::State_Sunken;
        }

        if ( const auto* style = option.widget ? option.widget->style() : nullptr ) {
            style->drawControl( QStyle::CE_PushButton, &buttonOption, painter );
        }
        else {
            QStyledItemDelegate::paint( painter, option, index );
        }
    }

    bool editorEvent( QEvent* event,
                      QAbstractItemModel*,
                      const QStyleOptionViewItem& option,
                      const QModelIndex& index ) override
    {
        if ( !index.data( ActionsTableModel::SendEnabledRole ).toBool() ) {
            return false;
        }

        if ( event->type() == QEvent::MouseButtonPress ) {
            const auto* mouseEvent = static_cast<QMouseEvent*>( event );
            if ( mouseEvent->button() == Qt::LeftButton
                 && option.rect.contains( mouseEvent->pos() ) ) {
                pressedIndex_ = index;
                if ( auto* view = qobject_cast<QAbstractItemView*>(
                         const_cast<QWidget*>( option.widget ) ) ) {
                    view->viewport()->update( option.rect );
                }
                return true;
            }
        }

        if ( event->type() == QEvent::MouseButtonRelease ) {
            const auto* mouseEvent = static_cast<QMouseEvent*>( event );
            const bool wasPressed = ( pressedIndex_ == index );
            const bool shouldTrigger
                = wasPressed && mouseEvent->button() == Qt::LeftButton
                  && option.rect.contains( mouseEvent->pos() );
            if ( wasPressed ) {
                pressedIndex_ = {};
                if ( auto* view = qobject_cast<QAbstractItemView*>(
                         const_cast<QWidget*>( option.widget ) ) ) {
                    view->viewport()->update( option.rect );
                }
            }
            if ( shouldTrigger && callback_ ) {
                callback_( index );
            }
            return wasPressed;
        }

        return false;
    }

  private:
    std::function<void( const QModelIndex& )> callback_;
    QPersistentModelIndex pressedIndex_;
};
} // namespace

ActionsResponsesWindow::ActionsResponsesWindow( QWidget* parent )
    : QWidget( parent )
{
    actionsModel_ = new ActionsTableModel( this );
    responsesModel_ = new ResponsesTableModel( this );

    auto* actionsProxy = new ActionsFilterProxyModel( this );
    actionsProxy->setSourceModel( actionsModel_ );
    actionsProxy_ = actionsProxy;

    auto* responsesProxy = new ResponsesFilterProxyModel( this );
    responsesProxy->setSourceModel( responsesModel_ );
    responsesProxy_ = responsesProxy;

    actionsFilter_ = new QLineEdit( this );
    actionsFilter_->setPlaceholderText( tr( "Filter actions..." ) );
    connect( actionsFilter_, &QLineEdit::textChanged, actionsProxy,
             &ActionsFilterProxyModel::setFilterText );

    responsesFilter_ = new QLineEdit( this );
    responsesFilter_->setPlaceholderText( tr( "Filter responses..." ) );
    connect( responsesFilter_, &QLineEdit::textChanged, responsesProxy,
             &ResponsesFilterProxyModel::setFilterText );

    actionsTable_ = new QTableView( this );
    actionsTable_->setModel( actionsProxy );
    actionsTable_->setSelectionBehavior( QAbstractItemView::SelectRows );
    actionsTable_->setSelectionMode( QAbstractItemView::SingleSelection );
    actionsTable_->setEditTriggers( QAbstractItemView::NoEditTriggers );
    actionsTable_->setHorizontalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    actionsTable_->setVerticalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    actionsTable_->setWordWrap( false );
    auto* actionsHeader = actionsTable_->horizontalHeader();
    actionsHeader->setSectionResizeMode( QHeaderView::Interactive );
    actionsHeader->setStretchLastSection( false );
    actionsTable_->resizeColumnsToContents();
    actionsTable_->setColumnWidth( 0, actionsTable_->columnWidth( 0 ) + 5 );
    capColumnWidth( actionsTable_, 1, 350 );
    capColumnWidth( actionsTable_, 2, 600 );

    auto* sendDelegate = new ActionSendDelegate(
        [ this, actionsProxy ]( const QModelIndex& proxyIndex ) {
            const auto sourceIndex = actionsProxy->mapToSource( proxyIndex );
            const auto actionId = sourceIndex.data( ActionsTableModel::ActionIdRole ).toInt();
            if ( actionId >= 0 ) {
                Q_EMIT sendActionRequested( actionId );
            }
        },
        actionsTable_ );
    actionsTable_->setItemDelegateForColumn( 0, sendDelegate );

    responsesTable_ = new QTableView( this );
    responsesTable_->setModel( responsesProxy );
    responsesTable_->setSelectionBehavior( QAbstractItemView::SelectRows );
    responsesTable_->setSelectionMode( QAbstractItemView::SingleSelection );
    responsesTable_->setEditTriggers( QAbstractItemView::NoEditTriggers );
    responsesTable_->setHorizontalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    responsesTable_->setVerticalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    responsesTable_->setWordWrap( false );
    auto* responsesHeader = responsesTable_->horizontalHeader();
    responsesHeader->setSectionResizeMode( QHeaderView::Interactive );
    responsesHeader->setStretchLastSection( false );
    responsesTable_->resizeColumnsToContents();
    capColumnWidth( responsesTable_, 1, 350 );
    capColumnWidth( responsesTable_, 2, 600 );

    auto* actionsPanel = new QWidget( this );
    auto* actionsLayout = new QVBoxLayout( actionsPanel );
    actionsLayout->setContentsMargins( 0, 0, 0, 0 );
    actionsLayout->addWidget( actionsFilter_ );
    actionsLayout->addWidget( actionsTable_ );

    auto* responsesPanel = new QWidget( this );
    auto* responsesLayout = new QVBoxLayout( responsesPanel );
    responsesLayout->setContentsMargins( 0, 0, 0, 0 );
    responsesLayout->addWidget( responsesFilter_ );
    responsesLayout->addWidget( responsesTable_ );
    autoResponsesCheck_ = new QCheckBox( tr( "Auto response enabled" ), this );
    autoResponsesCheck_->setChecked( ActionsManager::instance().autoResponsesEnabled() );
    responsesLayout->addWidget( autoResponsesCheck_ );

    auto* splitter = new QSplitter( Qt::Vertical, this );
    splitter->addWidget( actionsPanel );
    splitter->addWidget( responsesPanel );
    splitter->setStretchFactor( 0, 1 );
    splitter->setStretchFactor( 1, 1 );

    auto* layout = new QVBoxLayout( this );
    layout->addWidget( splitter );
    setLayout( layout );

    refreshActions();
    refreshResponses();
    updateWindowSize();

    connect( &ActionsManager::instance(), &ActionsManager::actionsChanged, this,
             &ActionsResponsesWindow::refreshActions );
    connect( &ActionsManager::instance(), &ActionsManager::responsesChanged, this,
             &ActionsResponsesWindow::refreshResponses );
    connect( autoResponsesCheck_, &QCheckBox::toggled, this,
             []( bool enabled ) { ActionsManager::instance().setAutoResponsesEnabled( enabled ); } );
    connect( &ActionsManager::instance(), &ActionsManager::autoResponsesEnabledChanged,
             this, [ this ]( bool enabled ) {
                 if ( autoResponsesCheck_ && autoResponsesCheck_->isChecked() != enabled ) {
                     const QSignalBlocker blocker( autoResponsesCheck_ );
                     autoResponsesCheck_->setChecked( enabled );
                 }
             } );
}

void ActionsResponsesWindow::setSendAvailable( bool available )
{
    if ( actionsModel_ ) {
        actionsModel_->setSendAvailable( available );
    }
}

void ActionsResponsesWindow::refreshActions()
{
    if ( actionsModel_ ) {
        actionsModel_->refresh();
    }
    if ( actionsTable_ ) {
        actionsTable_->resizeColumnsToContents();
        actionsTable_->setColumnWidth( 0, actionsTable_->columnWidth( 0 ) + 5 );
        capColumnWidth( actionsTable_, 1, 350 );
        capColumnWidth( actionsTable_, 2, 600 );
    }
}

void ActionsResponsesWindow::refreshResponses()
{
    if ( responsesModel_ ) {
        responsesModel_->refresh();
    }
    if ( responsesTable_ ) {
        responsesTable_->resizeColumnsToContents();
        capColumnWidth( responsesTable_, 1, 350 );
        capColumnWidth( responsesTable_, 2, 600 );
    }
}

void ActionsResponsesWindow::updateWindowSize()
{
    if ( !actionsTable_ || !responsesTable_ ) {
        return;
    }
    const int previousActionsMin = actionsTable_->minimumHeight();
    const int previousResponsesMin = responsesTable_->minimumHeight();
    actionsTable_->setMinimumHeight( tableHeightForRows( actionsTable_, 10 ) );
    responsesTable_->setMinimumHeight( tableHeightForRows( responsesTable_, 10 ) );
    if ( layout() ) {
        layout()->activate();
    }
    const QSize hint = sizeHint();
    actionsTable_->setMinimumHeight( previousActionsMin );
    responsesTable_->setMinimumHeight( previousResponsesMin );

    const int widthHint = qMax( tableWidthHint( actionsTable_ ),
                                tableWidthHint( responsesTable_ ) );
    const int desiredWidth
        = qMax( clampToScreenWidth( this, widthHint ), minimumSizeHint().width() );
    const int desiredHeight = qMax( hint.height(), minimumSizeHint().height() );
    if ( !sizeInitialized_ ) {
        resize( desiredWidth, desiredHeight );
        sizeInitialized_ = true;
    }
}
