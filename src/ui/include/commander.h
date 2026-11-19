#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QSerialPort>
#include <QString>
#include <QVariantMap>
#include <optional>

enum class CommanderAction {
    None,
    OpenFile,
    OpenUrl,
    OpenCom,
    CloseFile,
    CloseUrl,
    CloseCom,
    CloseKlogg,
    CloseAll,
    GetInfo,
    GetFilters,
    GetActions,
    GetResponses,
    CreateAction,
    UpdateAction,
    DeleteAction,
    CreateResponse,
    UpdateResponse,
    DeleteResponse,
    SendAction,
    WaitResponse,
    StartComm,
    PlayComm,
    PauseComm,
    StopComm,
    StartNewCommFile,
    GetCommStatus,
    StartLogging,
    StopLogging,
    AddComment,
    GetResponseCounter,
    ResetResponseCounter,
    ClearComm,
    RunScript,
    RunGlobalScript,
    RunScenario,
    RunSuite,
    StopScript,
    StopGlobalScript,
    StopScenarioRun,
    GetScriptStatus,
    GetGlobalScriptStatus,
    GetScenarioStatus,
    GetScriptSubscriptions,
    GetGlobalScriptSubscriptions,
    ClearScriptSubscriptions,
    ClearGlobalScriptSubscriptions,
    GetScenarioReport,
    FocusTab,
    SetFilter,
    CloseTab,
    Search,
    SetFollowMode,
    InvokeAction,
    DumpState,
    GetUi,
    SetUi,
    ActivateUi,
    GetPlugins,
    PluginManage,
    InvokePlugin,
    GetPluginJob,
    CancelPluginJob,
    SetActionsEnabled,
    SelectActionsTab,
    PreviewMessage,
    RegexEvaluate,
    GetAnalysisJob,
    GetAnalysisResults,
    CancelAnalysisJob,
    ReleaseAnalysisJob,
    SetAutoResponsesEnabled,
    ShowRegexLab,
    GetRegexLab,
    SetRegexLab,
    ApplyRegexLabPattern,
    ValueCounts,
    GetValueCount,
    ExportValueCounts,
    ApplyValueCount,
    OpenValueCount,
    ShowValueCounts,
    GetValueCounts,
    SetValueCounts,
    GetPreviewDecoder,
    SetPreviewDecoder,
    ShowPreviewDecoderSettings,
    OpenPluginView,
    ListReports,
    CreateReport,
    // Split panes (U16b).
    SplitPane,
    FocusPane,
    ClosePane,
    MergePanes,
    MoveTab,
    // Advanced plugin SDK (E17a).
    GetPluginTooltip,
};

enum class CommanderResultCode {
    Success,
    InvalidRequest,
    NotFound,
    ExecutionFailed,
    TransportError,
};

// What a tab-bound tool runs on (U15): the focused tab, one tab, a file, or all opened files.
// Text form (CLI, MCP, saved definitions): "focused", "all", "tab:<tab id>", "file:<path>".
// Resolution against the open tabs and the shared picker are in analysissource.h.
struct AnalysisSource {
    enum class Kind { Focused, Tab, File, AllOpen };

    Kind kind = Kind::Focused;
    QString tabId;
    QString filePath;

    static std::optional<AnalysisSource> parse( const QString& text,
                                                QString* errorMessage = nullptr );
    QString toString() const;

    bool operator==( const AnalysisSource& other ) const
    {
        return kind == other.kind && tabId == other.tabId && filePath == other.filePath;
    }
};

struct CommanderComSettings {
    QString portName;
    std::optional<QString> filePath;
    std::optional<qint32> baudRate;
    std::optional<QSerialPort::DataBits> dataBits;
    std::optional<QSerialPort::Parity> parity;
    std::optional<QSerialPort::StopBits> stopBits;
    std::optional<QSerialPort::FlowControl> flowControl;
    std::optional<bool> addTimestamps;
    std::optional<QString> timestampFormat;
    std::optional<bool> logTransmits;
    std::optional<bool> useForActions;
};

