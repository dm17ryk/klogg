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

#include <QFile>
#include <QTemporaryDir>

#include "comportutils.h"
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

TEST_CASE( "Next COM capture path uses current directory and naming convention", "[serial][path]" )
{
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    SerialCaptureSettings settings;
    settings.portName = "COM7";
    settings.baudRate = 230400;
    settings.filePath = tempDir.filePath( "existing.log" );

    const auto timestamp = QDateTime::fromString( "2026-05-06T14:15:16", Qt::ISODate );
    const auto nextPath = suggestedNextComCapturePath( settings, timestamp );

    REQUIRE( QFileInfo( nextPath ).absolutePath() == QFileInfo( settings.filePath ).absolutePath() );
    REQUIRE( QFileInfo( nextPath ).fileName() == "com7_230400_2026-05-06_14-15-16.log" );
    REQUIRE( QFileInfo( nextPath ).absoluteFilePath() != QFileInfo( settings.filePath ).absoluteFilePath() );
}

TEST_CASE( "Next COM capture path appends suffix for same-second collision", "[serial][path]" )
{
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    SerialCaptureSettings settings;
    settings.portName = "COM9";
    settings.baudRate = 115200;
    const auto timestamp = QDateTime::fromString( "2026-05-06T14:15:16", Qt::ISODate );
    settings.filePath = tempDir.filePath( "com9_115200_2026-05-06_14-15-16.log" );

    QFile currentFile( settings.filePath );
    REQUIRE( currentFile.open( QIODevice::WriteOnly ) );
    currentFile.close();

    QFile collisionFile( tempDir.filePath( "com9_115200_2026-05-06_14-15-16_2.log" ) );
    REQUIRE( collisionFile.open( QIODevice::WriteOnly ) );
    collisionFile.close();

    const auto nextPath = suggestedNextComCapturePath( settings, timestamp );

    REQUIRE( QFileInfo( nextPath ).fileName() == "com9_115200_2026-05-06_14-15-16_3.log" );
}

TEST_CASE( "Serial capture worker switches capture file without serial port", "[serial][worker]" )
{
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    SerialCaptureSettings settings;
    settings.portName = "COM7";
    settings.filePath = tempDir.filePath( "old.log" );

    SerialCaptureWorker worker( settings );
    QString errorMessage;
    const auto nextPath = tempDir.filePath( "new.log" );

    REQUIRE( worker.switchCaptureFile( nextPath, &errorMessage ) );
    REQUIRE( errorMessage.isEmpty() );

    worker.appendToFile( QByteArrayLiteral( "rotated data\n" ) );

    QFile newFile( nextPath );
    REQUIRE( newFile.open( QIODevice::ReadOnly ) );
    REQUIRE( newFile.readAll() == QByteArrayLiteral( "rotated data\n" ) );
}

TEST_CASE( "Serial capture worker reports switch capture file errors", "[serial][worker]" )
{
    SerialCaptureSettings settings;
    settings.portName = "COM7";
    SerialCaptureWorker worker( settings );

    QString errorMessage;
    REQUIRE_FALSE( worker.switchCaptureFile( QString{}, &errorMessage ) );
    REQUIRE_FALSE( errorMessage.isEmpty() );
}
