#pragma once

// A local HTTPS server for plugin repository tests (E15a, docs/plugin-repositories.md section 8):
// it answers catalog and package requests from fixed routes and records each request's method,
// path and headers. TrustingNetwork is the QNetworkAccessManager the PluginManager under test
// uses: it trusts this server's certificate and nothing else extra.
//
// The certificate and key below are a throwaway, test-only pair for 127.0.0.1 and localhost.
// They protect nothing and are not used anywhere outside these tests.

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslKey>
#include <QSslSocket>
#include <QTimer>
#include <QtGlobal>
#include <memory>

#if QT_VERSION >= QT_VERSION_CHECK( 6, 4, 0 ) && QT_CONFIG( ssl )
#include <QSslServer>
#define CILOGG_TEST_HTTPS_SERVER 1
#endif

namespace testhttps {
inline const char* const CertificatePem = R"(-----BEGIN CERTIFICATE-----
MIIDfDCCAmSgAwIBAgIUUI5kM1It2920KUcYgzk0v6IPx2kwDQYJKoZIhvcNAQEL
BQAwKzESMBAGA1UEAwwJMTI3LjAuMC4xMRUwEwYDVQQKDAxDSUxvZ2cgdGVzdHMw
IBcNMjYxMTE3MTQzNDU2WhgPMjEyNjEwMjQxNDM0NTZaMCsxEjAQBgNVBAMMCTEy
Ny4wLjAuMTEVMBMGA1UECgwMQ0lMb2dnIHRlc3RzMIIBIjANBgkqhkiG9w0BAQEF
AAOCAQ8AMIIBCgKCAQEAkkY/nxfyRqWHopfP3X0DwwkLF5y1ECxfAUi9kf+2rAP0
wHfiBuhjlLATCGcX7YhMX6QoD2rHQhWaAtNFmazci9PngHE+JB1sHcUFGyhY41RJ
d0vrtgfNa0zPxpMGurmQ2UfbgpLNAmh6SwoyX+OOCkdn2gomxdu3ZRvEFn0r4Ldz
8Fkv8o/jvfFjh31nNSQ8hzi2tfNa2+PF1N4VQVJN0gyDX2a3wAZxvF1AMoxx2Q94
27f7Au0UheVKNFvpXEsmyvBd+HEIHpQ5QOyxzB6h1u8RhrO51RvnQVU8QrDi5idh
HAXjb8suAVsdgsAT5pg7t0DpScaOP/vpv5GU9KsJBwIDAQABo4GVMIGSMB0GA1Ud
DgQWBBTSoFtfVwDvGS1M6N594LETB98E6zAfBgNVHSMEGDAWgBTSoFtfVwDvGS1M
6N594LETB98E6zAaBgNVHREEEzARhwR/AAABgglsb2NhbGhvc3QwDwYDVR0TAQH/
BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAqQwEwYDVR0lBAwwCgYIKwYBBQUHAwEwDQYJ
KoZIhvcNAQELBQADggEBAG+7q2dKp1/sF8h9wtG9NNAOzSaGiRHP2LAPNj3rbX5S
GnoRW3VvHYNC1KsQKqlwNQPLjKIzlXOkuBfL+L6/ryzovMNfXs94YG30AadGAPIu
oRYt++v/BBUookvb8/wk/epnYwj4+An8g6ZQH7ayFa4r6c+2GvwVqefqp7W4/Sa+
rwNs9PuW0JJ1f9OzGa0D/XTKSw+064DrPzDSQtZoDAw+wM29xQm1z++XdS5YTJ9V
If7rVrLjGKmwmBr8tsOcypIrjpLPRXh/DBjHQgMr97exS+2qE32umJKIA+DMBPU9
yJIxbTaKQFqKrbNUmr1yxl3j5XRtuRzjb41dI+N09RA=
-----END CERTIFICATE-----
)";

