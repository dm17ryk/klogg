#include "updatefiles.h"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace cilogg::update_files {
namespace {
constexpr std::array<std::uint32_t, 64> RoundConstants{ {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U
} };

constexpr std::size_t ReadBlockSize = 1024 * 1024;

constexpr std::uint32_t rotateRight( std::uint32_t value, unsigned bits )
{
    return ( value >> bits ) | ( value << ( 32U - bits ) );
}

void setError( std::string* errorMessage, const std::string& text )
{
    if ( errorMessage )
        *errorMessage = text;
}

bool isHexDigest( const std::string& text )
{
    return text.size() == 64 && std::all_of( text.cbegin(), text.cend(), []( char character ) {
               return std::isxdigit( static_cast<unsigned char>( character ) ) != 0;
           } );
}

std::string lowerCase( std::string text )
{
    std::transform( text.begin(), text.end(), text.begin(), []( char character ) {
        return static_cast<char>( std::tolower( static_cast<unsigned char>( character ) ) );
    } );
    return text;
}

bool sameComponent( const fs::path& left, const fs::path& right )
{
#ifdef _WIN32
    return CompareStringOrdinal( left.c_str(), -1, right.c_str(), -1, TRUE ) == CSTR_EQUAL;
#else
    return left == right;
#endif
}

#ifdef _WIN32
std::string systemErrorText( int code )
{
    return std::system_category().message( code ) + " (" + std::to_string( code ) + ")";
}

HANDLE toHandle( std::intptr_t handle )
{
    return reinterpret_cast<HANDLE>( handle );
}

std::intptr_t fromHandle( HANDLE handle )
{
    return reinterpret_cast<std::intptr_t>( handle );
}

std::string lastErrorText()
{
    return systemErrorText( static_cast<int>( GetLastError() ) );
}
#else
std::string errnoText( int code )
{
    return std::generic_category().message( code ) + " (" + std::to_string( code ) + ")";
}
#endif
} // namespace

RenameResult renameNoReplace( const fs::path& from, const fs::path& to, std::string* errorMessage )
{
#ifdef _WIN32
    // Without MOVEFILE_REPLACE_EXISTING (and without MOVEFILE_COPY_ALLOWED) this is a single
    // atomic rename that fails when `to` exists.
    if ( MoveFileExW( from.c_str(), to.c_str(), 0 ) )
        return RenameResult::Renamed;
    const auto error = GetLastError();
    setError( errorMessage, systemErrorText( static_cast<int>( error ) ) );
    if ( error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS )
        return RenameResult::DestinationExists;
    if ( error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND )
        return RenameResult::SourceMissing;
    return RenameResult::Failed;
#else
    // link() never replaces its destination, so it is the claim; the old name is dropped after.
    if ( ::link( from.c_str(), to.c_str() ) == 0 ) {
        if ( ::unlink( from.c_str() ) == 0 )
            return RenameResult::Renamed;
        const auto error = errno;
        ::unlink( to.c_str() );
        setError( errorMessage, "cannot remove the old name: " + errnoText( error ) );
        return RenameResult::Failed;
    }
    const auto error = errno;
    if ( error == EEXIST ) {
        setError( errorMessage, errnoText( error ) );
        return RenameResult::DestinationExists;
    }
    if ( error == ENOENT ) {
        setError( errorMessage, errnoText( error ) );
        return RenameResult::SourceMissing;
    }
    if ( error == EPERM || error == ENOTSUP ) {
        // File systems without hard links: check, then rename. Not atomic, but still never
        // replaces a destination that existed before the rename started.
        struct stat existing {};
        if ( ::lstat( to.c_str(), &existing ) == 0 ) {
            setError( errorMessage, errnoText( EEXIST ) );
            return RenameResult::DestinationExists;
        }
        if ( std::rename( from.c_str(), to.c_str() ) == 0 )
            return RenameResult::Renamed;
        const auto renameError = errno;
        setError( errorMessage, errnoText( renameError ) );
        return renameError == ENOENT ? RenameResult::SourceMissing : RenameResult::Failed;
    }
    setError( errorMessage, errnoText( error ) );
    return RenameResult::Failed;
#endif
}

bool isInsideDirectory( const fs::path& path, const fs::path& directory )
{
    std::error_code error;
    const auto canonicalPath = fs::canonical( path, error );
    if ( error )
        return false;
    const auto canonicalDirectory = fs::canonical( directory, error );
    if ( error )
        return false;
    auto pathPart = canonicalPath.begin();
    for ( const auto& directoryPart : canonicalDirectory ) {
        if ( directoryPart.empty() )
            continue;
        if ( pathPart == canonicalPath.end() || !sameComponent( *pathPart, directoryPart ) )
            return false;
        ++pathPart;
    }
    return pathPart != canonicalPath.end();
}

Sha256::Sha256()
    : state_{ { 0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU, 0x510e527fU, 0x9b05688cU,
                0x1f83d9abU, 0x5be0cd19U } }
    , buffer_{}
{
}

