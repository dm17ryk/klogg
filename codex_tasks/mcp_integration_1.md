Да, бро — посмотрел оба репо.
Короткий вывод: **`codex-win-gui-mcp` у тебя уже дорос до нормального session-first MCP**, с отдельными `adapters`, `loops`, `tests`, обновленным README и Qt/klogg-ориентированным state/action workflow. А в ветке **`klogg: win_gui_mcp_intergration`** уже есть хороший фундамент: `.codex`, `AGENTS.md`, `scripts/codex`, deterministic automation mode через `KLOGG_AUTOMATION=1`, `--dump-ui-tree`, objectName/accessibility-инструментирование ключевых контролов и automation-тесты. Но до режима **“использовать этот MCP на полную”** не хватает еще одного слоя: **app-side semantic automation contract** — не просто dump дерева, а полноценного **state + actions API** из самого `klogg`. ([GitHub][1])

Что уже реально сделано в `klogg` на этой ветке: project `.codex/config.toml` сейчас задает sandbox/approval policy для Windows; `AGENTS.md` уже фиксирует Windows/Qt build flow, говорит держать Win GUI MCP registration user-local и использовать `scripts/codex`; есть `build-windows.ps1`, `run-klogg-debug.ps1`, `run-tests.ps1`, `collect-artifacts.ps1`; а в приложении automation mode включается через `KLOGG_AUTOMATION=1` или `--dump-ui-tree`, ставит детерминированную геометрию окна и обходит restore session path. В compare summary также видно, что `--dump-ui-tree` уже пишет JSON, automation test targets проходят, а полный `ctest` еще падает по несвязанной commander-history проверке. ([GitHub][2])

Главный разрыв сейчас такой: в **`codex-win-gui-mcp`** твой `QtAdapter` уже работает как с richer app contract — он ожидает, что приложение сможет вернуть **JSON state dump** в файл, а `KloggAdapter` уже читает из него `kloggState` и top-level `actions`. При этом в `klogg` ветке я подтвердил только `--dump-ui-tree`/`automationUiTree()` с полями уровня `className`, `objectName`, `text`, `accessibleName`, `children`, плюс objectName на меню/actions и хорошо проставленные имена в `CrawlerWidget`. Я не увидел в текущем app-side dump ни `kloggState`, ни `actions`, ни richer полей вроде `role`, `enabled` или `checked`, из-за чего `klogg_open_log`, `klogg_search`, `klogg_toggle_follow` в MCP пока и остаются по сути semantically недозапитанными/заглушечными. ([GitHub][3])

## Что я бы добавил в `klogg` — подробный план / LLD

### P0. Свести `klogg` к контракту, который уже ожидает `codex-win-gui-mcp`

Это самый важный шаг. Не новый pyautogui, не еще один dump, а **ровно тот app-side JSON contract**, под который твой MCP уже написан. README `codex-win-gui-mcp` прямо описывает target-app support как stable `objectName`, `accessibleName`, automation mode и **state dump endpoint**, а в качестве полезных `kloggState` полей уже перечисляет `activeFile`, `activeTabTitle`, `cursorLine`, `cursorColumn`, `visibleLineStart`, `visibleLineEnd`, `searchText`, `matchCount`, `followMode`, `scratchPad`, `encoding`, `parserMode`. `QtAdapter` при этом зовет приложение с dump-аргументом и path, а `KloggAdapter` уже читает `kloggState`. ([GitHub][4])

#### Что конкретно сделать

**1) Добавить новый CLI-режим**
В `src/app/cli.h` и `src/app/main.cpp` добавь:

* `--dump-state-json <path>`
* опционально оставить `--dump-ui-tree` как debug/legacy alias

Я бы сделал поведение таким:

* `--dump-ui-tree` = быстрый debug stdout dump, как сейчас;
* `--dump-state-json <path>` = полноценный automation snapshot для MCP, который пишет JSON в файл.
  Это идеально ляжет на текущий `QtAdapter.dump_qt_state()`, который уже ждет именно file-based dump. ([GitHub][5])

**2) Не ломать текущую форму root-объекта**
Сейчас `MainWindow::automationUiTree()` уже возвращает root-дерево с `windowTitle`. Я бы не заменял это новой схемой с нуля, а **расширил текущий root** дополнительными top-level полями, чтобы сохранить совместимость.

Предлагаемая форма:

