#include "packeditors.h"

#include "log.h"
#include "packmerge.h"
#include "persistentinfo.h"
#include "previewpackregistry.h"
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QRegularExpression>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSplitter>
#include <QStringDecoder>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>

namespace pluginsui {
namespace {
constexpr auto MergeToolKey = "packs/mergeTool";
constexpr qsizetype MaxItemBytes = 1024 * 1024;
constexpr int ConflictRole = Qt::UserRole;
constexpr int ChoiceRole = Qt::UserRole + 1; // "mine", "theirs", "edit" or "both"
constexpr int ValueRole = Qt::UserRole + 2;

QString tr( const char* text )
{
    return QCoreApplication::translate( "PackEditors", text );
}

CommanderRequest packRequest( const QString& operation, const QString& packId,
                              const QVariantMap& options = {} )
{
    CommanderRequest request;
    request.action = CommanderAction::PluginManage;
    request.pluginOperation = operation;
    request.pluginId = packId;
    request.pluginArguments = options;
    request.origin = CommanderOriginGui;
    return request;
}

QPushButton* button( QBoxLayout* row, QWidget* parent, const QString& text, const QString& name )
{
    auto* pushButton = new QPushButton( text, parent );
    pushButton->setObjectName( name );
    pushButton->setAccessibleName( QString( text ).remove( QChar( 0x2026 ) ).trimmed() );
    row->addWidget( pushButton );
    return pushButton;
}

QString keepBothName( const QString& name )
{
    return tr( "%1 (mine)" ).arg( name );
}

// Keep Both copies the user's previews from `pack` (the pack as it is before the update) once
// the update is installed; checked at Apply, so a copy that could not be made stops it there:
// the copies' names (also against `incoming`, the new version's previews), and their decoder
// routes all together.
QString checkKeepBoth( const PreviewPackRegistry::Contribution& pack, const QStringList& names,
                       const QStringList& incoming )
{
    QMap<QString, QString> copies;
    for ( const auto& name : names ) {
        copies.insert( name, keepBothName( name ) );
        const auto& previews = pack.previews.previews;
        if ( std::none_of( previews.cbegin(), previews.cend(),
                           [ &name ]( const auto& preview ) { return preview.name == name; } ) ) {
            return tr( "\"%1\" is not a preview of the pack." ).arg( name );
        }
        if ( PreviewManager::instance().findByName( keepBothName( name ) ) ) {
            return tr( "You already have a preview named \"%1\". Rename or delete it first, or "
                       "choose Keep Mine or Take Theirs." )
                .arg( keepBothName( name ) );
        }
        // A copy with the name of a pack's preview, now or in the new version, would replace
        // that preview (yours win).
        if ( std::any_of( previews.cbegin(), previews.cend(),
                          [ &name ]( const auto& preview ) {
                              return preview.name == keepBothName( name );
                          } )
             || incoming.contains( keepBothName( name ) )
             || PreviewManager::instance().packOrigin( keepBothName( name ) ) ) {
            return tr( "A pack has a preview named \"%1\", which the copy would replace. Choose "
                       "Keep Mine or Take Theirs." )
                .arg( keepBothName( name ) );
        }
    }
    const auto routes = PreviewPackRegistry::instance().checkCopyRoutes( pack, copies );
    if ( !routes.isEmpty() ) {
        return routes + QLatin1Char( ' ' )
               + tr( "Change your preview routes first, or choose Keep Mine or Take Theirs." );
    }
    return {};
}

// The job of an update a merge settled: once it is installed, the merge's Keep Both copies are
// made; a copy that fails becomes a warning of the job (the update stays installed).
QVariantMap afterPackMerge( const QVariantMap& job, const AfterPackMerge& afterInstall )
{
    if ( !afterInstall || job.value( "state" ) != "completed" ) {
        return job;
    }
    const auto error = afterInstall();
    if ( error.isEmpty() ) {
        return job;
    }
    LOG_WARNING << "Keep Both copies not made after a pack update: " << error.toStdString();
    auto finished = job;
    auto result = finished.value( "result" ).toMap();
    const auto earlier = result.value( "warning" ).toString();
    result[ "warning" ] = ( earlier.isEmpty() ? QString{} : earlier + QLatin1Char( ' ' ) )
                          + tr( "Keep Both could not copy your previews: %1 Your versions stay "
                                "in the pack's history." )
                                .arg( error );
    finished[ "result" ] = result;
    return finished;
}

QFont fixedFont()
{
    return QFontDatabase::systemFont( QFontDatabase::FixedFont );
}

bool hasMarkers( const QString& text )
{
    return text.startsWith( QStringLiteral( "<<<<<<< " ) )
           || text.contains( QStringLiteral( "\n<<<<<<< " ) );
}

QString suffixOf( const QString& path )
{
    const auto dot = path.lastIndexOf( '.' );
    return dot < 0 ? QStringLiteral( ".txt" ) : path.mid( dot );
}

// Replaces the first diff3 conflict block of `text` with the chosen side.
QString takeFirstConflict( const QString& text, bool mine )
{
    auto lines = text.split( '\n' );
    const auto start = lines.indexOf( QRegularExpression( "^<<<<<<< .*" ) );
    if ( start < 0 ) {
        return text;
    }
    qsizetype baseMark = -1;
    qsizetype split = -1;
    qsizetype end = -1;
    for ( qsizetype index = start + 1; index < lines.size(); ++index ) {
        if ( baseMark < 0 && split < 0 && lines.at( index ).startsWith( QStringLiteral( "||||||| " ) ) ) {
            baseMark = index;
        }
        else if ( split < 0 && lines.at( index ) == QStringLiteral( "=======" ) ) {
            split = index;
        }
        else if ( split >= 0 && lines.at( index ).startsWith( QStringLiteral( ">>>>>>> " ) ) ) {
            end = index;
            break;
        }
    }
    if ( split < 0 || end < 0 ) {
        return text;
    }
    const auto mineEnd = baseMark >= 0 ? baseMark : split;
    const auto chosen = mine ? lines.mid( start + 1, mineEnd - start - 1 )
                             : lines.mid( split + 1, end - split - 1 );
    lines.erase( lines.begin() + start, lines.begin() + end + 1 );
    for ( qsizetype index = 0; index < chosen.size(); ++index ) {
        lines.insert( start + index, chosen.at( index ) );
    }
    return lines.join( '\n' );
}

void showHistory( QWidget* parent, Executor executor, const QString& packId, const QString& path,
                  const QString& title, const std::function<void()>& changed )
{
    const auto history = executor( packRequest( "pack_history", packId, { { "path", path } } ) );
    if ( !history.ok() ) {
        QMessageBox::warning( parent, title, history.message );
        return;
    }
    QDialog dialog( parent );
    dialog.setWindowTitle( title );
    dialog.setObjectName( "packHistoryDialog" );
    auto* layout = new QVBoxLayout( &dialog );
    auto* list = new QListWidget( &dialog );
    list->setObjectName( "packHistoryList" );
    list->setAccessibleName( tr( "Versions" ) );
    for ( const auto& value : history.payload.value( "commits" ).toList() ) {
        const auto commit = value.toMap();
        const auto when = QDateTime::fromString( commit.value( "time" ).toString(), Qt::ISODate );
        auto* item = new QListWidgetItem(
            QStringLiteral( "%1  %2%3" )
                .arg( QLocale().toString( when.toLocalTime(), QLocale::ShortFormat ),
                      commit.value( "message" ).toString(),
                      commit.value( "packVersion" ).toBool() ? tr( "  (pack version)" ) : QString{} ),
            list );
        item->setData( Qt::UserRole, commit.value( "commit" ) );
    }
    layout->addWidget( list );
    auto* row = new QHBoxLayout;
    layout->addLayout( row );
    auto* revert = button( row, &dialog, tr( "Revert to This Version" ), "packHistoryRevert" );
    row->addStretch( 1 );
    auto* close = button( row, &dialog, tr( "Close" ), "packHistoryClose" );
    QObject::connect( close, &QPushButton::clicked, &dialog, &QDialog::reject );
    QObject::connect( revert, &QPushButton::clicked, &dialog, [ & ] {
        const auto* item = list->currentItem();
        if ( !item ) {
            return;
        }
        // A revert is a new edit, so it can be undone the same way.
        const auto result = executor( packRequest(
            "pack_reset", packId, { { "path", path }, { "commit", item->data( Qt::UserRole ) } } ) );
        if ( !result.ok() ) {
            QMessageBox::warning( &dialog, title, result.message );
            return;
        }
        changed();
        dialog.accept();
    } );
    dialog.resize( 640, 360 );
    dialog.exec();
}
} // namespace

QString jobWarningSuffix( const QVariantMap& job )
{
    const auto warning = job.value( "result" ).toMap().value( "warning" ).toString();
    return warning.isEmpty() ? QString{} : QLatin1Char( ' ' ) + warning;
}

bool needsPackMerge( const QVariantMap& job )
{
    return job.value( "state" ) == "failed"
           && job.value( "result" ).toMap().value( "needsMerge" ).toBool();
}

namespace {
// Runs `request` and follows its job; a job that needs a merge opens the merge window, whose Apply
// runs the request again with that merge's choices. `afterInstall` belongs to the merge that led
// to this request: it runs once this job is installed, and is dropped when the job needs another
// merge (the pack was edited meanwhile): that merge's choices, Keep Both included, replace it.
void runWithMerge( QWidget* parent, QPointer<cilogg::plugins::PluginManager> manager,
                   Executor executor, const CommanderRequest& request, const QString& name,
                   const std::function<void( const QVariantMap& )>& done,
                   const AfterPackMerge& afterInstall )
{
    const auto started = executor( request );
    if ( !started.ok() ) {
        done( { { "state", "failed" }, { "error", started.message } } );
        return;
    }
    followJob( parent, manager, started.payload.value( "jobId" ).toString(),
               [ parent, manager, executor, request, name, done, afterInstall ]( const QVariantMap& job ) {
                   if ( !needsPackMerge( job ) ) {
                       done( afterPackMerge( job, afterInstall ) );
                       return;
                   }
                   // The installed version stays until Apply; Cancel reports the refusal.
                   const bool applied = openPackMerge(
                       parent, name, job,
                       [ parent, manager, executor, request, name, done,
                         revision = job.value( "result" ).toMap().value( "revision" ) ](
                           const QVariantList& resolutions, const AfterPackMerge& chosen ) {
                           auto again = request;
                           again.pluginArguments.insert( "resolutions", resolutions );
                           // The edits the choices were made for: after another edit the update
                           // asks for the merge again instead of overwriting it.
                           if ( revision.isValid() ) {
                               again.pluginArguments.insert( "revision", revision.toString() );
                           }
                           runWithMerge( parent, manager, executor, again, name, done, chosen );
                       } );
                   if ( !applied ) {
                       done( job );
                   }
               } );
}
} // namespace

void runPackAwareInstall( QWidget* parent, QPointer<cilogg::plugins::PluginManager> manager,
                          Executor executor, const CommanderRequest& request, const QString& name,
                          const std::function<void( const QVariantMap& )>& done )
{
    runWithMerge( parent, manager, executor, request, name, done, {} );
}

QString mergeToolCommand()
{
    return PersistentInfo::getSettings( app_settings{} ).value( MergeToolKey ).toString();
}

void setMergeToolCommand( const QString& command )
{
    PersistentInfo::getSettings( app_settings{} ).setValue( MergeToolKey, command );
}

QStringList mergeToolPresets()
{
    return { QStringLiteral( "\"BComp\" \"$LOCAL\" \"$REMOTE\" \"$BASE\" \"$MERGED\"" ),
             QStringLiteral( "\"kdiff3\" \"$BASE\" \"$LOCAL\" \"$REMOTE\" -o \"$MERGED\"" ),
             QStringLiteral( "\"WinMergeU\" -u -e -wl -wr \"$LOCAL\" \"$BASE\" \"$REMOTE\" -o \"$MERGED\"" ),
             QStringLiteral( "\"meld\" \"$LOCAL\" \"$BASE\" \"$REMOTE\" --output \"$MERGED\"" ),
             QStringLiteral( "\"p4merge\" \"$BASE\" \"$LOCAL\" \"$REMOTE\" \"$MERGED\"" ) };
}

std::optional<QString> runMergeTool( QWidget* parent, const QString& base, const QString& mine,
                                     const QString& theirs, const QString& suffix,
                                     QString* error )
{
    auto command = mergeToolCommand();
    if ( command.isEmpty() ) {
        // The first use asks for the tool.
        bool ok = false;
        command = QInputDialog::getItem( parent, tr( "External Merge Tool" ),
                                         tr( "Command ($BASE, $LOCAL, $REMOTE and $MERGED are the "
                                             "files):" ),
                                         mergeToolPresets(), 0, true, &ok );
        if ( !ok || command.trimmed().isEmpty() ) {
            return std::nullopt;
        }
        setMergeToolCommand( command.trimmed() );
    }
    QTemporaryDir folder;
    if ( !folder.isValid() ) {
        *error = tr( "Cannot create a folder for the merge tool." );
        return std::nullopt;
    }
    const QMap<QString, QString> files{ { "$BASE", folder.filePath( "BASE" + suffix ) },
                                        { "$LOCAL", folder.filePath( "LOCAL" + suffix ) },
                                        { "$REMOTE", folder.filePath( "REMOTE" + suffix ) },
                                        { "$MERGED", folder.filePath( "MERGED" + suffix ) } };
    // $MERGED starts as the merge with its conflict markers, as git mergetool does: a tool that
    // fails, is cancelled or returns at once leaves the markers, and the result is refused.
    const auto start = QString::fromUtf8(
        cilogg::plugins::packmerge::mergeText( base.toUtf8(), mine.toUtf8(), theirs.toUtf8() ).text );
    for ( const auto& [ key, text ] :
          { std::pair{ QStringLiteral( "$BASE" ), base }, std::pair{ QStringLiteral( "$LOCAL" ), mine },
            std::pair{ QStringLiteral( "$REMOTE" ), theirs },
            std::pair{ QStringLiteral( "$MERGED" ), start } } ) {
        QFile file( files.value( key ) );
        if ( !file.open( QIODevice::WriteOnly ) || file.write( text.toUtf8() ) < 0 ) {
            *error = tr( "Cannot write the files for the merge tool." );
            return std::nullopt;
        }
    }
    // Each argument is split first, then its placeholders are replaced, so paths with spaces
    // stay one argument.
    auto arguments = QProcess::splitCommand( command );
    if ( arguments.isEmpty() ) {
        *error = tr( "The merge tool command is empty." );
        return std::nullopt;
    }
    for ( auto& argument : arguments ) {
        for ( auto file = files.cbegin(); file != files.cend(); ++file ) {
            argument.replace( file.key(), QDir::toNativeSeparators( file.value() ) );
        }
    }
    QProcess process;
    process.setProgram( arguments.takeFirst() );
    process.setArguments( arguments );
    QEventLoop loop;
    QObject::connect( &process, &QProcess::finished, &loop, &QEventLoop::quit );
    QObject::connect( &process, &QProcess::errorOccurred, &loop, &QEventLoop::quit );
    QMessageBox waiting( QMessageBox::Information, tr( "External Merge Tool" ),
                         tr( "Merge the file in the merge tool, save it and close the tool." ),
                         QMessageBox::Cancel, parent );
    waiting.setWindowModality( Qt::WindowModal );
    QObject::connect( &waiting, &QMessageBox::rejected, &loop, &QEventLoop::quit );
    // No blocking wait: the loop ends when the tool finishes, fails to start or is cancelled.
    process.start();
    if ( process.state() != QProcess::NotRunning ) {
        waiting.show();
        loop.exec();
        waiting.hide();
    }
    if ( process.error() == QProcess::FailedToStart ) {
        *error = tr( "The merge tool did not start: %1" ).arg( process.errorString() );
        return std::nullopt;
    }
    if ( process.state() != QProcess::NotRunning ) {
        process.kill();
        process.waitForFinished( 3000 );
        return std::nullopt; // cancelled
    }
    if ( process.exitStatus() == QProcess::CrashExit ) {
        *error = tr( "The merge tool stopped unexpectedly." );
        return std::nullopt;
    }
    QFile merged( files.value( "$MERGED" ) );
    if ( !merged.open( QIODevice::ReadOnly ) || merged.size() > MaxItemBytes ) {
        *error = tr( "The merge tool left no result of at most 1 MiB." );
        return std::nullopt;
    }
    auto decoder = QStringDecoder( QStringDecoder::Utf8 );
    const QString text = decoder( merged.readAll() );
    if ( decoder.hasError() ) {
        *error = tr( "The merge tool's result is not UTF-8 text." );
        return std::nullopt;
    }
    if ( hasMarkers( text ) ) {
        *error = tr( "The merge tool's result still has conflict markers." );
        return std::nullopt;
    }
    return text;
}

void openPackItemEditor( QWidget* parent, Executor executor, const QString& packId,
                         const QString& packName, const QString& path )
{
    const auto read = executor( packRequest( "pack_read", packId, { { "path", path } } ) );
    const auto title = tr( "Edit Pack Item" );
    if ( !read.ok() ) {
        QMessageBox::warning( parent, title, read.message );
        return;
    }
    QDialog dialog( parent );
    dialog.setObjectName( "packItemEditor" );
    dialog.setWindowTitle(
        tr( "%1 - %2" ).arg( read.payload.value( "path" ).toString(), packName ) );
    auto* layout = new QVBoxLayout( &dialog );
    auto* note = new QLabel(
        tr( "Your edit stays when the pack is updated: a new version is merged with it." ), &dialog );
    note->setWordWrap( true );
    layout->addWidget( note );
    auto* text = new QPlainTextEdit( &dialog );
    text->setObjectName( "packItemEditorText" );
    text->setAccessibleName( tr( "Item" ) );
    text->setFont( fixedFont() );
    text->setLineWrapMode( QPlainTextEdit::NoWrap );
    text->setPlainText( read.payload.value( "content" ).toString() );
    layout->addWidget( text, 1 );
    auto* row = new QHBoxLayout;
    layout->addLayout( row );
    auto* external = button( row, &dialog, tr( "Open in External Editor" ), "packItemEditorExternal" );
    auto* reload = button( row, &dialog, tr( "Reload From External Editor" ), "packItemEditorReload" );
    reload->setEnabled( false );
    auto* history = button( row, &dialog, tr( "History…" ), "packItemEditorHistory" );
    auto* reset = button( row, &dialog, tr( "Reset to Pack Version" ), "packItemEditorReset" );
    row->addStretch( 1 );
    auto* save = button( row, &dialog, tr( "Save" ), "packItemEditorSave" );
    auto* close = button( row, &dialog, tr( "Close" ), "packItemEditorClose" );
    save->setDefault( true );
    auto folder = std::make_shared<QTemporaryDir>();
    const auto externalFile = folder->filePath( QStringLiteral( "item" ) + suffixOf( path ) );
    const auto reloadFromStore = [ & ] {
        const auto again = executor( packRequest( "pack_read", packId, { { "path", path } } ) );
        if ( again.ok() ) {
            text->setPlainText( again.payload.value( "content" ).toString() );
        }
    };
    QObject::connect( external, &QPushButton::clicked, &dialog, [ & ] {
        QFile file( externalFile );
        if ( !folder->isValid() || !file.open( QIODevice::WriteOnly )
             || file.write( text->toPlainText().toUtf8() ) < 0 ) {
            QMessageBox::warning( &dialog, title, tr( "Cannot write the file for the editor." ) );
            return;
        }
        file.close();
        QDesktopServices::openUrl( QUrl::fromLocalFile( externalFile ) );
        reload->setEnabled( true );
    } );
    QObject::connect( reload, &QPushButton::clicked, &dialog, [ & ] {
        QFile file( externalFile );
        if ( !file.open( QIODevice::ReadOnly ) || file.size() > MaxItemBytes ) {
            QMessageBox::warning( &dialog, title, tr( "Cannot read the edited file (at most 1 MiB)." ) );
            return;
        }
        // Saved in another encoding, the text would be loaded with replacement characters and
        // Save would store them: refused, and the editor keeps what it has.
        auto decoder = QStringDecoder( QStringDecoder::Utf8 );
        const QString edited = decoder( file.readAll() );
        if ( decoder.hasError() ) {
            QMessageBox::warning( &dialog, title,
                                  tr( "The edited file is not UTF-8 text. Save it as UTF-8 in the "
                                      "editor and reload it." ) );
            return;
        }
        text->setPlainText( edited );
    } );
    QObject::connect( history, &QPushButton::clicked, &dialog, [ & ] {
        showHistory( &dialog, executor, packId, path, tr( "History of %1" ).arg( path ),
                     reloadFromStore );
    } );
    QObject::connect( reset, &QPushButton::clicked, &dialog, [ & ] {
        if ( QMessageBox::question( &dialog, title,
                                    tr( "Replace your edit with the pack's version? You can undo "
                                        "it from History." ) )
             != QMessageBox::Yes ) {
            return;
        }
        const auto result = executor( packRequest( "pack_reset", packId, { { "path", path } } ) );
        if ( !result.ok() ) {
            QMessageBox::warning( &dialog, title, result.message );
            return;
        }
        reloadFromStore();
    } );
    QObject::connect( save, &QPushButton::clicked, &dialog, [ & ] {
        const auto result = executor( packRequest(
            "pack_edit", packId, { { "path", path }, { "content", text->toPlainText() } } ) );
        if ( !result.ok() ) {
            QMessageBox::warning( &dialog, title, result.message );
            return;
        }
        dialog.accept();
    } );
    QObject::connect( close, &QPushButton::clicked, &dialog, &QDialog::reject );
    dialog.resize( 820, 600 );
    dialog.exec();
}

void openPackFiles( QWidget* parent, Executor executor, const QString& packId,
                    const QString& packName )
{
    QDialog dialog( parent );
    dialog.setObjectName( "packFilesDialog" );
    dialog.setWindowTitle( tr( "Pack Files - %1" ).arg( packName ) );
    auto* layout = new QVBoxLayout( &dialog );
    auto* table = new QTableWidget( 0, 3, &dialog );
    table->setObjectName( "packFilesTable" );
    table->setAccessibleName( tr( "Pack items" ) );
    table->setHorizontalHeaderLabels( { tr( "Item" ), tr( "Kind" ), tr( "State" ) } );
    table->setSelectionBehavior( QAbstractItemView::SelectRows );
    table->setSelectionMode( QAbstractItemView::SingleSelection );
    table->setEditTriggers( QAbstractItemView::NoEditTriggers );
    table->verticalHeader()->setVisible( false );
    table->horizontalHeader()->setStretchLastSection( true );
    layout->addWidget( table, 1 );
    auto* row = new QHBoxLayout;
    layout->addLayout( row );
    auto* edit = button( row, &dialog, tr( "Edit…" ), "packFilesEdit" );
    auto* history = button( row, &dialog, tr( "History…" ), "packFilesHistory" );
    auto* reset = button( row, &dialog, tr( "Reset to Pack Version" ), "packFilesReset" );
    auto* resetAll = button( row, &dialog, tr( "Reset All to Pack Version" ), "packFilesResetAll" );
    row->addStretch( 1 );
    auto* close = button( row, &dialog, tr( "Close" ), "packFilesClose" );
    const auto states = QMap<QString, QString>{ { "pack", tr( "As in the pack" ) },
                                                { "edited", tr( "Edited" ) },
                                                { "added", tr( "Added" ) },
                                                { "deleted", tr( "Deleted" ) } };
    const auto kinds = QMap<QString, QString>{ { "preview", tr( "Preview" ) },
                                               { "block", tr( "Block" ) },
                                               { "order", tr( "Preview order" ) },
                                               { "meta", tr( "Previews settings" ) },
                                               { "file", tr( "File" ) } };
    const auto selected = [ table ]( int role ) {
        const auto* cell = table->currentRow() >= 0 ? table->item( table->currentRow(), 0 ) : nullptr;
        return cell ? cell->data( role ).toString() : QString{};
    };
    // A deleted item has nothing to edit, and the previews settings (version, $schema) are not an
    // item: they come with the pack and are not edited here.
    const auto editable = [ &selected ] {
        return !selected( Qt::UserRole ).isEmpty() && selected( Qt::UserRole + 1 ) != "deleted"
               && selected( Qt::UserRole + 2 ) != "meta";
    };
    const auto refresh = [ & ] {
        const auto files = executor( packRequest( "pack_files", packId ) );
        if ( !files.ok() ) {
            QMessageBox::warning( &dialog, dialog.windowTitle(), files.message );
            return;
        }
        const auto list = files.payload.value( "files" ).toList();
        table->setRowCount( static_cast<int>( list.size() ) );
        int index = 0;
        for ( const auto& value : list ) {
            const auto file = value.toMap();
            const QStringList texts{ file.value( "item" ).toString(),
                                     kinds.value( file.value( "kind" ).toString() ),
                                     states.value( file.value( "state" ).toString() ) };
            for ( int column = 0; column < texts.size(); ++column ) {
                auto* cell = new QTableWidgetItem( texts.at( column ) );
                cell->setData( Qt::UserRole, file.value( "path" ) );
                cell->setData( Qt::UserRole + 1, file.value( "state" ) );
                cell->setData( Qt::UserRole + 2, file.value( "kind" ) );
                table->setItem( index, column, cell );
            }
            ++index;
        }
        edit->setEnabled( editable() );
    };
    QObject::connect( table, &QTableWidget::currentCellChanged, &dialog,
                      [ & ] { edit->setEnabled( editable() ); } );
    QObject::connect( edit, &QPushButton::clicked, &dialog, [ & ] {
        if ( !editable() ) {
            return;
        }
        openPackItemEditor( &dialog, executor, packId, packName, selected( Qt::UserRole ) );
        refresh();
    } );
    QObject::connect( history, &QPushButton::clicked, &dialog, [ & ] {
        const auto path = selected( Qt::UserRole );
        if ( !path.isEmpty() ) {
            showHistory( &dialog, executor, packId, path, tr( "History of %1" ).arg( path ), refresh );
        }
    } );
    const auto resetPath = [ & ]( const QString& path ) {
        if ( QMessageBox::question( &dialog, dialog.windowTitle(),
                                    path.isEmpty()
                                        ? tr( "Replace all your edits of this pack with the pack's "
                                              "version? You can undo it from History." )
                                        : tr( "Replace your edit with the pack's version? You can "
                                              "undo it from History." ) )
             != QMessageBox::Yes ) {
            return;
        }
        const auto result = executor(
            packRequest( "pack_reset", packId,
                         path.isEmpty() ? QVariantMap{} : QVariantMap{ { "path", path } } ) );
        if ( !result.ok() ) {
            QMessageBox::warning( &dialog, dialog.windowTitle(), result.message );
        }
        refresh();
    };
    QObject::connect( reset, &QPushButton::clicked, &dialog, [ & ] {
        const auto path = selected( Qt::UserRole );
        if ( !path.isEmpty() ) {
            resetPath( path );
        }
    } );
    QObject::connect( resetAll, &QPushButton::clicked, &dialog, [ & ] { resetPath( {} ); } );
    QObject::connect( close, &QPushButton::clicked, &dialog, &QDialog::reject );
    refresh();
    dialog.resize( 640, 420 );
    dialog.exec();
}

bool openPackMerge( QWidget* parent, const QString& packName, const QVariantMap& job,
                    const std::function<void( const QVariantList&, const AfterPackMerge& )>& apply )
{
    const auto result = job.value( "result" ).toMap();
    const auto conflicts = result.value( "conflicts" ).toList();
    const auto items = result.value( "items" ).toMap();
    QDialog dialog( parent );
    dialog.setObjectName( "packMergeDialog" );
    dialog.setWindowTitle( tr( "Merge Pack Update - %1" ).arg( packName ) );
    auto* layout = new QVBoxLayout( &dialog );
    auto* note = new QLabel( tr( "The new version changes items you edited. Choose for each "
                                 "place; nothing changes until you press Apply." ),
                             &dialog );
    note->setWordWrap( true );
    layout->addWidget( note );
    auto* tabs = new QTabWidget( &dialog );
    tabs->setObjectName( "packMergeTabs" );
    layout->addWidget( tabs, 1 );

    // Structured items: pack > item > field with Installed, Yours, New version and Result.
    auto* itemsPage = new QWidget( tabs );
    auto* itemsLayout = new QVBoxLayout( itemsPage );
    auto* tree = new QTreeWidget( itemsPage );
    tree->setObjectName( "packMergeTree" );
    tree->setAccessibleName( tr( "Conflicting fields" ) );
    tree->setHeaderLabels(
        { tr( "Item / field" ), tr( "Installed" ), tr( "Yours" ), tr( "New version" ), tr( "Result" ) } );
    itemsLayout->addWidget( tree, 1 );
    auto* rowButtons = new QHBoxLayout;
    itemsLayout->addLayout( rowButtons );
    auto* keepMine = button( rowButtons, itemsPage, tr( "Keep Mine" ), "packMergeKeepMine" );
    auto* takeTheirs = button( rowButtons, itemsPage, tr( "Take Theirs" ), "packMergeTakeTheirs" );
    auto* editValue = button( rowButtons, itemsPage, tr( "Edit…" ), "packMergeEdit" );
    auto* keepBoth = button( rowButtons, itemsPage, tr( "Keep Both" ), "packMergeKeepBoth" );
    keepBoth->setToolTip( tr( "The new version stays the pack's preview; yours becomes one of your "
                              "own previews, named \"<name> (mine)\"." ) );
    rowButtons->addStretch( 1 );
    auto* allMine = button( rowButtons, itemsPage, tr( "Keep Mine for All" ), "packMergeAllMine" );
    auto* allTheirs = button( rowButtons, itemsPage, tr( "Take Theirs for All" ), "packMergeAllTheirs" );
    const auto absent = tr( "(none)" );
    const auto display = [ &absent ]( const QString& value ) {
        return value.isEmpty() ? absent : value;
    };
    const auto setChoice = [ & ]( QTreeWidgetItem* row, const QString& choice, const QString& value ) {
        row->setData( 0, ChoiceRole, choice );
        row->setData( 0, ValueRole, value );
        // An item too large to show is kept or taken as a whole: the choice is the result.
        const bool omitted = row->data( 0, ConflictRole ).toMap().value( "omitted" ).toBool();
        row->setText( 4, choice == "both"     ? tr( "Keep Both" )
                         : !omitted           ? display( value )
                         : choice == "theirs" ? tr( "Take Theirs" )
                                              : tr( "Keep Mine" ) );
    };
    QMap<QString, QTreeWidgetItem*> parents;
    // A Lua or Python file the manifest lists, or the preview order: resolved to a text, never
    // removed (the pack needs it).
    struct TextConflict {
        QString path;
        QString item;
        QPlainTextEdit* result;
        QString mine;
        QString theirs;
        bool wholeOnly = false;           // not UTF-8, or too large to show
        std::shared_ptr<QString> side;    // "mine" or "theirs": the whole side last chosen
    };
    auto textConflicts = std::make_shared<QVector<TextConflict>>();
    for ( const auto& value : conflicts ) {
        const auto conflict = value.toMap();
        const auto kind = conflict.value( "kind" ).toString();
        const auto path = conflict.value( "path" ).toString();
        if ( kind == "file" || kind == "order" ) {
            // Lua and Python: Installed, Yours and New version side by side, the result below.
            auto* page = new QWidget( tabs );
            auto* pageLayout = new QVBoxLayout( page );
            auto* sides = new QSplitter( Qt::Horizontal, page );
            const auto pane = [ & ]( const QString& label, const QString& content, const QString& name ) {
                auto* box = new QWidget( sides );
                auto* boxLayout = new QVBoxLayout( box );
                boxLayout->setContentsMargins( 0, 0, 0, 0 );
                boxLayout->addWidget( new QLabel( label, box ) );
                auto* edit = new QPlainTextEdit( box );
                edit->setObjectName( name );
                edit->setAccessibleName( label );
                edit->setReadOnly( true );
                edit->setFont( fixedFont() );
                edit->setLineWrapMode( QPlainTextEdit::NoWrap );
                edit->setPlainText( content );
                boxLayout->addWidget( edit );
            };
            const auto whole = items.value( path ).toMap();
            pane( tr( "Installed" ), conflict.value( "base" ).toString(), "packMergeBase" );
            pane( tr( "Yours" ), whole.value( "mine" ).toString(), "packMergeMine" );
            pane( tr( "New version" ), whole.value( "theirs" ).toString(), "packMergeTheirs" );
            pageLayout->addWidget( sides, 1 );
            pageLayout->addWidget( new QLabel( tr( "Result" ), page ) );
            auto* resultEdit = new QPlainTextEdit( page );
            resultEdit->setObjectName( "packMergeResult" );
            resultEdit->setAccessibleName( tr( "Result" ) );
            resultEdit->setFont( fixedFont() );
            resultEdit->setLineWrapMode( QPlainTextEdit::NoWrap );
            const auto merged = cilogg::plugins::packmerge::mergeText(
                conflict.value( "base" ).toString().toUtf8(), whole.value( "mine" ).toString().toUtf8(),
                whole.value( "theirs" ).toString().toUtf8() );
            resultEdit->setPlainText( QString::fromUtf8( merged.text ) );
            pageLayout->addWidget( resultEdit, 1 );
            auto* textButtons = new QHBoxLayout;
            pageLayout->addLayout( textButtons );
            auto* useMine = button( textButtons, page, tr( "Use Yours Here" ), "packMergeUseMine" );
            auto* useTheirs = button( textButtons, page, tr( "Use New Version Here" ), "packMergeUseTheirs" );
            auto* wholeMine = button( textButtons, page, tr( "Use Yours (Whole File)" ), "packMergeWholeMine" );
            auto* wholeTheirs
                = button( textButtons, page, tr( "Use New Version (Whole File)" ), "packMergeWholeTheirs" );
            auto* tool = button( textButtons, page, tr( "Open in External Merge Tool…" ), "packMergeTool" );
            textButtons->addStretch( 1 );
            const auto mineText = whole.value( "mine" ).toString();
            const auto theirsText = whole.value( "theirs" ).toString();
            const auto baseText = conflict.value( "base" ).toString();
            QObject::connect( useMine, &QPushButton::clicked, page, [ resultEdit ] {
                resultEdit->setPlainText( takeFirstConflict( resultEdit->toPlainText(), true ) );
            } );
            QObject::connect( useTheirs, &QPushButton::clicked, page, [ resultEdit ] {
                resultEdit->setPlainText( takeFirstConflict( resultEdit->toPlainText(), false ) );
            } );
            // The side a whole-side button chose, recorded and sent as `take`, never read back from
            // the editor: it shows a file that is not UTF-8 only as a lossy reading, and it turns
            // CRLF line endings into LF. Any other change of the result makes it a text again.
            auto side = std::make_shared<QString>();
            QObject::connect( wholeMine, &QPushButton::clicked, page, [ resultEdit, mineText, side ] {
                resultEdit->setPlainText( mineText );
                *side = QStringLiteral( "mine" );
            } );
            QObject::connect( wholeTheirs, &QPushButton::clicked, page, [ resultEdit, theirsText, side ] {
                resultEdit->setPlainText( theirsText );
                *side = QStringLiteral( "theirs" );
            } );
            QObject::connect( tool, &QPushButton::clicked, page,
                              [ page, resultEdit, baseText, mineText, theirsText, path ] {
                                  QString error;
                                  const auto text = runMergeTool( page, baseText, mineText, theirsText,
                                                                  suffixOf( path ), &error );
                                  if ( text ) {
                                      resultEdit->setPlainText( *text );
                                  }
                                  else if ( !error.isEmpty() ) {
                                      QMessageBox::warning( page, tr( "External Merge Tool" ), error );
                                  }
                              } );
            // Not UTF-8 text, or too large to carry in the job (`omitted`): only a whole side, kept
            // byte for byte; the panes show a reading (or nothing).
            const bool omitted = whole.value( "omitted" ).toBool();
            const bool wholeOnly = omitted || !whole.value( "utf8", true ).toBool();
            if ( wholeOnly ) {
                resultEdit->setReadOnly( true );
                resultEdit->setPlainText( mineText );
                for ( auto* textOnly : { useMine, useTheirs, tool } ) {
                    textOnly->setEnabled( false );
                }
                auto* reason = new QLabel( omitted ? tr( "This file is too large to show here: keep "
                                                         "yours or take the new version as a whole." )
                                                   : tr( "This file is not UTF-8 text: keep yours or "
                                                         "take the new version as a whole." ),
                                           page );
                reason->setObjectName( omitted ? "packMergeOmitted" : "packMergeNotUtf8" );
                pageLayout->addWidget( reason );
                *side = QStringLiteral( "mine" ); // what the read-only result shows
            }
            // Typing, a per-conflict button or the merge tool: the result is a text of its own (a
            // whole-side button sets its side again right after its setPlainText).
            QObject::connect( resultEdit, &QPlainTextEdit::textChanged, page, [ side ] { side->clear(); } );
            textConflicts->append( { path, conflict.value( "item" ).toString(), resultEdit, mineText,
                                     theirsText, wholeOnly, side } );
            tabs->addTab( page, conflict.value( "item" ).toString() );
            continue;
        }
        const auto whole = items.value( path ).toMap();
        const bool omitted = whole.value( "omitted" ).toBool();
        auto*& parentRow = parents[ path ];
        if ( omitted && parentRow ) {
            continue; // one row for an item too large to show
        }
        if ( !parentRow ) {
            parentRow = new QTreeWidgetItem( tree, { QStringLiteral( "%1 %2" ).arg(
                                                         kind == "preview" ? tr( "Preview" ) : tr( "Block" ),
                                                         conflict.value( "item" ).toString() ) } );
            parentRow->setData( 0, ConflictRole, QVariantMap{ { "path", path }, { "kind", kind } } );
            parentRow->setExpanded( true );
        }
        auto rowConflict = conflict;
        if ( omitted ) {
            // Too large to carry in the job: kept or taken as a whole (`take`).
            rowConflict = QVariantMap{ { "path", path },
                                       { "kind", kind },
                                       { "item", conflict.value( "item" ) },
                                       { "field", QString{} },
                                       { "omitted", true } };
        }
        else if ( conflict.value( "field" ).toString().isEmpty() && !conflict.contains( "mine" ) ) {
            // An item listed whole: its sides are in the job's items, once.
            rowConflict[ "mine" ] = whole.value( "mine" );
            rowConflict[ "theirs" ] = whole.value( "theirs" );
        }
        auto* row = new QTreeWidgetItem( parentRow );
        const auto field = rowConflict.value( "field" ).toString();
        const auto hidden = tr( "(too large to show)" );
        row->setText( 0, field.isEmpty() ? tr( "(whole item)" ) : field );
        row->setText( 1, omitted ? hidden : display( rowConflict.value( "base" ).toString() ) );
        row->setText( 2, omitted ? hidden : display( rowConflict.value( "mine" ).toString() ) );
        row->setText( 3, omitted ? hidden : display( rowConflict.value( "theirs" ).toString() ) );
        row->setData( 0, ConflictRole, rowConflict );
        setChoice( row, "mine", rowConflict.value( "mine" ).toString() );
    }
    if ( tree->topLevelItemCount() > 0 ) {
        tabs->insertTab( 0, itemsPage, tr( "Items" ) );
    }
    else {
        delete itemsPage;
    }
    tabs->setCurrentIndex( 0 );
    const auto fieldRows = [ tree ] {
        QList<QTreeWidgetItem*> rows;
        for ( int top = 0; top < tree->topLevelItemCount(); ++top ) {
            for ( int child = 0; child < tree->topLevelItem( top )->childCount(); ++child ) {
                rows.append( tree->topLevelItem( top )->child( child ) );
            }
        }
        return rows;
    };
    // The selected field, or every field of the selected item.
    const auto selectedRows = [ tree ] {
        auto* current = tree->currentItem();
        if ( !current ) {
            return QList<QTreeWidgetItem*>{};
        }
        if ( current->parent() ) {
            return QList<QTreeWidgetItem*>{ current };
        }
        QList<QTreeWidgetItem*> rows;
        for ( int child = 0; child < current->childCount(); ++child ) {
            rows.append( current->child( child ) );
        }
        return rows;
    };
    // Keep Both is for a preview as a whole: a field choice of one of its rows takes the whole
    // item out of it, the other rows back to Keep Mine (never a "both" row with no value, which
    // Apply would send as a removal).
    const auto leaveBoth = [ & ]( QTreeWidgetItem* row ) {
        auto* item = row->parent() ? row->parent() : row;
        for ( int child = 0; child < item->childCount(); ++child ) {
            auto* field = item->child( child );
            if ( field->data( 0, ChoiceRole ).toString() == QLatin1String( "both" ) ) {
                setChoice( field, "mine", field->data( 0, ConflictRole ).toMap().value( "mine" ).toString() );
            }
        }
    };
    const auto choose = [ & ]( const QList<QTreeWidgetItem*>& rows, bool mine ) {
        for ( auto* row : rows ) {
            leaveBoth( row );
            const auto conflict = row->data( 0, ConflictRole ).toMap();
            setChoice( row, mine ? "mine" : "theirs",
                       conflict.value( mine ? "mine" : "theirs" ).toString() );
        }
    };
    QObject::connect( keepMine, &QPushButton::clicked, &dialog, [ & ] { choose( selectedRows(), true ); } );
    QObject::connect( takeTheirs, &QPushButton::clicked, &dialog, [ & ] { choose( selectedRows(), false ); } );
    QObject::connect( allMine, &QPushButton::clicked, &dialog, [ & ] { choose( fieldRows(), true ); } );
    QObject::connect( allTheirs, &QPushButton::clicked, &dialog, [ & ] { choose( fieldRows(), false ); } );
    QObject::connect( editValue, &QPushButton::clicked, &dialog, [ & ] {
        auto* row = tree->currentItem();
        if ( !row || !row->parent() ) {
            return;
        }
        if ( row->data( 0, ConflictRole ).toMap().value( "omitted" ).toBool() ) {
            QMessageBox::information( &dialog, tr( "Edit Result" ),
                                      tr( "This item is too large to edit here: keep yours or take "
                                          "the new version as a whole." ) );
            return;
        }
        bool ok = false;
        const auto text = QInputDialog::getMultiLineText(
            &dialog, tr( "Edit Result" ), tr( "The field's value as JSON (empty removes it):" ),
            row->data( 0, ValueRole ).toString(), &ok );
        if ( !ok ) {
            return;
        }
        // Exactly one JSON value ("1,2" would parse as two).
        const auto parsed = QJsonDocument::fromJson( ( "[" + text + "]" ).toUtf8() );
        if ( !text.trimmed().isEmpty() && ( !parsed.isArray() || parsed.array().size() != 1 ) ) {
            QMessageBox::warning( &dialog, tr( "Edit Result" ), tr( "That is not one JSON value." ) );
            return;
        }
        leaveBoth( row );
        setChoice( row, "edit", text.trimmed() );
    } );
    QObject::connect( keepBoth, &QPushButton::clicked, &dialog, [ & ] {
        auto* row = tree->currentItem();
        auto* parentRow = row && row->parent() ? row->parent() : row;
        if ( !parentRow || parentRow->data( 0, ConflictRole ).toMap().value( "kind" ) != "preview" ) {
            QMessageBox::information( &dialog, tr( "Keep Both" ),
                                      tr( "Keep Both applies to previews." ) );
            return;
        }
        // Your version must exist to be kept: a preview you deleted has nothing to copy.
        const auto itemPath = parentRow->data( 0, ConflictRole ).toMap().value( "path" ).toString();
        if ( !items.value( itemPath ).toMap().value( "inMine" ).toBool() ) {
            QMessageBox::information( &dialog, tr( "Keep Both" ),
                                      tr( "You deleted this preview, so there is no version of "
                                          "yours to keep. Choose Take Theirs or Keep Mine." ) );
            return;
        }
        for ( int child = 0; child < parentRow->childCount(); ++child ) {
            setChoice( parentRow->child( child ), "both", {} );
        }
    } );

    auto* buttons = new QDialogButtonBox( &dialog );
    auto* applyButton = buttons->addButton( tr( "Apply" ), QDialogButtonBox::AcceptRole );
    applyButton->setObjectName( "packMergeApply" );
    auto* cancelButton = buttons->addButton( QDialogButtonBox::Cancel );
    cancelButton->setObjectName( "packMergeCancel" );
    layout->addWidget( buttons );
    QObject::connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
    QVariantList resolutions;
    QStringList keepBothPreviews;
    AfterPackMerge afterInstall;
    QObject::connect( buttons, &QDialogButtonBox::accepted, &dialog, [ & ] {
        resolutions.clear();
        keepBothPreviews.clear();
        afterInstall = {};
        for ( const auto& text : *textConflicts ) {
            QVariantMap resolution{ { "path", text.path } };
            if ( text.wholeOnly || !text.side->isEmpty() ) {
                // A whole side, byte for byte, as chosen: never read back from the editor.
                resolution.insert( "take", text.side->isEmpty() ? QStringLiteral( "mine" ) : *text.side );
                resolutions.append( resolution );
                continue;
            }
            auto value = text.result->toPlainText();
            if ( hasMarkers( value ) ) {
                tabs->setCurrentWidget( text.result->parentWidget() );
                QMessageBox::warning( &dialog, dialog.windowTitle(),
                                      tr( "%1 still has conflict markers." ).arg( text.item ) );
                return;
            }
            // The editor shows lines with LF only: a file whose sides use CRLF keeps CRLF.
            if ( text.mine.contains( QStringLiteral( "\r\n" ) )
                 || text.theirs.contains( QStringLiteral( "\r\n" ) ) ) {
                value.replace( QStringLiteral( "\r\n" ), QStringLiteral( "\n" ) );
                value.replace( QLatin1Char( '\n' ), QStringLiteral( "\r\n" ) );
            }
            resolution.insert( "text", value );
            resolutions.append( resolution );
        }
        for ( int top = 0; top < tree->topLevelItemCount(); ++top ) {
            auto* parentRow = tree->topLevelItem( top );
            const auto path = parentRow->data( 0, ConflictRole ).toMap().value( "path" ).toString();
            // Keep Both only when every row of the item has it (a field choice takes it out).
            bool both = parentRow->childCount() > 0;
            for ( int child = 0; both && child < parentRow->childCount(); ++child ) {
                both = parentRow->child( child )->data( 0, ChoiceRole ).toString() == QLatin1String( "both" );
            }
            if ( both ) {
                // The pack item becomes the new version as a whole, byte for byte (also one too
                // large to show); yours is copied once the update is installed.
                keepBothPreviews.append( parentRow->child( 0 )
                                             ->data( 0, ConflictRole )
                                             .toMap()
                                             .value( "item" )
                                             .toString() );
                resolutions.append( QVariantMap{ { "path", path }, { "take", "theirs" } } );
                continue;
            }
            for ( int child = 0; child < parentRow->childCount(); ++child ) {
                auto* row = parentRow->child( child );
                if ( row->data( 0, ConflictRole ).toMap().value( "omitted" ).toBool() ) {
                    // Too large to show: a whole side, as chosen.
                    resolutions.append( QVariantMap{
                        { "path", path },
                        { "take", row->data( 0, ChoiceRole ).toString() == QLatin1String( "theirs" )
                                      ? QStringLiteral( "theirs" )
                                      : QStringLiteral( "mine" ) } } );
                    continue;
                }
                const auto value = row->data( 0, ValueRole ).toString();
                QVariantMap resolution{ { "path", path },
                                        { "field", row->data( 0, ConflictRole ).toMap().value( "field" ) } };
                if ( value.isEmpty() ) {
                    resolution.insert( "remove", true );
                }
                else {
                    resolution.insert( "value",
                                       QJsonDocument::fromJson( ( "[" + value + "]" ).toUtf8() )
                                           .array()
                                           .first()
                                           .toVariant() );
                }
                resolutions.append( resolution );
            }
        }
        if ( !keepBothPreviews.isEmpty() ) {
            // Yours as it is now, enabled or not: the update replaces those previews, so the
            // copies are made from this snapshot, and only once the update is installed.
            const auto resultPack = result.value( "pluginId" ).toString();
            const auto packId = resultPack.isEmpty() ? job.value( "pluginId" ).toString() : resultPack;
            QString error;
            const auto snapshot = PreviewPackRegistry::instance().snapshotOfPack( packId, &error );
            if ( snapshot ) {
                error = checkKeepBoth( *snapshot, keepBothPreviews,
                                       result.value( "incomingPreviews" ).toStringList() );
            }
            else if ( error.isEmpty() ) {
                error = tr( "The pack is not available." );
            }
            if ( !error.isEmpty() ) {
                QMessageBox::warning( &dialog, tr( "Keep Both" ), error );
                return;
            }
            afterInstall = [ pack = *snapshot, names = keepBothPreviews ] {
                QStringList errors;
                for ( const auto& name : names ) {
                    const auto copyError = PreviewPackRegistry::instance().copyPreviewOfSnapshot(
                        pack, name, keepBothName( name ) );
                    if ( !copyError.isEmpty() ) {
                        errors.append( copyError );
                    }
                }
                return errors.join( QLatin1Char( ' ' ) );
            };
        }
        dialog.accept();
    } );
    dialog.resize( 980, 640 );
    if ( dialog.exec() != QDialog::Accepted ) {
        return false;
    }
    LOG_INFO << "Pack merge applied resolutions=" << resolutions.size()
             << " keep_both=" << keepBothPreviews.size();
    apply( resolutions, afterInstall );
    return true;
}
} // namespace pluginsui
