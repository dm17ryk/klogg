#pragma once

#include <functional>

#include <QObject>
#include <QPointer>
#include <QString>

#include "configuration.h"
#include "versionchecker.h"

class QProgressDialog;
class QWidget;

class UpdateUiController final : public QObject {
public:
    using ParentProvider = std::function<QWidget*()>;
    using ActionProvider = std::function<UpdateAction()>;

    UpdateUiController( VersionChecker& checker, ParentProvider parentProvider,
                        ActionProvider actionProvider, QString context, QObject* parent = nullptr );

    // Exits CILogg the way File > Exit does. When set, the report of a failed exit-time install
    // offers "Retry Now": CILogg exits, the update is installed and CILogg starts again.
    void setExitHandler( std::function<void()> exitHandler )
    {
        exitHandler_ = std::move( exitHandler );
    }

private:
    QWidget* parentWidget() const;
    void presentRelease( const ReleaseInfo& release );
    void updateProgress( qint64 received, qint64 total );
    void updateReady( const PendingUpdate& pending );
    void updateFailed( const QString& message );
    void installFailed( const QString& message );
    void closeProgress();

    VersionChecker& checker_;
    ParentProvider parentProvider_;
    ActionProvider actionProvider_;
    QString context_;
    std::function<void()> exitHandler_;
    QPointer<QProgressDialog> progress_;
};
