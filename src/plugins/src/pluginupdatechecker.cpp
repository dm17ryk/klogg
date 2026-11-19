#include "pluginupdatechecker.h"
#include "log.h"
#include "pluginmanager.h"

namespace cilogg::plugins {
PluginUpdateChecker::PluginUpdateChecker( PluginManager* manager, std::function<bool()> allowed,
                                          QObject* parent )
    : QObject( parent )
    , manager_( manager )
    , allowed_( std::move( allowed ) )
{
    timer_.setSingleShot( true );
    connect( &timer_, &QTimer::timeout, this, &PluginUpdateChecker::run );
}

void PluginUpdateChecker::start( int firstDelayMs, int intervalMs )
{
    intervalMs_ = qMax( 1, intervalMs );
    timer_.start( qMax( 0, firstDelayMs ) );
    LOG_DEBUG << "Plugin update checks scheduled first_ms=" << firstDelayMs
              << " interval_ms=" << intervalMs_;
}

void PluginUpdateChecker::stop()
{
    timer_.stop();
}

bool PluginUpdateChecker::isActive() const
{
    return timer_.isActive();
}

bool PluginUpdateChecker::mayRun( bool automationMode, const QString& platformName )
{
    if ( automationMode || qEnvironmentVariableIntValue( "CILOGG_AUTOMATION" ) > 0 ) {
        return false;
    }
    for ( const auto* headless : { "offscreen", "minimal", "minimalegl" } ) {
        if ( platformName.compare( QLatin1String( headless ), Qt::CaseInsensitive ) == 0 ) {
            return false;
        }
    }
    return true;
}

void PluginUpdateChecker::run()
{
    timer_.start( intervalMs_ );
    if ( !manager_ ) {
        return;
    }
    if ( allowed_ && !allowed_() ) {
        LOG_DEBUG << "Plugin update check turned off in the options";
        return;
    }
    const auto result = manager_->runScheduledUpdateCheck();
    if ( !result.ok() ) {
        LOG_WARNING << "Plugin update check not started: " << result.error.toStdString();
        return;
    }
    Q_EMIT checked( result.payload.value( "jobId" ).toString() );
}
} // namespace cilogg::plugins
