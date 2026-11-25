add_library(
  klogg_ui STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include/abstractlogview.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/actionsconfig.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/actionexpression.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/actionsconfigparser.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/actionsmanager.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/actionsrepository.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/actionsresponseswindow.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/actionstablemodel.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/actioneditdialog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/actionparametersdialog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/actionparameterdefinitiondialog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/actionparameterseditorwidget.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/actionruntime.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/comportutils.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/configurationexport.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/configurationexportdialog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/crawlerwidget.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/filteredview.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/matchlabeler.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/panemenuhost.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/searchbarlayout.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/highlightersdialog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/highlighteredit.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/highlightersetedit.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/highlighterset.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/highlightersmenu.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/highlightedmatch.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/predefinedfilters.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/checkcombobox.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/predefinedfilterscombobox.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/predefinedfiltersdialog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/infoline.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/labqueuewindow.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pathline.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/logmainview.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/mainwindow.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/regexlabdock.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/analysissource.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/analysissource.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/analysisruns.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/analysisruns.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginsourceruns.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginsourceruns.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/analysistextedit.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/analysisstatus.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/valuecountdock.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/valuecountdock.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/mainwindowvaluecounts.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/regexlabdock.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginuicontroller.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginuicontroller.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginsandpacks.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginsandpacks.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginsourcesection.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginsourcesection.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/packeditors.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/packeditors.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginupdatenotices.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginupdatenotices.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/previewpackregistry.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/previewpackregistry.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginview.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginview.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/plugintablemodel.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/plugintablemodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/plugintooltips.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/plugintooltips.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginicons.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginicons.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginchoicewidgets.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginchoicewidgets.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/plugintablebehaviour.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/plugintablebehaviour.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/plugintree.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/plugintree.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginfiletree.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginfiletree.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginviewtrees.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginlayout.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginlayout.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/plugintextview.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/plugintextview.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginchart.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginchart.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginviewlayout.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginmenuhost.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginmenus.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginmenus.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pluginviewhost.h
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pluginviewhost.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/mainwindowplugins.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/uiautomation.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/mainwindowtext.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/menu.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/opencomportdialog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/optionsdialog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/updatedialog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/updateuicontroller.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/overview.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/overviewwidget.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/previewconfig.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/previewconfigparser.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/previewdecodeutils.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/previewmanager.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/previewmessagetab.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/previewassembly.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/previewservice.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/previewdecodersettings.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/previewdecodersettingsdialog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/tsharkpreviewclient.h
  ${CMAKE_CURRENT_SOURCE_DIR}/preview_decoder.qrc
  ${CMAKE_CURRENT_SOURCE_DIR}/include/previewsource.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/previewtree.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/previewrepository.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/previewstablemodel.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/previewwindow.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/importactionsdialog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/importpreviewsdialog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/qfnotifications.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/quickfind.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/quickfindmux.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/quickfindpattern.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/quickfindwidget.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/recentfiles.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/responseeditdialog.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/responsestablemodel.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/scenariorunner.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/scenariorunnerwindow.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/savedsearches.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/selection.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/session.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/sessioninfo.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/signalmux.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/serialcaptureworker.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/scriptrunnerwindow.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/scriptsupervisor.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/streamsourceregistry.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/streamsession.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/startupprogress.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/tabbedcrawlerwidget.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/logpane.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/viewinterface.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/viewtools.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/scratchpad.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/tabbedscratchpad.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/encodings.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/favoritefiles.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/tabnamemapping.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/iconloader.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/displayfilepath.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/downloader.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/decompressor.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/fontutils.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/colorlabelsmanager.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/highlighteredit.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/include/highlightersetedit.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/include/highlightersdialog.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/include/predefinedfiltersdialog.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/include/optionsdialog.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/src/abstractlogview.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/abstractlogviewpreview.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/actionsconfig.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/actionexpression.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/actionsconfigparser.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/actionsmanager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/actionsrepository.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/actionsresponseswindow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/actionstablemodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/actioneditdialog.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/actionparametersdialog.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/actionparameterdefinitiondialog.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/actionparameterseditorwidget.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/actionruntime.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/comportutils.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/configurationexport.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/configurationexportdialog.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/mainwindowproject.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/crawlerwidget.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/crawlerwidgetpreview.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/filteredview.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/matchlabeler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/searchbarlayout.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/highlightersdialog.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/highlighteredit.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/highlightersmenu.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/highlightersetedit.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/highlighterset.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/predefinedfilters.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/checkcombobox.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/predefinedfilterscombobox.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/predefinedfiltersdialog.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/infoline.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/labqueuewindow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pathline.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/logmainview.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/mainwindow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/mainwindowpanes.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/logpane.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/mainwindowpreview.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/mainwindowregexlab.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/uiautomation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/mainwindowtext.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/menu.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/opencomportdialog.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/optionsdialog.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/updatedialog.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/updateuicontroller.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/overview.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/overviewwidget.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/previewconfig.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/previewconfigparser.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/previewdecodeutils.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/previewmanager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/previewmessagetab.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/previewassembly.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/previewservice.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/previewdecodersettings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/previewdecodersettingsdialog.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tsharkpreviewclient.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/previewsource.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/previewrepository.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/previewstablemodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/previewwindow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/importactionsdialog.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/importpreviewsdialog.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/quickfind.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/quickfindmux.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/quickfindpattern.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/quickfindwidget.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/recentfiles.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/responseeditdialog.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/responsestablemodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/scenariorunner.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/scenariorunnerwindow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/savedsearches.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/selection.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/session.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/sessioninfo.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/signalmux.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/serialcaptureworker.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/scriptrunnerwindow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/scriptsupervisor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/streamsession.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/startupprogress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tabbedcrawlerwidget.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/viewtools.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/scratchpad.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tabbedscratchpad.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/favoritefiles.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tabnamemapping.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/iconloader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/displayfilepath.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/downloader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/decompressor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/colorlabelsmanager.cpp
)