void Sha256::update( const unsigned char* bytes, std::size_t size )
{
    totalBytes_ += size;
    while ( size > 0 ) {
        const auto take = ( std::min )( size, buffer_.size() - buffered_ );
        std::copy( bytes, bytes + take,
                   buffer_.begin() + static_cast<std::ptrdiff_t>( buffered_ ) );
        buffered_ += take;
        bytes += take;
        size -= take;
        if ( buffered_ == buffer_.size() ) {
            compress( buffer_.data() );
            buffered_ = 0;
        }
    }
}

std::array<unsigned char, 32> Sha256::finish()
{
    const std::uint64_t bitLength = totalBytes_ * 8U;
    const unsigned char marker = 0x80;
    update( &marker, 1 );
    const unsigned char zero = 0;
    while ( buffered_ != 56 )
        update( &zero, 1 );
    std::array<unsigned char, 8> length{};
    for ( std::size_t i = 0; i < length.size(); ++i )
        length[ i ] = static_cast<unsigned char>( bitLength >> ( 56U - 8U * i ) );
    update( length.data(), length.size() );

    std::array<unsigned char, 32> digest{};
    for ( std::size_t i = 0; i < state_.size(); ++i ) {
        digest[ i * 4 ] = static_cast<unsigned char>( state_[ i ] >> 24 );
        digest[ i * 4 + 1 ] = static_cast<unsigned char>( state_[ i ] >> 16 );
        digest[ i * 4 + 2 ] = static_cast<unsigned char>( state_[ i ] >> 8 );
        digest[ i * 4 + 3 ] = static_cast<unsigned char>( state_[ i ] );
    }
    return digest;
}

