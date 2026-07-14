#include <catch2/catch.hpp>

#include "configuration.h"

#include <QSettings>
#include <QTemporaryDir>

TEST_CASE( "Fresh configuration shows the tabs bar by default", "[configuration]" )
{
    const Configuration configuration;

    REQUIRE( configuration.showTabsBarByDefault() );
}

TEST_CASE( "Updater preferences persist channel frequency and action", "[configuration][updater]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    QSettings settings( directory.filePath( "settings.ini" ), QSettings::IniFormat );

    Configuration saved;
    saved.setVersionCheckingEnabled( false );
    saved.setUpdateChannel( UpdateChannel::Ci );
    saved.setUpdateFrequency( UpdateFrequency::Monthly );
    saved.setUpdateAction( UpdateAction::DownloadAndInstall );
    saved.saveToStorage( settings );
    settings.sync();

    Configuration restored;
    restored.retrieveFromStorage( settings );
    REQUIRE_FALSE( restored.versionCheckingEnabled() );
    REQUIRE( restored.updateChannel() == UpdateChannel::Ci );
    REQUIRE( restored.updateFrequency() == UpdateFrequency::Monthly );
    REQUIRE( restored.updateAction() == UpdateAction::DownloadAndInstall );
}
