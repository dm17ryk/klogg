#include "previewwindow.h"

#include <QTabWidget>
#include <QVBoxLayout>

#include "previewmanager.h"
#include "previewmessagetab.h"

PreviewWindow::PreviewWindow( QWidget* parent )
    : QWidget( parent )
{
    tabWidget_ = new QTabWidget( this );
    tabWidget_->setTabsClosable( true );
    tabWidget_->setDocumentMode( true );

    auto* layout = new QVBoxLayout();
    layout->addWidget( tabWidget_ );
    setLayout( layout );

    connect( tabWidget_, &QTabWidget::tabCloseRequested, this, &PreviewWindow::handleTabClosed );
    connect( &PreviewManager::instance(), &PreviewManager::previewsChanged, this,
             &PreviewWindow::refreshTabs );
}

void PreviewWindow::openMessageTab( const QString& rawLine, const QString& initialPreviewNameOrAuto )
{
    auto* tab = new PreviewMessageTab( rawLine, initialPreviewNameOrAuto, ++tabCounter_, this );
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
