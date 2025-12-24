#include "previewmanager.h"

#include <algorithm>

#include "log.h"

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
        auto existing = std::find_if( previews_.begin(), previews_.end(),
                                      [ &incoming ]( const auto& preview ) {
                                          return preview.name == incoming.name;
                                      } );
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

    result.ok = repository_.save( previews_ );
    if ( !result.ok ) {
        result.errors.push_back( "Failed to save previews configuration." );
        return result;
    }

    Q_EMIT previewsChanged();
    return result;
}

bool PreviewManager::removeByName( const QString& name )
{
    const auto it = std::find_if( previews_.begin(), previews_.end(),
                                  [ &name ]( const auto& preview ) {
                                      return preview.name == name;
                                  } );
    if ( it == previews_.end() ) {
        return false;
    }

    const auto index = std::distance( previews_.begin(), it );
    const auto removed = *it;
    previews_.erase( it );
    if ( !repository_.save( previews_ ) ) {
        previews_.insert( previews_.begin() + index, removed );
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
    const auto it = std::find_if( previews_.begin(), previews_.end(),
                                  [ &name ]( const auto& preview ) {
                                      return preview.name == name;
                                  } );
    if ( it == previews_.end() ) {
        return nullptr;
    }
    return &*it;
}

void PreviewManager::setEnabled( const QString& name, bool enabled )
{
    auto it = std::find_if( previews_.begin(), previews_.end(),
                            [ &name ]( const auto& preview ) {
                                return preview.name == name;
                            } );
    if ( it == previews_.end() ) {
        return;
    }
    if ( it->enabled == enabled ) {
        return;
    }
    it->enabled = enabled;
    it->hasEnabled = true;
    repository_.save( previews_ );
    Q_EMIT previewEnabledChanged( name, enabled );
    Q_EMIT previewsChanged();
}

QString PreviewManager::findFirstMatchingEnabledPreview( const QString& rawLine ) const
{
    for ( const auto& preview : previews_ ) {
        if ( !preview.enabled ) {
            continue;
        }
        if ( preview.compiled.match( rawLine ).hasMatch() ) {
            return preview.name;
        }
    }
    return {};
}
