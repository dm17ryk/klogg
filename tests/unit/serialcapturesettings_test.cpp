/*
 * Copyright (C) 2026 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <catch2/catch.hpp>

#include "serialcaptureworker.h"

TEST_CASE( "Serial capture settings serialize/deserialize roundtrip", "[serial][session]" )
{
    SerialCaptureSettings original;
    original.portName = "COM7";
    original.baudRate = 230400;
    original.dataBits = QSerialPort::Data7;
    original.parity = QSerialPort::EvenParity;
    original.stopBits = QSerialPort::TwoStop;
    original.flowControl = QSerialPort::HardwareControl;
    original.filePath = "D:/logs/com7.log";
    original.addTimestamps = true;
    original.timestampFormat = "dd/MM/yyyy HH:mm:ss.zzz";
    original.logTransmits = true;
    original.useForActions = true;

    const auto serialized = serializeSerialCaptureSettings( original );
    const auto restored = deserializeSerialCaptureSettings( serialized );

    REQUIRE( restored.has_value() );
    REQUIRE( restored->portName == original.portName );
    REQUIRE( restored->baudRate == original.baudRate );
    REQUIRE( restored->dataBits == original.dataBits );
    REQUIRE( restored->parity == original.parity );
    REQUIRE( restored->stopBits == original.stopBits );
    REQUIRE( restored->flowControl == original.flowControl );
    REQUIRE( restored->filePath == original.filePath );
    REQUIRE( restored->addTimestamps == original.addTimestamps );
    REQUIRE( restored->timestampFormat == original.timestampFormat );
    REQUIRE( restored->logTransmits == original.logTransmits );
    REQUIRE( restored->useForActions == original.useForActions );
}

TEST_CASE( "Serial capture settings deserialize rejects invalid payload", "[serial][session]" )
{
    REQUIRE_FALSE( deserializeSerialCaptureSettings( QString{} ).has_value() );
    REQUIRE_FALSE( deserializeSerialCaptureSettings( QStringLiteral( "not_json" ) ).has_value() );
    REQUIRE_FALSE( deserializeSerialCaptureSettings( QStringLiteral( "{\"baudRate\":115200}" ) )
                       .has_value() );
}
