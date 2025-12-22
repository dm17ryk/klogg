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
    QString filePath() const;

  Q_SIGNALS:
    void errorOccurred( const QString& message );

  private:
    SerialCaptureSettings settings_;
    QThread thread_;
    SerialCaptureWorker* worker_ = nullptr;
    bool started_ = false;
};
