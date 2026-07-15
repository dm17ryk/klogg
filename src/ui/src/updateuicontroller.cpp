#include "updateuicontroller.h"

#include <QDesktopServices>
#include <QMessageBox>
#include <QProgressDialog>

#include "log.h"
#include "updatedialog.h"

UpdateUiController::UpdateUiController( VersionChecker& checker, ParentProvider parentProvider,
                                        ActionProvider actionProvider, QString context,
                                        QObject* parent )
    : QObject( parent )
    , checker_( checker )
    , parentProvider_( std::move( parentProvider ) )
    , actionProvider_( std::move( actionProvider ) )
    , context_( std::move( context ) )
{
    connect( &checker_, &VersionChecker::releaseFound, this, &UpdateUiController::presentRelease );
    connect( &checker_, &VersionChecker::downloadProgress, this,
             &UpdateUiController::updateProgress );
    connect( &checker_, &VersionChecker::updateReady, this, &UpdateUiController::updateReady );
    connect( &checker_, &VersionChecker::errorOccurred, this, &UpdateUiController::updateFailed );
    LOG_INFO << "Update UI controller attached; context=" << context_;
}

QWidget* UpdateUiController::parentWidget() const
{
    auto* widget = parentProvider_ ? parentProvider_() : nullptr;
    LOG_DEBUG << "Update UI parent decision; context=" << context_
              << ", available=" << ( widget != nullptr );
    return widget;
}

void UpdateUiController::presentRelease( const ReleaseInfo& release )
{
    const auto action = actionProvider_ ? actionProvider_() : UpdateAction::Notify;
    LOG_INFO << "Presenting update dialog; context=" << context_ << ", tag=" << release.tag
             << ", action=" << static_cast<int>( action )
             << ", installable=" << release.canAutomaticallyUpdate();
    UpdateDialog dialog( release, action, parentWidget() );
    dialog.exec();
    if ( dialog.choice() == UpdateDialog::Choice::OpenRelease ) {
        const bool opened = QDesktopServices::openUrl( release.pageUrl );
        LOG_INFO << "Update dialog decision; context=" << context_
                 << ", choice=open-release, result=" << opened;
        return;
    }
    if ( dialog.choice() == UpdateDialog::Choice::Later ) {
        LOG_INFO << "Update dialog decision; context=" << context_ << ", choice=later";
        return;
    }

    const bool installOnExit = dialog.choice() == UpdateDialog::Choice::DownloadAndInstall;
    LOG_INFO << "Update dialog decision; context=" << context_
             << ", choice=download, install-on-exit=" << installOnExit;
    closeProgress();
    progress_ = new QProgressDialog( tr( "Downloading and verifying CILogg update…" ),
                                     tr( "Cancel" ), 0, 100, parentWidget() );
    progress_->setWindowModality( Qt::WindowModal );
    progress_->setAutoClose( false );
    connect( progress_, &QProgressDialog::canceled, &checker_, &VersionChecker::cancelDownload );
    progress_->show();
    checker_.downloadUpdate( release, installOnExit );
}

void UpdateUiController::updateProgress( qint64 received, qint64 total )
{
    if ( !progress_ ) {
        LOG_DEBUG << "Update progress ignored because no dialog is active; context=" << context_;
        return;
    }
    if ( total <= 0 ) {
        LOG_DEBUG << "Update progress remains indeterminate; context=" << context_
                  << ", received=" << received << ", total=" << total;
        return;
    }
    const auto percent = static_cast<int>( received * 100 / total );
    progress_->setValue( percent );
    LOG_DEBUG << "Update progress; context=" << context_ << ", received=" << received
              << ", total=" << total << ", percent=" << percent;
}

void UpdateUiController::updateReady( const PendingUpdate& pending )
{
    closeProgress();
    LOG_INFO << "Verified update ready; context=" << context_
             << ", install-on-exit=" << pending.installOnExit
             << ", package=" << pending.packagePath;
    QMessageBox::information(
        parentWidget(), tr( "CILogg Update" ),
        pending.installOnExit
            ? tr( "The verified update is staged and will be installed after CILogg exits "
                  "cleanly." )
            : tr( "The verified update package is ready at:\n%1" ).arg( pending.packagePath ) );
}

void UpdateUiController::updateFailed( const QString& message )
{
    closeProgress();
    LOG_WARNING << "Update UI reports failure; context=" << context_ << ", message=" << message;
    QMessageBox::warning( parentWidget(), tr( "CILogg Update" ), message );
}

void UpdateUiController::closeProgress()
{
    if ( !progress_ ) {
        LOG_DEBUG << "Update progress close skipped; context=" << context_ << ", active=false";
        return;
    }
    LOG_DEBUG << "Closing update progress dialog; context=" << context_;
    progress_->close();
    progress_->deleteLater();
    progress_.clear();
}
