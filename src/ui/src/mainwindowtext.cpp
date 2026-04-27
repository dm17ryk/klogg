//
// Created by marvin on 23-4-26.
//

#include "mainwindowtext.h"

#include <QtGlobal>

using namespace klogg::mainwindow;

// menu
const char* menu::fileTitle = QT_TR_NOOP( "&File" );
const char* menu::editTitle = QT_TR_NOOP( "&Edit" );
const char* menu::viewTitle = QT_TR_NOOP( "&View" );
const char* menu::openedFilesTitle = QT_TR_NOOP( "Opened files" );
const char* menu::toolsTitle = QT_TR_NOOP( "&Tools" );
const char* menu::highlightersTitle = QT_TR_NOOP( "Highlighters" );
const char* menu::favoritesTitle = QT_TR_NOOP( "F&avorites" );
const char* menu::helpTitle = QT_TR_NOOP( "&Help" );
const char* menu::encodingTitle = QT_TR_NOOP( "E&ncoding" );

// toolbar
const char* toolbar::toolbarTitle = QT_TR_NOOP( "&Toolbar" );

// trayicon
const char* trayicon::trayiconTip = QT_TR_NOOP( "CILogg log viewer" );

// action
const char* action::newWindowText = QT_TR_NOOP( "&New window" );
const char* action::newWindowStatusTip = QT_TR_NOOP( "Create new CILogg window" );
const char* action::openText = QT_TR_NOOP( "&Open..." );
const char* action::openStatusTip = QT_TR_NOOP( "Open a file" );
const char* action::openComPortText = QT_TR_NOOP( "Open COM port..." );
const char* action::openComPortStatusTip = QT_TR_NOOP( "Open a COM port stream" );
const char* action::recentFilesCleanupText = QT_TR_NOOP("Clear List");
const char* action::closeText = QT_TR_NOOP( "&Close" );
const char* action::closeStatusTip = QT_TR_NOOP( "Close document" );
const char* action::closeAllText = QT_TR_NOOP( "Close &All" );
const char* action::closeAllStatusTip = QT_TR_NOOP( "Close all documents" );
const char* action::exitText = QT_TR_NOOP( "E&xit" );
const char* action::exitStatusTip = QT_TR_NOOP( "Exit the application" );
const char* action::copyText = QT_TR_NOOP( "&Copy" );
const char* action::copyStatusTip = QT_TR_NOOP( "Copy the selection" );
const char* action::selectAllText = QT_TR_NOOP( "Select &All" );
const char* action::selectAllStatusTip = QT_TR_NOOP( "Select all the text" );
const char* action::goToLineText = QT_TR_NOOP( "Go to line..." );
const char* action::goToLineStatusTip
    = QT_TR_NOOP( "Scrolls selected main view to specified line" );
const char* action::findText = QT_TR_NOOP( "&Find..." );
const char* action::findStatusTip = QT_TR_NOOP( "Find the text" );
const char* action::clearLogText = QT_TR_NOOP( "Clear file..." );
const char* action::clearLogStatusTip = QT_TR_NOOP( "Clear current file" );
const char* action::openContainingFolderText = QT_TR_NOOP( "Open containing folder" );
const char* action::openContainingFolderStatusTip
    = QT_TR_NOOP( "Open folder containing current file" );
const char* action::openInEditorText = QT_TR_NOOP( "Open in editor" );
const char* action::openInEditorStatusTip = QT_TR_NOOP( "Open current file in default editor" );
const char* action::copyPathToClipboardText = QT_TR_NOOP( "Copy full path" );
const char* action::copyPathToClipboardStatusTip
    = QT_TR_NOOP( "Copy full path for file to clipboard" );
const char* action::openClipboardText = QT_TR_NOOP( "Open from clipboard" );
const char* action::openClipboardStatusTip = QT_TR_NOOP( "Open clipboard as log file" );
const char* action::openUrlText = QT_TR_NOOP( "Open from URL..." );
const char* action::openUrlStatusTip = QT_TR_NOOP( "Open URL as log file" );
const char* action::overviewVisibleText = QT_TR_NOOP( "Matches &overview" );
const char* action::lineNumbersVisibleInMainText = QT_TR_NOOP( "Line &numbers in main view" );
const char* action::lineNumbersVisibleInFilteredText
    = QT_TR_NOOP( "Line &numbers in filtered view" );
