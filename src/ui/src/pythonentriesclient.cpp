// SPDX-License-Identifier: GPL-3.0-or-later
#include "pythonentriesclient.h"
#include "previewdecodersettings.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace {
const QString BackendName = QStringLiteral( "python_entries" );
constexpr int DetailLimit = 128;
constexpr double LineLimit = 100000000;

bool boundedString( const QJsonValue& value, int maximum )
{
    return value.isString() && value.toString().size() <= maximum;
}
// One module of the worker's answer: a decoder, or why it is not one.
bool validModule( const QJsonValue& value )
{
    // A pack ID and a decoder ID (configpack.cpp pythonDecoderTarget).
    static const QRegularExpression decoder( QRegularExpression::anchoredPattern(
        "[a-z0-9][a-z0-9._-]{0,127}/[a-z0-9][a-z0-9._-]{0,63}" ) );
    static const QStringList reasons{
        "",          "no_decode",   "async_decode", "not_callable", "syntax_error",
        "too_large", "too_complex", "unreadable",   "timeout"
    };
    if ( !value.isObject() )
        return false;
    const auto module = value.toObject();
    const auto line = module.value( "line" ).toDouble( -1 );
    const auto reason = module.value( "reason" ).toString();
    return module.size() == 5 && module.value( "decoder" ).isString()
           && decoder.match( module.value( "decoder" ).toString() ).hasMatch()
           && module.value( "entry" ).isBool() && module.value( "reason" ).isString()
           && reasons.contains( reason ) && module.value( "entry" ).toBool() == reason.isEmpty()
           && module.value( "line" ).isDouble() && std::isfinite( line )
           && std::floor( line ) == line && line >= 0 && line <= LineLimit
           && boundedString( module.value( "detail" ), DetailLimit );
}
// The worker's answer (scripts/plugins/message_decoder_python_entries.py), strictly.
bool validResponse( const QJsonObject& value, const QString& id )
{
    if ( value.size() != 6 || value.value( "version" ) != 1 || value.value( "id" ) != id
         || value.value( "backend" ) != BackendName
         || !boundedString( value.value( "reason" ), 128 ) || !value.value( "modules" ).isArray() )
        return false;
    const auto state = value.value( "state" ).toString();
    const auto reason = value.value( "reason" ).toString();
    const auto modules = value.value( "modules" ).toArray();
    if ( state == QLatin1String( "failed" ) )
        return !reason.isEmpty() && modules.isEmpty();
    return state == QLatin1String( "completed" ) && reason.isEmpty()
           && modules.size() <= PreviewDecoderSettings::ProfilePythonLimit
           && std::all_of( modules.begin(), modules.end(), validModule );
}
} // namespace

PythonEntriesClient& PythonEntriesClient::instance()
{
    static auto* client = new PythonEntriesClient( QCoreApplication::instance() );
    return *client;
}
PythonEntriesClient::PythonEntriesClient( QObject* parent )
    : PreviewWorkerClient( parent, { BackendName,
                                     QStringLiteral( "entries" ),
                                     QStringLiteral( "python_modules" ),
                                     QStringLiteral( "message_decoder_python_entries.py" ),
                                     { QStringLiteral( "message_decoder_python_entries.py" ),
                                       QStringLiteral( "message_decoder_python_worker.py" ),
                                       QStringLiteral( "message_decoder_preview_worker.py" ),
                                       QStringLiteral( "message_decoder_wire.py" ),
                                       QStringLiteral( "workbench_process.py" ) },
                                     validResponse,
                                     {} } )
{
}
void PythonEntriesClient::check( QObject* owner, const QVariantList& packs, const QString& id,
                                 Completion done )
{
    // The payload is what to check: the packs' modules, as the decoder worker takes them.
    const auto payload
        = QJsonDocument( QJsonObject{ { "packs", QJsonArray::fromVariantList( packs ) } } )
              .toJson( QJsonDocument::Compact );
    submit( owner, {}, payload, TimeoutMs, id, std::move( done ) );
}
