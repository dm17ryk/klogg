#pragma once

#include <functional>

#include <QString>

struct StartupProgressState {
    int minimum = 0;
    int maximum = 1;
    int value = 0;
    QString status;
    QString detail;
};

class StartupProgress {
  public:
    using Callback = std::function<void( const StartupProgressState& state )>;

    static void setCallback( Callback callback );
    static void clearCallback();

    static void setRange( int minimum, int maximum );
    static void setValue( int value, const QString& status = {}, const QString& detail = {} );
    static void advance( const QString& status = {}, const QString& detail = {}, int step = 1 );
    static void message( const QString& status, const QString& detail = {} );
    static bool isActive();
};
