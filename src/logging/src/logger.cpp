/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
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

#include "logger.h"
#include "log.h"

#include <atomic>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <shared_mutex>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>

namespace logging {

namespace {
constexpr auto MessagePattern = "%{time} %{type} [%{threadid}] [%{function}@%{line}] %{message}";
constexpr int DefaultRecentCapacity = 2000;
} // namespace

void kloggMessageHandler( QtMsgType type, const QMessageLogContext& context, const QString& msg );

class Logger {
  public:
    static Logger& instance()
    {
        static Logger l;
        return l;
    }

    // One handler for every message: the recent-lines tail keeps warnings and errors always and
    // anything the console or file log records; the console and file get what their level allows.
    void handleMessage( QtMsgType type, const QMessageLogContext& context, const QString& msg )
    {
        const bool toOutput = outputWants( type );
        if ( !toOutput && !tailWants( type ) ) {
            return;
        }

        if ( !toOutput ) {
            // A burst of the same warning (Qt can repeat one per paint) costs one comparison.
            ScopedLock lock( mutex_ );
            if ( type == lastTailType_ && msg == lastTailMessage_ ) {
                ++lastTailRepeats_;
                return;
            }
        }

        const auto formattedMessage = qFormatLogMessage( type, context, msg );

        ScopedLock lock( mutex_ );
        if ( lastTailRepeats_ > 0 ) {
            appendRecent( QStringLiteral( "    (previous message repeated %1 more times)" )
                              .arg( lastTailRepeats_ ) );
            lastTailRepeats_ = 0;
        }
        lastTailType_ = type;
        lastTailMessage_ = msg;
        appendRecent( formattedMessage );

        if ( !toOutput ) {
            return;
        }

        const auto messageToPrint = formattedMessage.toUtf8();
        if ( logFile_ ) {
            QTextStream ts( logFile_.get() );
            ts << messageToPrint << '\n';
        }
        if ( isConsoleLogEnabled_ || !logFile_ ) {
            std::cout << messageToPrint.constData() << std::endl;
        }
    }

    QStringList recentLines() const
    {
        SharedLock lock( mutex_ );
        QStringList lines;
        lines.reserve( static_cast<int>( recent_.size() ) + 1 );
        for ( const auto& line : recent_ ) {
            lines.push_back( line );
        }
        if ( lastTailRepeats_ > 0 ) {
            lines.push_back( QStringLiteral( "    (previous message repeated %1 more times)" )
                                 .arg( lastTailRepeats_ ) );
        }
        return lines;
    }

    void setRecentCapacity( int lines )
    {
        ScopedLock lock( mutex_ );
        recentCapacity_ = qMax( 1, lines );
        while ( static_cast<int>( recent_.size() ) > recentCapacity_ ) {
            recent_.pop_front();
        }
    }

    void clearRecent()
    {
        ScopedLock lock( mutex_ );
        recent_.clear();
        lastTailMessage_.clear();
        lastTailRepeats_ = 0;
    }

    void enableLogging( bool isEnabled, uint8_t logLevel )
    {
        ScopedLock lock( mutex_ );

        qSetMessagePattern( MessagePattern );

        isConsoleLogEnabled_ = isEnabled;
        logLevel_ = logLevel;

        setMessageHandler();
    }

    void enableFileLogging( bool isEnabled, uint8_t logLevel )
    {
        ScopedLock lock( mutex_ );

        qSetMessagePattern( MessagePattern );

        isFileLogEnabled_ = isEnabled;
        logLevel_ = logLevel;

        if ( isEnabled && !logFile_ ) {
            auto logFileName
                = QString( "klogg_%1_%2.log" )
                      .arg( QDateTime::currentDateTime().toString( "yyyy-MM-dd_HH-mm-ss" ) )
                      .arg( QCoreApplication::applicationPid() );

            logFile_ = std::make_unique<QFile>( QDir::temp().filePath( logFileName ) );
            if ( !logFile_->open( QIODevice::WriteOnly | QIODevice::Append ) ) {
                logFile_.reset();
            }
        }
        else if ( !isEnabled && logFile_ ) {
            logFile_.reset();
        }

        setMessageHandler();
    }

    bool isAnyEnabled() const
    {
        return isConsoleLogEnabled_ || isFileLogEnabled_;
    }

    bool needLogging( QtMsgType type ) const
    {
        return outputWants( type ) || tailWants( type );
    }

  private:
    Logger()
    {
        qSetMessagePattern( MessagePattern );
        setMessageHandler();
    }

    bool outputWants( QtMsgType type ) const
    {
        return isAnyEnabled() && static_cast<uint8_t>( logLevel( type ) ) <= logLevel_;
    }

    static bool tailWants( QtMsgType type )
    {
        return static_cast<uint8_t>( logLevel( type ) )
               <= static_cast<uint8_t>( LogLevel::Warning );
    }

    void setMessageHandler()
    {
        qInstallMessageHandler( logging::kloggMessageHandler );
    }

    // Callers hold mutex_.
    void appendRecent( const QString& line )
    {
        recent_.push_back( line );
        while ( static_cast<int>( recent_.size() ) > recentCapacity_ ) {
            recent_.pop_front();
        }
    }

  private:
    static LogLevel logLevel( QtMsgType type )
    {
        switch ( type ) {
        case QtFatalMsg:
            return LogLevel::Fatal;
        case QtCriticalMsg:
            return LogLevel::Error;

        case QtWarningMsg:
            return LogLevel::Warning;

        case QtInfoMsg:
            return LogLevel::Info;
        case QtDebugMsg:
            return LogLevel::Debug;
        default:
            return LogLevel::Info;
        }
    }

  private:
    mutable std::shared_mutex mutex_;
    using ScopedLock = std::unique_lock<std::shared_mutex>;
    using SharedLock = std::shared_lock<std::shared_mutex>;

    std::atomic_bool isConsoleLogEnabled_ = false;
    std::atomic_bool isFileLogEnabled_ = false;

    std::atomic_int logLevel_ = 0;

    std::unique_ptr<QFile> logFile_;

    std::deque<QString> recent_;
    int recentCapacity_ = DefaultRecentCapacity;
    QtMsgType lastTailType_ = QtDebugMsg;
    QString lastTailMessage_;
    int lastTailRepeats_ = 0;
};

void enableLogging( bool isEnabled, LogLevel logLevel )
{
    Logger::instance().enableLogging( isEnabled, static_cast<uint8_t>( logLevel ) );
}

void enableFileLogging( bool isEnabled, LogLevel logLevel )
{
    Logger::instance().enableFileLogging( isEnabled, static_cast<uint8_t>( logLevel ) );
}

void kloggMessageHandler( QtMsgType type, const QMessageLogContext& context, const QString& msg )
{
    Logger::instance().handleMessage( type, context, msg );
}

bool needLogging( QtMsgType type )
{
    return Logger::instance().needLogging( type );
}

QStringList recentLogLines()
{
    return Logger::instance().recentLines();
}

void setRecentLogCapacity( int lines )
{
    Logger::instance().setRecentCapacity( lines );
}

void clearRecentLogLines()
{
    Logger::instance().clearRecent();
}

} // namespace logging