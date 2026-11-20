#include "previewmanager.h"

#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <functional>

#include "log.h"
#include "persistentinfo.h"
#include "previewdecodeutils.h"
#include "previewservice.h"

namespace {
// The user's on/off choice for pack previews, per `<pack-id>/<name>`: user state, not pack
// content, so it survives pack updates (docs/plugin-repositories.md 4.5).
constexpr auto PackPreviewStateKey = "previewPacks/enabled";

QString packItemKey( const QString& packId, const QString& name )
{
    return packId + '/' + name;
}

void rewriteReferences( QVector<PreviewFieldSpec>& fields,
                        const std::function<QString( const QString& )>& rename )
{
    for ( auto& field : fields ) {
        if ( !field.blockTemplate.trimmed().isEmpty() ) {
            field.blockTemplate = rename( field.blockTemplate.trimmed() );
        }
        rewriteReferences( field.fields, rename );
        rewriteReferences( field.bitfieldMap, rename );
    }
}

PreviewFieldSpec rewriteBlock( const PreviewFieldSpec& block,
                               const std::function<QString( const QString& )>& rename )
{
    QVector<PreviewFieldSpec> one{ block };
    rewriteReferences( one, rename );
    return one.first();
}

void collectReferences( const QVector<PreviewFieldSpec>& fields, QStringList* references )
{
    for ( const auto& field : fields ) {
        if ( !field.blockTemplate.trimmed().isEmpty() ) {
            references->append( field.blockTemplate.trimmed() );
        }
        collectReferences( field.fields, references );
        collectReferences( field.bitfieldMap, references );
    }
}
} // namespace

PreviewManager& PreviewManager::instance()
{
    static PreviewManager manager;
    return manager;
}

void PreviewManager::loadFromRepository()
{
    auto result = repository_.load();
    if ( !result.errors.isEmpty() ) {
        for ( const auto& error : result.errors ) {
            LOG_ERROR << "Preview config error: " << error.toStdString();
        }
    }
    if ( !result.warnings.isEmpty() ) {
        for ( const auto& warning : result.warnings ) {
            LOG_WARNING << "Preview config warning: " << warning.toStdString();
        }
    }
    previews_ = std::move( result.previews );
    blocks_ = std::move( result.blocks );
    for ( auto& preview : previews_ ) {
        preview.hasEnabled = true;
    }
    Q_EMIT previewsChanged();
}

PreviewImportResult PreviewManager::importFromFile( const QString& path )
{
    PreviewImportResult result;
    PreviewConfigParser parser;
    auto parsed = parser.parseFile( path );
    result.errors = parsed.errors;
    result.warnings = parsed.warnings;
    if ( !result.errors.isEmpty() ) {
        result.ok = false;
        return result;
    }

    for ( auto& incoming : parsed.previews ) {
        auto existing = std::find_if(
            previews_.begin(), previews_.end(),
            [ &incoming ]( const auto& preview ) { return preview.name == incoming.name; } );
        if ( existing != previews_.end() ) {
            const bool keepEnabled = !incoming.hasEnabled;
            if ( keepEnabled ) {
                incoming.enabled = existing->enabled;
            }
            incoming.hasEnabled = true;
            *existing = std::move( incoming );
        }
        else {
            if ( !incoming.hasEnabled ) {
                incoming.enabled = true;
            }
            incoming.hasEnabled = true;
            previews_.push_back( std::move( incoming ) );
        }
    }

    for ( auto it = parsed.blocks.begin(); it != parsed.blocks.end(); ++it ) {
        blocks_.insert( it.key(), it.value() );
    }

    result.ok = repository_.save( previews_, blocks_ );
    if ( !result.ok ) {
        result.errors.push_back( "Failed to save previews configuration." );
        return result;
    }

    Q_EMIT previewsChanged();
    return result;
}

bool PreviewManager::removeByName( const QString& name )
{
    const auto it
        = std::find_if( previews_.begin(), previews_.end(),
                        [ &name ]( const auto& preview ) { return preview.name == name; } );
    if ( it == previews_.end() ) {
        return false;
    }

    const auto index = std::distance( previews_.begin(), it );
    const auto removed = *it;
    previews_.erase( it );
    if ( !repository_.save( previews_, blocks_ ) ) {
        previews_.insert( previews_.begin() + index, removed );
        return false;
    }

    Q_EMIT previewsChanged();
    return true;
}

