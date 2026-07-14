#include "updatedialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

UpdateDialog::UpdateDialog( const ReleaseInfo& release, UpdateAction action, QWidget* parent )
    : QDialog( parent )
{
    setWindowTitle( tr( "CILogg Update Available" ) );
    setMinimumSize( 620, 420 );

    auto* layout = new QVBoxLayout( this );
    auto* title
        = new QLabel( tr( "<h2>CILogg %1 is available</h2><p>Installed release: %2</p>" )
                          .arg( release.version.toHtmlEscaped(), release.tag.toHtmlEscaped() ),
                      this );
    title->setTextFormat( Qt::RichText );
    layout->addWidget( title );

    auto* notes = new QTextBrowser( this );
    notes->setOpenExternalLinks( false );
    notes->setMarkdown( release.body.isEmpty() ? tr( "No release notes were provided." )
                                               : release.body );
    layout->addWidget( notes, 1 );

    if ( !release.canAutomaticallyUpdate() ) {
        auto* warning = new QLabel( tr( "Automatic update is unavailable: %1" )
                                        .arg( release.automaticUpdateBlockReason.toHtmlEscaped() ),
                                    this );
        warning->setWordWrap( true );
        warning->setStyleSheet( QStringLiteral( "color: #b06000;" ) );
        layout->addWidget( warning );
    }

    auto* buttons = new QDialogButtonBox( this );
    auto* open = buttons->addButton( tr( "Open Release" ), QDialogButtonBox::ActionRole );
    auto* later = buttons->addButton( tr( "Later" ), QDialogButtonBox::RejectRole );
    QPushButton* update = nullptr;
    if ( release.canAutomaticallyUpdate() && action == UpdateAction::Download ) {
        update = buttons->addButton( tr( "Download and Verify" ), QDialogButtonBox::AcceptRole );
    }
    else if ( release.canAutomaticallyUpdate() && action == UpdateAction::DownloadAndInstall ) {
        update = buttons->addButton( tr( "Download, Verify, and Install on Exit" ),
                                     QDialogButtonBox::AcceptRole );
    }
    connect( later, &QPushButton::clicked, this, &QDialog::reject );
    connect( open, &QPushButton::clicked, this, [ this ] {
        choice_ = Choice::OpenRelease;
        accept();
    } );
    if ( update ) {
        connect( update, &QPushButton::clicked, this, [ this, action ] {
            choice_ = action == UpdateAction::DownloadAndInstall ? Choice::DownloadAndInstall
                                                                 : Choice::Download;
            accept();
        } );
        update->setDefault( true );
    }
    layout->addWidget( buttons );
}
