#pragma once

#include "streamsession.h"

// Simulate the session's logical connection state without opening a device or
// starting a worker. Tests observe scheduling signals, never physical transport.
struct StreamSessionTestAccess {
    static void openConnection( StreamSession& session )
    {
        session.connectionOpen_ = true;
    }

    static void feedData( StreamSession& session, const QByteArray& data )
    {
        session.handleDataReceived( data );
    }
};
