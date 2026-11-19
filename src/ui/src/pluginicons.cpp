// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "pluginicons.h"

#include "log.h"
#include "pluginfeatures.h"
#include "pluginmanager.h"
#include "plugintablemodel.h"
#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QRegularExpression>
#include <QStyle>
#include <optional>

namespace {
// Built-in icons that the platform style provides.
std::optional<QStyle::StandardPixmap> standardPixmap( const QString& name )
{
    static const QHash<QString, QStyle::StandardPixmap> map{
        { "info", QStyle::SP_MessageBoxInformation },
        { "warning", QStyle::SP_MessageBoxWarning },
        { "error", QStyle::SP_MessageBoxCritical },
        { "question", QStyle::SP_MessageBoxQuestion },
        { "ok", QStyle::SP_DialogOkButton },
        { "cancel", QStyle::SP_DialogCancelButton },
        { "delete", QStyle::SP_TrashIcon },
        { "save", QStyle::SP_DialogSaveButton },
        { "open", QStyle::SP_DialogOpenButton },
        { "folder", QStyle::SP_DirIcon },
        { "file", QStyle::SP_FileIcon },
        { "search", QStyle::SP_FileDialogContentsView },
        { "filter", QStyle::SP_FileDialogListView },
        { "refresh", QStyle::SP_BrowserReload },
        { "settings", QStyle::SP_FileDialogDetailedView },
        { "play", QStyle::SP_MediaPlay },
        { "pause", QStyle::SP_MediaPause },
        { "stop", QStyle::SP_MediaStop },
        { "upload", QStyle::SP_ArrowUp },
        { "download", QStyle::SP_ArrowDown },
        { "up", QStyle::SP_ArrowUp },
        { "down", QStyle::SP_ArrowDown },
        { "left", QStyle::SP_ArrowLeft },
        { "right", QStyle::SP_ArrowRight },
        { "link", QStyle::SP_FileLinkIcon },
        { "device", QStyle::SP_DriveHDIcon },
        { "port", QStyle::SP_DriveNetIcon },
        { "chip", QStyle::SP_ComputerIcon },
        { "bookmark", QStyle::SP_DirLinkIcon },
    };
    const auto found = map.constFind( name );
    if ( found == map.cend() ) {
        return std::nullopt;
    }
    return *found;
}

// The other built-ins: a symbol drawn in the palette's text colour, so it follows the theme.
QIcon drawnGlyph( const QString& symbol )
{
    QPixmap pixmap( 32, 32 );
    pixmap.fill( Qt::transparent );
    QPainter painter( &pixmap );
    painter.setRenderHint( QPainter::Antialiasing );
    const auto color = QApplication::palette().color( QPalette::WindowText );
    painter.setPen( QPen( color, 3 ) );
    painter.drawEllipse( QRectF( 2.5, 2.5, 27, 27 ) );
    auto font = painter.font();
    font.setPixelSize( 18 );
    font.setBold( true );
    painter.setFont( font );
    painter.drawText( QRect( 0, 0, 32, 32 ), Qt::AlignCenter, symbol );
    painter.end();
    return QIcon( pixmap );
}

QImage recoloured( const QImage& image, const QColor& color )
{
    auto result = image.convertToFormat( QImage::Format_ARGB32_Premultiplied );
    QPainter painter( &result );
    painter.setCompositionMode( QPainter::CompositionMode_SourceIn );
    painter.fillRect( result.rect(), color );
    painter.end();
    return result;
}

bool validImageId( const QString& id )
{
    static const QRegularExpression pattern(
        QRegularExpression::anchoredPattern( "[A-Za-z0-9._-]{1,64}" ) );
    return pattern.match( id ).hasMatch();
}
} // namespace

PluginIconRegistry& PluginIconRegistry::instance()
{
    static PluginIconRegistry registry;
    return registry;
}

void PluginIconRegistry::attach( cilogg::plugins::PluginManager* manager )
{
    if ( !manager || attached_.contains( manager ) ) {
        return;
    }
    attached_.insert( manager );
    connect( manager, &QObject::destroyed, this,
             [ this, manager ] { attached_.remove( manager ); } );
    connect( manager, &cilogg::plugins::PluginManager::viewRequest, this,
             [ this ]( const QString& pluginId, const QString& method, const QJsonObject& params ) {
                 if ( method == QLatin1String( "ui/icons" ) ) {
                     apply( pluginId, params );
                 }
             } );
    // Registered images live until the plugin stops (design section 6).
    connect( manager, &cilogg::plugins::PluginManager::pluginStopped, this,
             [ this ]( const QString& pluginId ) { clear( pluginId ); } );
}

