# Plugin repositories and configuration packs (E15, E16) — proposed design

**Status:** proposed on 2026-11-17 for the user's approval (decision D16 in
[docs/workbench/decisions.md](workbench/decisions.md)). Nothing here is implemented. The tasks are
[E15](../plans/cilogg-workbench-2026-10-21/tasks/E15.md) (plugin repositories) and
[E16](../plans/cilogg-workbench-2026-10-21/tasks/E16.md) (configuration packs). The open decisions are
listed at the end.

**Request (2026-11-17):** can CILogg install and update plugins from a private Azure DevOps
repository? An internal tool plugin must not be published publicly. Message previews (the JSON
preview definitions, Lua decoders and Python decoders) should be delivered and updated the same way,
possibly as plugins. Licensed protocol Lua sources cannot be public either.

**Follow-up (2026-11-17):** the user removed the private working configuration (previews, actions,
highlighters, predefined filters) from the public repository and asked to deliver it, with the
TShark Lua, as a plugin from the private Azure DevOps repository. Packs therefore carry actions,
highlighters and filters as well as previews and decoders.

## 1. Summary

- The Plugins window manages a list of **plugin repositories**. Each one is a `plugins.json`
  catalog (schema 1, unchanged) at an HTTPS address. Users add, remove and turn repositories on or
  off; the same operations work through Commander, the CLI, native MCP and Python.
- A repository signs in with **none** (public HTTPS) or **Azure DevOps**. Azure DevOps reuses the
  Git Credential Manager that Git already uses (`git credential fill`), so a user who can clone the
  repository needs no extra setup. A personal access token (PAT) is the fallback, kept in the
  operating system's credential store. Credentials are never written to settings, exports, logs,
  the activity journal or problem reports, are sent only to the repository's own host and
  organization, and are never forwarded across redirects.
- **Browse Plugins** shows one list merged from all enabled repositories, with a Source column. A
  plugin always updates from the repository it was installed from.
- CILogg checks for updates **silently at startup and once a day**, like the application updater.
  Installing stays a user action unless **auto-update** is turned on for that repository.
- A new plugin kind, the **configuration pack**, carries preview definitions (the JSON of
  `previews.json`), Lua decoders for TShark, Python decoders, actions with automatic responses,
  highlighters and predefined filters. Packs are versioned and updated from the same repositories.
  Their items are read-only (copy one to edit it), a pack's automatic responses start switched off,
  and TShark gets the pack's Lua folder as a trusted path, because installing from a repository the
  user added is the trust decision.
- Everything in this repository stays generic. A company keeps its private plugins, packs and the
  pipeline that builds its catalog in its own repository (section 7).

## 2. What exists today

- `PluginManager::catalogUrl()` reads one `catalogUrl` setting from the plugin `settings.ini`;
  the default is `https://raw.githubusercontent.com/dm17ryk/klogg/master/plugins.json`. The
  `catalog` operation saves the URL it was given.
- `PluginManager::fetch()` accepts HTTPS only, follows redirects with
  `NoLessSafeRedirectPolicy`, stops after 30 seconds, limits a catalog to 1 MiB and a package to
  128 MiB, compares the package SHA-256 with the catalog and checks that the staged `plugin.json`
  has the catalog's ID and version. It sends no authentication.
- `PluginManager::loadCatalog()` accepts `schema_version: 1` with at most 1,000 entries, keeps the
  entries for this `os`/`arch`, requires `protocol_version`, `id`, `version`, an HTTPS `url` and a
  64-digit `sha256`, and adds a host-generated `entry_id` (a hash of the entry, unknown keys
  included). One invalid entry for this platform fails the whole catalog.
- `Manifest::parse()` requires `protocol_version: 1`, at least one capability (`datasource`,
  `converter`, `tool`) and a package-relative executable or `python`.
- **Browse Plugins** (`PluginUiController::browseDialog`) has one URL field, a Fetch button, a
  table (Plugin, Version, Description) and Install / Update Selected.
- Plugin `secret` configuration fields are kept in settings with current-user file permissions; the
  [plugin SDK](plugin-sdk.md) states that this is not encrypted credential storage. Repository
  credentials therefore do not reuse them.
- User previews live in `previews.json` beside the application settings
  (`PreviewRepository`: `version: 1`, `previews`, `blocks`).
