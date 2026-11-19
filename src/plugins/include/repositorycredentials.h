#pragma once

#include "azuredevops.h"
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QProcessEnvironment>
#include <QString>
#include <functional>
#include <memory>
#include <optional>

namespace cilogg::plugins {
// Zeroes a buffer this object owns alone, then empties it. A shared buffer is only released:
// writing to it would detach and wipe a copy.
void wipeSecret( QByteArray& bytes );

// A credential for one job (docs/plugin-repositories.md 3.6): the Authorization header and, for
// the Git Credential Manager, the complete `git credential fill` answer that approve and reject
// need. It cannot be copied or printed and wipes its buffers when destroyed.
class Credential {
public:
    enum class Source { GitCredentialManager, PersonalAccessToken };

    Credential( Source source, QByteArray authorization, QByteArray fillOutput = {} );
    ~Credential();
    Credential( const Credential& ) = delete;
    Credential& operator=( const Credential& ) = delete;
    Credential( Credential&& ) = delete;
    Credential& operator=( Credential&& ) = delete;

    Source source() const
    {
        return source_;
    }
    const QByteArray& authorization() const
    {
        return authorization_;
    }
    const QByteArray& fillOutput() const
    {
        return fillOutput_;
    }

private:
    Source source_;
    QByteArray authorization_;
    QByteArray fillOutput_;
};
using CredentialPtr = std::shared_ptr<const Credential>;

// Runs `git credential fill|approve|reject` for an Azure DevOps repository (design 3.5): Git by
// an absolute path, `-c core.askPass=`, arguments as a list, GIT_TERMINAL_PROMPT=0, no askpass
// variables, and GCM_INTERACTIVE=never unless the operation was started in the GUI. Each run
// starts in a new, empty folder of its own under the temporary folder, removed afterwards, so no
// repository config can name a credential helper. Standard error is discarded; only the outcome
// is reported.
class GitCredentialHelper {
public:
    static constexpr int SilentTimeoutMs = 15000;
    static constexpr int InteractiveTimeoutMs = 180000;
    static constexpr int MaxAnswerBytes = 64 * 1024;

    enum class Outcome { Ok, GitNotFound, Failed, TimedOut, NoCredential };
    using FillDone = std::function<void( CredentialPtr credential, Outcome outcome )>;
    using Done = std::function<void( Outcome outcome )>;

    // The configured path when it is an absolute, existing, executable file; otherwise the
    // absolute path QStandardPaths finds for "git"; empty when there is none.
    static QString resolveGit( const QString& configured );
    // The environment of a run in `workingDirectory`: repository discovery stops there.
    static QProcessEnvironment environment( bool interactive, const QString& workingDirectory );
    static QByteArray fillRequest( const AzureDevOpsLocation& location );
    static QString outcomeText( Outcome outcome );

    // `context` owns the process; callbacks run on its thread and never after it is destroyed.
    // `timeoutMs` 0 means the silent or interactive default.
    static void fill( const QString& git, const AzureDevOpsLocation& location, bool interactive,
                      QObject* context, FillDone done, int timeoutMs = 0 );
    static void approve( const QString& git, const Credential& credential, QObject* context,
                         Done done = {} );
    static void reject( const QString& git, const Credential& credential, QObject* context,
                        Done done = {} );
};

// Where personal access tokens are kept (design 3.5): the operating system credential store, or
// this session's memory when there is none. Keyed by PluginManager::tokenKey(), which names the
// CILogg profile, the repository ID and its address.
class PatStore {
public:
    struct Status {
        QString error;
        bool sessionOnly = false; // kept in memory only, gone when CILogg exits
    };
    using ReadDone = std::function<void( std::optional<QByteArray> token, Status status )>;
    using Done = std::function<void( Status status )>;

    virtual ~PatStore() = default;
    virtual void read( const QString& key, ReadDone done ) = 0;
    virtual void write( const QString& key, const QByteArray& token, Done done ) = 0;
    virtual void remove( const QString& key, Done done ) = 0;
    // The name the token is stored under in the operating system store.
    static QString storeName( const QString& key );
};

// Tokens in memory for this session. Also the store used by tests.
class MemoryPatStore : public PatStore {
public:
    ~MemoryPatStore() override;
    void read( const QString& key, ReadDone done ) override;
    void write( const QString& key, const QByteArray& token, Done done ) override;
    void remove( const QString& key, Done done ) override;
    bool contains( const QString& key ) const;

private:
    QHash<QString, QByteArray> tokens_;
};

// The operating system store when CILogg was built with one (QtKeychain), falling back to memory
// when that store is not usable at run time.
std::unique_ptr<PatStore> createSystemPatStore();
} // namespace cilogg::plugins