inline const char* const TestOnlyKeyPem = R"(-----BEGIN RSA PRIVATE KEY-----
MIIEpQIBAAKCAQEAkkY/nxfyRqWHopfP3X0DwwkLF5y1ECxfAUi9kf+2rAP0wHfi
BuhjlLATCGcX7YhMX6QoD2rHQhWaAtNFmazci9PngHE+JB1sHcUFGyhY41RJd0vr
tgfNa0zPxpMGurmQ2UfbgpLNAmh6SwoyX+OOCkdn2gomxdu3ZRvEFn0r4Ldz8Fkv
8o/jvfFjh31nNSQ8hzi2tfNa2+PF1N4VQVJN0gyDX2a3wAZxvF1AMoxx2Q9427f7
Au0UheVKNFvpXEsmyvBd+HEIHpQ5QOyxzB6h1u8RhrO51RvnQVU8QrDi5idhHAXj
b8suAVsdgsAT5pg7t0DpScaOP/vpv5GU9KsJBwIDAQABAoIBAA0eQrdsaNyAHOVu
kwRMoWk85uNDuRNNv2l4khPFaKcNpcYbGki/T6r2jYZr5H/3gM31BWi4jU02bb0C
IJRTJeZeZz0kKswHXXpgAIC6ghpFJV+PDgEhuwD7/whSiEB3wu9Rm/SeYMduAkW3
kOP7nvH6DGzIC0kkVtK5BkF0EVrSRlPorqD5Wk5NnTe5djuP6c0vVdDHVX7Catux
/32pGg9P1FiNpvH/aeAjYTfgZ0rRUPfao6To+1QOdBTJrAClg8Bx98dlflkpx8nz
7DG4pq7BoCXVLlJzUy6zFXNKFeKWzZlv078WkgDICQycfd0LeDZ4uPkj0UJXGLBb
cxu4tkkCgYEAyjB+kGO1CvYu7DqZRRNQP5pSuuJmeGjfMf6svyNOomj8676ItMYL
9IQ7ZoMzxXTdhZ9PUr1BD5HoRJwpC9eQGbbzmMdF5rgvBDa7hdeDjLUef4QPLIjf
tPh9o+Ikq41vzfP2teV6kROkP0nJ2qMo8wpiW3w8gLnySpwQqE9IVekCgYEAuTQr
cI3rUg4VjE0lCJXl9fPAomxAPGj11CRhwzI4YmoW2/kGR3YvMoTB/W7mazjaLCf5
9cGKH1in15xY5hXK/zqiOsFkX8PngcDg041ts5IhzmyZnNdu+hMVIhtFO/S0V+6G
bRH/5l7OPkHHtQEVonJVPwm4jZ1eOk8IH57s4W8CgYEAh34D8qK+mf/r5eZG++E7
b8PtGMvLO3mbyJcQEG6KrY+JnjlqxynIrS1H2SSnNvVf1cGymJG23cY7Tm8Pmp30
NSdDCujPUROwfjRnagPIZiyhqXZH7OI0d8Pl/kL3q/b2K6AHflGv1zTAZqD+IgQg
1u3oh8IzkVgKpDD9gKzQnrECgYEAoUyUgMS5rI9jqPwPQvj6fUk7/rn1ACgR4h+F
nbGBdgUbv52VIGwIEyGYqpcJy1yKd2ahxq43E7HtDFW4/zr/v2ZboHyKZPWEDXfk
15sXbgvZsaTX7MeceeRgxYGmvN8UqnitTxUcIJF1Yrd7rWMyanbVU7+tonjqdjIa
yx9pA7UCgYEAkUXa/qqacTX8Xt8ccjEUVaOCmU9oMpteZtSXp9ztHej6inltoh1d
fbQOYb0BYINqDv1KWEUoXZysji+bwbA29gtwk1SIbnG2ajOz6gzuKmBC3W3lUvXh
+bXbJwkji8vPERiWEVB8EdMLopH0zL5/z4RZ6qNt2S0RY7wtTm2sNpk=
-----END RSA PRIVATE KEY-----
)";

inline QSslCertificate certificate()
{
    return QSslCertificate( QByteArray( CertificatePem ), QSsl::Pem );
}

struct Route {
    int status = 200;
    QByteArray body;
    QByteArray contentType = "application/json";
    int delayMs = 0;
    // Extra response headers, e.g. Location. The initializer lets routes leave it out without
    // -Wmissing-field-initializers (GCC and Clang -Wextra).
    QList<QPair<QByteArray, QByteArray>> headers = {};
};

struct RecordedRequest {
    QByteArray method;
    QByteArray target;
    QMap<QByteArray, QByteArray> headers; // names in lower case
};

// Trusts the test certificate for the requests of the manager under test.
class TrustingNetwork final : public QNetworkAccessManager {
public:
    int sslErrorsIgnored = 0;
    // Requests to these hosts go to the given port on 127.0.0.1, so a test can answer for
    // dev.azure.com while the manager sees the real address.
    QMap<QString, quint16> hostToLocalPort;

protected:
    QNetworkReply* createRequest( Operation operation, const QNetworkRequest& original,
                                  QIODevice* data ) override
    {
        QNetworkRequest request( original );
        const auto mapped = hostToLocalPort.constFind( request.url().host().toLower() );
        if ( mapped != hostToLocalPort.constEnd() && request.url().port( 443 ) == 443 ) {
            auto url = request.url();
            url.setHost( QStringLiteral( "127.0.0.1" ) );
            url.setPort( *mapped );
            request.setUrl( url );
        }
#if QT_CONFIG( ssl )
        auto configuration = request.sslConfiguration();
        configuration.addCaCertificate( certificate() );
        request.setSslConfiguration( configuration );
#endif
        auto* reply = QNetworkAccessManager::createRequest( operation, request, data );
#if QT_CONFIG( ssl )
        // Some TLS backends do not take a custom root for a self-signed server certificate:
        // errors about exactly this certificate are expected, any other error stays.
        QObject::connect( reply, &QNetworkReply::sslErrors, reply,
                          [ this, reply ]( const QList<QSslError>& errors ) {
                              for ( const auto& error : errors ) {
                                  if ( !error.certificate().isNull()
                                       && error.certificate() != certificate() ) {
                                      return;
                                  }
                              }
                              ++sslErrorsIgnored;
                              reply->ignoreSslErrors( errors );
                          } );
#endif
        return reply;
    }
};

