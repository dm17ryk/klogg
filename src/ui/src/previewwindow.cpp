#include "previewwindow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include <limits>

#include "analysissource.h"
#include "log.h"
#include "previewmanager.h"
#include "previewmessagetab.h"

PreviewWindow::PreviewWindow( QWidget* parent )
    : QWidget( parent )
{
    setObjectName( QStringLiteral( "previewWindow" ) );
    tabWidget_ = new QTabWidget( this );
    tabWidget_->setObjectName( QStringLiteral( "previewTabs" ) );
    tabWidget_->setTabsClosable( true );
    tabWidget_->setDocumentMode( true );
    if ( auto* tabBar = tabWidget_->tabBar(); tabBar != nullptr ) {
        tabBar->setObjectName( QStringLiteral( "previewTabBar" ) );
    }

    auto* layout = new QVBoxLayout();
    layout->addWidget( tabWidget_ );
    setLayout( layout );

    connect( tabWidget_, &QTabWidget::tabCloseRequested, this, &PreviewWindow::handleTabClosed );
}

void PreviewWindow::setLineSource( SourcesProvider sources, LineOpener openLine )
{
    openLine_ = std::move( openLine );
    if ( lineRow_ != nullptr ) {
        return;
    }
    lineRow_ = new QWidget( this );
    lineRow_->setObjectName( QStringLiteral( "previewLineRow" ) );
    auto* row = new QHBoxLayout( lineRow_ );
    row->setContentsMargins( 0, 0, 0, 0 );
    auto* sourceLabel = new QLabel( tr( "Source" ), lineRow_ );
    source_ = new AnalysisSourcePicker( std::move( sources ), SourceRules::oneSource(), lineRow_ );
    source_->setObjectName( QStringLiteral( "previewSource" ) );
    source_->setAccessibleName( tr( "Preview source" ) );
    sourceLabel->setBuddy( source_ );
    auto* lineLabel = new QLabel( tr( "Line" ), lineRow_ );
    line_ = new QSpinBox( lineRow_ );
    line_->setObjectName( QStringLiteral( "previewLine" ) );
    line_->setAccessibleName( tr( "Preview line number" ) );
    line_->setRange( 1, std::numeric_limits<int>::max() );
    lineLabel->setBuddy( line_ );
    previewLine_ = new QPushButton( tr( "Preview line" ), lineRow_ );
    previewLine_->setObjectName( QStringLiteral( "previewOpenLine" ) );
    previewLine_->setAccessibleName( previewLine_->text() );
    lineStatus_ = new QLabel( lineRow_ );
    lineStatus_->setObjectName( QStringLiteral( "previewLineStatus" ) );
    row->addWidget( sourceLabel );
    row->addWidget( source_, 2 );
    row->addWidget( lineLabel );
    row->addWidget( line_ );
    row->addWidget( previewLine_ );
    row->addWidget( lineStatus_, 1 );
    static_cast<QVBoxLayout*>( layout() )->insertWidget( 0, lineRow_ );
    connect( previewLine_, &QPushButton::clicked, this, &PreviewWindow::previewLine );
}

void PreviewWindow::previewLine()
{
    if ( !openLine_ || source_ == nullptr ) {
        return;
    }
    const auto source = source_->selection();
    const auto line = static_cast<quint64>( line_->value() );
    const auto result = openLine_( source, line );
    // Empty on success; otherwise why the line could not be shown (or that a file was opened).
    lineStatus_->setText( result.message );
    LOG_DEBUG << "Preview line requested source=" << source.toStdString() << " line=" << line
              << " ok=" << result.ok();
}

void PreviewWindow::openMessageTab( const QString& rawLine, const QString& initialPreviewNameOrAuto )
{
    openPreparedMessageTab( preparePreviewText( rawLine ), initialPreviewNameOrAuto );
}

void PreviewWindow::openPreparedMessageTab( const PreparedPreview& prepared, const QString& initialPreviewNameOrAuto )
{
    auto* tab = new PreviewMessageTab( prepared, initialPreviewNameOrAuto, ++tabCounter_, this );
    const auto index = tabWidget_->addTab( tab, tab->title() );
    tabWidget_->setCurrentIndex( index );

    connect( tab, &PreviewMessageTab::titleChanged, this, [ this, tab ]( const QString& title ) {
        const auto tabIndex = tabWidget_->indexOf( tab );
        if ( tabIndex >= 0 ) {
            tabWidget_->setTabText( tabIndex, title );
        }
    } );
}

void PreviewWindow::handleTabClosed( int index )
{
    auto* widget = tabWidget_->widget( index );
    tabWidget_->removeTab( index );
    if ( widget ) {
        widget->deleteLater();
    }
}

void PreviewWindow::refreshTabs()
{
    for ( int i = 0; i < tabWidget_->count(); ++i ) {
        if ( auto* tab = qobject_cast<PreviewMessageTab*>( tabWidget_->widget( i ) ) ) {
            tab->refreshPreviewList();
        }
    }
}
