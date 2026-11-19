#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <optional>

#include "previewconfig.h"
#include "previewrepository.h"

struct PreviewImportResult {
    bool ok = false;
    QStringList errors;
    QStringList warnings;
};

// The previews and blocks of one enabled configuration pack, as the pack ships them
// (docs/plugin-repositories.md 4.2).
struct PreviewPackSource {
    QString packId;
    QString packName;
    QVector<PreviewDefinition> previews;
    QMap<QString, PreviewFieldSpec> blocks;
};

// Where a pack preview comes from.
struct PreviewPackOrigin {
    QString packId;
    QString packName;
};

class PreviewManager : public QObject {
    Q_OBJECT

public:
    static PreviewManager& instance();

    void loadFromRepository();
    PreviewImportResult importFromFile( const QString& path );
    bool removeByName( const QString& name );
    bool movePreview( int fromRow, int toRow );
    bool clearAll();

    // The user's own previews and blocks: saved in previews.json, exported and captured in
    // projects. Pack items are never part of them.
    const QVector<PreviewDefinition>& all() const;
    QVector<PreviewDefinition> enabled() const;
    const PreviewDefinition* findByName( const QString& name ) const;
    const QMap<QString, PreviewFieldSpec>& blocks() const;
    const PreviewFieldSpec* findBlock( const QString& name ) const;
    void setEnabled( const QString& name, bool enabled );
    QString findFirstMatchingEnabledPreview( const QString& rawLine ) const;

    // Configuration packs. Pack blocks are renamed `<pack-id>/<block>` and the pack's
    // references rewritten, so they never collide with the user's or another pack's blocks.
    void setPackPreviews( const QVector<PreviewPackSource>& packs );
    // Pack previews of the enabled packs, in pack ID order, with the user's on/off choice.
    const QVector<PreviewDefinition>& packPreviews() const;
    // The pack a pack preview comes from, or nothing for the user's own.
    std::optional<PreviewPackOrigin> packOrigin( const QString& packPreviewName ) const;
    // A pack preview the user replaced with an item of the same name.
    bool replacedByUser( const QString& packPreviewName ) const;
    // What evaluation uses: the user's previews, then the pack previews they did not replace.
    QVector<PreviewDefinition> effective() const;
    QVector<PreviewDefinition> effectiveEnabled() const;
    QMap<QString, PreviewFieldSpec> effectiveBlocks() const;
    // An independent copy of a pack preview in the user's previews.json, with every block it
    // needs; copied blocks whose names the user already uses are renamed. Returns the error.
    QString copyPackPreview( const QString& name );
    // The blocks a set of fields can reach through block references, among `blocks`.
    static QStringList blockClosure( const QVector<PreviewFieldSpec>& fields,
                                     const QMap<QString, PreviewFieldSpec>& blocks );
    // Renames the pack blocks `<pack-id>/<block>` and rewrites the pack's references.
    static PreviewPackSource namespaced( const PreviewPackSource& pack );

Q_SIGNALS:
    void previewsChanged();
    void previewEnabledChanged( const QString& name, bool enabled );

private:
    PreviewManager() = default;
    void applyPackEnabledState();

    PreviewRepository repository_;
    QVector<PreviewDefinition> previews_;
    QMap<QString, PreviewFieldSpec> blocks_;
    // The packs as shipped (for Copy to My Previews), and their namespaced previews and blocks.
    QHash<QString, PreviewPackSource> packSources_;
    QVector<PreviewDefinition> packPreviews_;
    QMap<QString, PreviewFieldSpec> packBlocks_;
    QHash<QString, PreviewPackOrigin> packOrigins_;
};