bool PluginIconRegistry::darkTheme()
{
    return QApplication::palette().color( QPalette::Window ).lightness() < 128;
}

bool PluginIconRegistry::safeSvg( const QByteArray& svg )
{
    const auto text = QString::fromUtf8( svg );
    static const QRegularExpression forbidden(
        "<!DOCTYPE|<!ENTITY|<script|<foreignObject|<\\?xml-stylesheet|@import",
        QRegularExpression::CaseInsensitiveOption );
    if ( forbidden.match( text ).hasMatch() ) {
        return false;
    }
    // A fragment of the document itself, or an embedded raster image. An embedded SVG is refused:
    // it would be decoded without this check.
    const auto allowed = []( QString target ) {
        target = target.trimmed();
        static const QRegularExpression raster( "^data:image/(png|jpeg|gif)[;,]",
                                                QRegularExpression::CaseInsensitiveOption );
        return target.isEmpty() || target.startsWith( '#' ) || raster.match( target ).hasMatch();
    };
    static const QRegularExpression href( "href\\s*=\\s*([\"'])(.*?)\\1",
                                          QRegularExpression::CaseInsensitiveOption
                                              | QRegularExpression::DotMatchesEverythingOption );
    for ( auto it = href.globalMatch( text ); it.hasNext(); ) {
        if ( !allowed( it.next().captured( 2 ) ) ) {
            return false;
        }
    }
    static const QRegularExpression url( "url\\(\\s*([\"']?)([^)\"']*)\\1\\s*\\)",
                                         QRegularExpression::CaseInsensitiveOption );
    for ( auto it = url.globalMatch( text ); it.hasNext(); ) {
        if ( !allowed( it.next().captured( 2 ) ) ) {
            return false;
        }
    }
    return true;
}

QStringList PluginIconRegistry::apply( const QString& pluginId, const QJsonObject& params )
{
    QStringList refused;
    auto& images = images_[ pluginId ];
    bool changedAny = false;
    for ( const auto& id : params.value( "remove" ).toArray() ) {
        const auto name = id.toString();
        if ( images.icons.contains( name ) ) {
            images.iconBytes -= images.icons.value( name ).bytes;
            images.decodedBytes -= images.icons.value( name ).image.sizeInBytes();
            images.icons.remove( name );
            changedAny = true;
        }
        if ( images.pictures.contains( name ) ) {
            images.decodedBytes -= images.pictures.value( name ).image.sizeInBytes();
            images.pictures.remove( name );
            changedAny = true;
        }
    }
    const auto icons = params.value( "icons" ).toObject();
    for ( auto it = icons.begin(); it != icons.end(); ++it ) {
        const auto id = it.key();
        const auto entry = it.value().toObject();
        const bool picture = entry.value( "picture" ).toBool();
        const auto maxBytes = picture ? MaxPictureBytes : MaxIconBytes;
        const auto maxPixels = picture ? MaxPicturePixels : MaxIconPixels;
        const auto refuse = [ & ]( const QString& reason ) {
            refused.append( QStringLiteral( "%1: %2" ).arg( id.left( 64 ), reason ) );
        };
        if ( !validImageId( id ) || !it.value().isObject() ) {
            refuse( QStringLiteral( "invalid id or entry" ) );
            continue;
        }
        QByteArray bytes;
        QByteArray format;
        if ( entry.value( "png" ).isString() ) {
            const auto decoded = QByteArray::fromBase64Encoding(
                entry.value( "png" ).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors );
            if ( !decoded ) {
                refuse( QStringLiteral( "invalid base64" ) );
                continue;
            }
            bytes = *decoded;
            format = "png";
        }
        else if ( entry.value( "svg" ).isString() ) {
            bytes = entry.value( "svg" ).toString().toUtf8();
            format = "svg";
            if ( bytes.size() <= maxBytes && !safeSvg( bytes ) ) {
                refuse( QStringLiteral( "SVG refers to something other than data: URIs" ) );
                continue;
            }
        }
        else {
            refuse( QStringLiteral( "needs png or svg" ) );
            continue;
        }
        if ( bytes.size() > maxBytes ) {
            refuse( QStringLiteral( "larger than %1 bytes" ).arg( maxBytes ) );
            continue;
        }
        QBuffer buffer( &bytes );
        buffer.open( QIODevice::ReadOnly );
        QImageReader reader( &buffer, format );
        const auto size = reader.size();
        if ( size.isValid() && ( size.width() > maxPixels || size.height() > maxPixels ) ) {
            refuse( QStringLiteral( "larger than %1 by %1 pixels" ).arg( maxPixels ) );
            continue;
        }
        if ( format == "svg" && !size.isValid() ) {
            reader.setScaledSize( QSize( 64, 64 ) );
        }
        const auto image = reader.read();
        if ( image.isNull() || image.width() > maxPixels || image.height() > maxPixels ) {
            refuse( QStringLiteral( "cannot be decoded" ) );
            continue;
        }
        Image stored{ image, bytes.size(), entry.value( "mask" ).toBool() && format == "svg" };
        const auto replaced
            = ( picture ? images.pictures : images.icons ).value( id ).image.sizeInBytes();
        if ( images.decodedBytes - replaced + image.sizeInBytes() > MaxDecodedBytes ) {
            refuse( QStringLiteral( "decoded images exceed %1 MiB" )
                        .arg( MaxDecodedBytes / ( 1024 * 1024 ) ) );
            continue;
        }
        if ( picture ) {
            if ( !images.pictures.contains( id ) && images.pictures.size() >= MaxPictures ) {
                refuse( QStringLiteral( "more than %1 pictures" ).arg( MaxPictures ) );
                continue;
            }
            images.decodedBytes += image.sizeInBytes() - replaced;
            images.pictures.insert( id, stored );
        }
        else {
            const auto previous = images.icons.value( id ).bytes;
            if ( !images.icons.contains( id ) && images.icons.size() >= MaxIcons ) {
                refuse( QStringLiteral( "more than %1 icons" ).arg( MaxIcons ) );
                continue;
            }
            if ( images.iconBytes - previous + stored.bytes > MaxIconTotalBytes ) {
                refuse( QStringLiteral( "icons exceed %1 bytes in total" ).arg( MaxIconTotalBytes ) );
                continue;
            }
            images.iconBytes += stored.bytes - previous;
            images.decodedBytes += image.sizeInBytes() - replaced;
            images.icons.insert( id, stored );
        }
        changedAny = true;
    }
    for ( const auto& reason : refused ) {
        LOG_WARNING << "Plugin image refused plugin=" << pluginId.toStdString() << " "
                    << reason.toStdString();
    }
    if ( changedAny ) {
        ++images.generation;
        Q_EMIT changed( pluginId );
    }
    return refused;
}

