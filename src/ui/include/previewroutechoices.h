// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

// What the TShark / Lua preview decoder settings offer in their route fields, and the checks Apply
// runs on the routes (issue #180). Nothing here starts a process: the dialog reads TShark's
// protocol list itself and hands it in.
namespace previewroutes {

// The dissector names a Lua source registers with `Proto("<name>", ...)` or `Proto.new(...)`, in
// order and without duplicates. Wireshark registers the dissector, and its PDML protocol, under
// the lower-case name, so the names are lower case. Declarations in comments and strings do not
// count, nor names a route could not take (PreviewDecoderSettings::isDissectorName).
QStringList luaProtoNames( const QString& source );
// The same for a Lua file, read up to the settings' 1 MiB Lua limit; empty when it cannot be read.
QStringList luaProtoNamesInFile( const QString& path );
// The protocol filter names in `tshark -G protocols` output (its third column), which are also the
// names TShark's own dissectors are found by, sorted. Names a route could not take are left out.
QStringList tsharkProtocolNames( const QByteArray& output );

struct LuaFile {
    QString path;
    QStringList names;
};

struct Choices {
    // The user's previews, then the enabled packs' previews.
    QStringList previews;
    // The protocols message assembly gives messages.
    QStringList assemblyProtocols;
    // The selected Lua files, then the enabled packs' Lua files, with what each declares.
    QVector<LuaFile> lua;
    // `python:<pack-id>/<decoder-id>` for every Python decoder of the enabled packs: the modules
    // Python's check found a decode() in (E16b, PreviewDecoderPacks::pythonEntries).
    QStringList python;
    // The same for every module the enabled packs list, helpers included: a route to any of them
    // runs (a decode() bound at run time works), so only a route to another is reported.
    QStringList pythonModules;
    // TShark's protocols, while `tsharkLoaded`.
    QStringList tshark;
    bool tsharkLoaded = false;
    // Why TShark's list could not be read ("timed out", ...), or empty.
    QString tsharkFailure;
    // TShark is still listing its protocols: names only its list could settle are not reported.
    bool tsharkPending = false;
    // TShark is disabled, so no TShark route runs: names only its list could settle are not
    // reported either.
    bool tsharkDisabled = false;

    // The names the Lua files declare, in file order, without duplicates.
    QStringList luaNames() const;
    // A TShark dissector: the Lua names, then TShark's protocols.
    QStringList tsharkDissectors() const;
    // A route's target: the Lua names, then the Python decoders, then TShark's protocols.
    QStringList routeTargets() const;
};

// The choices from the user's previews, the enabled packs, message assembly and `userLua` (the
// selected Lua files). TShark's protocols are left empty.
Choices collect( const QStringList& userLua );

struct Warning {
    // What the warning is about: `Preview route "X"`, `Default named dissector`, ...
    QString field;
    // The value as configured (a dissector, or a Python route's `<pack-id>/<decoder-id>`).
    QString name;
    // The declared dissector name to use instead, or empty.
    QString suggestion;
    // The explanation, without the field.
    QString text;
};

// The dissectors in `configuration` (its default and its routes) that no Lua file in `choices`
// declares and TShark does not know, and the Python routes to a decoder no enabled pack has.
// When the configured name looks like a Lua file's name, or differs from a known name only in
// case, the warning suggests the dissector name to use.
QVector<Warning> check( const QVariantMap& configuration, const Choices& choices );

} // namespace previewroutes
