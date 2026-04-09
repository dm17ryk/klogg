#pragma once

#include <cstdlib>

#include <QVariantMap>

#include "scenariobatchrequest.h"

class KloggApp;
class MainWindow;

class ScenarioHeadlessRunner {
  public:
    struct Result {
        int exitCode = EXIT_FAILURE;
        QString message;
        bool outputToStderr = false;
        QVariantMap payload;

        bool hasPayload() const
        {
            return !payload.isEmpty();
        }
    };

    explicit ScenarioHeadlessRunner( KloggApp& app );

    Result run( const ScenarioBatchRequest& request );

  private:
    KloggApp& app_;
};
