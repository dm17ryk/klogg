#pragma once
// Sets the FAKE_GIT_* variables of tests/helpers/fake_git.cpp (and stray askpass variables) for
// one test and restores them. FAKE_GIT_PATH is defined by the test target.
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QTemporaryDir>
#include <catch2/catch.hpp>
#include <optional>

namespace testgit {
class FakeGit {
public:
    explicit FakeGit( const QByteArray& mode, const QByteArray& tokens = "token-1" )
    {
        REQUIRE( directory_.isValid() );
        record_ = directory_.filePath( "git-calls.jsonl" );
        set( "FAKE_GIT_RECORD", record_.toUtf8() );
        set( "FAKE_GIT_MODE", mode );
        set( "FAKE_GIT_TOKENS", tokens );
        set( "GIT_ASKPASS", "C:/askpass.exe" );
        set( "SSH_ASKPASS", "C:/askpass.exe" );
        set( "GCM_INTERACTIVE", {} );
    }
    ~FakeGit()
    {
        for ( auto it = saved_.cbegin(); it != saved_.cend(); ++it ) {
            if ( it.value() ) {
                qputenv( it.key().constData(), *it.value() );
            }
            else {
                qunsetenv( it.key().constData() );
            }
        }
    }
    FakeGit( const FakeGit& ) = delete;
    FakeGit& operator=( const FakeGit& ) = delete;

    static QString path()
    {
        return QStringLiteral( FAKE_GIT_PATH );
    }
    void setMode( const QByteArray& mode )
    {
        qputenv( "FAKE_GIT_MODE", mode );
    }
    QList<QJsonObject> calls() const
    {
        QList<QJsonObject> result;
        QFile file( record_ );
        if ( file.open( QIODevice::ReadOnly ) ) {
            for ( const auto& line : file.readAll().split( '\n' ) ) {
                if ( !line.trimmed().isEmpty() ) {
                    result.append( QJsonDocument::fromJson( line ).object() );
                }
            }
        }
        return result;
    }
    QStringList verbs() const
    {
        QStringList result;
        for ( const auto& call : calls() ) {
            result.append( call.value( "verb" ).toString() );
        }
        return result;
    }

private:
    void set( const char* name, const QByteArray& value )
    {
        if ( !saved_.contains( name ) ) {
            saved_.insert( name, qEnvironmentVariableIsSet( name )
                                     ? std::optional<QByteArray>( qgetenv( name ) )
                                     : std::nullopt );
        }
        if ( value.isNull() ) {
            qunsetenv( name );
        }
        else {
            qputenv( name, value );
        }
    }
    QTemporaryDir directory_;
    QString record_;
    QMap<QByteArray, std::optional<QByteArray>> saved_;
};
} // namespace testgit
