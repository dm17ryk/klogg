#pragma once

#include <QMessageBox>
#include <QObject>
#include <QPointer>
#include <QVariantMap>

namespace cilogg::plugins {
class PluginManager;
}

// The application's notices about plugin updates (docs/plugin-repositories.md 3.7), one object
// for all windows: one non-modal notice per new version (Open Plugins, Skip This Version, Close),
// one per automatic update and one per failed automatic update. Each automatic update is also
// recorded in the activity journal (R06), also when no notice is shown.
class PluginUpdateNotices final : public QObject {
    Q_OBJECT
public:
    // More new versions than this at once give one notice that lists them.
    static constexpr int MaxSeparateNotices = 3;

    explicit PluginUpdateNotices( cilogg::plugins::PluginManager* manager,
                                  QObject* parent = nullptr );
    // In automation mode no notice is shown; journal entries are still written.
    void setInteractive( bool interactive );

private:
    void showFound( const QVariantList& updates );
    void autoUpdated( const QVariantMap& update );
    void autoUpdateFailed( const QVariantMap& update );
    QMessageBox* notice( QMessageBox::Icon icon, const QString& objectName, const QString& text );
    // The main window to show a notice over: the active one, else any visible one.
    static QWidget* noticeParent();
    static void openPlugins( QWidget* window );

    QPointer<cilogg::plugins::PluginManager> manager_;
    bool interactive_ = true;
};
