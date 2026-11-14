#pragma once

// Qt-free file primitives shared by CILogg and its updater helper (cilogg_updater), so both
// sides of the exit-time update hand-off follow the same claim, liveness and verification rules.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace cilogg::update_files {

enum class RenameResult { Renamed, DestinationExists, SourceMissing, Failed };

// Renames `from` to `to` in one step and never replaces an existing `to`. Of several processes
// renaming the same source, or onto the same destination, at most one succeeds.
RenameResult renameNoReplace( const std::filesystem::path& from, const std::filesystem::path& to,
                              std::string* errorMessage = nullptr );

// True when `path` and `directory` both exist and `path` lies strictly below `directory`
// once links and relative parts are resolved.
bool isInsideDirectory( const std::filesystem::path& path,
                        const std::filesystem::path& directory );

class Sha256 {
public:
    Sha256();
    void update( const unsigned char* bytes, std::size_t size );
    std::array<unsigned char, 32> finish();

private:
    void compress( const unsigned char* block );

    std::array<std::uint32_t, 8> state_;
    std::array<unsigned char, 64> buffer_;
    std::size_t buffered_ = 0;
    std::uint64_t totalBytes_ = 0;
};

// Lower-case hexadecimal text of `digest`.
std::string toHex( const std::array<unsigned char, 32>& digest );

// The updater helper holds this lock for its whole lifetime, so CILogg can tell a running
// install from one that ended without a result. Windows keeps the file open without sharing;
// POSIX systems hold an flock() on it. The lock is never inherited by child processes.
class ExclusiveFileLock {
public:
    ExclusiveFileLock() = default;
    ~ExclusiveFileLock();
    ExclusiveFileLock( const ExclusiveFileLock& ) = delete;
    ExclusiveFileLock& operator=( const ExclusiveFileLock& ) = delete;

    // Creates `path` if needed, takes it exclusively and writes `pid` into it for diagnostics.
    bool acquire( const std::filesystem::path& path, long long pid,
                  std::string* errorMessage = nullptr );
    void release();
    bool isAcquired() const
    {
        return handle_ != -1;
    }

private:
    std::intptr_t handle_ = -1;
};

// True while a process (this one included) holds the ExclusiveFileLock at `path`.
bool isLockHeld( const std::filesystem::path& path );

// A file whose exact content was verified against an expected SHA-256. While it stays open,
// Windows denies every other writer and any rename or deletion of the file; POSIX systems have
// no such sharing mode, so there it only proves the content at verification time.
class VerifiedFile {
public:
    enum class Result { Verified, Mismatch, Unavailable };

    VerifiedFile() = default;
    ~VerifiedFile();
    VerifiedFile( const VerifiedFile& ) = delete;
    VerifiedFile& operator=( const VerifiedFile& ) = delete;

    // Opens `path`, hashes it through the open handle and keeps it open when the digest equals
    // `expectedSha256Hex` (64 hexadecimal characters). Unavailable means the file could not be
    // opened or read, which can be transient while security software scans it.
    Result open( const std::filesystem::path& path, const std::string& expectedSha256Hex,
                 std::string* errorMessage = nullptr );
    void close();
    bool isOpen() const
    {
        return handle_ != -1;
    }

private:
    std::intptr_t handle_ = -1;
};

} // namespace cilogg::update_files