bool PreviewManager::movePreview( int fromRow, int toRow )
{
    if ( fromRow == toRow ) {
        return true;
    }
    if ( fromRow < 0 || fromRow >= previews_.size() || toRow < 0 || toRow >= previews_.size() ) {
        return false;
    }

    const auto previous = previews_;
    previews_.move( fromRow, toRow );
    if ( !repository_.save( previews_, blocks_ ) ) {
        previews_ = previous;
        return false;
    }

    Q_EMIT previewsChanged();
    return true;
}

bool PreviewManager::clearAll()
{
    if ( previews_.isEmpty() ) {
        return true;
    }

    const auto previous = previews_;
    const auto previousBlocks = blocks_;
    previews_.clear();
    blocks_.clear();
    if ( !repository_.save( previews_, blocks_ ) ) {
        previews_ = previous;
        blocks_ = previousBlocks;
        return false;
    }

    Q_EMIT previewsChanged();
    return true;
}

const QVector<PreviewDefinition>& PreviewManager::all() const
{
    return previews_;
}

QVector<PreviewDefinition> PreviewManager::enabled() const
{
    QVector<PreviewDefinition> enabledPreviews;
    for ( const auto& preview : previews_ ) {
        if ( preview.enabled ) {
            enabledPreviews.push_back( preview );
        }
    }
    return enabledPreviews;
}

const PreviewDefinition* PreviewManager::findByName( const QString& name ) const
{
    const auto it
        = std::find_if( previews_.begin(), previews_.end(),
                        [ &name ]( const auto& preview ) { return preview.name == name; } );
    if ( it == previews_.end() ) {
        return nullptr;
    }
    return &*it;
}

const QMap<QString, PreviewFieldSpec>& PreviewManager::blocks() const
{
    return blocks_;
}

const PreviewFieldSpec* PreviewManager::findBlock( const QString& name ) const
{
    const auto it = blocks_.find( name );
    if ( it == blocks_.end() ) {
        return nullptr;
    }
    return &it.value();
}

void PreviewManager::setEnabled( const QString& previewName, bool enabled )
{
    // A copy: callers may pass a name that lives in a list a signal handler replaces.
    const QString name = previewName;
    auto it = std::find_if( previews_.begin(), previews_.end(),
                            [ &name ]( const auto& preview ) { return preview.name == name; } );
    if ( it == previews_.end() ) {
        // A pack preview: the choice is kept in the user's settings, never in the pack.
        const auto pack
            = std::find_if( packPreviews_.begin(), packPreviews_.end(),
                            [ &name ]( const auto& preview ) { return preview.name == name; } );
        if ( pack == packPreviews_.end() || pack->enabled == enabled ) {
            return;
        }
        auto& settings = PersistentInfo::getSettings( app_settings{} );
        auto state = settings.value( PackPreviewStateKey ).toMap();
        state[ packItemKey( packOrigins_.value( name ).packId, name ) ] = enabled;
        settings.setValue( PackPreviewStateKey, state );
        pack->enabled = enabled;
        Q_EMIT previewEnabledChanged( name, enabled );
        Q_EMIT previewsChanged();
        return;
    }
    if ( it->enabled == enabled ) {
        return;
    }
    it->enabled = enabled;
    it->hasEnabled = true;
    repository_.save( previews_, blocks_ );
    Q_EMIT previewEnabledChanged( name, enabled );
    Q_EMIT previewsChanged();
}

QString PreviewManager::findFirstMatchingEnabledPreview( const QString& rawLine ) const
{
    for ( const auto& preview : effectiveEnabled() ) {
        if ( preview.compiled.match( rawLine ).hasMatch() ) {
            return preview.name;
        }
    }
    return {};
}

PreviewPackSource PreviewManager::namespaced( const PreviewPackSource& pack )
{
    auto result = pack;
    const auto rename
        = [ &pack ]( const QString& reference ) { return packItemKey( pack.packId, reference ); };
    for ( auto& preview : result.previews ) {
        rewriteReferences( preview.fields, rename );
    }
    result.blocks.clear();
    for ( auto block = pack.blocks.cbegin(); block != pack.blocks.cend(); ++block ) {
        result.blocks.insert( rename( block.key() ), rewriteBlock( block.value(), rename ) );
    }
    return result;
}