```json
{
  "schemaVersion": 1,
  "className": "MainWindow",
  "objectName": "mainWindow",
  "windowTitle": "klogg - sample.log",
  "text": "klogg - sample.log",
  "accessibleName": "",
  "children": [...],

  "actions": [...],
  "kloggState": {...},
  "windowInfo": {...}
}
```

То есть:

* `className/objectName/text/accessibleName/children` — как сейчас;
* `actions` — новый app-action каталог;
* `kloggState` — новый semantic state;
* `windowInfo` — можно просто обернуть/переиспользовать существующий `commanderWindowInfo()`, где уже есть `windowId`, `windowIndex`, `tabs`, `tabId`, `tabIndex`, `filePath`, `displayName`, `sourceType` и пр. ([GitHub][6])

### P0.1. Вынести в `MainWindow` три отдельные automation-функции

В `src/ui/include/mainwindow.h` я бы добавил:

```cpp
QVariantMap automationSnapshot() const;
QVariantMap automationTree() const;
QVariantList automationActions() const;
QVariantMap automationState() const;
```

А в `src/ui/src/mainwindow.cpp`:

* `automationTree()` — текущий `automationObjectTree(this)` с enrich-полями;
* `automationActions()` — список всех invokable действий;
* `automationState()` — semantic состояние `klogg`.

Сейчас у тебя уже есть `automationUiTree()` и `commanderWindowInfo()`. Их лучше не дублировать, а собрать в один чистый snapshot pipeline. ([GitHub][7])

---

## P0.2. Добавить `kloggState` — это основной missing piece

Минимум, который я бы сделал **обязательным**, потому что он уже нужен MCP:

```json
"kloggState": {
  "startupReady": true,
  "windowId": 0,
  "windowIndex": 0,
  "activeTabIndex": 0,
  "activeTabTitle": "sample.log",
  "activeFile": "C:/.../sample.log",
  "sourceType": "file",

  "cursorLine": 123,
  "cursorColumn": 17,
  "visibleLineStart": 100,
  "visibleLineEnd": 180,

  "searchText": "ERROR",
  "matchCount": 17,
  "searchInProgress": false,

  "followMode": false,
  "loadingInProgress": false,

  "encoding": "UTF-8",
  "parserMode": "default",
  "scratchPadVisible": false,
  "previewerVisible": false,
  "actionsResponsesVisible": false
}
```

Почему именно это:

* первая половина нужна для **semantic targeting** и multi-window/session logic;
* `visibleLineStart/End`, `cursorLine`, `matchCount`, `followMode` прямо нужны твоему текущему MCP-слою;
* `sourceType`, `windowId`, `tabId` и близкие поля уже логично следуют из существующего `commanderWindowInfo()`;
* `startupReady/searchInProgress/loadingInProgress` дадут MCP нормальные **wait/assert oracles**, а не sleep’ы. ([GitHub][4])

### Где это собирать

Тут точный owner зависит от текущей архитектуры `klogg`, и это единственное место, где я не могу быть на 100% категоричным без полного локального grep по исходникам. Но по видимому коду логика должна лечь так:

* `MainWindow` — window/meta/tabs/visibility дочерних pane’ов;
* текущий active tab / `CrawlerWidget` / log-view owner — `searchText`, search flags, `matchCount`, `followMode`, visible range, cursor position;
* существующий commander info — `windowId`, `windowIndex`, `tabId`, `sourceType`, `filePath`. ([GitHub][6])

---

## P0.3. Сделать tree богаче: `role`, `enabled`, `visible`, `checked`, `bounds`

Сейчас из публичной реализации `automationObjectTree()` я подтвердил только `className`, `objectName`, `text`, `accessibleName`, `children`. Для полного semantic-use этого мало. Твой `QtAdapter.find_qt_object()` уже умеет искать не только по `objectName`/`accessibleName`, но и по `role`, а `toggle_qt_control()` опирается на логическое состояние вроде `checked`. ([GitHub][6])

Я бы добавил в каждый узел дерева хотя бы:

```json
{
  "className": "QAction",
  "objectName": "showScratchPadAction",
  "text": "Scratch pad",
  "accessibleName": "",
  "role": "action",
  "enabled": true,
  "visible": true,
  "checked": false,
  "children": []
}
```

И по типам:

* `QAction` / `QAbstractButton` → `checked`, `enabled`
* `QWidget` → `visible`, `enabled`
* `QMenu` → `role = "menu"`
* line edits / combo / tab / statusbar / splitter / custom log view → свои роли вроде `lineedit`, `combobox`, `tab`, `statusbar`, `logview`

