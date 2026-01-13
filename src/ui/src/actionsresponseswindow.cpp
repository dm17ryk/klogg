#include "actionsresponseswindow.h"

#include <QHeaderView>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
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
        if ( event->type() == QEvent::MouseButtonRelease ) {
            const auto* mouseEvent = static_cast<QMouseEvent*>( event );
            if ( mouseEvent->button() == Qt::LeftButton
                 && option.rect.contains( mouseEvent->pos() )
                 && index.data( ActionsTableModel::SendEnabledRole ).toBool() ) {
                if ( callback_ ) {
                    callback_( index );
                }
                return true;
            }
        }
        return false;
    }

  private:
    std::function<void( const QModelIndex& )> callback_;
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
    actionsTable_->horizontalHeader()->setSectionResizeMode( 0, QHeaderView::ResizeToContents );
    actionsTable_->horizontalHeader()->setSectionResizeMode( 1, QHeaderView::Stretch );
    actionsTable_->horizontalHeader()->setSectionResizeMode( 2, QHeaderView::Stretch );

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
    responsesTable_->horizontalHeader()->setSectionResizeMode( 0, QHeaderView::ResizeToContents );
    responsesTable_->horizontalHeader()->setSectionResizeMode( 1, QHeaderView::Stretch );
    responsesTable_->horizontalHeader()->setSectionResizeMode( 2, QHeaderView::Stretch );

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

    connect( &ActionsManager::instance(), &ActionsManager::actionsChanged, this,
             &ActionsResponsesWindow::refreshActions );
    connect( &ActionsManager::instance(), &ActionsManager::responsesChanged, this,
             &ActionsResponsesWindow::refreshResponses );
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
}

void ActionsResponsesWindow::refreshResponses()
{
    if ( responsesModel_ ) {
        responsesModel_->refresh();
    }
}