QStringList PreviewManager::blockClosure( const QVector<PreviewFieldSpec>& fields,
                                          const QMap<QString, PreviewFieldSpec>& blocks )
{
    QStringList references;
    collectReferences( fields, &references );
    QStringList closure;
    QSet<QString> seen;
    static const QRegularExpression placeholder( "\\$?\\{[^}]*\\}" );
    while ( !references.isEmpty() ) {
        const auto reference = references.takeLast();
        if ( seen.contains( reference ) ) {
            continue;
        }
        seen.insert( reference );
        // A template reaches every block its placeholders can produce, and the generic
        // fallback block of its kind.
        const QRegularExpression pattern( previewBlockReferencePattern( reference ) );
        auto candidates = previewFallbackBlockNames( QString( reference ).replace( placeholder, "x" ) );
        for ( auto block = blocks.cbegin(); block != blocks.cend(); ++block ) {
            if ( pattern.match( block.key() ).hasMatch() ) {
                candidates.append( block.key() );
            }
        }
        for ( const auto& name : candidates ) {
            if ( blocks.contains( name ) && !closure.contains( name ) ) {
                closure.append( name );
                collectReferences( { blocks.value( name ) }, &references );
            }
        }
    }
    return closure;
}

void PreviewManager::setPackPreviews( const QVector<PreviewPackSource>& packs )
{
    packSources_.clear();
    packPreviews_.clear();
    packBlocks_.clear();
    packOrigins_.clear();
    for ( const auto& pack : packs ) {
        packSources_.insert( pack.packId, pack );
        const auto renamed = namespaced( pack );
        for ( auto preview : renamed.previews ) {
            // Two enabled packs never share a preview name (the pack check refuses it); if they
            // did, the first pack in ID order would keep it.
            if ( packOrigins_.contains( preview.name ) ) {
                LOG_WARNING << "Preview from two packs ignored name=" << preview.name.toStdString()
                            << " pack=" << pack.packId.toStdString();
                continue;
            }
            if ( !preview.hasEnabled ) {
                preview.enabled = true;
            }
            preview.hasEnabled = true;
            packOrigins_.insert( preview.name, { pack.packId, pack.packName } );
            packPreviews_.push_back( std::move( preview ) );
        }
        for ( auto block = renamed.blocks.cbegin(); block != renamed.blocks.cend(); ++block ) {
            packBlocks_.insert( block.key(), block.value() );
        }
    }
    applyPackEnabledState();
    LOG_DEBUG << "Pack previews set packs=" << packs.size() << " previews=" << packPreviews_.size()
              << " blocks=" << packBlocks_.size();
    Q_EMIT previewsChanged();
}

void PreviewManager::applyPackEnabledState()
{
    const auto state
        = PersistentInfo::getSettings( app_settings{} ).value( PackPreviewStateKey ).toMap();
    for ( auto& preview : packPreviews_ ) {
        const auto key = packItemKey( packOrigins_.value( preview.name ).packId, preview.name );
        if ( state.contains( key ) ) {
            preview.enabled = state.value( key ).toBool();
        }
    }
}

const QVector<PreviewDefinition>& PreviewManager::packPreviews() const
{
    return packPreviews_;
}

std::optional<PreviewPackOrigin> PreviewManager::packOrigin( const QString& packPreviewName ) const
{
    const auto it = packOrigins_.find( packPreviewName );
    if ( it == packOrigins_.end() ) {
        return std::nullopt;
    }
    return it.value();
}

bool PreviewManager::replacedByUser( const QString& packPreviewName ) const
{
    return packOrigins_.contains( packPreviewName ) && findByName( packPreviewName ) != nullptr;
}

QVector<PreviewDefinition> PreviewManager::effective() const
{
    auto result = previews_;
    for ( const auto& preview : packPreviews_ ) {
        if ( !findByName( preview.name ) ) {
            result.push_back( preview );
        }
    }
    return result;
}

QVector<PreviewDefinition> PreviewManager::effectiveEnabled() const
{
    QVector<PreviewDefinition> result;
    for ( const auto& preview : effective() ) {
        if ( preview.enabled ) {
            result.push_back( preview );
        }
    }
    return result;
}

QMap<QString, PreviewFieldSpec> PreviewManager::effectiveBlocks() const
{
    auto result = blocks_;
    // A pack's blocks always win inside its own `<pack-id>/` namespace: a block of the user's
    // with such a name (the pack check refuses one) never replaces what the pack's previews use.
    for ( auto block = packBlocks_.cbegin(); block != packBlocks_.cend(); ++block ) {
        result.insert( block.key(), block.value() );
    }
    return result;
}

