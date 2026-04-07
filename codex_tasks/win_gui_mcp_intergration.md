Да — **для `klogg` я бы добавил проектный `.codex` прямо в репо `klogg`, а не пытался держать всю магию только в `codex-win-gui-mcp`**. Причина простая: Codex читает **project-scoped** конфиг из `.codex/config.toml`, Local environments тоже хранит в `.codex` в корне проекта, а `AGENTS.md` загружает **до начала работы**. Значит, когда ты открываешь именно `klogg` в Codex, именно его проектные настройки и инструкции должны быть рядом с кодом. MCP тоже можно скопировать на уровень проекта, но только если сервер доступен по стабильному URL или глобально установленной команде. ([OpenAI Developers][1])

Сразу честно: твой приватный `codex-win-gui-mcp` я из этой сессии не открыл — GitHub-коннектор тут недоступен — поэтому ниже даю **лучший практический расклад** на основе публичного `klogg` и официальных доков Codex/Qt. По публичному `klogg` уже видно, что у тебя есть хороший фундамент: CMake/MSVC/Qt-сборка, `tests/`, `test_data/`, запуск через `ctest`, CLI-флаги `-n/-m/-l/-d`, file logging, crash handler и даже action для генерации dump. Это почти идеальная база, чтобы сделать не “слепой кликер”, а воспроизводимый GUI debug-loop. ([GitHub][2])

## Как бы я разделил ответственность

**В `codex-win-gui-mcp`:**

* общий Windows/Qt automation engine;
* `klogg`-адаптер: window title regex, default launch args, known object names, artifact collection.

**В `klogg`:**

* `.codex/config.toml`;
* Local environments, созданные через Codex app settings и закоммиченные в `.codex/`;
* `AGENTS.md`;
* `scripts/codex/*.ps1`;
* маленькие изменения в Qt UI: `objectName`, `accessibleName`, automation mode. ([OpenAI Developers][3])

## Что добавить в `klogg` в первую очередь

### 1) `.codex/config.toml` в корне `klogg`

Не в корень репо, а именно в **`.codex/config.toml`**.

```toml
#:schema https://developers.openai.com/codex/config-schema.json

approval_policy = "on-request"
sandbox_mode = "workspace-write"

[windows]
sandbox = "elevated"
sandbox_private_desktop = false

[mcp_servers.klogg_gui]
url = "http://127.0.0.1:8765/mcp"
required = true
startup_timeout_sec = 20
tool_timeout_sec = 120
enabled_tools = [
  "launch_app",
  "restart_app",
  "focus_window",
  "capture_screenshot",
  "get_uia_tree",
  "click_object",
  "click_xy",
  "drag_mouse",
  "send_hotkey",
  "type_text",
  "tail_log",
  "collect_artifacts"
]
```

Почему так:

* `workspace-write` достаточно для обычной работы по репо;
* на Windows рекомендован `elevated` sandbox;
* `sandbox_private_desktop = false` полезен именно для GUI automation compatibility, потому что private desktop по умолчанию изолирует UI сильнее;
* project-scoped MCP хорош, **если** твой GUI MCP поднят как локальный HTTP server на фиксированном порту. Если он пока стартует через локальный `python.exe` из конкретной папки, тогда **сам MCP-блок лучше держать в `~/.codex/config.toml`**, а в репо оставить только project-scoped настройки и инструкции. ([OpenAI Developers][4])

### 2) `AGENTS.md` в корне `klogg`

Это не замена MCP, а второй обязательный слой. Codex читает `AGENTS.md` до начала работы, а официальные best practices прямо советуют класть туда build/test команды, repo-specific conventions и устойчивые правила поведения агента. ([OpenAI Developers][5])

Минимальный `AGENTS.md` для `klogg` я бы сделал таким:

```md
# klogg Codex guide

## Build
- On Windows use scripts/codex/build-windows.ps1.
- Default config is RelWithDebInfo.
- After changing UI, search, tabs, or menu behavior, run scripts/codex/run-tests.ps1.

## Run
- For deterministic GUI repros, use scripts/codex/run-klogg-debug.ps1.
- Default launch args: -n -m -l -dddd <sample-log>.
- Prefer MCP tools that target Qt objectName before pixel coordinates.

## Debug artifacts
- Collect screenshot + klogg log + crash dump after repro.
- For repeatable UI bugs, add a targeted QtTest regression when practical.

## GUI specifics
- The main log views may require coordinate/screenshot fallback.
- Menus, buttons, search controls, and dialogs should be targeted by objectName.
```

