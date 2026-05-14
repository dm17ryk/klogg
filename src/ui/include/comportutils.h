#pragma once

#include <QComboBox>
#include <QDateTime>
#include <QSerialPort>
#include <QString>

#include "commander.h"
#include "serialcaptureworker.h"

// Returns a writable default directory for COM capture logs (Documents/kloggs or home fallback),
// creating it if needed.
QString defaultComLogDirectory();

// Build the default serial capture settings from current Preferences.
SerialCaptureSettings defaultSerialCaptureSettings();

// Build a suggested capture file path based on the port/baud and the configured capture directory.
QString suggestedComCapturePath( const SerialCaptureSettings& settings );

// Build the next capture file path in the same directory as the current capture.
QString suggestedNextComCapturePath(
    const SerialCaptureSettings& settings,
    const QDateTime& timestamp = QDateTime::currentDateTime() );

// Resolve commander COM options against Preferences/defaults.
SerialCaptureSettings resolveCommanderComSettings( const CommanderComSettings& settings );

// Ensures that the given capture file can be created/opened for append.
// Returns true on success, false on failure and optionally populates errorMessage.
bool ensureComCaptureFileWritable( const QString& path, QString* errorMessage = nullptr );

// Populate standard serial parameter combos with common values and defaults.
void populateSerialControls( QComboBox* baudCombo,
                             QComboBox* dataBitsCombo,
                             QComboBox* parityCombo,
                             QComboBox* stopBitsCombo,
                             QComboBox* flowCombo );

