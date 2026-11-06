// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#define CATCH_CONFIG_RUNNER
#include <QCoreApplication>
#include <catch2/catch.hpp>

int main( int argc, char** argv )
{
    QCoreApplication application( argc, argv );
    return Catch::Session().run( argc, argv );
}
