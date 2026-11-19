#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <functional>

namespace cilogg::plugins {
class PluginManager;

// Runs the silent plugin update check (docs/plugin-repositories.md 3.7): about a minute after
// startup, then every hour. Each run fetches only the repositories not checked in the last 24
// hours (PluginManager::runScheduledUpdateCheck), so every repository is checked about once a
// day while CILogg runs. The application starts it only in an interactive GUI process, and
// `allowed` (the Options setting) is asked before every run.
class PluginUpdateChecker final : public QObject {
    Q_OBJECT
public:
    static constexpr int StartupDelayMs = 60 * 1000;
    static constexpr int IntervalMs = 60 * 60 * 1000;

    PluginUpdateChecker( PluginManager* manager, std::function<bool()> allowed,
                         QObject* parent = nullptr );
    void start( int firstDelayMs = StartupDelayMs, int intervalMs = IntervalMs );
    void stop();
    bool isActive() const;
    // Like the application updater: never with CILOGG_AUTOMATION set, in automation mode or on
    // a headless platform (offscreen, minimal).
    static bool mayRun( bool automationMode, const QString& platformName );

Q_SIGNALS:
    // A run happened; `jobId` is empty when no repository was due.
    void checked( const QString& jobId );

private:
    void run();
    QPointer<PluginManager> manager_;
    std::function<bool()> allowed_;
    QTimer timer_;
    int intervalMs_ = IntervalMs;
};
} // namespace cilogg::plugins