set_target_properties(klogg_ui PROPERTIES AUTOUIC ON)
set_target_properties(klogg_ui PROPERTIES AUTOMOC ON)
set_target_properties(klogg_ui PROPERTIES AUTORCC ON)

set_property(
  TARGET klogg_ui
  APPEND
  PROPERTY AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/ui"
)
# The forms are kept next to their headers; a source that includes its form's generated header
# itself (the header holding the form as a member) finds the form there.
set_property(
  TARGET klogg_ui
  APPEND
  PROPERTY AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(KLOGG_USE_LTO)
  set_property(TARGET klogg_ui PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Plugin view tables sort text that is no finite number ("nan", "inf") last, and number fields
# refuse it: std::isfinite must see NaN and infinities, which -ffast-math folds away.
set_exact_floating_point("${CMAKE_CURRENT_SOURCE_DIR}/src/plugintablemodel.cpp")
set_exact_floating_point("${CMAKE_CURRENT_SOURCE_DIR}/src/pluginview.cpp")
# E17f: chart and sparkline gaps are NaN, which -ffast-math would not see.
set_exact_floating_point("${CMAKE_CURRENT_SOURCE_DIR}/src/pluginchart.cpp")
set_exact_floating_point("${CMAKE_CURRENT_SOURCE_DIR}/src/pluginviewlayout.cpp")

target_include_directories(klogg_ui
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_SOURCE_DIR}/src/app"
)
target_link_libraries(
  klogg_ui
  PUBLIC project_options
         project_warnings
         klogg_commander
         klogg_plugins
         klogg_lab
         klogg_logdata
         klogg_versioncheck
         klogg_karchive
         Qt${QT_VERSION_MAJOR}::Widgets
         Qt${QT_VERSION_MAJOR}::Concurrent
         Qt${QT_VERSION_MAJOR}::Network
         Qt${QT_VERSION_MAJOR}::SerialPort
         Qt${QT_VERSION_MAJOR}::Xml
)

if(WIN32)
  target_link_libraries(klogg_ui PUBLIC user32)
endif()

add_library(cilogg_stream_registry STATIC include/streamsourceregistry.h src/streamsourceregistry.cpp)
target_include_directories(cilogg_stream_registry PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(cilogg_stream_registry PUBLIC klogg_logdata Qt${QT_VERSION_MAJOR}::SerialPort
  project_options project_warnings)
target_link_libraries(klogg_ui PUBLIC cilogg_stream_registry)

if(KLOGG_BUILD_TESTS OR KLOGG_SHARED_SERIAL_WRITER)
  add_library(cilogg_communication_contracts STATIC
    include/communicationtypes.h src/communicationtypes.cpp)
  target_include_directories(cilogg_communication_contracts PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_link_libraries(cilogg_communication_contracts PUBLIC
    klogg_logdata Qt${QT_VERSION_MAJOR}::Core project_options project_warnings)
  add_library(cilogg_shared_writer STATIC include/sharedwriter.h src/sharedwriter.cpp)
  target_link_libraries(cilogg_shared_writer PUBLIC cilogg_communication_contracts)
  add_library(cilogg_serial_write_driver STATIC include/serialwritedriver.h
    include/serialwritebridge.h src/serialwritedriver.cpp)
  set_target_properties(cilogg_serial_write_driver PROPERTIES AUTOMOC ON)
  target_link_libraries(cilogg_serial_write_driver PUBLIC cilogg_shared_writer)
endif()
if(KLOGG_SHARED_SERIAL_WRITER)
  target_sources(klogg_ui PRIVATE src/streamsessionwriter.cpp src/serialcapturewriter.cpp
    src/backendthreadretirement.cpp)
  target_compile_definitions(klogg_ui PUBLIC KLOGG_SHARED_SERIAL_WRITER=1)
  target_link_libraries(klogg_ui PUBLIC cilogg_serial_write_driver)
else()
  # The flag changes QObject signals. Keep OFF metadata separate when reusing an ON build tree.
  set_target_properties(klogg_ui PROPERTIES
    AUTOGEN_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/klogg_ui_autogen_legacy")
endif()
