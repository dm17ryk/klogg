#pragma once

#include <QString>
#include <QComboBox>
#include <QSerialPort>

// Returns a writable default directory for COM capture logs (Documents/kloggs or home fallback),
// creating it if needed.
QString defaultComLogDirectory();

// Ensures that the given capture file can be created/opened for append.
// Returns true on success, false on failure and optionally populates errorMessage.
bool ensureComCaptureFileWritable( const QString& path, QString* errorMessage = nullptr );

// Populate standard serial parameter combos with common values and defaults.
void populateSerialControls( QComboBox* baudCombo,
                             QComboBox* dataBitsCombo,
                             QComboBox* parityCombo,
                             QComboBox* stopBitsCombo,
                             QComboBox* flowCombo );

