#pragma once

// Qt-free helpers for replacing an installed CILogg while some of its processes keep running.
//
// Windows lets a running executable or loaded DLL be renamed but not overwritten or deleted. The
// Windows installer (through cilogg_updater's installer modes) therefore moves the files that may
// be in use aside instead of closing processes: `cilogg.exe mcp serve` instances started by AI
// agents, CLI jobs and other windows keep running their old copy, and new starts get the new
// version. The updater helper uses the same process queries to name what blocked an install.

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace cilogg::install_prepare {

// Suffix of a file moved aside during an update: "<name>.cilogg-old", or "<name>.cilogg-old-<n>"
// while an earlier copy is still in use. It is deleted once no process uses it: at the end of that
// update or by the next one.
inline constexpr char MovedAsideSuffix[] = ".cilogg-old";
// The record of the files moved aside. It lives inside the installation folder, and every entry
// must lie inside that folder. In Program Files only administrators can change it; whoever can
// write a custom installation folder controls the installer's writes there anyway.
inline constexpr char ManifestFileName[] = "cilogg-update-moves.txt";

// Exit codes of the installer modes.
inline constexpr int PreparedExitCode = 0;
inline constexpr int UsageExitCode = 1;
// A file could not be moved aside; nothing was changed.
inline constexpr int BlockedExitCode = 2;
// Some previous files could not be restored; the manifest is kept for the next attempt.
inline constexpr int RollbackIncompleteExitCode = 3;
// The manifest could not be written; nothing was changed.
inline constexpr int ManifestFailedExitCode = 4;
// Installed, but some files the installation did not replace could not be put back; the
// manifest keeps them for the next update, which puts them back first.
inline constexpr int FinishIncompleteExitCode = 5;
// The helper stopped on an unexpected error; files may have been moved aside already.
inline constexpr int UnexpectedErrorExitCode = 6;
// Asked to roll back an installation whose cleanup had already started ("#finishing"): it was
// completed instead, since its old copies may be gone already. The new version is installed.
inline constexpr int FinishedInsteadExitCode = 7;
// As FinishedInsteadExitCode, but files that installation did not replace could not be put back;
// the manifest keeps them for the next update.
inline constexpr int FinishedInsteadIncompleteExitCode = 8;

struct ProcessInfo {
    unsigned long pid = 0;
    // Empty when the process cannot be queried (another user's or a protected process).
    std::filesystem::path image;
    // Executable file name, also when the full path is not available.
    std::string name;
    unsigned long parentPid = 0;
    // Empty when the parent has exited or its process ID was reused.
    std::string parentName;
};

struct MovedFile {
    std::filesystem::path original;
    std::filesystem::path movedTo;
};

// One UTF-8 line per event, without a line break.
using Log = std::function<void( const std::string& )>;

// "<pid>\t<name>\t<image>\t<parent pid>\t<parent name>" (UTF-8), the install-result.txt format.
std::string formatBlocker( const ProcessInfo& process );

// "cilogg.exe (PID 12, C:\...\cilogg.exe), started by claude.exe (PID 7)", for logs.
std::string describeProcess( const ProcessInfo& process );

// "<original>\t<moved to>" (UTF-8), one line of the manifest.
std::string formatManifestLine( const MovedFile& moved );
// Lines that are not two absolute paths are skipped.
std::vector<MovedFile> parseManifest( const std::string& text );

bool isMovedAside( const std::filesystem::path& path );
// Executable images (.exe, .dll, .pyd) a process may load at any moment of an installation.
bool isExecutableImage( const std::filesystem::path& path );
// True when `candidate` is a name moveAside() gives `original`: same folder, the original file
// name, MovedAsideSuffix and an optional "-<n>".
bool isMovedAsideNameOf( const std::filesystem::path& original,
                         const std::filesystem::path& candidate );

// Every running process whose executable lies inside `directory` (Windows; empty elsewhere).
std::vector<ProcessInfo> processesRunningFrom( const std::filesystem::path& directory );
// The processes that keep any of `files` open, from the Windows Restart Manager (empty
// elsewhere or when the query fails; `error` then says why).
std::vector<ProcessInfo> processesUsing( const std::vector<std::filesystem::path>& files,
                                         std::string* error = nullptr );

// True when the file cannot be opened for writing the way the installer replaces it: a running
// executable, a loaded DLL, or a file another process opened without sharing write access.
bool isInUse( const std::filesystem::path& file );

// Deletes files moved aside by earlier updates in the installation in `directory` once no process
// uses them, and puts back one whose original is missing. Returns how many are kept.
std::size_t removeMovedAside( const std::filesystem::path& directory, const Log& log );

struct PrepareResult {
    std::vector<MovedFile> moved;
    // Files that could not be moved aside. The caller then puts `moved` back with rollback().
    std::vector<std::filesystem::path> blocked;
    // Top-level DLLs that could not be moved and may be another program's: left where they were.
    std::vector<std::filesystem::path> leftInPlace;
    // False when a folder of the installation could not be read to its end.
    bool listedAll = true;
};

// Moves aside every file of the CILogg installation in `directory`, so running processes keep
// theirs and a failed installation can put the previous version back exactly. First the .exe
// files (so nothing can start from a half-replaced folder), then the DLLs, then the rest.
//
// Only CILogg's files are considered: the top-level files cilogg.nsi installs (its executables,
// the uninstaller, the DLLs and its documents) and the folders it installs. A folder that holds
// no CILogg installation is left alone. Links and junctions are never followed, and every rename
// first checks that no folder on the way is one. `onMoved` sees each move as it happens, so a
// manifest can be written before the next one. Files that cannot be moved are tried again after
// a pause; a top-level DLL that still cannot be moved is left in place (it may be another
// program's). A blocked file does not stop the pass: every blocked file is reported.
PrepareResult moveAside( const std::filesystem::path& directory, const Log& log,
                         const std::function<void( const MovedFile& )>& onMoved = {} );

// After the new files were copied. An original the installation replaced is deleted, or, while
// a process still uses it, left for the next update. An original it did not replace is put back.
// Returns the files that could not be put back. Entries an earlier pass handled are skipped.
std::vector<MovedFile> finish( const std::filesystem::path& directory,
                               const std::vector<MovedFile>& moved, const Log& log );

// After a failed copy: puts every original back, moving a partly installed new file aside. With
// `removeAdded` (only when `moved` lists every file of the previous version), it then removes the
// files the failed installation added: every other file of the installation except
// `leftInPlace`. Entries an earlier pass restored are skipped. Returns the entries that could
// not be restored for now (another attempt may succeed); a file lost meanwhile and an entry
// outside the installation folder are logged and dropped, as nothing can ever act on them.
// `addedKept` receives how many added files could not be removed (another attempt may succeed).
std::vector<MovedFile> rollback( const std::filesystem::path& directory,
                                 const std::vector<MovedFile>& moved, const Log& log,
                                 bool removeAdded = false,
                                 const std::vector<std::filesystem::path>& leftInPlace = {},
                                 std::size_t* addedKept = nullptr );

// The installer modes of cilogg_updater. "prepare" first restores what an interrupted earlier
// update left (from its manifest), then moves files aside and records them in the manifest; when
// a file is blocked, a report lists it and the processes using it. "finish" and "rollback" read
// the manifest, which they check against `directory`, and delete it once nothing is left to do.
int runPrepare( const std::filesystem::path& directory, const std::filesystem::path& report,
                const Log& log );
int runFinish( const std::filesystem::path& directory, const Log& log );
int runRollback( const std::filesystem::path& directory, const Log& log );

} // namespace cilogg::install_prepare