`bounds` тоже полезен, даже если это будет просто window-relative rect. Тогда MCP сможет быстрее и надежнее падать на coordinate fallback, не делая полный visual re-localization.

---

## P1. Не изобретать второй command layer — расширить существующий `commander`

Вот это я считаю очень сильным архитектурным решением для `klogg`:
**не делать отдельный “automation RPC” параллельно commander’у, а расширить commander до semantic automation surface.**

Почему:

* в `klogg` уже есть structured commander actions: `OpenFile`, `OpenUrl`, `OpenCom`, `CloseFile`, `CloseUrl`, `CloseCom`, `CloseAll`, `GetInfo`, `GetFilters`, `FocusTab`, `SetFilter`, `CloseTab`;
* в `tests/unit/commander_test.cpp` уже есть покрытие CLI/command-парсинга;
* compare ветка уже затрагивает commander tests. ([GitHub][6])

### Что добавить в commander

Я бы добавил такие действия:

* `DumpState` — возвращает/пишет тот же snapshot, что и `--dump-state-json`
* `SearchSetText`
* `SearchRun`
* `SearchClear`
* `SetSearchFlags`
  (`matchCase`, `useRegexp`, `inverseMatch`, `booleanSearch`, `autoRefresh`, `keepResults`)
* `SetFollowMode`
* `GetVisibleRange`
* `GetCursorPosition`
* `InvokeAction`
  (по `objectName`, например `openClipboardAction`)
* `FocusObject`
  (по `objectName`)
* `GetUiTree`
  (если захочешь унифицировать и debug dump тоже)

Тогда `codex-win-gui-mcp` можно будет обновить минимально:

* `klogg_open_log` звать через уже существующий `OpenFile`;
* `klogg_search` — через `SearchSetText + SearchRun`;
* `klogg_toggle_follow` — через `SetFollowMode` или toggle-вариант;
* `invoke_qt_action` — через `InvokeAction(objectName)`. ([GitHub][8])

---

## P1.1. Добавить `actions` в snapshot

`QtAdapter.invoke_qt_action()` уже смотрит в top-level `actions`. Значит, `klogg` должен сам отдавать список семантически invokable actions, а не заставлять MCP угадывать по raw tree. ([GitHub][3])

Пример:

```json
"actions": [
  {
    "objectName": "openClipboardAction",
    "text": "Open from clipboard",
    "role": "action",
    "enabled": true
  },
  {
    "objectName": "showScratchPadAction",
    "text": "Scratch pad",
    "role": "action",
    "enabled": true,
    "checked": false
  }
]
```

Лучше строить это из:

* `QAction`
* menu actions
* tab/window toggle actions
* search-related buttons/toggles

---

## P1.2. Дорасширить objectName/accessibility backlog

То, что я смог подтвердить, уже неплохо: в `CrawlerWidget` у тебя проставлены objectName и accessibility для `visibilityComboBox`, `searchLineEdit`, `searchLineEditInner`, `matchCaseButton`, `useRegexpButton`, `inverseMatchButton`, `booleanSearchButton`, `searchRefreshButton`, `clearSearchButton`, `searchButton`, `keepSearchResultsButton`, `stopSearchButton`, `predefinedFiltersComboBox`, `searchInfoLine`; в `MainWindow` уже есть objectName на `mainWindow`, меню и нескольких actions. ([GitHub][9])

Следующий backlog я бы сделал таким:

* **tab bar / tab widget**
* **status bar** и его ключевые labels
* **main splitter / side panes**
* **central log view** и его viewport/gutter
* **scratch pad / previewer / actions responses** окна и переключатели
* **dialogs**: open file/url/encoding/options/error
* **message boxes** и transient notifications

Где возможно:

* `objectName` — обязательно;
* `accessibleName` — для icon-only и ambiguous controls;
* `accessibleDescription` — для того, что человеку понятно визуально, но неочевидно модели.

---

## P1.3. Для `klogg` особенно важно вынести семантику log-view

Для меню и search-bar тебе уже хватит objectName + UI tree.
Но чтобы MCP был реально “как человек”, ему нужна не только кнопка Search, а смысл того, **что происходит в самом log view**.

То есть отдельно вытащить в app-state:

* `visibleLineStart`, `visibleLineEnd`
* `cursorLine`, `cursorColumn`
* `selectionStartLine`, `selectionEndLine` — если есть
* `matchCount`
* `currentMatchIndex`
* `followMode`
* `loadingInProgress`
* `parserMode`
* `encoding`

Это именно тот участок, где generic Windows/UIA automation уже слабеет, а app-side semantic API дает максимальный выигрыш.

