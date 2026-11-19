#pragma once

#include <QString>
#include <QStringList>
#include <optional>

namespace cilogg::plugins {
// Semantic Versioning 2.0.0 (https://semver.org/spec/v2.0.0.html). One implementation for
// manifest validation, catalog validation and update selection (docs/plugin-repositories.md 3.4).
struct SemanticVersion {
    // Decimal digits without leading zeros, compared by length and then text, so any size works.
    QString major;
    QString minor;
    QString patch;
    QStringList prerelease;
    QString build; // ignored by the order

    static std::optional<SemanticVersion> parse( const QString& text );
};

// The longest version text accepted.
constexpr int MaxVersionLength = 128;

bool isSemanticVersion( const QString& text );
// <0 when a is lower, 0 when equal in precedence (build metadata ignored), >0 when a is higher;
// nullopt when either text is not a version.
std::optional<int> compareSemanticVersions( const QString& a, const QString& b );
} // namespace cilogg::plugins
