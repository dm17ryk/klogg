// Personal access tokens in the operating system credential store through QtKeychain (Windows
// Credential Manager, macOS Keychain, Secret Service or KWallet on Linux), with this session's
// memory as the fallback when that store cannot be used (docs/plugin-repositories.md 3.5).
#include "repositorycredentials.h"

#include <qtkeychain/keychain.h>

namespace cilogg::plugins {
namespace {
const QString TokenKey = QStringLiteral( "personal-access-token" );

bool storeUnusable( QKeychain::Error error )
{
    return error == QKeychain::NoBackendAvailable || error == QKeychain::NotImplemented;
}

class KeychainPatStore : public PatStore {
public:
    void read( const QString& repository, ReadDone done ) override
    {
        auto state = state_;
        if ( state->unusable ) {
            state->session.read( repository, std::move( done ) );
            return;
        }
        auto* job = new QKeychain::ReadPasswordJob( storeName( repository ) );
        job->setKey( TokenKey );
        QObject::connect( job, &QKeychain::Job::finished, [ state, repository, done ]( QKeychain::Job* finished ) {
            const auto error = finished->error();
            if ( error == QKeychain::NoError ) {
                auto token = static_cast<QKeychain::ReadPasswordJob*>( finished )->binaryData();
                done( token, {} );
                wipeSecret( token );
                return;
            }
            if ( storeUnusable( error ) ) {
                state->unusable = true;
            }
            if ( error == QKeychain::EntryNotFound || storeUnusable( error ) ) {
                // A token kept for the session only is still offered.
                state->session.read( repository, done );
                return;
            }
            done( std::nullopt,
                  { QStringLiteral( "The credential store did not return the stored token." ),
                    false } );
        } );
        job->start();
    }

    void write( const QString& repository, const QByteArray& token, Done done ) override
    {
        auto state = state_;
        if ( state->unusable ) {
            state->session.write( repository, token, std::move( done ) );
            return;
        }
        auto* job = new QKeychain::WritePasswordJob( storeName( repository ) );
        job->setKey( TokenKey );
        job->setBinaryData( token );
        QObject::connect( job, &QKeychain::Job::finished,
                          [ state, repository, token, done ]( QKeychain::Job* finished ) {
                              auto copy = token;
                              const auto error = finished->error();
                              if ( error == QKeychain::NoError ) {
                                  state->session.remove( repository, []( Status ) {} );
                                  done( {} );
                              }
                              else if ( storeUnusable( error ) ) {
                                  state->unusable = true;
                                  state->session.write( repository, copy, done );
                              }
                              else {
                                  done( { QStringLiteral( "The credential store did not accept "
                                                          "the token." ),
                                          false } );
                              }
                              wipeSecret( copy );
                          } );
        job->start();
    }

    void remove( const QString& repository, Done done ) override
    {
        auto state = state_;
        state->session.remove( repository, []( Status ) {} );
        if ( state->unusable ) {
            done( { {}, true } );
            return;
        }
        auto* job = new QKeychain::DeletePasswordJob( storeName( repository ) );
        job->setKey( TokenKey );
        QObject::connect( job, &QKeychain::Job::finished, [ state, done ]( QKeychain::Job* finished ) {
            const auto error = finished->error();
            if ( error == QKeychain::NoError || error == QKeychain::EntryNotFound ) {
                done( {} );
            }
            else if ( storeUnusable( error ) ) {
                state->unusable = true;
                done( { {}, true } );
            }
            else {
                done( { QStringLiteral( "The credential store did not delete the token." ),
                        false } );
            }
        } );
        job->start();
    }

private:
    // Shared with running jobs, which can finish after the store is gone.
    struct State {
        MemoryPatStore session;
        bool unusable = false;
    };
    std::shared_ptr<State> state_ = std::make_shared<State>();
};
} // namespace

std::unique_ptr<PatStore> createSystemPatStore()
{
    return std::make_unique<KeychainPatStore>();
}
} // namespace cilogg::plugins