const char* action::followText = QT_TR_NOOP( "&Follow File" );
const char* action::wrapText = QT_TR_NOOP( "&Wrap text" );
const char* action::showTabsBarText = QT_TR_NOOP( "Show tabs bar" );
const char* action::reloadText = QT_TR_NOOP( "&Reload" );
const char* action::stopText = QT_TR_NOOP( "&Stop" );
const char* action::optionsText = QT_TR_NOOP( "&Preferences..." );
const char* action::optionsStatusTip = QT_TR_NOOP( "Show application settings dialog" );
const char* action::editHighlightersText = QT_TR_NOOP( "Configure &highlighters..." );
const char* action::editHighlightersStatusTip = QT_TR_NOOP( "Show highlighters configuration" );
const char* action::showDocumentationText = QT_TR_NOOP( "&Documentation..." );
const char* action::showDocumentationStatusTip = QT_TR_NOOP( "Show documentation" );
const char* action::aboutText = QT_TR_NOOP( "&About" );
const char* action::aboutStatusTip = QT_TR_NOOP( "Show the About box" );
const char* action::aboutQtText = QT_TR_NOOP( "About &Qt" );
const char* action::aboutQtStatusTip = QT_TR_NOOP( "Show the Qt library's About box" );
const char* action::reportIssueText = QT_TR_NOOP( "Report issue..." );
const char* action::reportIssueStatusTip = QT_TR_NOOP( "Report an issue on GitHub" );
const char* action::joinDiscordText = QT_TR_NOOP( "Join Discord community..." );
const char* action::joinDiscordStatusTip
    = QT_TR_NOOP( "Join CILogg development community at Discord" );
const char* action::joinTelegramText = QT_TR_NOOP( "Join Telegram community..." );
const char* action::joinTelegramStatusTip
    = QT_TR_NOOP( "Join CILogg development community at Telegram" );
const char* action::generateDumpText = QT_TR_NOOP( "Generate crash dump" );
const char* action::generateDumpStatusTip = QT_TR_NOOP( "Generate diagnostic crash dump" );
const char* action::showScratchPadText = QT_TR_NOOP( "Scratchpad" );
const char* action::showScratchPadStatusTip = QT_TR_NOOP( "Show the scratchpad" );
const char* action::showPreviewerText = QT_TR_NOOP( "Previewer" );
const char* action::showPreviewerStatusTip = QT_TR_NOOP( "Show the previewer" );
const char* action::showActionsResponsesText = QT_TR_NOOP( "Actions/Responses" );
const char* action::showActionsResponsesStatusTip
    = QT_TR_NOOP( "Show actions and responses" );
const char* action::showScriptRunnerText = QT_TR_NOOP( "Script Runner" );
const char* action::showScriptRunnerStatusTip = QT_TR_NOOP( "Show the Python script runner" );
const char* action::showScenarioRunnerText = QT_TR_NOOP( "Scenario Runner" );
const char* action::showScenarioRunnerStatusTip = QT_TR_NOOP( "Show the Python scenario runner" );
const char* action::showLabQueueText = QT_TR_NOOP( "Lab Queue" );
const char* action::showLabQueueStatusTip = QT_TR_NOOP( "Show the remote lab queue monitor" );
const char* action::addToFavoritesText = QT_TR_NOOP( "Add to favorites" );
const char* action::removeFromFavoritesText = QT_TR_NOOP( "Remove from favorites..." );
const char* action::selectOpenFileText = QT_TR_NOOP( "Switch to opened file..." );
const char* action::predefinedFiltersDialogText = QT_TR_NOOP( "Predefined filters..." );
const char* action::predefinedFiltersDialogStatusTip
    = QT_TR_NOOP( "Show dialog to configure filters" );
const char* action::importPreviewsDialogText = QT_TR_NOOP( "Import previews..." );
const char* action::importPreviewsDialogStatusTip
    = QT_TR_NOOP( "Import structured preview definitions" );
const char* action::importActionsDialogText = QT_TR_NOOP( "Import actions..." );
const char* action::importActionsDialogStatusTip
    = QT_TR_NOOP( "Import actions and responses definitions" );
const char* action::exportActionsDialogText = QT_TR_NOOP( "Export actions..." );
const char* action::exportActionsDialogStatusTip
    = QT_TR_NOOP( "Export actions and responses definitions" );
const char* action::autoEncodingText = QT_TR_NOOP( "Auto" );
const char* action::autoEncodingStatusTip
    = QT_TR_NOOP( "Automatically detect the file's encoding" );
                             