---

## P2. Readiness / wait-oracles внутри самого приложения

Сейчас у тебя уже есть deterministic startup path для automation mode. Следующий шаг — дать MCP понять, **когда можно продолжать**, а не заставлять его делать `sleep(1.0)`. ([GitHub][10])

Я бы добавил в `kloggState`:

* `startupReady`
* `searchInProgress`
* `loadingInProgress`
* `hasModalDialog`
* `lastErrorText`
* `statusBarText`

Тогда MCP сможет делать:

* `wait until startupReady == true`
* `wait until searchInProgress == false`
* `assert lastErrorText == ""`

Это намного ближе к живому QA/debug flow.

---

## P2.1. Артефакты: путь до лога/дампа/последнего bundle

У тебя уже есть `collect-artifacts.ps1`, а `run-klogg-debug.ps1` поднимает приложение с расширенным логированием и automation env. Хороший следующий шаг — положить ссылки на артефакты прямо в automation snapshot. ([GitHub][11])

Например:

```json
"artifacts": {
  "logFilePath": "C:/.../klogg.log",
  "lastDumpPath": "C:/.../klogg.dmp",
  "artifactDir": "C:/.../artifacts/run-2026-04-08T12-34-56"
}
```

Тогда MCP после repro сможет не искать артефакты по файлам, а брать их по app-state.

---

## P2.2. Тесты — довести automation contract до first-class API

У тебя уже есть automation tests для UI tree и парсинга CLI. Я бы просто расширил этот слой, а не заводил отдельный. ([GitHub][12])

### Что добавить

**`tests/unit/commander_test.cpp`**

* parse/validation для `--dump-state-json <path>`
* новые commander actions: `DumpState`, `SearchSetText`, `SetFollowMode`, `InvokeAction`

**`tests/ui/mainwindow_test.cpp`**

* snapshot содержит top-level `actions`
* snapshot содержит `kloggState.activeTabTitle`, `windowId`, `windowIndex`
* automation tree node имеет `role/enabled/visible`

**`tests/ui/crawlerwidget_test.cpp`**

* `searchText` в state синхронизирован с `searchLineEditInner`
* `SetSearchFlags` правильно отражается в `checked`
* `matchCount`/`searchInProgress` корректно меняются, если такие данные доступны

---

## Что менять по файлам

Вот как бы я это разложил по репо.

### Обязательные изменения

**`src/app/cli.h`**

* новые CLI-параметры: `dump_state_json`, возможно `dump_state_pretty`
* help text

**`src/app/main.cpp`**

* обработка `--dump-state-json <path>`
* reuse текущего automation path
* после `show()` / нескольких `processEvents()` вызывать `mw->automationSnapshot()`
* запись JSON в файл
* `--dump-ui-tree` оставить как debug shortcut

**`src/ui/include/mainwindow.h`**

* `automationSnapshot()`
* `automationActions()`
* `automationState()`
* возможно `automationInvokeAction(const QString&)`

**`src/ui/src/mainwindow.cpp`**

* собрать snapshot
* enrich tree fields
* каталог `actions`
* state-слой на основе active tab / commander info / visible panes

**`src/ui/src/crawlerwidget.cpp`**
и, возможно, соответствующий header

* геттеры/сеттеры для search text, flags, match count, info line
* semantic helpers, которые потом дернет `MainWindow`/commander

**Активный log-view owner**
(точный файл нужно взять по локальному grep)

* visible range
* cursor position
* follow mode
* возможно selection state

**`tests/unit/commander_test.cpp`**

* CLI и action coverage

**`tests/ui/mainwindow_test.cpp`**

* snapshot/schema coverage

**`tests/ui/crawlerwidget_test.cpp`**

* state/search/toggle coverage

### Полезные, но не блокирующие

**`scripts/codex/run-klogg-debug.ps1`**

* после запуска optionally smoke-check через `--dump-state-json`

**`AGENTS.md`**

* документировать новый contract:

  * `--dump-state-json`
  * `kloggState`
  * commander actions
  * what fields MCP relies on

---

## Что я бы не делал сейчас

Я **не стал бы** пока добавлять project-scoped `[mcp_servers.*]` в `.codex/config.toml` внутри `klogg`, потому что в текущем репо этот файл осознанно минимальный, а `AGENTS.md` прямо говорит, что Win GUI MCP registration должна оставаться user-local. Это правильно, если сервер стартует из пользовательской среды, а не как стабильный HTTP endpoint на фиксированном локальном адресе. Если позже закрепишь `codex-win-gui-mcp` как стабильный local HTTP server, тогда уже можно будет обсуждать project-scoped MCP block. ([GitHub][2])

