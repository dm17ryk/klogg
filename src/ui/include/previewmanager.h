#pragma once

#include <QObject>
#include <QStringList>

#include "previewconfig.h"
#include "previewrepository.h"

struct PreviewImportResult {
    bool ok = false;
    QStringList errors;
    QStringList warnings;
};

class PreviewManager : public QObject {
    Q_OBJECT

  public:
    static PreviewManager& instance();

    void loadFromRepository();
    PreviewImportResult importFromFile( const QString& path );
    bool removeByName( const QString& name );

    const QVector<PreviewDefinition>& all() const;
    QVector<PreviewDefinition> enabled() const;
    const PreviewDefinition* findByName( const QString& name ) const;
    void setEnabled( const QString& name, bool enabled );
    QString findFirstMatchingEnabledPreview( const QString& rawLine ) const;

  Q_SIGNALS:
    void previewsChanged();
    void previewEnabledChanged( const QString& name, bool enabled );

  private:
    PreviewManager() = default;

    PreviewRepository repository_;
    QVector<PreviewDefinition> previews_;
};
