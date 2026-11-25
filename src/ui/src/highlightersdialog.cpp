/*
 * Copyright (C) 2009, 2010 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2019 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QFileDialog>
#include <QMessageBox>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <qboxlayout.h>
#include <qcheckbox.h>
#include <qcolor.h>
#include <qlineedit.h>
#include <qnamespace.h>
#include <qpushbutton.h>
#include <utility>

#include "configurationexportdialog.h"
#include "containers.h"
#include "dispatch_to.h"
#include "highlightersdialog.h"
#include "highlighterset.h"
#include "highlightersetedit.h"
#include "iconloader.h"
#include "log.h"
#include "packcanonical.h"
#include "ui_highlightersdialog.h"

static constexpr QLatin1String DEFAULT_NAME = QLatin1String( "New Highlighter set", 19 );

namespace {
// On a pack row: whether one of the user's sets with its id replaces it.
constexpr int ReplacedRole = Qt::UserRole + 1;
// On a pack row: its set's id (the user's rows have none).
constexpr int PackSetRole = Qt::UserRole + 2;
} // namespace

// Construct the box, including a copy of the global highlighterSet
// to handle ok/cancel/apply
HighlightersDialog::HighlightersDialog( QWidget* parent )
    : QDialog( parent )
    , ui_( std::make_unique<Ui::HighlightersDialog>() )
{
    ui_->setupUi( this );

    // Configuration pack highlighter sets (E16c2): copied into the user's own, edited in place,
    // and the packs themselves in the Plugins and Packs window.
    const auto textButton = [ this ]( const char* name, const QString& text, const QString& tip ) {
        auto* button = new QToolButton( this );
        button->setObjectName( QLatin1String( name ) );
        button->setText( text );
        button->setAccessibleName( text );
        button->setToolTip( tip );
        button->setToolButtonStyle( Qt::ToolButtonTextOnly );
        return button;
    };
    copyPackSetButton_ = textButton(
        "copyPackSetButton", tr( "Copy to My Highlighters" ),
        tr( "Copy this pack highlighter set into your own sets. Your copy replaces the pack's, "
            "and updates of the pack do not change it." ) );
    editPackSetButton_ = textButton(
        "editPackSetButton", tr( "Edit..." ),
        tr( "Edit this pack highlighter set in place. Updates of the pack keep your edit and ask "
            "when they change the same lines." ) );
    packsButton_ = textButton( "packsButton", tr( "Packs..." ),
                               tr( "Install, update and manage configuration packs." ) );
    copyPackSetButton_->setEnabled( false );
    editPackSetButton_->setVisible( false );
    packsButton_->setVisible( false );
    auto* packButtons = new QHBoxLayout;
    packButtons->addWidget( copyPackSetButton_ );
    packButtons->addWidget( editPackSetButton_ );
    packButtons->addWidget( packsButton_ );
    packButtons->addStretch();
    ui_->verticalLayout->addLayout( packButtons );
    connect( copyPackSetButton_, &QToolButton::clicked, this, &HighlightersDialog::copyPackSet );
    connect( editPackSetButton_, &QToolButton::clicked, this, &HighlightersDialog::editPackSet );
    connect( packsButton_, &QToolButton::clicked, this, [ this ] {
        if ( packsOpener_ ) {
            packsOpener_();
        }
    } );
    // A pack enabled, disabled, updated or edited meanwhile: its rows follow.
    connect( &PackHighlighters::instance(), &PackHighlighters::changed, this,
             &HighlightersDialog::refreshPackRows );

    highlighterSetEdit_ = new HighlighterSetEdit( this );
    highlighterSetEdit_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    ui_->highlighterLayout->addWidget( highlighterSetEdit_ );

    ui_->splitter->setStretchFactor( 0, 0 );
    ui_->splitter->setStretchFactor( 1, 1 );

    // Reload the highlighter list from disk (in case it has been changed
    // by another glogg instance) and copy it to here.
    highlighterSetCollection_ = HighlighterSetCollection::getSynced();

    populateHighlighterList();

    // Start with all buttons disabled except 'add'
    ui_->removeHighlighterButton->setEnabled( false );
    ui_->upHighlighterButton->setEnabled( false );
    ui_->downHighlighterButton->setEnabled( false );

    connect( ui_->addHighlighterButton, &QToolButton::clicked, this,
             &HighlightersDialog::addHighlighterSet );
    connect( ui_->removeHighlighterButton, &QToolButton::clicked, this,
             &HighlightersDialog::removeHighlighterSet );

    connect( ui_->upHighlighterButton, &QToolButton::clicked, this,
             &HighlightersDialog::moveHighlighterSetUp );
    connect( ui_->downHighlighterButton, &QToolButton::clicked, this,
             &HighlightersDialog::moveHighlighterSetDown );

    connect( ui_->exportButton, &QPushButton::clicked, this,
             &HighlightersDialog::exportHighlighters );
    connect( ui_->importButton, &QPushButton::clicked, this,
             &HighlightersDialog::importHighlighters );

    // No highlighter selected by default
    selectedRow_ = -1;

    connect( ui_->highlighterListWidget, &QListWidget::itemSelectionChanged, this,
             &HighlightersDialog::updatePropertyFields );

    connect( highlighterSetEdit_, &HighlighterSetEdit::changed, this,
             &HighlightersDialog::updateHighlighterProperties );

    connect( ui_->buttonBox, &QDialogButtonBox::clicked, this, &HighlightersDialog::resolveDialog );

    if ( !highlighterSetCollection_.highlighters_.empty() ) {
        setCurrentRow( 0 );
    }

    ui_->quickHighlightLayout->removeWidget( ui_->colorLabelsPlaceholder );
    ui_->quickHighlightLayout->removeWidget( ui_->colorLabelsForeColor );
    ui_->quickHighlightLayout->removeWidget( ui_->colorLabelsBackColor );
    ui_->quickHighlightLayout->removeWidget( ui_->colorLabelsCycle );

    ui_->quickHighlightLayout->addWidget( ui_->colorLabelsPlaceholder, 0, 0 );
    ui_->quickHighlightLayout->addWidget( ui_->colorLabelsForeColor, 0, 1, Qt::AlignCenter );
    ui_->quickHighlightLayout->addWidget( ui_->colorLabelsBackColor, 0, 2, Qt::AlignCenter );
    ui_->quickHighlightLayout->addWidget( ui_->colorLabelsCycle, 0, 3, Qt::AlignCenter );

    const auto quickHighlighters = highlighterSetCollection_.quickHighlighters();
    for ( int i = 0; i < quickHighlighters.size(); ++i ) {
        const auto row = i + 1;
        auto nameEdit = new QLineEdit( quickHighlighters[ i ].name );
        auto foreButton = new QPushButton;
        auto backButton = new QPushButton;
        auto cycleCheckbox = new QCheckBox;

        HighlighterEdit::updateIcon( foreButton, quickHighlighters[ i ].color.foreColor );
        HighlighterEdit::updateIcon( backButton, quickHighlighters[ i ].color.backColor );
        cycleCheckbox->setChecked( quickHighlighters[ i ].useInCycle );

        connect( nameEdit, &QLineEdit::textChanged, nameEdit,
                 [ this, index = i ]( const QString& newName ) {
                     auto highlighters = highlighterSetCollection_.quickHighlighters();
                     if ( !newName.isEmpty() ) {
                         highlighters[ index ].name = newName;
                         highlighterSetCollection_.setQuickHighlighters( highlighters );
                     }
                 } );

        connect( foreButton, &QPushButton::clicked, foreButton, [ foreButton, this, index = i ]() {
            auto highlighters = highlighterSetCollection_.quickHighlighters();
            QColor newColor;
            if ( HighlighterEdit::showColorPicker( highlighters[ index ].color.foreColor,
                                                   newColor ) ) {
                highlighters[ index ].color.foreColor = newColor;
                highlighterSetCollection_.setQuickHighlighters( highlighters );
                HighlighterEdit::updateIcon( foreButton, newColor );
            }
        } );

        connect( backButton, &QPushButton::clicked, backButton, [ backButton, this, index = i ]() {
            auto highlighters = highlighterSetCollection_.quickHighlighters();
            QColor newColor;
            if ( HighlighterEdit::showColorPicker( highlighters[ index ].color.backColor,
                                                   newColor ) ) {
                highlighters[ index ].color.backColor = newColor;
                highlighterSetCollection_.setQuickHighlighters( highlighters );
                HighlighterEdit::updateIcon( backButton, newColor );
            }
        } );

        connect( cycleCheckbox, &QCheckBox::clicked, cycleCheckbox,
                 [ this, index = i ]( bool isChecked ) {
                     auto highlighters = highlighterSetCollection_.quickHighlighters();
                     highlighters[ index ].useInCycle = isChecked;
                     highlighterSetCollection_.setQuickHighlighters( highlighters );
                 } );

        ui_->quickHighlightLayout->addWidget( nameEdit, row, 0 );
        ui_->quickHighlightLayout->addWidget( foreButton, row, 1 );
        ui_->quickHighlightLayout->addWidget( backButton, row, 2 );
        ui_->quickHighlightLayout->addWidget( cycleCheckbox, row, 3, Qt::AlignCenter );
    }

    dispatchToObject( [ this ] {
        IconLoader iconLoader( this );

        ui_->addHighlighterButton->setIcon( iconLoader.load( "icons8-plus-16" ) );
        ui_->removeHighlighterButton->setIcon( iconLoader.load( "icons8-minus-16" ) );
        ui_->upHighlighterButton->setIcon( iconLoader.load( "icons8-up-16" ) );
        ui_->downHighlighterButton->setIcon( iconLoader.load( "icons8-down-arrow-16" ) );
    }, this );
}

HighlightersDialog::~HighlightersDialog() = default;

void HighlightersDialog::setPackItemEditor( PackItemEditor editor )
{
    packItemEditor_ = std::move( editor );
    editPackSetButton_->setVisible( static_cast<bool>( packItemEditor_ ) );
    updatePackButtons();
}

void HighlightersDialog::setPacksOpener( std::function<void()> opener )
{
    packsOpener_ = std::move( opener );
    packsButton_->setVisible( static_cast<bool>( packsOpener_ ) );
}

//
// Q_SLOTS:
//

void HighlightersDialog::exportHighlighters()
{
    QStringList names;
    for ( const auto& set : highlighterSetCollection_.highlighterSets() ) {
        names.append( set.name() );
    }
    ConfigurationExportDialog dialog( ConfigurationExportKind::Highlights, names, this );
    if ( dialog.exec() != QDialog::Accepted ) {
        LOG_DEBUG << "Highlight export selection cancelled";
        return;
    }
    QString error;
    QStringList savedFiles;
    bool saved = false;
    if ( dialog.multipleFiles() ) {
        const auto directory
            = QFileDialog::getExistingDirectory( this, tr( "Export highlight groups" ) );
        if ( directory.isEmpty() ) {
            return;
        }
        saved = ConfigurationExport::exportHighlightFiles(
            highlighterSetCollection_, dialog.selectedRows(), directory, &error, &savedFiles );
    }
    else {
        auto file = QFileDialog::getSaveFileName( this, tr( "Export highlights" ), {},
                                                  tr( "Highlighters (*.conf)" ) );
        if ( file.isEmpty() ) {
            return;
        }
        if ( !file.endsWith( ".conf", Qt::CaseInsensitive ) ) {
            file += ".conf";
        }
        saved = ConfigurationExport::exportSelected( ConfigurationExportKind::Highlights,
                                                     dialog.selectedRows(), file, &error,
                                                     &highlighterSetCollection_ );
        if ( saved ) {
            savedFiles.append( file );
        }
    }
    if ( !savedFiles.isEmpty() ) {
        Q_EMIT configurationsExported( savedFiles );
    }
    if ( !saved ) {
        QMessageBox::warning( this, tr( "Export highlights" ), error );
    }
}

void HighlightersDialog::importHighlighters()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr( "Select one or more files to open" ), "", tr( "Highlighters (*.conf)" ) );

    for ( const auto& file : files ) {
        LOG_INFO << "Loading highlighters from " << file;
        QSettings settings{ file, QSettings::IniFormat };
        HighlighterSetCollection collection;
        collection.retrieveFromStorage( settings );
        for ( const auto& set : klogg::as_const( collection.highlighters_ ) ) {
            if ( highlighterSetCollection_.hasSet( set.id() )
                 || highlighterSetCollection_.hasSetByName( set.name() ) ) {

                LOG_INFO << "Skipping set " << set.name() << " (" << set.id() << ")";

                continue;
            }

            LOG_INFO << "Adding set " << set.name() << " (" << set.id() << ")";

            // The user's sets come before the pack rows.
            const auto row = ownRowCount();
            const QSignalBlocker blocker( ui_->highlighterListWidget );
            highlighterSetCollection_.highlighters_.append( set );
            ui_->highlighterListWidget->insertItem( row, set.name() );
        }
    }
    // An imported set with a pack set's id replaces it.
    refreshPackRows();
}

void HighlightersDialog::addHighlighterSet()
{
    LOG_DEBUG << "addHighlighter()";

    // Add and select the newly created highlighter, after the user's sets and before the pack rows
    const auto row = ownRowCount();
    {
        const QSignalBlocker blocker( ui_->highlighterListWidget );
        highlighterSetCollection_.highlighters_.append(
            HighlighterSet::createNewSet( DEFAULT_NAME ) );
        ui_->highlighterListWidget->insertItem( row, DEFAULT_NAME );
    }

    setCurrentRow( row );
}

void HighlightersDialog::removeHighlighterSet()
{
    int index = ui_->highlighterListWidget->currentRow();
    LOG_DEBUG << "removeHighlighter() index " << index;

    // Only the user's sets are removed here; a pack's set goes with its pack.
    if ( index >= 0 && index < ownRowCount() ) {
        setCurrentRow( -1 );
        dispatchToObject(
            [ this, index ] {
                if ( index >= ownRowCount() ) {
                    return;
                }
                {
                    // A copy of a pack set keeps its id: removed, the pack's set is listed again
                    // and stays active if the copy was (as Copy to My Highlighters keeps it).
                    const auto& set = highlighterSetCollection_.highlighters_.at( index );
                    const auto id = set.id();
                    const bool packHasIt
                        = std::any_of( packRows_.cbegin(), packRows_.cend(),
                                       [ &id ]( const ListedHighlighterSet& listed ) {
                                           return listed.set.id() == id;
                                       } );
                    if ( !packHasIt ) {
                        highlighterSetCollection_.deactivateSet( id );
                    }
                }

                {
                    const QSignalBlocker blocker( ui_->highlighterListWidget );
                    highlighterSetCollection_.highlighters_.removeAt( index );
                    delete ui_->highlighterListWidget->takeItem( index );
                }
                // The pack set it replaced, if any, is listed again.
                refreshPackRows();

                int count = ownRowCount();
                if ( index < count ) {
                    // Select the new item at the same index
                    setCurrentRow( index );
                }
                else {
                    // or the previous index if it is at the end
                    setCurrentRow( count - 1 );
                }
            },
            this );
    }
}

void HighlightersDialog::moveHighlighterSetUp()
{
    int index = ui_->highlighterListWidget->currentRow();
    LOG_DEBUG << "moveHighlighterUp() index " << index;

    // The user's sets move among themselves; the pack rows follow them in the packs' order.
    if ( index > 0 && index < ownRowCount() ) {
        highlighterSetCollection_.highlighters_.move( index, index - 1 );

        dispatchToObject(
            [ this, index ] {
                QListWidgetItem* item = ui_->highlighterListWidget->takeItem( index );
                ui_->highlighterListWidget->insertItem( index - 1, item );

                setCurrentRow( index - 1 );
            },
            this );
    }
}

void HighlightersDialog::moveHighlighterSetDown()
{
    int index = ui_->highlighterListWidget->currentRow();
    LOG_DEBUG << "moveHighlighterDown() index " << index;

    if ( ( index >= 0 ) && ( index < ( ownRowCount() - 1 ) ) ) {
        highlighterSetCollection_.highlighters_.move( index, index + 1 );

        dispatchToObject(
            [ this, index ] {
                QListWidgetItem* item = ui_->highlighterListWidget->takeItem( index );
                ui_->highlighterListWidget->insertItem( index + 1, item );

                setCurrentRow( index + 1 );
            },
            this );
    }
}

void HighlightersDialog::resolveDialog( QAbstractButton* button )
{
    LOG_DEBUG << "resolveDialog()";

    QDialogButtonBox::ButtonRole role = ui_->buttonBox->buttonRole( button );
    if ( role == QDialogButtonBox::RejectRole ) {
        reject();
        return;
    }

    // persist it to disk
    auto& persistentHighlighterSet = HighlighterSetCollection::get();
    if ( role == QDialogButtonBox::AcceptRole ) {
        persistentHighlighterSet = std::move( highlighterSetCollection_ );
        accept();
    }
    else if ( role == QDialogButtonBox::ApplyRole ) {
        persistentHighlighterSet = highlighterSetCollection_;
    }
    else {
        LOG_ERROR << "unhandled role : " << role;
        return;
    }
    // The active sets highlight with the rules edited here and the packs' current ones (E16c2).
    persistentHighlighterSet.refreshPackSets();
    persistentHighlighterSet.save();
    Q_EMIT optionsChanged();
}

void HighlightersDialog::setCurrentRow( int row )
{
    // ugly hack for mac
    dispatchToObject( [ this, row ]() { ui_->highlighterListWidget->setCurrentRow( row ); }, this );
}

void HighlightersDialog::updatePropertyFields()
{
    if ( ui_->highlighterListWidget->selectedItems().count() >= 1 )
        selectedRow_ = ui_->highlighterListWidget->row(
            ui_->highlighterListWidget->selectedItems().at( 0 ) );
    else
        selectedRow_ = -1;

    LOG_DEBUG << "updatePropertyFields(), row = " << selectedRow_;

    const auto own = ownRowCount();
    if ( selectedRow_ >= 0 && selectedRow_ < own ) {
        const HighlighterSet& currentSet
            = highlighterSetCollection_.highlighters_.at( selectedRow_ );
        highlighterSetEdit_->setEnabled( true );
        highlighterSetEdit_->setHighlighters( currentSet );

        // Enable the buttons if needed
        ui_->removeHighlighterButton->setEnabled( true );
        ui_->upHighlighterButton->setEnabled( selectedRow_ > 0 );
        ui_->downHighlighterButton->setEnabled( selectedRow_ < ( own - 1 ) );
    }
    else if ( const auto* pack = packSetAt( selectedRow_ ) ) {
        // A pack's set is shown read only: Edit... changes it in the pack, Copy to My
        // Highlighters makes it the user's.
        highlighterSetEdit_->setHighlighters( pack->set );
        highlighterSetEdit_->setEnabled( false );

        ui_->removeHighlighterButton->setEnabled( false );
        ui_->upHighlighterButton->setEnabled( false );
        ui_->downHighlighterButton->setEnabled( false );
    }
    else {
        highlighterSetEdit_->setEnabled( true );
        highlighterSetEdit_->reset();

        ui_->removeHighlighterButton->setEnabled( false );
        ui_->upHighlighterButton->setEnabled( false );
        ui_->downHighlighterButton->setEnabled( false );
    }
    updatePackButtons();
}

void HighlightersDialog::updateHighlighterProperties()
{
    LOG_DEBUG << "updateHighlighterProperties()";

    // If one of the user's rows is selected (a pack row is read only)
    if ( selectedRow_ >= 0 && selectedRow_ < ownRowCount() ) {
        HighlighterSet& currentSet = highlighterSetCollection_.highlighters_[ selectedRow_ ];
        currentSet = highlighterSetEdit_->highlighters();
        // Update the entry in the highlighterList widget
        if ( auto* item = ui_->highlighterListWidget->item( selectedRow_ ) ) {
            item->setText( currentSet.name() );
        }
    }
}

//
// Private functions
//

void HighlightersDialog::populateHighlighterList()
{
    {
        const QSignalBlocker blocker( ui_->highlighterListWidget );
        ui_->highlighterListWidget->clear();
        packRows_.clear();
        for ( const HighlighterSet& highlighterSet :
              klogg::as_const( highlighterSetCollection_.highlighters_ ) ) {
            auto* new_item = new QListWidgetItem( highlighterSet.name() );
            // new_item->setFlags( Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsEnabled );
            ui_->highlighterListWidget->addItem( new_item );
        }
    }
    refreshPackRows();
}

int HighlightersDialog::ownRowCount() const
{
    return static_cast<int>( highlighterSetCollection_.highlighters_.size() );
}

const ListedHighlighterSet* HighlightersDialog::packSetAt( int row ) const
{
    const auto index = row - ownRowCount();
    if ( row < 0 || index < 0 || index >= packRows_.size() ) {
        return nullptr;
    }
    return &packRows_.at( index );
}

bool HighlightersDialog::isReplacedRow( int row ) const
{
    const auto* item = ui_->highlighterListWidget->item( row );
    return packSetAt( row ) && item && item->data( ReplacedRole ).toBool();
}

void HighlightersDialog::refreshPackRows()
{
    auto* list = ui_->highlighterListWidget;
    const auto own = ownRowCount();
    // The pack set selected stays selected (by its id: rows of the user's may have been added).
    const auto before = list->selectedItems();
    const auto selectedPackSet
        = before.isEmpty() ? QString{} : before.first()->data( PackSetRole ).toString();
    {
        const QSignalBlocker blocker( list );
        while ( list->count() > own ) {
            delete list->takeItem( list->count() - 1 );
        }
        packRows_.clear();

        // A set of the user's with a pack set's id replaces it (design 4.5).
        QSet<QString> ownIds;
        for ( const auto& set : highlighterSetCollection_.highlighters_ ) {
            ownIds.insert( set.id() );
        }
        const auto& packSets = PackHighlighters::instance().sets();
        // Past the limit with these sets of the user's, the packs' sets give way in the menu and
        // the highlighting (HighlighterSetCollection::listedSets): they say so here.
        const auto listedPackSets = std::count_if(
            packSets.cbegin(), packSets.cend(), [ &ownIds ]( const ListedHighlighterSet& listed ) {
                return !ownIds.contains( listed.set.id() );
            } );
        const bool givingWay = own + listedPackSets > MaxListedHighlighterSets;
        for ( const auto& listed : packSets ) {
            const bool replaced = ownIds.contains( listed.set.id() );
            const auto& name = listed.set.name();
            auto* item = new QListWidgetItem(
                replaced ? tr( "%1 (%2, replaced by your set)" ).arg( name, listed.packName )
                : givingWay
                    ? tr( "%1 (%2, not listed: more than %3 sets)" )
                          .arg( name, listed.packName, QString::number( MaxListedHighlighterSets ) )
                    : tr( "%1 (%2)" ).arg( name, listed.packName ) );
            item->setData( ReplacedRole, replaced );
            item->setData( PackSetRole, listed.set.id() );
            item->setToolTip(
                replaced    ? tr( "Your highlighter set with the same id replaces this pack set." )
                : givingWay ? tr( "Your highlighter sets and the packs' would be more than %1, "
                                  "so the packs' sets are not listed until you have fewer." )
                                  .arg( MaxListedHighlighterSets )
                            : tr( "From the configuration pack \"%1\". Edit it in place or copy "
                                  "it to your own highlighter sets." )
                                  .arg( listed.packName ) );
            auto font = item->font();
            font.setItalic( true );
            item->setFont( font );
            list->addItem( item );
            packRows_.append( listed );
            if ( !selectedPackSet.isEmpty() && listed.set.id() == selectedPackSet ) {
                list->setCurrentItem( item );
            }
        }
    }
    // The pack set shown in the editor may have changed or gone, or the selected row moved.
    const auto after = list->selectedItems();
    const auto row = after.isEmpty() ? -1 : list->row( after.first() );
    if ( !selectedPackSet.isEmpty() || row != selectedRow_ ) {
        updatePropertyFields();
    }
    else {
        updatePackButtons();
    }
}

void HighlightersDialog::updatePackButtons()
{
    const bool packRow = packSetAt( selectedRow_ ) && !isReplacedRow( selectedRow_ );
    copyPackSetButton_->setEnabled( packRow );
    editPackSetButton_->setEnabled( packRow && static_cast<bool>( packItemEditor_ ) );
}

void HighlightersDialog::copyPackSet()
{
    const auto row = selectedRow_;
    const auto* pack = packSetAt( row );
    if ( !pack || isReplacedRow( row ) ) {
        return;
    }
    // The user's copy keeps the id, so it replaces the pack's set and stays active if the pack's
    // was (as Copy to My Previews keeps the name); it is saved with the user's other sets.
    const auto copy = pack->set;
    const auto target = ownRowCount();
    {
        const QSignalBlocker blocker( ui_->highlighterListWidget );
        highlighterSetCollection_.highlighters_.append( copy );
        ui_->highlighterListWidget->insertItem( target, copy.name() );
    }
    refreshPackRows();
    setCurrentRow( target );
}

void HighlightersDialog::editPackSet()
{
    const auto* pack = packSetAt( selectedRow_ );
    if ( !packItemEditor_ || !pack || isReplacedRow( selectedRow_ ) ) {
        return;
    }
    // Copied: the editor's save changes the pack's sets, which rebuilds the pack rows.
    const auto packId = pack->packId;
    const auto path = QStringLiteral( "highlights/%1.set" )
                          .arg( cilogg::plugins::packcanonical::encodeName( pack->set.id() ) );
    packItemEditor_( this, packId, path );
}