Для `klogg` это особенно полезно, потому что ты один раз объясняешь Codex правила запуска и отладки, и они путешествуют вместе с репо. ([OpenAI Developers][5])

### 3) Local environments через Codex app settings

Официальный путь такой: настраиваешь Local environments в Settings, а Codex сам кладет эту конфигурацию в `.codex` у корня проекта; ее можно коммитить и шарить с командой. Для `klogg` это важнее, чем вручную сочинять неизвестные внутренние файлы. ([OpenAI Developers][3])

Я бы завел такие actions:

* `Build`
* `Run clean`
* `Run sample log`
* `Tests`
* `Collect artifacts`

## Готовые скрипты, которые я бы положил в `klogg/scripts/codex`

### `scripts/codex/build-windows.ps1`

```powershell
param(
  [string]$Config = "RelWithDebInfo",
  [string]$VsDevCmd = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\Community\Common7\Tools\VsDevCmd.bat",
  [string]$QtEnvBat = "C:\Qt\5.15.2\msvc2019_64\bin\qtenv2.bat"
)

$cmd = @"
call "$VsDevCmd" -arch=x64
call "$QtEnvBat"
cmake -S . -B build_root -G "Visual Studio 16 2019" -A x64 -DCMAKE_BUILD_TYPE=$Config -DKLOGG_USE_SENTRY=ON
cmake --build build_root --config $Config --target klogg
"@

cmd /c $cmd
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

`klogg` публично документирует Windows build через Visual Studio + `qtenv2.bat` + CMake, складывает бинарники в `build_root/output`, а crash dump/reporting можно включить через `-DKLOGG_USE_SENTRY=ON`. ([GitHub][6])

### `scripts/codex/run-klogg-debug.ps1`

```powershell
param(
  [string]$LogFile = ".\test_data\sample.log",
  [string]$Config = "RelWithDebInfo"
)

$exe = Join-Path (Get-Location) "build_root\output\klogg.exe"
if (-not (Test-Path $exe)) {
  throw "klogg.exe not found at $exe. Build first."
}

$env:KLOGG_AUTOMATION = "1"

Start-Process `
  -FilePath $exe `
  -WorkingDirectory (Get-Location) `
  -ArgumentList @("-n", "-m", "-l", "-dddd", $LogFile)
```

Почему именно эти аргументы:

* `-n` — новый session;
* `-m` — multiple instances;
* `-l` — писать лог в файл;
* `-dddd` — высокий verbosity;
* путь к файлу можно передать прямо из `test_data`.
  Это все соответствует публичной CLI-документации `klogg`, а `main.cpp` реально включает file logging и crash handler на старте. ([GitHub][7])

### `scripts/codex/run-tests.ps1`

```powershell
param(
  [string]$Config = "RelWithDebInfo"
)

ctest --test-dir build_root --build-config $Config --verbose
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

`klogg` уже документирует запуск тестов через `ctest`, а QtTest у него уже есть в зависимостях для test build. ([GitHub][8])

### `scripts/codex/collect-artifacts.ps1`

```powershell
param(
  [string]$OutDir = ".\artifacts"
)

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

if (Test-Path ".\build_root\output") {
  Get-ChildItem ".\build_root\output" -File |
    Where-Object { $_.Extension -in ".log", ".dmp", ".txt" } |
    Copy-Item -Destination $OutDir -Force
}
```

Точный путь до логов и dump’ов тебе, возможно, придется подправить под свою локальную сборку, но сама идея правильная: Codex должен иметь отдельный action для сбора артефактов после repro. У `klogg` уже есть и file logging, и crash handling, и action для генерации dump, так что собирать есть что. ([GitHub][9])

## Что поменять в самом Qt-коде `klogg`

Вот это — самый сильный апгрейд для GUI automation 🔧

Я **не нашел** `objectName` в web-снимках `mainwindow.cpp` и `tabbedcrawlerwidget.cpp`, а это значит, что сейчас у тебя, скорее всего, мало стабильных идентификаторов для надежного таргетинга UI. В Qt это очень важный слой: у `QObject` есть `objectName`, `setObjectName()` и `findChild()`, а у `QWidget` есть `accessibleName` и связанная accessibility-модель. ([GitHub][10])

### Минимальный патч в `crawlerwidget.cpp`

По публичным фрагментам видно, что у тебя там есть `searchLineEdit`, `matchCaseButton`, `useRegexpButton`, `searchRefreshButton`, `searchButton`, `stopButton`. Им нужно дать стабильные имена. ([GitHub][11])

```cpp
searchLineEdit->setObjectName("searchLineEdit");
searchLineEdit->lineEdit()->setObjectName("searchLineEditInner");
searchLineEdit->lineEdit()->setAccessibleName(tr("Search pattern"));

