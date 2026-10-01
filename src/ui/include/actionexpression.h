#pragma once
#include <functional>

#include <QHash>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QVector>

struct ActionExpressionLimits {
    int maximumExpressionLength = 16 * 1024;
    int maximumTokens = 4096;
    int maximumNestingDepth = 64;
    int maximumFunctionArguments = 256;
    int maximumOutputBytes = 1024 * 1024;
};

struct ActionExpressionResult {
    bool ok = false;
    QVariant value;
    QString error;
};

using ActionExpressionFunction
    = std::function<ActionExpressionResult( const QVector<QVariant>& arguments )>;

class ActionExpressionRegistry {
  public:
    static ActionExpressionRegistry& instance();

    bool registerFunction( const QString& name,
                           ActionExpressionFunction function,
                           QString* errorMessage = nullptr );
    void freeze();
    bool isFrozen() const;
    bool contains( const QString& name ) const;
    ActionExpressionResult call( const QString& name,
                                 const QVector<QVariant>& arguments ) const;

  private:
    ActionExpressionRegistry();
    void registerBuiltins();

    QHash<QString, ActionExpressionFunction> functions_;
    bool frozen_ = false;
};

ActionExpressionResult evaluateActionExpression(
    const QString& expression,
    const QVariantMap& variables,
    const ActionExpressionLimits& limits = {} );

ActionExpressionResult actionExpressionValueToBytes(
    const QVariant& value,
    int maximumOutputBytes = ActionExpressionLimits{}.maximumOutputBytes );
