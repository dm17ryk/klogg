#pragma once

#include <functional>

#include <QDialog>
#include <QVariantMap>
#include <QVector>

#include "actionsconfig.h"

class QComboBox;

class ActionParametersDialog : public QDialog {
    Q_OBJECT

  public:
    explicit ActionParametersDialog( const ActionDefinition& action,
                                     QWidget* parent = nullptr,
                                     bool embedded = false );

    QVariantMap values() const;
    QVariantMap currentValues() const;
    bool validateCurrentValues( QString* errorMessage = nullptr ) const;
    void rememberCurrentValues();

  protected:
    void accept() override;

  private:
    struct EditorBinding {
        ActionParameterDefinition field;
        std::function<QVariant()> read;
        std::function<void( const QVariant& )> write;
    };

    QVariantMap rememberedValues() const;
    QVector<QVariantMap> recentValues() const;
    void applyValues( const QVariantMap& values );
    void saveRememberedValues( const QVariantMap& values );

    ActionDefinition action_;
    QVector<EditorBinding> editors_;
    QComboBox* recentCombo_ = nullptr;
    QVariantMap acceptedValues_;
    bool embedded_ = false;
};
