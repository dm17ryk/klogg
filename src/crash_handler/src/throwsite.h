// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Internal: the per-thread record of the latest C++ throw (R06b). No C++ standard headers here:
// throwsite_posix.cpp, which defines __cxa_throw, includes it.

#ifndef KLOGG_THROWSITE_H
#define KLOGG_THROWSITE_H

namespace cilogg::diagnostics::detail {

struct ThrowSite {
    static constexpr int Capacity = 48;
    void* frames[ Capacity ] = {};
    int count = 0;
};

// Starts recording throws; the latest one of the calling thread.
void enableThrowSiteCapture();
const ThrowSite& throwSite();

} // namespace cilogg::diagnostics::detail

#endif