void PluginIconRegistry::clear( const QString& pluginId )
{
    const auto found = images_.find( pluginId );
    if ( found == images_.end() ) {
        return;
    }
    const bool hadImages = !found->icons.isEmpty() || !found->pictures.isEmpty();
    found->icons.clear();
    found->pictures.clear();
    found->iconBytes = 0;
    found->decodedBytes = 0;
    if ( hadImages ) {
        ++found->generation;
        Q_EMIT changed( pluginId );
    }
}

int PluginIconRegistry::iconCount( const QString& pluginId ) const
{
    return static_cast<int>( images_.value( pluginId ).icons.size() );
}

int PluginIconRegistry::pictureCount( const QString& pluginId ) const
{
    return static_cast<int>( images_.value( pluginId ).pictures.size() );
}

quint64 PluginIconRegistry::generation( const QString& pluginId ) const
{
    return images_.value( pluginId ).generation;
}

QImage PluginIconRegistry::picture( const QString& pluginId, const QString& id ) const
{
    return images_.value( pluginId ).pictures.value( id ).image;
}

void PluginIconRegistry::warnOnce( const QString& pluginId, const QString& reference,
                                   const QString& reason )
{
    const auto key = pluginId + '|' + reference;
    if ( warned_.contains( key ) || warned_.size() >= 1024 ) {
        return;
    }
    warned_.insert( key );
    LOG_WARNING << "Plugin icon not usable, using the generic icon plugin=" << pluginId.toStdString()
                << " icon=" << reference.left( 256 ).toStdString() << " " << reason.toStdString();
}

QIcon PluginIconRegistry::builtin( const QString& name ) const
{
    if ( const auto standard = standardPixmap( name ) ) {
        return QApplication::style()->standardIcon( *standard );
    }
    static const QHash<QString, QString> symbols{
        { "add", "+" },    { "remove", "-" }, { "edit", "E" }, { "copy", "C" },
        { "paste", "P" },  { "record", "R" }, { "lock", "L" }, { "unlock", "U" },
        { "clock", "T" },  { "star", "*" },   { "bell", "!" },
    };
    if ( name == "record" ) {
        return statusDot( "error" );
    }
    return drawnGlyph( symbols.value( name, QStringLiteral( "?" ) ) );
}

