Please address the comments from this code review:

## Overall Comments
- The COM defaults logic (baud/data bits/parity/flow control lists and label texts) is now duplicated between `OpenComPortDialog` and `OptionsDialog::setupComDefaults`; consider centralizing this in a shared helper to keep options and the open dialog in sync.
- The `fallbackDocs` and base-directory selection logic for COM log paths is duplicated in `OpenComPortDialog::updateSuggestedFileName` and `suggestedFileName`; extracting a small utility (or at least a shared static function) would reduce repetition and the chance of divergence.
- The COM default configuration fields are stored as bare `int` values with comments referencing `QSerialPort` enums; using the actual `QSerialPort` enum types (or a strongly-typed wrapper) would improve type safety and self-documentation of these settings.

## Individual Comments

### Comment 1
<location> `src/ui/src/optionsdialog.cpp:234-243` </location>
<code_context>
+    comFlowComboBox->addItem( tr( "XON/XOFF" ), QSerialPort::SoftwareControl );
+}
+
+void OptionsDialog::browseComLogPath()
+{
+    auto current = comLogPathEdit->text().trimmed();
+    QString initialDir;
+    if ( !current.isEmpty() && QDir( current ).exists() ) {
+        initialDir = current;
+    }
+    else {
+        auto docs = QStandardPaths::writableLocation( QStandardPaths::DocumentsLocation );
+        if ( docs.isEmpty() ) {
+            docs = QDir::homePath();
+        }
+        initialDir = QDir( docs ).filePath( QStringLiteral( "kloggs" ) );
+        QDir initDir( initialDir );
+        if ( !initDir.exists() ) {
+            initDir.mkpath( "." );
+        }
+    }
+
+    const auto dir = QFileDialog::getExistingDirectory( this, tr( "Select log folder" ), initialDir );
+    if ( !dir.isEmpty() ) {
+        comLogPathEdit->setText( dir );
+    }
+}
</code_context>

<issue_to_address>
**suggestion:** Log-directory fallback logic is repeated in several places and could be centralized

This default log-directory selection (Documents vs home, app-specific subfolder, `mkpath` fallback) is now implemented in at least three places: `OptionsDialog::browseComLogPath()`, `OpenComPortDialog::updateSuggestedFileName()`, and `OpenComPortDialog::suggestedFileName()`. Extracting this into a shared helper would avoid duplication and keep future behavior changes in one place.

Suggested implementation:

```cpp
namespace {
QString defaultLogDirectory()
{
    auto docs = QStandardPaths::writableLocation( QStandardPaths::DocumentsLocation );
    if ( docs.isEmpty() ) {
        docs = QDir::homePath();
    }

    const QString initialDir = QDir( docs ).filePath( QStringLiteral( "kloggs" ) );
    QDir initDir( initialDir );
    if ( !initDir.exists() ) {
        initDir.mkpath( "." );
    }

    return initialDir;
}
}

void OptionsDialog::browseComLogPath()

```

```cpp
{
    const auto current = comLogPathEdit->text().trimmed();

    QString initialDir;
    if ( !current.isEmpty() && QDir( current ).exists() ) {
        initialDir = current;
    } else {
        initialDir = defaultLogDirectory();
    }

    const auto dir = QFileDialog::getExistingDirectory( this, tr( "Select log folder" ), initialDir );
    if ( !dir.isEmpty() ) {
        comLogPathEdit->setText( dir );
    }
}

```

To fully remove duplication, similar centralization should be done for:
1. `OpenComPortDialog::updateSuggestedFileName()`
2. `OpenComPortDialog::suggestedFileName()`

Since these live in a different translation unit, you likely want to:
- Move `defaultLogDirectory()` to a shared helper (e.g. a small utility header/source used by both `OptionsDialog` and `OpenComPortDialog`), or
- Declare it in a common header and define it in a shared `.cpp` file, then replace the repeated code in those `OpenComPortDialog` methods with calls to this helper.
</issue_to_address>