struct CommanderRequest {
    CommanderAction action = CommanderAction::None;
    QString filePath;
    QString url;
    QString portName;
    QString tabId;
    QString filterId;
    QString filterString;
    QString searchText;
    QString objectName;
    QString entityName;
    QString commentText;
    QString scriptFilePath;
    QString argsJsonFilePath;
    QString scenarioFilePath;
    QString suiteFilePath;
    QString pluginId;
    QString pluginOperation;
    QString pluginAction;
    QString pluginJobId;
    QString viewId;
    // U15 source selection (focused, all, tab:<id>, file:<path>) of a tab-bound tool. An explicit
    // tab selector (tabId, or windowIndex with tabIndex) wins over it.
    QString source;
    QVariantMap pluginArguments;
    QVariantMap analysisArguments;
    std::optional<int> windowIndex;
    std::optional<int> tabIndex;
    // Pane commands: the pane (by id, or by index in the window's layout order), the side of a
    // new pane ("right" | "down", empty means right) and what close_pane does with the pane's
    // tabs ("close" | "move", empty refuses a pane that has tabs).
    QString paneId;
    std::optional<int> paneIndex;
    QString paneDirection;
    QString paneTabs;
    std::optional<int> filterIndex;
    std::optional<int> entityId;
    std::optional<int> timeoutMs;
    uint64_t beforeContext = 0;
    uint64_t afterContext = 0;
    std::optional<uint64_t> maxMatches;
    QString matchMode = QStringLiteral( "contains" );
    std::optional<bool> enabled;
    bool allEntities = false;
    bool timestampComment = false;
    bool followFile = false;
    bool predefinedFilters = false;
    bool prettyOutput = false;
    bool runSearch = false;
    bool rearmAutoRefresh = false;
    bool searchUseRegex = false;
    bool searchCaseSensitive = false;
    bool searchInverseMatch = false;
    bool searchUseBoolean = false;
    bool searchAutoRefresh = false;
    bool searchKeepResults = false;
    // pause_comm --wait: return only after the port is closed (bounded by timeoutMs).
    // search --wait: return once every searched tab finished (bounded by timeoutMs).
    bool waitForCompletion = false;
    CommanderComSettings comSettings;
    QVariantMap definitionPayload;
    QVariantMap actionParameters;
    // Who sent the request: gui, cli, mcp, python or scenario (docs/plugin-repositories.md 3.6).
    // Set by the transport, never by a payload; empty counts as an automation origin. Only gui
    // may show a sign-in window, and only gui and cli may submit a personal access token.
    QString origin;
};

inline const QString CommanderOriginGui = QStringLiteral( "gui" );
inline const QString CommanderOriginCli = QStringLiteral( "cli" );
inline const QString CommanderOriginMcp = QStringLiteral( "mcp" );
inline const QString CommanderOriginPython = QStringLiteral( "python" );
inline const QString CommanderOriginScenario = QStringLiteral( "scenario" );
// The origin a CLI process reports: cli, or mcp when native MCP started it (the MCP runtime sets
// CILOGG_COMMANDER_ORIGIN=mcp). Never gui.
QString commanderCliOrigin();

struct CommanderResult {
    CommanderResultCode code = CommanderResultCode::Success;
    QString message;
    QVariantMap payload;

    bool ok() const
    {
        return code == CommanderResultCode::Success;
    }

    bool hasPayload() const
    {
        return !payload.isEmpty();
    }
};

QString commanderActionToString( CommanderAction action );
std::optional<CommanderAction> commanderActionFromString( const QString& action );
QString commanderResultCodeToString( CommanderResultCode code );
QString normalizeCommanderFilePath( const QString& path );
QString normalizeCommanderUrl( const QString& url );
bool isCommanderOpenAction( CommanderAction action );
bool isCommanderAnalysisAction( CommanderAction action );
// The actions that accept a U15 `source` selection.
bool commanderActionAcceptsSource( CommanderAction action );
// True if the request names a tab explicitly (tabId, or a tab index); such a selector wins over
// `source`.
bool hasCommanderTabSelector( const CommanderRequest& request );
bool isCommanderPaneAction( CommanderAction action );

QVariantMap commanderRequestToVariantMap( const CommanderRequest& request );
std::optional<CommanderRequest> commanderRequestFromVariantMap( const QVariantMap& map,
                                                                QString* errorMessage = nullptr );
QVariantMap commanderResultToVariantMap( const CommanderResult& result );
std::optional<CommanderResult> commanderResultFromVariantMap( const QVariantMap& map,
                                                              QString* errorMessage = nullptr );

bool writeCommanderResult( const QString& resultPath, const CommanderResult& result );
std::optional<CommanderResult> readCommanderResult( const QString& resultPath,
                                                    QString* errorMessage = nullptr );
std::optional<CommanderResult> waitForCommanderResult( const QString& resultPath, int timeoutMs,
                                                      QString* errorMessage = nullptr );

CommanderResult commanderSuccess( const QString& message = {}, const QVariantMap& payload = {} );
CommanderResult commanderFailure( CommanderResultCode code, const QString& message );

Q_DECLARE_METATYPE( CommanderRequest )
Q_DECLARE_METATYPE( CommanderResult )
