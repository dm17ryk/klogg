// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QtGlobal>
#include <atomic>

// Private lifetime witness for the existing worker thread, never a device owner.
// Qt's Windows finished-state fast path can precede CRT thread-local destruction.
class BackendThreadRetirement final {
public:
    ~BackendThreadRetirement();
    bool captureCurrentThread();
    bool complete( bool wait = false ) const;

private:
    std::atomic<void*> handle_{ nullptr };
};
