// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <utility>
#include <vector>

// Capability negotiation, icon references and tooltip answers of the advanced plugin SDK
// (docs/plugin-sdk-advanced.md sections 4, 6 and 9). Pure functions, shared by the manifest, the
// plugin manager and the UI.
namespace cilogg::plugins {
constexpr int MaxHandledMethods = 64;
constexpr int MaxMethodNameChars = 64;
constexpr int MaxTooltipKeyChars = 128;
constexpr int MaxTooltipTextChars = 16 * 1024; // all text of one tooltip answer
constexpr int MaxTooltipRows = 32;
constexpr int DefaultTooltipTtlMs = 30000;
constexpr int MaxTooltipTtlMs = 10 * 60 * 1000;
// View limits announced in `host_limits`; PluginView checks that it uses the same values.
constexpr int MaxViewElements = 512;
constexpr int MaxViewTableCells = 50000;
constexpr int MaxViewTextChars = 4096;

// The features this host announces in `initialize`, in order, with their versions (section 4.1).
// `views` 2 adds the element properties `accessible_name` and `accessible_description`.
const std::vector<std::pair<QString, int>>& hostFeatures();
QJsonArray hostFeatureNames();
QJsonObject hostFeatureVersions();
// `host_limits` of `initialize`.
QJsonObject hostLimits();

// Host queries (section 5.1).
struct QueryTimeouts {
    int defaultMs = 0;
    int maximumMs = 0;
};
// Zero timeouts for a method that is not a query of this host.
QueryTimeouts queryTimeouts( const QString& method );
bool isQueryMethod( const QString& method );
// The timeout of one query: `requested` within [1, maximum], or the default when it is not
// positive.
int boundedQueryTimeout( const QString& method, int requested );

// Manifest `handles`: a list of host-to-plugin method names. Names this host does not know are
// allowed (a newer plugin); an empty string means valid.
QString validateHandles( const QJsonValue& handles );
QStringList handledMethods( const QJsonValue& handles );
// The initialize result's `features` (lenient: anything invalid is ignored).
QStringList pluginFeatures( const QJsonValue& features );
// The initialize result's `sdk` (`{name, version}`), or an empty object when invalid.
QJsonObject pluginSdk( const QJsonValue& sdk );

// Manifest `requires` (section 4.3): `{"host_features": ["name", "name>=N"]}`. Returns an error
// for a malformed value; `missing` receives the requirements this host does not meet.
QString validateRequires( const QJsonValue& requirement, QStringList* missing );

// Icon references (section 6): a plugin file (`.png`, `.ico`, `.svg`), `builtin:<name>`,
// `status:<tone>`, `image:<id>`, or `{"light": <ref>, "dark": <ref>}`.
QString validateIconReference( const QJsonValue& reference );
const QStringList& builtinIconNames();
const QStringList& toneNames();

// A `tooltip/resolve` answer (section 9.2), normalized: `title`, `text`, `rows` (pairs of
// strings), `tone`, `icon`, `footer` and `ttl_ms`, every text plain and within the limits. A null
// answer and an answer without any text give an empty object ("nothing"). `error` is set for an
// invalid answer.
QJsonObject sanitizeTooltip( const QJsonValue& answer, QString* error );
} // namespace cilogg::plugins