- The TShark decoder configuration ([TShark previews](workbench/tshark-preview.md)) holds the
  `tshark` and `text2pcap` executables, up to 8 absolute Lua paths of at most 1 MiB each, one global
  `allowExternalLua` approval ("I trust and approve execution of the selected external Lua
  scripts") that any Lua requires, the default dissector, and at most 128 preview and 32 protocol
  routes; the saved configuration is at most 64 KiB. Executable and Lua paths come only from these
  trusted settings. The limit of 8 scripts is enforced in `previewdecodersettings.cpp`,
  `tsharkpreviewclient.cpp`, `scripts/plugins/message_decoder_preview_worker.py` and
  `scripts/plugins/message_decoder_pdu.py`.
- When a TShark session starts, the preview worker copies each selected script into its own
  isolated folder as `selected-<n>.lua`, loads them with `-X lua_script:`, records each script's
  SHA-256 (`scriptSha256`) and removes every `WIRESHARK_*` and `LUA_*` variable from the
  environment, so a script cannot `require` modules from elsewhere.
- Commander requests carry no origin: the GUI dialogs, the CLI, native MCP and Python all reach the
  same executor with the same request type.
- The application updater (`VersionChecker`) checks on a schedule, skips the check when version
  checking is off or CILogg runs in automation or headless mode, and retries a failed background
  check silently.

## 3. Plugin repositories (E15)

### 3.1 The repository record

Repositories are stored in the plugin `settings.ini`, next to the plugin data, so a portable copy
keeps its own list:

| Key | Meaning |
|---|---|
| `id` | Lowercase identifier made from the name when the repository is added, with a numeric suffix if it is taken (`team`, `team-2`). It never changes, and the IDs of removed repositories are kept in a list and never given to a new repository, so a new repository cannot inherit old source records or a stored PAT. |
| `name` | Display name. |
| `url` | For sign-in `none`: the `plugins.json` address. For Azure DevOps: the repository address, a file link copied from the web portal, or an Items API address (section 3.5). |
| `branch`, `path` | Azure DevOps only. Default branch `catalog`, default path `/plugins.json`. A file link sets both. |
| `signIn` | `none` or `azure-devops`. |
| `credentialSource` | Azure DevOps only: `git-credential-manager` (default) or `personal-access-token`. Never the credential itself. |
| `enabled` | Disabled repositories are not fetched or checked for updates. |
| `autoUpdate` | Off by default (section 3.7). |
| `lastCheck`, `lastError` | Time of the last successful check and the last error text, shown in the Plugins window. |

Rules:

- At most 16 repositories. Addresses must use HTTPS and must not contain user information
  (`https://user:secret@host/...` is rejected, so a credential cannot be stored as part of a URL).
- **The address is part of the identity.** `url`, `branch` and `path` (normalized: for Azure DevOps
  the organization, project, repository, branch and path) cannot be edited. Pointing at another
  catalog means adding a new repository, which gets a new ID; the old one can then be removed. So a
  PAT stored for one organization is never sent to another, and plugins installed from the old
  address never update from the new one without the explicit source switch (section 3.4). Name,
  `enabled`, `autoUpdate` and `credentialSource` can be edited.
- **Removing a repository** also deletes its stored PAT, so no token is left orphaned in the
  credential store.
- **Migration:** the first start with E15 turns the existing `catalogUrl` into the repository
  `default` (name "CILogg examples (GitHub)", sign-in `none`, enabled). An address the user had
  changed is kept. `catalogUrl` stays readable so an older CILogg copy that shares the folder keeps
  working.
- A controlled deployment can ship a portable copy whose `settings.ini` already lists its
  repositories. No company address is compiled into CILogg.

### 3.2 Catalog format

`plugins.json` stays at `schema_version: 1`, and every entry keeps `protocol_version: 1`. These
keys are added; older CILogg versions keep them in the entry (and in its `entry_id` hash) but do not
act on them:

- `kind`: `process` (default, today's plugins) or `config-pack` (section 4). A host hides every
  entry whose `kind` it does not support, so a host with E15 but without E16 does not offer
  configuration packs.
- `os` and `arch` may be `any` for packages that run everywhere (configuration packs, pure-Python
  plugins). An older CILogg skips such entries because they match no platform name, so it never
  offers a package it cannot install.
- `min_host_version`: the oldest CILogg version that can use the package. Hosts from E15 on hide
  entries they cannot use and say why; older hosts ignore the key, which is one more reason for
  packs to use `os: any`.
- `released`, `homepage`, `license`: shown in the details pane.

The host-generated `entry_id` also covers the repository ID, so the same package listed by two
repositories gives two distinct entries.

**Invalid entries.** Today one invalid entry for the host's platform fails the whole catalog. With
several repositories, an invalid entry fails only itself: it is left out, counted in the
repository's warnings and named in its status line, and the rest of that repository still loads. An
unreadable `plugins.json` (bad JSON, wrong schema, too large, an HTML sign-in page) still fails its
whole repository, and only that repository.

### 3.3 The merged catalog in the Plugins window

- **Manage Plugins** gains a **Repositories…** button. Its dialog lists the repositories with their
  state (signed in, sign-in needed, last check, last error) and offers Add, Edit, Remove, Enable,
  Disable, Sign In, Sign Out and Auto-update. Adding an Azure DevOps repository tests the address
  and the sign-in before saving.
- **Browse Plugins** replaces its URL field with the merged list from all enabled repositories:
  columns Plugin, Version, Kind, Source (repository name), Description, plus an Installed column
  with the installed version. Fetch refreshes every enabled repository in parallel (at most 4 at a
  time) in one job. A repository that fails does not hide the others: its row in the status area
  says "Sign-in needed", "Not found" or the network error, with a Sign In button where it helps.
- Install and Update use the selected entry (`catalog_entry_id`), as today.
- Manage Plugins shows each installed plugin's source: a repository name, "Local file" or "Shipped
  with CILogg", and an Update button when the source offers a newer version.

### 3.4 Install source and updates

- Installing from a repository records, per plugin: the repository ID, the installed version and
  the package address. The record lives in the plugin settings, not in the plugin folder.
- Updates come only from that repository. If another repository lists a plugin with the same ID,
  the list shows "Also offered by <repository>", and installing it from there is an explicit
  **Switch source** with a confirmation that names both repositories. This stops a second
  repository from silently replacing a plugin.
- A plugin installed from a local file has no source and is never updated automatically. A shipped
  plugin is updated with CILogg; installing it from a repository makes the repository its source
  (user installs already take precedence over shipped copies).
- Removing or disabling a repository keeps the plugins installed from it. They show "Source
  unavailable" and get no updates until the repository is back or the user switches the source.
- The staged `plugin.json` must have the selected entry's `kind` as well as its ID and version;
  a mismatch rejects the package. An update cannot change a plugin's kind: an installed process
  plugin is never replaced by a configuration pack with the same ID, or the other way round.
- Versions are ordered by one Semantic Versioning 2.0.0 implementation, shared by manifest
  validation, catalog validation and update selection: major, minor and patch compare as numbers;
  a pre-release is lower than its release (`1.0.0-alpha < 1.0.0`); pre-release identifiers compare
  as numbers when numeric and as text otherwise, numeric before text (`1.0.0-alpha.2 <
  1.0.0-alpha.10 < 1.0.0-beta`); build metadata is ignored. Two entries for the same ID and
  platform whose versions are equal in that order but whose packages differ are both left out with
  a warning. An older version is never installed automatically; installing one is an explicit
  action.

### 3.5 Sign-in

**None.** As today: HTTPS with no credential.

**Azure DevOps.** For a repository on `dev.azure.com`. An address in the older
`https://<organization>.visualstudio.com/...` form is converted to
`https://dev.azure.com/<organization>/...` when the repository is added, so the credential request
and the scope always use one host.

1. **Address.** The user pastes the repository address
   (`https://dev.azure.com/<organization>/<project>/_git/<repository>`) or a file link copied from
   the web portal (`...?path=/plugins.json&version=GBcatalog`). CILogg derives the Items API
   address that returns the raw file:
   `https://dev.azure.com/<organization>/<project>/_apis/git/repositories/<repository>/items?path=<path>&versionDescriptor.versionType=branch&versionDescriptor.version=<branch>&$format=octetStream&download=true&api-version=7.1`,
   requested with `Accept: application/octet-stream`, so the reply is the file and not the item's
   metadata. Package addresses in an Azure DevOps catalog may use either form; CILogg converts them
   the same way. The exact parameters are checked against Azure DevOps in E15b.
2. **Git Credential Manager (default).** CILogg runs Git by the absolute path that
   `QStandardPaths::findExecutable("git")` returns (or a Git path the user sets in the trusted
   settings), never a bare `git` that Windows could resolve from the current folder. The command is
   `git -c core.askPass= credential fill`, with arguments as a list, never a shell command line, and
   with `GIT_TERMINAL_PROMPT=0` set and `GIT_ASKPASS` and `SSH_ASKPASS` removed, so Git can neither
   wait on a console nor start another prompt program. Its standard input is `protocol=https`,
   `host=dev.azure.com`, `path=<organization>/<project>/_git/<repository>` and
   `capability[]=authtype`: the request Git makes when it clones that repository. A user who
   already cloned the repository is signed in without a prompt. An interactive sign-in is the credential
   manager's own window, allowed only for operations started in the GUI (section 3.6); every other
   call also sets `GCM_INTERACTIVE=never`. The result is used as follows:
   - `authtype=Bearer` with `credential=<token>` (Git 2.46 or newer): `Authorization: Bearer`.
   - `username`/`password` where the password is a Microsoft Entra access token (a JWT, which is
     what Git Credential Manager returns by default): `Authorization: Bearer <password>`.
   - Any other `username`/`password` (for example a PAT stored by the credential manager):
     `Authorization: Basic`.
   After a successful request CILogg runs `git credential approve` with the complete `fill` output.
   `git credential reject` deletes the credential that Git itself uses, so CILogg runs it only after
   a 401 from an address inside the repository's scope, during an operation the user started in the
   GUI, with the complete `fill` output, and then asks once more with `fill` (the credential manager
   shows its sign-in window). Silent checks and CLI/MCP/Python operations never reject; they mark
   the repository "Sign-in needed".
3. **Personal access token (fallback).** When Git is not installed, the credential manager is
   missing or the user prefers it, **Sign In** asks for a PAT with the Code (Read) scope. It is
   stored in the operating system credential store through QtKeychain (Windows Credential Manager,
   macOS Keychain, Secret Service on Linux) under `CILogg plugin repository <id>`. Without a usable
   store the PAT is kept in memory for the session only, and the dialog says so. It is sent as
   `Authorization: Basic` with an empty user name, as Azure DevOps documents.

Every Azure DevOps request also sends `X-TFS-FedAuthRedirect: Suppress`, so a missing or expired
credential gives 401 instead of a redirect to a sign-in page. An HTML page or a 203 status in reply
to a catalog or package request is treated as "Sign-in needed" and is never parsed as a catalog.

Git, the credential manager and the PAT are only ways to obtain a credential. CILogg does not clone
or fetch Git repositories; all downloads stay in `PluginManager::fetch()` with its limits and
checks.

### 3.6 Credential rules

- **Storage.** Settings hold only `signIn` and `credentialSource`. A PAT lives only in the credential
  store; a credential manager token is fetched for each job and released when the job ends.
- **Never written out.** Credentials do not appear in `settings.ini`, exported settings, logs,
  `get_plugins` or `repositories` results, job results, the R06 activity journal, problem reports
  or the public issue text. The journal records only operation names and key names, as for every
  other command. Messages from `git credential` on standard error are not logged; only its exit
  code is. Redirect addresses are not logged either, because signed download addresses carry
  tokens in their query.
- **Crash dumps.** A crash dump (R06b) is an image of process memory and can hold a token that a
  job was using at the time. The guarantee above covers what CILogg writes, not memory images: R06b
  keeps dumps out of the shareable report zip unless the user adds them, and its notice says that a
  dump can contain secrets and must be shared only privately. Full-memory dumps stay opt-in.
- **Scope.** The repository's scope is its scheme, host and port, and on `dev.azure.com` also the
  path prefix `/<organization>/`. A request is in scope only if its address matches after
  normalization: the path is percent-decoded, the organization is compared without regard to
  case, and a path with `..` segments or an encoded `/` or `\` is out of scope. A credential is
  attached only to requests in scope; a package address outside the scope is downloaded without
  one.
- **Redirects.** Qt follows redirects by copying the original request, and nothing documents that a
  hand-set `Authorization` header is removed on the way, so CILogg assumes it would be forwarded.
  Requests that carry a credential therefore use `QNetworkRequest::ManualRedirectPolicy`: CILogg
  follows at most 5 hops itself, HTTPS only, checks each target against the scope, keeps the
  credential only while the target stays in scope and continues without it once a hop leaves the
  scope. Each hop gets a fresh 30-second timer and the same size limit. Requests without a
  credential keep today's policy.
- **Request origin.** Commander requests gain an `origin` (`gui`, `cli`, `mcp`, `python`,
  `scenario`) that the transport sets and a request payload cannot set or change: the GUI dialogs set
  `gui`, the CLI socket sets `cli`, native MCP sets `mcp`, and so on. The rules below use it.
- **Input.** In the GUI a PAT is typed into a password field. The CLI accepts it only from a file or
  standard input (`--token-file`, `--token-stdin`), never as an argument, so it does not end up in
  the shell history or the process list. Requests with origin `mcp`, `python` or `scenario` cannot
  submit a PAT.
- **Interactive prompts.** Only requests with origin `gui` may show the credential manager's
  sign-in window. For every other origin, and for the silent update check, `repository_sign_in`
  with the credential manager is a silent test that reports `signedIn` or `sign_in_required`. A
  `git credential` call is stopped after 15 seconds when silent and 3 minutes when interactive.

### 3.7 Update checks and auto-update

- A silent check runs about a minute after startup and then every 24 hours while CILogg runs, per
  enabled repository; a repository checked less than 24 hours ago is skipped. Like the application
  updater, the check does not run in automation or headless mode, and a failure is retried at the
  next check without a dialog. A setting next to the application update settings turns it off.
- A check fetches the catalog and compares it with the installed versions from that repository. New
  versions appear as an **Updates (n)** entry in the Plugins menu and on the Update buttons of
  Manage Plugins, with one non-modal notice per new version. **Skip this version** hides a version
  until a newer one appears.
- **Auto-update** is a per-repository switch, off by default. When it is on, a check that finds an
  update downloads, verifies and installs it for plugins already installed from that repository: a
  running plugin is replaced when it stops (as today), a configuration pack at once. Auto-update stays
  within the installed major version; a new major version waits for a click. It never installs a
  new plugin, never enables a disabled one and keeps configuration (as updates do today). Each
  automatic update is journaled and announced in a notice; a failed one is reported once and
  retried at the next check.

### 3.8 Commander, CLI, native MCP and Python

The existing `plugin_manage` action gains these operations; native MCP (`cilogg_execute`) and Python
use the same Commander implementation:

| Operation | Options | Result |
|---|---|---|
| `repositories` | none | Repositories with `id`, `name`, `url`, `signIn`, `credentialSource`, `signedIn`, `enabled`, `autoUpdate`, `lastCheck`, `lastError`. |
| `repository_add` | `name`, `url`, `sign_in`, optional `branch`, `path`, `enabled`, `auto_update` | The new record. Rejects user information in the address and duplicate addresses. |
| `repository_update` | `repository` plus `name`, `enabled`, `auto_update`, `credential_source` | The changed record. The address, branch and path cannot be changed (section 3.1). |
| `repository_remove` | `repository` | Deletes the record and its stored PAT. Installed plugins keep running; their source becomes unavailable. |
| `repository_sign_in` | `repository`, `method` (`git-credential-manager` or `personal-access-token`); a PAT only from the GUI or the CLI (`--token-file` / `--token-stdin`) | A job; the result says whether the sign-in works. Outside the GUI it is a silent test (section 3.6). |
| `repository_sign_out` | `repository` | Deletes a stored PAT. It does not touch the Git credential manager, which Git also uses. |
| `catalog` | optional `repository` | One job; `plugins` (each with `repository` and `kind`) and `repositories` (per repository: state, error, entry count). The old `url` option still works: it adds that address as a repository with sign-in `none` if no repository has it, and fetches it. |
| `install`, `update` | `catalog_entry_id`, as today; `switch_source: true` to change the source | A job. |
| `check_updates` | optional `repository` | A job listing `id`, `installed`, `available`, `repository`, `entry_id`. |

`get_plugins` adds `kind`, `source` (`repository`, `local` or `shipped`, the repository ID and the
installed version) and `update` (the newest version from the source, if any).

## 4. Configuration packs (E16)

### 4.1 The pack

A configuration pack is a plugin package whose `plugin.json` says `"kind": "config-pack"`. It has no
executable and never starts a process. Everything it delivers is listed under `content`; each entry is
optional, and a pack may carry only previews, only Lua, or everything:

```json
{
  "id": "org.example.protocol-config",
  "name": "Example protocol configuration",
  "version": "1.2.0",
  "protocol_version": 1,
  "kind": "config-pack",
  "pack_format": 1,
  "content": {
    "previews": "previews.json",
    "actions": "actions.json",
    "highlights": "highlights.conf",
    "filters": "filters.conf",
    "lua": { "folder": "lua", "load": ["common.lua", "protocol.lua"] },
    "decoder_routes": {
      "previewRoutes": { "Protocol frame": { "dissector": "protocol" } },
      "protocolRoutes": {}
    },
    "python_decoders": [ { "id": "frame-summary", "module": "python/frame_summary.py" } ]
  }
}
```

- `Manifest::parse()` reads `kind` first. For `config-pack` the process rules do not apply, and a
  pack that declares any process key at the top level (`capabilities`, `executable`, `arguments`,
  `actions`, `views`, `configuration`, `enabled_by_default`) is invalid: a pack has no process to
  use them and no settings of its own. `id`, `name`, `version` and `protocol_version: 1` are checked
  as for every plugin, so the identity check of downloads is unchanged.
- The content files use the formats CILogg already imports and exports, so a pack is made by
  exporting a working configuration:
  - `previews`: the format of the user's `previews.json` (`version: 1`, `previews`, `blocks`);
  - `actions`: the format of `actions.json` (actions and automatic responses);
  - `highlights`: an exported highlighter collection (`HighlighterSetCollection`, version 2);
  - `filters`: an exported predefined-filter collection (`PredefinedFiltersCollection`, version 3).
  Preferences and session files are never part of a pack: they belong to one user and machine.
- `lua.load` lists every Lua file of the pack, in loading order: at most 8 files of at most 1 MiB
  each, inside `lua.folder`. Files in the folder that are not listed are an error (section 4.3).
- `decoder_routes` uses the format of the TShark decoder routes. Its `previewRoutes` keys must be
  names of the pack's own previews; its `protocolRoutes` keys are assembled protocol names, as in
  the user's routes. Each list keeps the decoder's limits (128 preview routes, 32 protocol routes).
- `python_decoders` is described in section 4.4; actions, highlighters and filters in section 4.5.

Packs install, update, enable, disable and remove exactly like plugins, from the same repositories
or from a local zip. Operations that need a process (`restart`, plugin actions, views) are rejected
for packs. Packs are platform independent, so their catalog entries use `os: any` and `arch: any`.

### 4.2 Read-only previews

- Previews of enabled packs appear in the preview list with a lock and the pack name. They cannot
  be edited or deleted.
- **Blocks stay inside their pack.** A pack preview resolves `source: block` references only from
  its own pack's `blocks`, and a user preview only from the user's. When previews are merged, a
  pack's blocks are renamed internally to `<pack-id>/<block>` and the pack's references are
  rewritten, so pack blocks never collide with the user's or another pack's.
- **Copy to My Previews** makes an editable copy in `previews.json` together with every block the
  preview needs, directly or through other blocks. A copied block whose name is already used in
  the user's blocks gets a free name (`<block> (<pack name>)`, then a number), and the copy's
  references are rewritten to it. The copy then depends on nothing in the pack: updating,
  disabling or removing the pack does not change it.
- A user preview with the same name as a pack preview takes precedence, the way a user-installed
  plugin takes precedence over a shipped one. The pack preview then shows "Replaced by your copy".
- **Between packs** there is no precedence: enabling a pack (or applying an update) that has a
  preview name already used by another enabled pack is refused, with a message naming both packs.
- **Quotas.** The JSON preview service gives up entirely above 128 previews or 4,096 blocks
  (`PreviewService::definitions()` returns nothing), and it also limits fields, depth and text.
  Enabling or updating a pack is refused if the user's previews and all enabled packs together
  would exceed any of these limits, so a pack can never switch previews off.
- **Exports.** Configuration exports read `PreviewManager::all()` and `blocks()` today, both for the
  live export and for the project snapshot (`configurationexport.cpp`). E16 makes both select only
  the user's previews and blocks, so exports and project snapshots never contain pack definitions.
  A pack update never changes the user's previews or copies.

### 4.3 Lua for TShark

- While a pack is enabled, its Lua files are trusted TShark scripts. Installing the pack from a
  repository the user added, and enabling it, is the trust decision: pack Lua does not need the
  global `allowExternalLua` approval, which still governs the user's own Lua files.
- Pack Lua is merged into the profile when a TShark session starts (the snapshot), never into the
  saved decoder configuration, which keeps its own limits (8 user scripts, 64 KiB).
- The worker treats pack files like the user's: it copies each listed file into its isolated
  folder, loads it with `-X lua_script:` in the listed order and records its SHA-256 in
  `scriptSha256`. Packs load in ID order, then the user's own scripts. Because the worker removes
  `LUA_*` variables and runs only the copies, a pack cannot `require` its own modules in
  `pack_format` 1: a script needed by another is listed before it. A later `pack_format` could copy
  the whole bounded `lua` folder into the isolated folder and set the search path to that copy.
- **One Lua environment per pack.** TShark runs all scripts in one Lua state, so two packs that use
  the same global name would overwrite each other. The worker therefore loads a pack's files
  through a small generated loader that runs them with `load(source, name, "t", env)` in one
  environment table per pack, whose reads fall back to the global table. A pack's files share their
  globals with each other and with nothing else; the Wireshark API (`Proto`, `DissectorTable`, and so
  on) stays reachable through the fallback. The user's own scripts keep running in the global
  environment, as today. E16 checks this loader on the supported Wireshark versions.
- Two scripts that register the same protocol fail visibly, as TShark reports them.
- The profile limit grows from 8 Lua files to 32 in total, the user's own 8 included, with at most
  8 per pack and 1 MiB per file. The limit of 8 is enforced today in four places
  (`previewdecodersettings.cpp` for the saved configuration, and `tsharkpreviewclient.cpp`,
  `message_decoder_preview_worker.py` and `message_decoder_pdu.py` for the profile); E16 raises it
  to 32 in the last three. Enabling a pack that would exceed 32 is refused with a message.
- The `tshark` and `text2pcap` executables remain user settings. A pack cannot set or change those
  paths, and preview requests and imported JSON still cannot name Lua or executables.
- **Pack Lua is unrestricted local code.** The isolated folder and the removed variables only make
  loading predictable; they are not a sandbox. Wireshark's Lua can use `os.execute`, `io.popen` and
  the file functions, so a pack's Lua can start programs and read or write files with the user's
  permissions, exactly like a process plugin. The install, enable and update confirmations of a pack
  with Lua say so ("This pack contains Lua code that runs on this computer"), and auto-update of
  such a pack is the same trust decision as auto-update of a process plugin.
- Enabling, disabling or updating a pack discards the warm TShark sessions, so the next preview
  loads the current scripts.
- `decoder_routes` are merged below the user's routes when the profile is built: a user route with
  the same key wins. Enabling a pack whose routes would take a merged list past 128 preview routes
  or 32 protocol routes is refused with a message.
- If the decoder is disabled or TShark is not configured, the pack's Lua is idle, previews fall
  back to JSON, and the Plugins window shows "Needs TShark (Preview Decoder Settings)".

### 4.4 Python decoders

CILogg has no Python decoder backend for the preview pane yet: the Python helper that ships today
only carries data between CILogg and TShark. Task MD03 plans the supervised decoder contract. Until
it exists, a pack's `python_decoders` are installed and updated with the pack and listed as
"Requires a newer CILogg"; nothing runs them. MD03 activates them without changing the pack format.
A Python decoder that is needed sooner can be shipped as an ordinary process plugin (a `converter`
or `tool`).

### 4.5 Actions, highlighters and filters

The same rules as for previews (section 4.2) apply to the other content of a pack:

- **Read-only, copy to edit.** Pack actions and automatic responses appear in the Actions window,
  pack highlighter sets in the Highlighters dialog and pack predefined filters in the filters list,
  each with a lock and the pack name. **Copy to My Actions / Highlighters / Filters** makes an
  editable copy in the user's own configuration; a pack update never changes a copy.
- **Precedence and collisions.** A user item with the same name takes precedence over the pack's,
  which shows "Replaced by your copy". Two enabled packs with an item of the same name are refused
  at enable or update time, with a message naming both packs.
- **Automatic responses start off.** An automatic response sends data to a device without a click,
  so every response a pack brings is installed switched off, and the user turns on each one (or all
  of the pack's responses at once, after a confirmation that lists them). The on or off choice is
  kept per pack and response name in the user's settings and survives pack updates. Responses keep
  the existing coupling with Actions (decision D05). Pack actions only run when the user or a
  scenario runs them, as for the user's own actions.
- **Highlighter sets** from packs are offered in the set list; which set is active stays the user's
  choice.
- **Exports** and project snapshots contain only the user's items, as for previews.
- **Limits.** The existing limits of the action, highlighter and filter stores apply to the user's
  items and all enabled packs together; enabling or updating a pack that would exceed one is
  refused with a message.

## 5. Security

- Plugins are programs and Lua is code. The protection is that CILogg installs only from
  repositories the user added, only over HTTPS, with a SHA-256 check, and installing stays a user
  action unless the user turned auto-update on for that repository.
- The SHA-256 comes from the same repository as the package. It protects against a damaged or
  partial download and against a package changed after the catalog was written, not against a
  repository whose catalog and packages were both replaced. Package signing is not part of this
  design (open decision 6).
- Update-source pinning (section 3.4) prevents a second repository from taking over a plugin ID.
- Credentials stay inside their scope (section 3.6). An unauthenticated catalog may list packages
  anywhere, but the credential of another repository is never attached to them.
- A pack's Lua is code with the user's permissions (section 4.3); nothing in this design sandboxes
  it. Pack previews follow the same validation and limits as user previews.
- Pack actions send data to devices only when run; pack automatic responses start switched off
  (section 4.5).
- Crash dumps can contain tokens that were in memory (section 3.6); they are not shared unless the
  user adds them.

## 6. Compatibility

- The plugin protocol version stays 1. Process plugins do not change.
- `plugins.json` stays at schema 1; old hosts ignore the new keys and skip `os: any` entries.
- An old CILogg that shares the plugin folder never runs a pack: the pack has no executable, so the
  old version lists it as an invalid plugin. Going back to a CILogg without E15/E16 leaves the
  `repositories` settings unread and installed plugins working.

## 7. A private repository on Azure DevOps

This section is a recipe for any organization. Names in angle brackets are placeholders.

**Layout.** One Azure DevOps Git repository holds the sources of the catalog on its main branch and
the built catalog on a `catalog` branch that only the pipeline writes:

```text
main branch
  sources.json            what to package (see below)
  packs/<pack>/           configuration packs authored here: plugin.json and its content files
  tools/build_catalog.py  copy of the klogg script, taken at a recorded klogg commit
  azure-pipelines.yml     the build
catalog branch (written by the pipeline only)
  plugins.json
  packages/<id>-<version>.zip
```

`sources.json` lists each package: the folder (in this repository or in another repository of the
same project), the branch, the platforms, and files to leave out (tests, documentation):

```json
{ "packages": [
  { "repository": "self", "path": "packs/<pack>", "os": ["any"], "arch": ["any"] },
  { "repository": "<other-repository>", "ref": "refs/heads/main", "path": "<folder with plugin.json>",
    "os": ["windows"], "arch": ["x86_64", "arm64"], "exclude": ["tests/**", "docs/**"] }
] }
```

**Build.** E15 adds `scripts/plugins/build_catalog.py`, generalized from
`scripts/plugins/package_examples.py`: it builds deterministic zips (fixed timestamps, stored
entries, LF line endings for text, as today), computes each SHA-256, and writes `plugins.json` with
absolute Items API addresses for a given organization, project, repository and branch. It refuses to
replace an existing `<id>-<version>.zip` with different bytes, so a changed plugin must change its
version, and identical rebuilds are no-ops.

**Pipeline.** Triggered by changes on the main branch, by changes to the plugin folders in the
other repositories, or by hand. It checks out the sources, runs the script against a checkout of
the `catalog` branch and pushes one commit that updates the packages and `plugins.json` together, so
a catalog never names a missing package:

```yaml
trigger: { batch: true, branches: { include: [ main ] } }
resources:
  repositories:
    - repository: plugin_source
      type: git
      name: <project>/<other-repository>
      ref: refs/heads/main
      trigger: { branches: { include: [ main ] }, paths: { include: [ <plugin folder> ] } }
pool: { vmImage: ubuntu-latest }
steps:
  - checkout: self
    persistCredentials: true
  - checkout: plugin_source
  - script: |
      set -e
      cd "$(Build.SourcesDirectory)/<this repository>"
      git fetch origin catalog
      git worktree add "$(Agent.TempDirectory)/catalog" origin/catalog
      python tools/build_catalog.py --sources sources.json --checkouts "$(Build.SourcesDirectory)" \
        --catalog "$(Agent.TempDirectory)/catalog" \
        --organization <org> --project <project> --repository <this repository> --branch catalog
      cd "$(Agent.TempDirectory)/catalog"
      git add -A
      git diff --cached --quiet || {
        git -c user.name="Catalog pipeline" -c user.email="pipeline@invalid" \
          commit -m "catalog: build $(Build.BuildNumber)"
        git push origin HEAD:catalog
      }
```

`tools/build_catalog.py` is a copy of the klogg script taken at a recorded klogg commit, so the
pipeline does not depend on the public repository at build time. `batch: true` keeps runs from
piling up; if a push is still rejected because another run moved `catalog` first, the run fails
without changing anything and the next run builds again. The exact YAML is checked on its first
run. The project's build service account needs Read on the source repositories and Contribute
on the catalog repository. Users need only Read on the catalog repository.

**Users** add the repository once (Plugins > Manage Plugins > Repositories > Add, sign-in Azure
DevOps, address `https://dev.azure.com/<org>/<project>/_git/<repository>`) and are signed in through
the Git Credential Manager they already use for that organization.

## 8. Tests

- **E15:** catalog merge with one repository failing; one invalid entry left out while the rest of
  its repository loads; entries of an unsupported `kind` hidden; migration of a changed
  `catalogUrl`; entry IDs that differ per repository; update only from the install source and the
  explicit source switch; repository IDs unique and never reused; the address not editable; removal
  deleting the stored PAT; a staged package whose `kind` differs from its entry rejected; Semantic
  Versioning order with pre-release and build-metadata boundary cases (`1.0.0-alpha.2`,
  `1.0.0-alpha.10`, `1.0.0+build`); update check deadline, automation skip and auto-update within
  the major version; a fake `git credential` helper (Bearer, JWT password, Basic, failure, timeout, the
  environment it receives: no askpass variables, `GIT_TERMINAL_PROMPT=0`, `GCM_INTERACTIVE=never`
  outside the GUI; approve and reject calls with the complete `fill` output, no reject from silent
  checks); `visualstudio.com` addresses converted to `dev.azure.com`; PAT through a fake credential
  store; the credential absent from settings, logs, journal, job results and problem reports; no
  credential outside the scope, for paths with `..` or encoded slashes, or after a redirect that
  leaves the scope; 203 and HTML replies treated as sign-in needed; the request `origin` set by each
  transport and not settable from the payload; the CLI refusing a token argument; MCP and Python
  refusing a PAT and never prompting. A local HTTPS test server answers catalog and package
  requests and records the headers.
- **E16:** pack manifest validation (`kind` read first, no process keys including `configuration`,
  `protocol_version: 1`, every Lua file listed, count and size limits, preview routes naming only
  pack previews); read-only pack previews; Copy to My Previews with the block closure and renamed
  colliding blocks, unchanged after the pack is updated or removed; pack blocks resolved only inside
  their pack; user preview precedence; a preview-name collision between two packs refused; the
  aggregate preview quotas; exports and project snapshots without pack definitions; pack Lua in the
  profile snapshot only while enabled, copied, hashed and loaded in order, never written into the
  saved configuration, in a per-pack environment where two packs with the same global keep their
  own values; the 32-file and route limits; warm sessions discarded on update; JSON fallback
  without TShark; `python_decoders` listed and inactive; pack actions, highlighters and filters
  read-only with Copy to My..., user precedence, collisions between packs refused, store limits,
  exports without pack items; pack automatic responses off after install and their on or off choice
  kept across an update.
- Both: GUI, CLI, native MCP and Python give the same results; `[plugins]` and new `[workbench][E15]`
  / `[workbench][E16]` Catch2 cases; `tests/plugins` for the catalog script.

## 9. Slices

- **E15a:** repository list, migration, merged catalog with sources, install-source pinning,
  `build_catalog.py`, Commander/CLI/MCP/Python operations for public repositories.
- **E15b:** Azure DevOps sign-in (credential manager, PAT store), credential scope and redirect
  rules.
- **E15c:** update check, notices, auto-update.
- **E16a:** configuration pack kind with JSON previews and Lua for TShark.
- **E16b:** Python decoders, activated with MD03.
- **E16c:** actions with automatic responses (off until enabled), highlighters and predefined
  filters in packs.

## 10. Open decisions

1. **Sign-in:** Git Credential Manager first, PAT as the fallback (recommended), or PAT only.
2. **Azure layout:** a Git repository with a pipeline that writes a `catalog` branch (recommended),
   or an Azure Artifacts feed.
3. **Updates:** notify only, with auto-update as a per-repository switch that is off by default and
   stays within a major version (recommended).
4. **PAT storage:** QtKeychain on all platforms (recommended; a new third-party dependency), or the
   Windows Credential Manager only.
5. **Python decoders:** carried in packs now and activated by MD03 (recommended), or delivered as
   process plugins until then.
6. **Package signing:** not in E15 (recommended); SHA-256 from an authenticated repository is the
   integrity check. Signing could be added later as a catalog key.
7. **Approval and order:** whether E15 (and then E16) may start ahead of G02/G03, as E13a and R06
   did.
