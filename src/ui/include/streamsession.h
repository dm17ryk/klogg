#pragma once

#include <QObject>
#include <QThread>

#include "serialcaptureworker.h"

class StreamSession : public QObject {
    Q_OBJECT

  public:
    explicit StreamSession( SerialCaptureSettings settings, QObject* parent = nullptr );
    ~StreamSession() override;

    void start();
    void stop();
    bool isConnectionOpen() const;
    QString sourceDisplayName() const;
    QString filePath() const;

  public Q_SLOTS:
    void closeConnection();

  Q_SIGNALS:
    void errorOccurred( const QString& message );
    void connectionClosed();

  private:
    void setConnectionClosed();

    SerialCaptureSettings settings_;
    QThread thread_;
    SerialCaptureWorker* worker_ = nullptr;
    bool started_ = false;
    bool connectionOpen_ = false;
};