matchCaseButton->setObjectName("matchCaseButton");
matchCaseButton->setAccessibleName(tr("Match case"));

useRegexpButton->setObjectName("useRegexpButton");
useRegexpButton->setAccessibleName(tr("Use regular expression"));

searchRefreshButton->setObjectName("searchRefreshButton");
searchRefreshButton->setAccessibleName(tr("Auto refresh"));

searchButton->setObjectName("searchButton");
searchButton->setAccessibleName(tr("Search"));

stopButton->setObjectName("stopSearchButton");
stopButton->setAccessibleName(tr("Stop search"));
```

### Минимальный патч в `mainwindow.cpp`

По публичным фрагментам видно `toolsMenu`, `highlightersMenu`, `favoritesMenu`, `optionsAction`, `showDocumentationAction`, `generateDumpAction`, `showScratchPadAction` и другие `QAction`. Так как `QAction` — это `QObject`, ему тоже можно задавать `objectName`. ([GitHub][10])

```cpp
setObjectName("mainWindow");

toolsMenu->setObjectName("toolsMenu");
highlightersMenu->setObjectName("highlightersMenu");
favoritesMenu->setObjectName("favoritesMenu");

optionsAction->setObjectName("optionsAction");
showDocumentationAction->setObjectName("showDocumentationAction");
generateDumpAction->setObjectName("generateDumpAction");
showScratchPadAction->setObjectName("showScratchPadAction");
openClipboardAction->setObjectName("openClipboardAction");
openUrlAction->setObjectName("openUrlAction");
```

Это даст твоему MCP слою стратегию **objectName-first**, а не screen-coordinates-first.

## Самый полезный klogg-специфичный хук: automation mode

В `main.cpp` у тебя сейчас при создании нового окна идет `mw = app.newWindow(); mw->reloadGeometry(); mw->show();`. Для человека это ок, для automation — источник флаки: окно может появляться в разных размерах/позициях, а session restore может вмешиваться в воспроизводимость. Плюс логика запуска уже учитывает last session и `-n/--new-session`. ([GitHub][9])

Я бы добавил поддержку `KLOGG_AUTOMATION=1`:

```cpp
const bool automationMode = qEnvironmentVariableIntValue("KLOGG_AUTOMATION") > 0;

...

if (automationMode) {
    // force deterministic startup
    mw = app.newWindow();
    mw->resize(1600, 1000);
    mw->move(40, 40);
    mw->show();
} else {
    mw = app.newWindow();
    mw->reloadGeometry();
    mw->show();
}
```

И вдобавок в automation mode:

* всегда вести себя как `--new-session`;
* не восстанавливать last session;
* логировать путь до файла логов/дампов в stdout или в отдельный marker log.

Это очень уменьшит флакiness.

## Еще один сильный ход: `--dump-ui-tree`

Поскольку у Qt есть `objectName`, `findChild()` и даже `dumpObjectTree()`, я бы добавил debug-only CLI/command, который печатает дерево важных объектов окна. Тогда Codex/MCP сможет не угадывать, а читать актуальные Qt IDs прямо из приложения. ([Qt Documentation][12])

Пример идеи:

* `klogg.exe --dump-ui-tree`
* или debug menu action `Dump UI tree`
* или локальный automation socket в debug-сборке

Для Qt-приложения это намного надежнее, чем жить только на Win32/UIA.

## Почему для `klogg` нужен и object-name layer, и pixel fallback

Есть важный нюанс: по комментарию maintainer’а в issue видно, что `klogg` исторически наследует **custom text rendering**, а не обычный Qt model/view для некоторых частей UI. Это значит, что меню, кнопки и диалоги можно отлично автоматизировать через object names/UIA, но логические pane’ы, выделение текста, сложный hover/selection и часть визуальных сценариев все равно придется держать через screenshot/coordinates. ([GitHub][13])

Именно поэтому `codex-win-gui-mcp` для `klogg` должен работать так:

1. `objectName` / Qt bridge;
2. потом UIA/Win32;
3. потом screenshot + coordinate fallback.

## Не забудь про QtTest

`klogg` уже собирает тесты и использует QtTest, а Qt Test умеет `mouseClick`, `mouseMove`, `keyClicks`, `qWaitForWindowActive` и прочие вещи, которые идеально подходят для фиксирования уже найденных Codex’ом GUI-багов в regression tests. Для `klogg` лучшая схема такая: **Codex воспроизводит и локализует баг через MCP/GUI harness, а потом закрепляет его QtTest-ом**. ([GitHub][6])

Минимальный skeleton после добавления `objectName`:

```cpp
class SearchBarTest : public QObject {
    Q_OBJECT
private slots:
    void searchLine_accepts_input_and_search_trigger();
};