QString PreviewManager::copyPackPreview( const QString& name, const QString& copyName )
{
    // The copy keeps the pack preview's name (and so replaces it), or takes `copyName` (Keep
    // Both in the merge window: "<name> (mine)").
    const auto origin = packOrigin( name );
    if ( !origin ) {
        return tr( "\"%1\" is not a preview of an enabled pack." ).arg( name );
    }
    return copyPreviewFrom( packSources_[ origin->packId ], name, copyName );
}

QString PreviewManager::copyPreviewFrom( const PreviewPackSource& pack, const QString& name,
                                         const QString& copyName )
{
    const auto target = copyName.isEmpty() ? name : copyName;
    if ( findByName( target ) ) {
        return tr( "You already have a preview named \"%1\"." ).arg( target );
    }
    // A copy under another name never takes an enabled pack preview's name: yours win, so it
    // would replace that preview. (A copy under its own name replaces its original on purpose.)
    if ( target != name && packOrigins_.contains( target ) ) {
        return tr( "An enabled pack has a preview named \"%1\", which the copy would replace." )
            .arg( target );
    }
    const auto original
        = std::find_if( pack.previews.cbegin(), pack.previews.cend(),
                        [ &name ]( const auto& preview ) { return preview.name == name; } );
    if ( original == pack.previews.cend() ) {
        return tr( "\"%1\" is not a preview of the pack." ).arg( name );
    }
    const auto closure = blockClosure( original->fields, pack.blocks );
    // Blocks whose names the user already uses are renamed, with their references
    // (design 4.2): "<block> (<pack name>)", then a number. So are names an enabled pack's block
    // has (a pack block literally named "<other-pack-id>/<name>"): that pack's block would replace
    // the copy's in effectiveBlocks().
    const auto collides = [ this, &closure ]( const QString& suffix ) {
        return std::any_of( closure.cbegin(), closure.cend(), [ this, &suffix ]( const auto& block ) {
            return blocks_.contains( block + suffix ) || packBlocks_.contains( block + suffix );
        } );
    };
    QString suffix;
    for ( int number = 1; collides( suffix ); ++number ) {
        suffix = number == 1 ? QStringLiteral( " (%1)" ).arg( pack.packName )
                             : QStringLiteral( " (%1 %2)" ).arg( pack.packName ).arg( number );
    }
    // Only references that reach a copied block follow its new name; any other reference (to a
    // block the pack does not have) is left as it is.
    const auto rename = [ &suffix, &closure ]( const QString& reference ) {
        const QRegularExpression pattern( previewBlockReferencePattern( reference ) );
        const auto reaches = std::any_of( closure.cbegin(), closure.cend(), [ &pattern ]( const auto& block ) {
            return pattern.match( block ).hasMatch();
        } );
        return reaches ? reference + suffix : reference;
    };
    auto copy = *original;
    copy.name = target;
    const auto current = std::find_if(
        packPreviews_.cbegin(), packPreviews_.cend(),
        [ &name ]( const auto& preview ) { return preview.name == name; } );
    copy.enabled = current != packPreviews_.cend() ? current->enabled : true;
    copy.hasEnabled = true;
    if ( !suffix.isEmpty() ) {
        rewriteReferences( copy.fields, rename );
    }
    const auto previousPreviews = previews_;
    const auto previousBlocks = blocks_;
    previews_.push_back( copy );
    for ( const auto& block : closure ) {
        blocks_.insert( block + suffix, suffix.isEmpty()
                                            ? pack.blocks.value( block )
                                            : rewriteBlock( pack.blocks.value( block ), rename ) );
    }
    // The preview service gives up above its limits, and then drops the packs' previews: a copy
    // that would take your previews and the enabled packs past them is refused before anything
    // is saved (Keep Both copies after an update too).
    if ( PreviewService::definitionsFor( effective(), effectiveBlocks() ).isEmpty() ) {
        previews_ = previousPreviews;
        blocks_ = previousBlocks;
        return tr( "Copying \"%1\" would take your previews and the enabled packs past the preview "
                   "limits (128 previews, 4,096 blocks, 1 MiB of definitions)." )
            .arg( target );
    }
    if ( !repository_.save( previews_, blocks_ ) ) {
        previews_ = previousPreviews;
        blocks_ = previousBlocks;
        return tr( "Failed to save previews configuration." );
    }
    LOG_INFO << "Pack preview copied name=" << name.toStdString()
             << " pack=" << pack.packId.toStdString() << " blocks=" << closure.size()
             << " renamed=" << !suffix.isEmpty();
    Q_EMIT previewsChanged();
    return {};
}