QIcon PluginIconRegistry::statusDot( const QString& tone )
{
    auto color = pluginview::toneColor( tone );
    if ( !color.isValid() ) {
        color = QApplication::palette().color( QPalette::WindowText );
    }
    QPixmap pixmap( 32, 32 );
    pixmap.fill( Qt::transparent );
    QPainter painter( &pixmap );
    painter.setRenderHint( QPainter::Antialiasing );
    painter.setPen( Qt::NoPen );
    painter.setBrush( color );
    painter.drawEllipse( QRectF( 6, 6, 20, 20 ) );
    painter.end();
    return QIcon( pixmap );
}

QIcon PluginIconRegistry::resolve( const QString& pluginId, const QString& directory,
                                   const QString& reference )
{
    if ( reference.startsWith( QLatin1String( "builtin:" ) ) ) {
        const auto name = reference.mid( 8 );
        if ( !cilogg::plugins::builtinIconNames().contains( name ) ) {
            warnOnce( pluginId, reference, QStringLiteral( "unknown built-in icon" ) );
            return QApplication::style()->standardIcon( QStyle::SP_FileIcon );
        }
        return builtin( name );
    }
    if ( reference.startsWith( QLatin1String( "status:" ) ) ) {
        return statusDot( reference.mid( 7 ) );
    }
    if ( reference.startsWith( QLatin1String( "image:" ) ) ) {
        const auto icons = images_.value( pluginId ).icons;
        const auto found = icons.constFind( reference.mid( 6 ) );
        if ( found == icons.cend() ) {
            return {};
        }
        const auto image
            = found->mask
                  ? recoloured( found->image, QApplication::palette().color( QPalette::WindowText ) )
                  : found->image;
        return QIcon( QPixmap::fromImage( image ) );
    }
    const auto path = QDir( directory ).filePath( reference );
    if ( directory.isEmpty() || !QFileInfo::exists( path ) ) {
        warnOnce( pluginId, reference, QStringLiteral( "missing file" ) );
        return {};
    }
    // A plugin's SVG file gets the same check as a registered one: no outside references.
    if ( reference.endsWith( QLatin1String( ".svg" ), Qt::CaseInsensitive ) ) {
        QFile file( path );
        if ( file.size() > MaxPictureBytes || !file.open( QIODevice::ReadOnly )
             || !safeSvg( file.readAll() ) ) {
            warnOnce( pluginId, reference,
                      QStringLiteral( "SVG larger than 1 MiB or with outside references" ) );
            return {};
        }
    }
    QIcon loaded( path );
    if ( loaded.isNull() || loaded.pixmap( IconSize, IconSize ).isNull() ) {
        warnOnce( pluginId, reference, QStringLiteral( "unreadable file" ) );
        return {};
    }
    return loaded;
}

QIcon PluginIconRegistry::icon( const QString& pluginId, const QString& directory,
                                const QJsonValue& reference, const QIcon& fallback )
{
    if ( reference.isUndefined() || reference.isNull() ) {
        return fallback;
    }
    const auto error = cilogg::plugins::validateIconReference( reference );
    if ( !error.isEmpty() ) {
        warnOnce( pluginId, reference.toVariant().toString(), error );
        return fallback;
    }
    QString chosen;
    if ( reference.isObject() ) {
        const auto themed = reference.toObject();
        const auto first = darkTheme() ? "dark" : "light";
        const auto second = darkTheme() ? "light" : "dark";
        chosen = themed.value( QLatin1String( first ) )
                     .toString( themed.value( QLatin1String( second ) ).toString() );
    }
    else {
        chosen = reference.toString();
    }
    const auto resolved = resolve( pluginId, directory, chosen );
    return resolved.isNull() ? fallback : resolved;
}

QString PluginIconRegistry::iconHtml( const QString& pluginId, const QString& directory,
                                      const QJsonValue& reference )
{
    const auto resolved = icon( pluginId, directory, reference );
    if ( resolved.isNull() ) {
        return {};
    }
    const auto pixmap = resolved.pixmap( IconSize, IconSize );
    QByteArray png;
    QBuffer buffer( &png );
    buffer.open( QIODevice::WriteOnly );
    if ( pixmap.isNull() || !pixmap.save( &buffer, "PNG" ) ) {
        return {};
    }
    return QStringLiteral( "<img src=\"data:image/png;base64,%1\" width=\"%2\" height=\"%2\">" )
        .arg( QString::fromLatin1( png.toBase64() ) )
        .arg( IconSize );
}