void SearchBarTest::searchLine_accepts_input_and_search_trigger() {
    MainWindow w;
    w.show();
    QVERIFY(QTest::qWaitForWindowActive(&w));

    auto* edit = w.findChild<QLineEdit*>("searchLineEditInner");
    QVERIFY(edit != nullptr);

    QTest::keyClicks(edit, "ERROR");
    QTest::keyClick(edit, Qt::Key_Return);

    // assert expected state here
}

QTEST_MAIN(SearchBarTest)
```

## Мой прямой вердикт

**Да:**

* добавь **`.codex/config.toml` в `klogg`**;
* добавь **`AGENTS.md` в `klogg`**;
* создай **Local environments** для `klogg` и закоммить `.codex`;
* внеси в Qt UI **`objectName` + `accessibleName`** для ключевых контролов;
* добавь **automation mode** и, по возможности, **`--dump-ui-tree`**.

**Нет:**

* не пытайся хранить все только в `codex-win-gui-mcp`, если основной рабочий проект — `klogg`;
* не коммить в `klogg` user-specific пути к Python/venv/API key.

**Лучший split:**

* `codex-win-gui-mcp` = reusable engine;
* `klogg` = project config, scripts, instructions, Qt instrumentation. ([OpenAI Developers][1])

Следующим сообщением соберу тебе уже **готовые файлы под `klogg`**: `.codex/config.toml`, `AGENTS.md`, `build-windows.ps1`, `run-klogg-debug.ps1`, `run-tests.ps1` и список `objectName`, который я бы прошил первым.

[1]: https://developers.openai.com/codex/config-reference "https://developers.openai.com/codex/config-reference"
[2]: https://github.com/variar/klogg "https://github.com/variar/klogg"
[3]: https://developers.openai.com/codex/app/local-environments "https://developers.openai.com/codex/app/local-environments"
[4]: https://developers.openai.com/codex/windows "https://developers.openai.com/codex/windows"
[5]: https://developers.openai.com/codex/guides/agents-md "https://developers.openai.com/codex/guides/agents-md"
[6]: https://github.com/variar/klogg/blob/master/BUILD.md "https://github.com/variar/klogg/blob/master/BUILD.md"
[7]: https://github.com/variar/klogg/blob/master/DOCUMENTATION.md "https://github.com/variar/klogg/blob/master/DOCUMENTATION.md"
[8]: https://github.com/variar/klogg/diffs/0?base_sha=edc7582077ebdadd2825da3fa7b1c2931f816bcf&head_user=mdhvg&name=master&pull_number=681&qualified_name=refs%2Fheads%2Fmaster&sha1=edc7582077ebdadd2825da3fa7b1c2931f816bcf&sha2=f8d37c970971cf82c53b9427f50ee5ccd3dc0e1c&short_path=40f60e1&unchanged=expanded&w=false "https://github.com/variar/klogg/diffs/0?base_sha=edc7582077ebdadd2825da3fa7b1c2931f816bcf&head_user=mdhvg&name=master&pull_number=681&qualified_name=refs%2Fheads%2Fmaster&sha1=edc7582077ebdadd2825da3fa7b1c2931f816bcf&sha2=f8d37c970971cf82c53b9427f50ee5ccd3dc0e1c&short_path=40f60e1&unchanged=expanded&w=false"
[9]: https://github.com/variar/klogg/blob/master/src/app/main.cpp "https://github.com/variar/klogg/blob/master/src/app/main.cpp"
[10]: https://github.com/variar/klogg/blob/master/src/ui/src/mainwindow.cpp "https://github.com/variar/klogg/blob/master/src/ui/src/mainwindow.cpp"
[11]: https://github.com/variar/klogg/commit/c8f7c2886da449aa55a338e23c2a467066320f78.diff "https://github.com/variar/klogg/commit/c8f7c2886da449aa55a338e23c2a467066320f78.diff"
[12]: https://doc.qt.io/qt-6/qobject.html "https://doc.qt.io/qt-6/qobject.html"
[13]: https://github.com/variar/klogg/issues/128 "https://github.com/variar/klogg/issues/128"
