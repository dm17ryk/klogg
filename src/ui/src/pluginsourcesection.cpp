// Copyright (C) 2026 CILogg contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "pluginsourcesection.h"

#include "pluginsandpacks.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace pluginsui {
namespace {
// The end of the diagnostics is what explains a failure; CILogg keeps at most 32 KiB of them.
constexpr int MaxShownDiagnosticsChars = 8000;
constexpr int DiagnosticsLines = 8;

QString tr( const char* text )
{
    return QCoreApplication::translate( "PluginSourceSection", text );
}

QLabel* wrappedLabel( const QString& text, QWidget* parent, const QString& name )
{
    auto* label = new QLabel( text, parent );
    label->setObjectName( name );
    label->setWordWrap( true );
    label->setTextFormat( Qt::PlainText ); // paths and errors come from the plugin
    label->setTextInteractionFlags( Qt::TextSelectableByMouse );
    return label;
}
} // namespace

QString problemText( const QVariantMap& plugin )
{
    QStringList parts;
    const auto error = plugin.value( "error" ).toString();
    if ( !error.isEmpty() ) {
        parts.append( tr( "Error: %1" ).arg( error ) );
    }
    auto diagnostics = plugin.value( "diagnostics" ).toString().trimmed();
    if ( !diagnostics.isEmpty() ) {
        if ( diagnostics.size() > MaxShownDiagnosticsChars ) {
            diagnostics = QChar( 0x2026 ) + diagnostics.right( MaxShownDiagnosticsChars );
        }
        parts.append( tr( "Plugin output (its stderr and how CILogg started it):" ) + '\n'
                      + diagnostics );
    }
    return parts.join( QStringLiteral( "\n\n" ) );
}

QString choosePluginSource( QWidget* parent, const QString& start, bool zip )
{
    const auto folder = QFileInfo( start ).isDir() ? start : QFileInfo( start ).absolutePath();
    return zip ? QFileDialog::getOpenFileName( parent, tr( "Plugin package" ),
                                               start.isEmpty() ? QString{} : folder,
                                               QStringLiteral( "ZIP (*.zip)" ) )
               : QFileDialog::getExistingDirectory( parent, tr( "Plugin folder" ),
                                                    start.isEmpty() ? QString{} : folder );
}

SourceSection createSourceSection( QWidget* parent, const QVariantMap& plugin )
{
    SourceSection section;
    auto* group = new QGroupBox( tr( "Installed from" ), parent );
    group->setObjectName( "pluginSourceSection" );
    section.widget = group;
    auto* layout = new QVBoxLayout( group );
    auto* form = new QFormLayout;
    layout->addLayout( form );

    const auto source = plugin.value( "source" ).toMap();
    const auto type = source.value( "type" ).toString();
    QString kind = sourceLabel( plugin );
    if ( type == QLatin1String( "local" ) ) {
        kind = source.value( "pathKind" ) == QLatin1String( "zip" ) ? tr( "Local ZIP" )
                                                                    : tr( "Local folder" );
    }
    else if ( type == QLatin1String( "repository" ) ) {
        kind = tr( "Repository: %1" ).arg( sourceLabel( plugin ) );
    }
    form->addRow( tr( "Source" ), wrappedLabel( kind, group, "pluginSourceKind" ) );

    if ( type == QLatin1String( "local" ) ) {
        // The folder or ZIP it is reinstalled from; a plugin installed before CILogg recorded it
        // has none until one is chosen here.
        auto* row = new QWidget( group );
        auto* rowLayout = new QHBoxLayout( row );
        rowLayout->setContentsMargins( 0, 0, 0, 0 );
        section.path
            = new QLineEdit( QDir::toNativeSeparators( source.value( "path" ).toString() ), row );
        section.path->setObjectName( "pluginSourcePath" );
        section.path->setAccessibleName( tr( "Plugin source folder or ZIP" ) );
        section.path->setPlaceholderText(
            tr( "Not recorded: choose the folder or ZIP the plugin was installed from" ) );
        rowLayout->addWidget( section.path, 1 );
        auto* path = section.path;
        for ( const bool zip : { false, true } ) {
            auto* browse = new QPushButton( zip ? tr( "ZIP…" ) : tr( "Folder…" ), row );
            browse->setObjectName( zip ? "pluginSourceBrowseZip" : "pluginSourceBrowseFolder" );
            browse->setAccessibleName( zip ? tr( "Choose the plugin ZIP" )
                                           : tr( "Choose the plugin folder" ) );
            QObject::connect( browse, &QPushButton::clicked, row, [ row, path, zip ] {
                const auto chosen = choosePluginSource( row, path->text(), zip );
                if ( !chosen.isEmpty() ) {
                    path->setText( QDir::toNativeSeparators( chosen ) );
                }
            } );
            rowLayout->addWidget( browse );
        }
        section.reinstall = new QPushButton( tr( "Reinstall" ), row );
        section.reinstall->setObjectName( "pluginSourceReinstall" );
        section.reinstall->setAccessibleName( tr( "Reinstall from this folder or ZIP" ) );
        section.reinstall->setToolTip(
            tr( "Install the plugin again from this folder or ZIP and restart it" ) );
        // Nothing to reinstall from until a folder or ZIP is given.
        auto* reinstall = section.reinstall;
        const auto updateReinstall = [ reinstall ]( const QString& text ) {
            reinstall->setEnabled( !text.trimmed().isEmpty() );
        };
        updateReinstall( path->text() );
        QObject::connect( path, &QLineEdit::textChanged, reinstall, updateReinstall );
        rowLayout->addWidget( section.reinstall );
        form->addRow( tr( "Folder or ZIP" ), row );
    }
    const auto directory = QDir::toNativeSeparators( plugin.value( "directory" ).toString() );
    form->addRow( tr( "Installed copy" ), wrappedLabel( directory, group, "pluginInstalledCopy" ) );

    const auto problem = problemText( plugin );
    if ( !problem.isEmpty() ) {
        auto* text = new QPlainTextEdit( problem, group );
        text->setObjectName( "pluginSourceDiagnostics" );
        text->setAccessibleName( tr( "Plugin error and output" ) );
        text->setReadOnly( true );
        text->setMaximumHeight( text->fontMetrics().lineSpacing() * DiagnosticsLines );
        layout->addWidget( text );
    }
    return section;
}

} // namespace pluginsui
