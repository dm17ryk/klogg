#pragma once
#include <QString>
#include <QStringList>

// One place that knows where the bundled Python runtime package (the `klogg`
// package, worker_bootstrap.py, scenario_bootstrap.py) is deployed.
//
//  - Windows and Linux (build tree, DEB/RPM, private-Qt layout, AppImage):
//    <executable dir>/python_runtime, next to the shipped plugins.
//  - macOS: <bundle>/Contents/Resources/python_runtime. Scripts stay out of
//    Contents/MacOS so `codesign --deep` only sees Mach-O code there.
namespace cilogg::pythonruntime {

// Ordered directories that may hold the runtime package for an executable in
// `applicationDir`. Exposed for tests and diagnostics.
QStringList rootCandidates( const QString& applicationDir );

// First candidate that contains worker_bootstrap.py, or an empty string.
QString rootFor( const QString& applicationDir );

// rootFor( QCoreApplication::applicationDirPath() ).
QString root();

// Ordered interpreter paths a package may bundle for an executable in
// `applicationDir` (inside the runtime roots first, then beside the executable).
// Callers keep the first one that exists before falling back to PATH.
QStringList bundledInterpreterCandidates( const QString& applicationDir );

// Python interpreters found on PATH, best first. On Windows real executables come first:
// python.exe, then python3.exe, so a launcher script such as the pyenv-win shim python3.bat
// (which can take half a minute to start Python) is used only when nothing else is installed;
// Microsoft Store aliases come last. Elsewhere python3 comes first, then python. `searchPaths`
// replaces PATH (tests).
QStringList pathInterpreterCandidates( const QStringList& searchPaths = {} );

// Whether `path` is a launcher script rather than an interpreter (.bat or .cmd on Windows).
bool isLauncherScript( const QString& path );

// Whether `path` is a Microsoft Store app alias (in ...\Microsoft\WindowsApps). Without the
// Store's Python installed, its python.exe only says how to install it.
bool isStoreAlias( const QString& path );

} // namespace cilogg::pythonruntime
