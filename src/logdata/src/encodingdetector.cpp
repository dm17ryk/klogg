/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
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

#include "encodingdetector.h"

#include <QTextCodec>
#include <cstring>
#include <simdutf.h>

#include "containers.h"
#include "log.h"
#include <uchardet.h>

namespace {

class UchardetHolder {
  public:
    UchardetHolder()
        : ud_{ uchardet_new() }
    {
    }
    ~UchardetHolder()
    {
        uchardet_delete( ud_ );
    }

    UchardetHolder( const UchardetHolder& ) = delete;
    UchardetHolder& operator=( const UchardetHolder& ) = delete;

    UchardetHolder( UchardetHolder&& other ) = delete;
    UchardetHolder& operator=( UchardetHolder&& other ) = delete;

    int handle_data( const char* data, size_t len )
    {
        return uchardet_handle_data( ud_, data, len );
    }

    void data_end()
    {
        uchardet_data_end( ud_ );
    }

    const char* get_charset()
    {
        return uchardet_get_charset( ud_ );
    }

  private:
    uchardet_t ud_;
};

} // namespace

EncodingParameters::EncodingParameters( const QTextCodec* codec )
{
    static constexpr QChar LineFeed( QChar::LineFeed );
    static constexpr int Utf8Mib = 106;
    static constexpr int Utf16LEMib = 1014;
    static constexpr int UsAsciiMib = 3;

    isUtf8Compatible = codec->mibEnum() == Utf8Mib || codec->mibEnum() == UsAsciiMib;
    isUtf16LE = codec->mibEnum() == Utf16LEMib;

    QTextCodec::ConverterState convertState( QTextCodec::IgnoreHeader );
    const QByteArray encodedLineFeed = codec->fromUnicode( &LineFeed, 1, &convertState );

    lineFeedWidth = static_cast<int>( encodedLineFeed.size() );
    lineFeedIndex
        = encodedLineFeed[ 0 ] == '\n' ? 0 : ( static_cast<int>( encodedLineFeed.size() ) - 1 );
}

QTextCodec* EncodingDetector::detectEncoding( const klogg::vector<char>& block ) const
{
    const QByteArray blockArray = QByteArray::fromRawData( block.data(), klogg::isize( block ) );
    if ( auto* bomCodec = QTextCodec::codecForUtfText( blockArray, nullptr ) ) {
        LOG_DEBUG << "Encoding detection: BOM selected " << bomCodec->name().constData()
                  << " sampleBytes=" << block.size();
        return bomCodec;
    }

    // NUL may indicate BOM-less UTF-16/32; ESC may introduce an ISO-2022 encoding.
    // Neither should be classified as UTF-8 merely because its bytes validate.
    const bool ambiguous = block.empty()
                           || std::memchr( block.data(), '\0', block.size() ) != nullptr
                           || std::memchr( block.data(), '\x1b', block.size() ) != nullptr;
    if ( !ambiguous && simdutf::validate_utf8( block.data(), block.size() ) ) {
        const auto* name = simdutf::validate_ascii( block.data(), block.size() ) ? "US-ASCII" : "UTF-8";
        auto* codec = QTextCodec::codecForName( name );
        if ( codec != nullptr ) {
            LOG_DEBUG << "Encoding detection: validated fast path selected " << name
                      << " sampleBytes=" << block.size();
            return codec;
        }
        LOG_DEBUG << "Encoding detection: validated codec unavailable, preserving legacy fallback: "
                  << name;
    }

    LOG_DEBUG << "Encoding detection: using legacy fallback sampleBytes=" << block.size()
              << " ambiguous=" << ambiguous;

    // Mutable detector state belongs to this invocation. Qt's codec lookup functions
    // are thread-safe, so independent file opens do not need a singleton-wide lock.
    UchardetHolder ud;

    auto rc = ud.handle_data( block.data(), block.size() );
    if ( rc == 0 ) {
        ud.data_end();
    }
    else {
        LOG_WARNING << "Encoding detection: uchardet rejected the sample rc=" << rc
                    << " sampleBytes=" << block.size();
    }

    QTextCodec* uchardetCodec = nullptr;
    if ( rc == 0 ) {
        auto uchardetGuess = ud.get_charset();
        LOG_DEBUG << "Uchardet encoding guess " << uchardetGuess;
        uchardetCodec = QTextCodec::codecForName( uchardetGuess );
        if ( uchardetCodec ) {
            LOG_DEBUG << "Uchardet codec selected " << uchardetCodec->name().constData();
        }
        else {
            LOG_DEBUG << "Uchardet codec not found for guess " << uchardetGuess;
        }
    }

    auto encodingGuess = uchardetCodec ? QTextCodec::codecForUtfText( blockArray, uchardetCodec )
                                       : QTextCodec::codecForUtfText( blockArray );

    LOG_DEBUG << "Final encoding guess " << encodingGuess->name().constData();

    return encodingGuess;
}

TextCodecHolder::TextCodecHolder( QTextCodec* codec )
    : codec_{ codec }
    , encodingParams_{ codec }
{
    assert( codec != nullptr );
}

QTextCodec* TextCodecHolder::codec() const
{
    SharedLock guard( mutex_ );
    return codec_;
}

EncodingParameters TextCodecHolder::encodingParameters() const
{
    SharedLock guard( mutex_ );
    return encodingParams_;
}

int TextCodecHolder::mibEnum() const
{
    SharedLock guard( mutex_ );
    return codec_->mibEnum();
}

void TextCodecHolder::setCodec( QTextCodec* codec )
{
    UniqueLock guard( mutex_ );
    codec_ = codec;
    encodingParams_ = EncodingParameters{ codec_ };
}

TextDecoder TextCodecHolder::makeDecoder() const
{
    SharedLock guard( mutex_ );
    return { std::make_unique<QTextDecoder>( codec_ ), encodingParams_ };
}
