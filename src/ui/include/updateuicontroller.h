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

private:
    QWidget* parentWidget() const;
    void presentRelease( const ReleaseInfo& release );
    void updateProgress( qint64 received, qint64 total );
    void updateReady( const PendingUpdate& pending );
    void updateFailed( const QString& message );
    void closeProgress();

    VersionChecker& checker_;
    ParentProvider parentProvider_;
    ActionProvider actionProvider_;
    QString context_;
    QPointer<QProgressDialog> progress_;
};