void Sha256::compress( const unsigned char* block )
{
    std::array<std::uint32_t, 64> words{};
    for ( std::size_t i = 0; i < 16; ++i ) {
        words[ i ] = ( static_cast<std::uint32_t>( block[ i * 4 ] ) << 24 )
                     | ( static_cast<std::uint32_t>( block[ i * 4 + 1 ] ) << 16 )
                     | ( static_cast<std::uint32_t>( block[ i * 4 + 2 ] ) << 8 )
                     | static_cast<std::uint32_t>( block[ i * 4 + 3 ] );
    }
    for ( std::size_t i = 16; i < words.size(); ++i ) {
        const auto sigma0 = rotateRight( words[ i - 15 ], 7 ) ^ rotateRight( words[ i - 15 ], 18 )
                            ^ ( words[ i - 15 ] >> 3 );
        const auto sigma1 = rotateRight( words[ i - 2 ], 17 ) ^ rotateRight( words[ i - 2 ], 19 )
                            ^ ( words[ i - 2 ] >> 10 );
        words[ i ] = words[ i - 16 ] + sigma0 + words[ i - 7 ] + sigma1;
    }

    auto a = state_[ 0 ];
    auto b = state_[ 1 ];
    auto c = state_[ 2 ];
    auto d = state_[ 3 ];
    auto e = state_[ 4 ];
    auto f = state_[ 5 ];
    auto g = state_[ 6 ];
    auto h = state_[ 7 ];
    for ( std::size_t i = 0; i < words.size(); ++i ) {
        const auto upper1 = rotateRight( e, 6 ) ^ rotateRight( e, 11 ) ^ rotateRight( e, 25 );
        const auto choose = ( e & f ) ^ ( ~e & g );
        const auto first = h + upper1 + choose + RoundConstants[ i ] + words[ i ];
        const auto upper0 = rotateRight( a, 2 ) ^ rotateRight( a, 13 ) ^ rotateRight( a, 22 );
        const auto majority = ( a & b ) ^ ( a & c ) ^ ( b & c );
        const auto second = upper0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    state_[ 0 ] += a;
    state_[ 1 ] += b;
    state_[ 2 ] += c;
    state_[ 3 ] += d;
    state_[ 4 ] += e;
    state_[ 5 ] += f;
    state_[ 6 ] += g;
    state_[ 7 ] += h;
}

std::string toHex( const std::array<unsigned char, 32>& digest )
{
    static constexpr char Digits[] = "0123456789abcdef";
    std::string text;
    text.reserve( digest.size() * 2 );
    for ( const auto byte : digest ) {
        text.push_back( Digits[ byte >> 4 ] );
        text.push_back( Digits[ byte & 0x0f ] );
    }
    return text;
}

ExclusiveFileLock::~ExclusiveFileLock()
{
    release();
}

bool ExclusiveFileLock::acquire( const fs::path& path, long long pid, std::string* errorMessage )
{
    release();
    const auto text = std::to_string( pid ) + "\n";
#ifdef _WIN32
    // No sharing at all: every other open of the file fails while the helper runs.
    const auto file = CreateFileW( path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
    if ( file == INVALID_HANDLE_VALUE ) {
        setError( errorMessage, lastErrorText() );
        return false;
    }
    LARGE_INTEGER start{};
    DWORD written = 0;
    if ( !SetFilePointerEx( file, start, nullptr, FILE_BEGIN ) || !SetEndOfFile( file )
         || !WriteFile( file, text.data(), static_cast<DWORD>( text.size() ), &written,
                        nullptr ) ) {
        // The lock is held anyway; the PID is only diagnostic.
        setError( errorMessage, "lock acquired, PID not written: " + lastErrorText() );
    }
    handle_ = fromHandle( file );
#else
    const auto descriptor = ::open( path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600 );
    if ( descriptor < 0 ) {
        setError( errorMessage, errnoText( errno ) );
        return false;
    }
    if ( ::flock( descriptor, LOCK_EX | LOCK_NB ) != 0 ) {
        setError( errorMessage, errnoText( errno ) );
        ::close( descriptor );
        return false;
    }
    if ( ::ftruncate( descriptor, 0 ) != 0
         || ::write( descriptor, text.data(), text.size() ) < 0 ) {
        // The lock is held anyway; the PID is only diagnostic.
        setError( errorMessage, "lock acquired, PID not written: " + errnoText( errno ) );
    }
    handle_ = descriptor;
#endif
    return true;
}

void ExclusiveFileLock::release()
{
    if ( handle_ == -1 )
        return;
#ifdef _WIN32
    CloseHandle( toHandle( handle_ ) );
#else
    // Closing the descriptor releases the flock().
    ::close( static_cast<int>( handle_ ) );
#endif
    handle_ = -1;
}

bool isLockHeld( const fs::path& path )
{
#ifdef _WIN32
    const auto file
        = CreateFileW( path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
    if ( file == INVALID_HANDLE_VALUE )
        return GetLastError() == ERROR_SHARING_VIOLATION;
    CloseHandle( file );
    return false;
#else
    const auto descriptor = ::open( path.c_str(), O_RDONLY | O_CLOEXEC );
    if ( descriptor < 0 )
        return false;
    // A shared probe: concurrent probes never block each other, the helper's lock blocks them.
    const bool held = ::flock( descriptor, LOCK_SH | LOCK_NB ) != 0 && errno == EWOULDBLOCK;
    ::close( descriptor );
    return held;
#endif
}

VerifiedFile::~VerifiedFile()
{
    close();
}

VerifiedFile::Result VerifiedFile::open( const fs::path& path, const std::string& expectedSha256Hex,
                                         std::string* errorMessage )
{
    close();
    if ( !isHexDigest( expectedSha256Hex ) ) {
        setError( errorMessage, "the expected SHA-256 digest is malformed" );
        return Result::Mismatch;
    }
    Sha256 hash;
    std::vector<unsigned char> block( ReadBlockSize );
#ifdef _WIN32
    // Readers only: no other process can write, rename or delete the file while it is open.
    const auto file = CreateFileW( path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr );
    if ( file == INVALID_HANDLE_VALUE ) {
        setError( errorMessage, "cannot open the package: " + lastErrorText() );
        return Result::Unavailable;
    }
    for ( ;; ) {
        DWORD received = 0;
        if ( !ReadFile( file, block.data(), static_cast<DWORD>( block.size() ), &received,
                        nullptr ) ) {
            setError( errorMessage, "cannot read the package: " + lastErrorText() );
            CloseHandle( file );
            return Result::Unavailable;
        }
        if ( received == 0 )
            break;
        hash.update( block.data(), received );
    }
    const auto keep = fromHandle( file );
#else
    const auto descriptor = ::open( path.c_str(), O_RDONLY | O_CLOEXEC );
    if ( descriptor < 0 ) {
        setError( errorMessage, "cannot open the package: " + errnoText( errno ) );
        return Result::Unavailable;
    }
    for ( ;; ) {
        const auto received = ::read( descriptor, block.data(), block.size() );
        if ( received < 0 ) {
            if ( errno == EINTR )
                continue;
            setError( errorMessage, "cannot read the package: " + errnoText( errno ) );
            ::close( descriptor );
            return Result::Unavailable;
        }
        if ( received == 0 )
            break;
        hash.update( block.data(), static_cast<std::size_t>( received ) );
    }
    const std::intptr_t keep = descriptor;
#endif
    const auto actual = toHex( hash.finish() );
    if ( actual != lowerCase( expectedSha256Hex ) ) {
        setError( errorMessage, "SHA-256 mismatch: expected " + lowerCase( expectedSha256Hex )
                                    + ", found " + actual );
#ifdef _WIN32
        CloseHandle( toHandle( keep ) );
#else
        ::close( static_cast<int>( keep ) );
#endif
        return Result::Mismatch;
    }
    handle_ = keep;
    return Result::Verified;
}

void VerifiedFile::close()
{
    if ( handle_ == -1 )
        return;
#ifdef _WIN32
    CloseHandle( toHandle( handle_ ) );
#else
    ::close( static_cast<int>( handle_ ) );
#endif
    handle_ = -1;
}

} // namespace cilogg::update_files