---

## Мой приоритетный порядок внедрения

Если совсем приземленно, делал бы так:

1. **`--dump-state-json <path>`**
2. **top-level `kloggState` + `actions`**
3. **`role/enabled/visible/checked` в automation tree**
4. **reuse commander для `DumpState/Search/InvokeAction/SetFollowMode`**
5. **поднять visible range / cursor / match count из active log tab**
6. **расширить UI/objectName backlog**
7. **дотянуть tests**

---

## Definition of Done

Считал бы задачу закрытой, когда выполняется вот это:

* `codex-win-gui-mcp` может вызвать app-side dump и получить **валидный JSON файл**, а не только stdout tree; ([GitHub][3])
* `klogg_get_state`, `klogg_get_active_tab`, `klogg_get_visible_range` работают из настоящего `kloggState`, а не из частичных догадок; ([GitHub][8])
* `klogg_open_log`, `klogg_search`, `klogg_toggle_follow` перестают быть “requires app-side automation support” и начинают реально дергать `klogg`; ([GitHub][8])
* MCP может сделать сценарий:
  `launch -> wait -> dump_state -> open log -> search -> assert matchCount > 0 -> toggle follow -> collect artifacts`
* на это есть UI/unit tests в ветке `klogg`. ([GitHub][12])

Следующим сообщением могу собрать тебе **уже конкретный patch-plan по файлам `klogg`**: с сигнатурами методов, примером `automationSnapshot()` JSON и списком commander actions в формате “что куда добавить”.

[1]: https://github.com/dm17ryk/codex-win-gui-mcp "https://github.com/dm17ryk/codex-win-gui-mcp"
[2]: https://raw.githubusercontent.com/dm17ryk/klogg/win_gui_mcp_intergration/.codex/config.toml "https://raw.githubusercontent.com/dm17ryk/klogg/win_gui_mcp_intergration/.codex/config.toml"
[3]: https://raw.githubusercontent.com/dm17ryk/codex-win-gui-mcp/master/adapters/qt_adapter.py "https://raw.githubusercontent.com/dm17ryk/codex-win-gui-mcp/master/adapters/qt_adapter.py"
[4]: https://raw.githubusercontent.com/dm17ryk/codex-win-gui-mcp/master/README.md "https://raw.githubusercontent.com/dm17ryk/codex-win-gui-mcp/master/README.md"
[5]: https://github.com/dm17ryk/klogg/blob/win_gui_mcp_intergration/src/app/cli.h "https://github.com/dm17ryk/klogg/blob/win_gui_mcp_intergration/src/app/cli.h"
[6]: https://github.com/dm17ryk/klogg/blob/win_gui_mcp_intergration/src/ui/src/mainwindow.cpp "https://github.com/dm17ryk/klogg/blob/win_gui_mcp_intergration/src/ui/src/mainwindow.cpp"
[7]: https://github.com/dm17ryk/klogg/blob/win_gui_mcp_intergration/src/ui/include/mainwindow.h "https://github.com/dm17ryk/klogg/blob/win_gui_mcp_intergration/src/ui/include/mainwindow.h"
[8]: https://raw.githubusercontent.com/dm17ryk/codex-win-gui-mcp/master/adapters/klogg_adapter.py "https://raw.githubusercontent.com/dm17ryk/codex-win-gui-mcp/master/adapters/klogg_adapter.py"
[9]: https://github.com/dm17ryk/klogg/blob/win_gui_mcp_intergration/tests/ui/crawlerwidget_test.cpp "https://github.com/dm17ryk/klogg/blob/win_gui_mcp_intergration/tests/ui/crawlerwidget_test.cpp"
[10]: https://github.com/dm17ryk/klogg/blob/win_gui_mcp_intergration/src/app/main.cpp "https://github.com/dm17ryk/klogg/blob/win_gui_mcp_intergration/src/app/main.cpp"
[11]: https://raw.githubusercontent.com/dm17ryk/klogg/win_gui_mcp_intergration/scripts/codex/run-klogg-debug.ps1 "https://raw.githubusercontent.com/dm17ryk/klogg/win_gui_mcp_intergration/scripts/codex/run-klogg-debug.ps1"
[12]: https://github.com/dm17ryk/klogg/compare/master...win_gui_mcp_intergration "https://github.com/dm17ryk/klogg/compare/master...win_gui_mcp_intergration"
