#pragma once

#include <QDialog>

#include "configuration.h"
#include "updatetypes.h"

class UpdateDialog final : public QDialog {
    Q_OBJECT

public:
    enum class Choice { Later, OpenRelease, Download, DownloadAndInstall };

    UpdateDialog( const ReleaseInfo& release, UpdateAction action, QWidget* parent = nullptr );
    Choice choice() const
    {
        return choice_;
    }

private:
    Choice choice_ = Choice::Later;
};