#ifdef CILOGG_TEST_HTTPS_SERVER
class CatalogServer {
public:
    QList<RecordedRequest> requests;

    CatalogServer()
    {
        auto configuration = QSslConfiguration::defaultConfiguration();
        configuration.setLocalCertificate( certificate() );
        configuration.setPrivateKey( QSslKey( QByteArray( TestOnlyKeyPem ), QSsl::Rsa ) );
        configuration.setPeerVerifyMode( QSslSocket::VerifyNone );
        server_.setSslConfiguration( configuration );
        QObject::connect( &server_, &QTcpServer::pendingConnectionAvailable, &server_,
                          [ this ] { accept(); } );
        listening_ = QSslSocket::supportsSsl() && server_.listen( QHostAddress::LocalHost, 0 );
    }

    bool listening() const
    {
        return listening_;
    }

    QString url( const QString& path ) const
    {
        return QStringLiteral( "https://127.0.0.1:%1%2" ).arg( server_.serverPort() ).arg( path );
    }

    quint16 port() const
    {
        return server_.serverPort();
    }

    // `path` is matched with its query string, then without it.
    void route( const QString& path, Route answer )
    {
        routes_[ path.toUtf8() ] = std::move( answer );
    }

    QList<RecordedRequest> requestsFor( const QByteArray& target ) const
    {
        QList<RecordedRequest> found;
        for ( const auto& request : requests ) {
            if ( request.target == target ) {
                found.append( request );
            }
        }
        return found;
    }

private:
    void accept()
    {
        while ( auto* socket = qobject_cast<QSslSocket*>( server_.nextPendingConnection() ) ) {
            auto buffer = std::make_shared<QByteArray>();
            QObject::connect( socket, &QSslSocket::readyRead, socket, [ this, socket, buffer ] {
                buffer->append( socket->readAll() );
                const auto end = buffer->indexOf( "\r\n\r\n" );
                if ( end >= 0 ) {
                    respond( socket, buffer->left( end ) );
                    buffer->clear();
                }
            } );
            QObject::connect( socket, &QSslSocket::disconnected, socket, &QObject::deleteLater );
            if ( socket->bytesAvailable() > 0 ) {
                Q_EMIT socket->readyRead();
            }
        }
    }

    void respond( QSslSocket* socket, const QByteArray& head )
    {
        RecordedRequest request;
        const auto lines = head.split( '\n' );
        const auto first = lines.value( 0 ).trimmed().split( ' ' );
        request.method = first.value( 0 );
        request.target = first.value( 1 );
        for ( int index = 1; index < lines.size(); ++index ) {
            const auto line = lines.at( index ).trimmed();
            const auto colon = line.indexOf( ':' );
            if ( colon > 0 ) {
                request.headers.insert( line.left( colon ).trimmed().toLower(),
                                        line.mid( colon + 1 ).trimmed() );
            }
        }
        requests.append( request );
        auto path = request.target;
        if ( !routes_.contains( path ) && path.contains( '?' ) ) {
            path = path.left( path.indexOf( '?' ) );
        }
        const auto answer = routes_.value( path, Route{ 404, "not found", "text/plain", 0 } );
        const auto send = [ socket, answer ] {
            const QByteArray reason = answer.status == 200   ? "OK"
                                      : answer.status == 203 ? "Non-Authoritative Information"
                                      : answer.status == 302 ? "Found"
                                      : answer.status == 401 ? "Unauthorized"
                                      : answer.status == 404 ? "Not Found"
                                                             : "Status";
            QByteArray extra;
            for ( const auto& [ name, value ] : answer.headers ) {
                extra += "\r\n" + name + ": " + value;
            }
            QByteArray response
                = "HTTP/1.1 " + QByteArray::number( answer.status ) + ' ' + reason
                  + "\r\nContent-Type: " + answer.contentType
                  + "\r\nContent-Length: " + QByteArray::number( answer.body.size() ) + extra
                  + "\r\nConnection: close\r\n\r\n" + answer.body;
            socket->write( response );
            socket->flush();
            socket->disconnectFromHost();
        };
        if ( answer.delayMs > 0 ) {
            QTimer::singleShot( answer.delayMs, socket, send );
        }
        else {
            send();
        }
    }

    QSslServer server_;
    bool listening_ = false;
    QMap<QByteArray, Route> routes_;
};
#endif
} // namespace testhttps
