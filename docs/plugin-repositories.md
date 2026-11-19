# Plugin repositories and configuration packs (E15, E16) — accepted design

**Status:** accepted on 2026-11-17; the user approved every decision in section 10 (decision D16 in
[docs/workbench/decisions.md](workbench/decisions.md)). Slice E15a (public repositories, the
Plugins and Packs window, the merged catalog, install-source pinning, `build_catalog.py` and the
in-app guide) is merged (PR https://github.com/dm17ryk/klogg/pull/122); slice E15b (Azure DevOps
sign-in, the token store, credential scope and redirects, request origin) is implemented in PR
https://github.com/dm17ryk/klogg/pull/124; E15c and E16 are not. The tasks are
[E15](../plans/cilogg-workbench-2026-10-21/tasks/E15.md) (plugin repositories), which may start
ahead of G02/G03, and then [E16](../plans/cilogg-workbench-2026-10-21/tasks/E16.md) (configuration
packs).

**Request (2026-11-17):** can CILogg install and update plugins from a private Azure DevOps
repository? An internal tool plugin must not be published publicly. Message previews (the JSON
preview definitions, Lua decoders and Python decoders) should be delivered and updated the same way,
possibly as plugins. Licensed protocol Lua sources cannot be public either.

**Follow-up (2026-11-17):** the user removed the private working configuration (previews, actions,
highlighters, predefined filters) from the public repository and asked to deliver it, with the
TShark Lua, as a plugin from the private Azure DevOps repository. Packs therefore carry actions,
highlighters and filters as well as previews and decoders.

**Requirements (Dmitry, 2026-11-17):** "now we have one
`https://raw.githubusercontent.com/dm17ryk/klogg/master/plugins.json`, and it is in edit box, we
need table of repos add/remove button and ordering buttons, so we can load plugins from different
repos download and install them. also add description/procedure in cilogg how to pack plugins, how
to create your own plugin repo and use it." Sections 3.3 (the ordered table), 3.9 (the in-app guide)
and decision 7 cover this.

**Requirements (Dmitry, 2026-11-17, later):** "same with highlighters, actions, responses,
previewers window with repo management ability do download and install/update/remove so all this
can also be shared online with public and private repos", "and filters", and: "thinking about it, in
opposite to code plugins like CPF/ADB/SysLog, text json/lua/python/klogg format files like in
highlighters and filters we can have conflicts - when user changed it locally and want to update -
we need to be able to merge it somehow". Sections 3.3 (one window for plugins and packs), 4.6 (local
edits and merge on update) and 4.7 (Export as Pack) cover this.

**Decisions and requirements (Dmitry, 2026-11-17, later):** the pack editing model is "edit in
place with merge", and the proposed merge UI is approved ("ok, go with your recommendation"). He
added: "we can also internally use git for automatic merging, and so also think how user can post
his additions/changes to this files". Section 4.6 (the local pack store), section 4.8 (sharing
changes back) and decisions 8 to 10 cover this; section 10 lists all decisions.

## 1. Summary

- The Plugins window manages an ordered table of **plugin repositories**. Each one is a
  `plugins.json` catalog (schema 1, unchanged) at an HTTPS address. Users add, remove, reorder and
  turn repositories on or off; the order is a priority. The same operations work through Commander,
  the CLI, native MCP and Python.
- A repository signs in with **none** (public HTTPS) or **Azure DevOps**. Azure DevOps reuses the
  Git Credential Manager that Git already uses (`git credential fill`), so a user who can clone the
  repository needs no extra setup. A personal access token (PAT) is the fallback, kept in the
  operating system's credential store. Credentials are never written to settings, exports, logs,
  the activity journal or problem reports, are sent only to the repository's own host and
  organization, and are never forwarded across redirects.
- One **Plugins and Packs** window (tabs Installed, Available, Repositories) replaces Manage
  Plugins and Browse Plugins. Its Available tab shows one list merged from all enabled
  repositories, grouped in repository order, with Kind and Source columns and a Kind filter. A plugin always updates from the repository it was installed from.
- An **in-app guide** (in Help > Documentation, and from the Repositories dialog) explains how to
  pack a plugin, how to create a repository of your own (public or private) and how to use it.
- CILogg checks for updates **silently at startup and once a day**, like the application updater.
  Installing stays a user action unless **auto-update** is turned on for that repository.
- A new plugin kind, the **configuration pack**, carries preview definitions (the JSON of
  `previews.json`), Lua decoders for TShark, Python decoders, actions with automatic responses,
  highlighters and predefined filters. Packs are versioned and installed, updated and removed in
  the same window as plugins, from the same repositories. Their items can be edited in place: the
  installed version stays as the merge base, and an update merges the local edits (three-way, with a
  dialog for conflicts). A pack's automatic responses start switched off, TShark gets the pack's Lua
  folder as a trusted path, because installing from a repository the user added is the trust
  decision. **Export as Pack** turns the user's own items into a pack to share, and **Propose
  Changes** sends edits of an installed pack back to its source repository as a pull request.
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
  table (Plugin, Version, Description) and Install / Update Selected. The default catalog on master
  is empty (`{"schema_version": 1, "plugins": []}`), so Fetch shows an empty table and the status
  line, which prints `state + ": " + error`, reads only "completed:".
- Help > Documentation shows `DOCUMENTATION.md`, compiled at build time into the
  `documentation.html` resource, in a text browser window (`MainWindow::documentation()`).
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

- **Order.** The repositories form an ordered list, stored as the list of their IDs
  (`repositories/order`). A new repository is added at the end; the migrated `default` repository
  starts first. The order is a priority (section 3.3).
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
- `contents`: for packs, the content types they carry (`previews`, `actions`, `highlights`,
  `filters`, `lua`, `python_decoders`), so the Kind filter works before anything is downloaded.
  `build_catalog.py` fills it from the pack's `plugin.json`.
- `source_repository`: for packs, where the pack is authored (Git repository address, path and
  branch), used by Propose Changes (section 4.8).

The host-generated `entry_id` also covers the repository ID, so the same package listed by two
repositories gives two distinct entries.

**Invalid entries.** Today one invalid entry for the host's platform fails the whole catalog. With
several repositories, an invalid entry fails only itself: it is left out, counted in the
repository's warnings and named in its status line, and the rest of that repository still loads. An
unreadable `plugins.json` (bad JSON, wrong schema, too large, an HTML sign-in page) still fails its
whole repository, and only that repository.

### 3.3 One window for plugins and packs

The Manage Plugins and Browse Plugins dialogs become one **Plugins and Packs** window with three
tabs. Install, Update and Remove behave the same for every kind of package.

- **Installed:** every installed plugin and pack, with Name, Kind, Version, Source (a repository
  name, "Local file" or "Shipped with CILogg") and State ("Enabled", "Running", "Update to 1.3.0",
  "Edited locally (3 items)", "Update needs a merge"). Buttons: **Update**, **Remove**,
  **Enable / Disable**, **Configure** (process plugins), **Reset to Pack Version** and
  **Export as Pack…** (packs, sections 4.6 and 4.7). What Manage Plugins shows today
  (configuration, storage limits, the captures folder, restart) stays on this tab.
- **Available:** the merged list from all enabled repositories, grouped in repository order, with
  Name, Kind, Version, Source, Installed (the installed version) and Description, a **Kind** filter
  (All, Plugins, Previewers, Decoders (Lua and Python), Actions and responses, Highlighters,
  Filters), a search box, **Fetch** and **Install / Update**. Fetch refreshes every enabled
  repository in parallel (at most 4 at a time) in one job. A repository that fails does not hide
  the others: its group says "Sign-in needed", "Not found" or the network error, with a Sign In
  button where it helps.
- **Repositories:** a table with one row per repository, in order: position, Enabled (check box),
  Name, Address, Sign-in, State (signed in, sign-in needed, number of packages, "No plugins in this
  repository"), Last check and Auto-update (check box). The buttons are **Add**, **Edit**, **Remove**,
  **Move Up**, **Move Down**, **Sign In**, **Sign Out** and **How to...** (section 3.9). Adding an
  Azure DevOps repository tests the address and the sign-in before saving. Today's single URL field
  is gone; its address is the first row after migration.
- **Entry points.** The Plugins menu keeps Manage Plugins… (opens Installed) and Browse Plugins…
  (opens Available); the Commander `plugin_manage` contract is unchanged. The Highlighters dialog,
  the predefined filters dialog, the Actions window and the preview settings each get a **Packs…**
  button that opens Available filtered to their kind, and their own lists show installed pack items
  with the pack's name (sections 4.2 and 4.5).
- **Kind of a package.** Process plugins are "Plugins". A pack whose `contents` (section 3.2) has one
  type is listed as that type: a pack with only filters is "Filters", only highlighters
  "Highlighters", only previews (with or without their Lua) "Previewers", only Lua or Python
  "Decoders", actions and responses "Actions and responses". A pack with several types is
  "Configuration pack", and the details pane lists its types; the Kind filter finds it under each
  of them.
- **The order is a priority.** The Available tab groups its list by repository in this order. When
  two repositories offer the same package ID, both rows are shown with their source, and a new
  install takes the entry from the higher repository unless the user picks the other row.
  Installed packages stay pinned to the repository they came from (section 3.4): moving a
  repository up or down never changes an installed package's source, never offers it as an update
  from another repository and never installs anything.
- **Status texts are words, never a bare job state.** A repository whose catalog has no entry for
  this platform shows "No plugins in this repository" (in its group, in the status area and in the
  Repositories table). An empty merged list says "No plugins in the enabled repositories" with a
  link to the guide. A finished fetch says, for example, "12 packages from 2 repositories"; a
  running one "Loading 2 repositories…".
- Install and Update use the selected entry (`catalog_entry_id`), as today. Updating a pack that the
  user edited merges the edits (section 4.6).

### 3.4 Install source and updates

- Installing from a repository records, per plugin: the repository ID, the installed version and
  the package address. The record lives in the plugin settings, not in the plugin folder.
- A new install of a plugin that several repositories offer takes the higher repository's entry by
  default (section 3.3); the user can pick another row.
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
   wait on a console nor start another prompt program. Git runs in the temporary folder with
   `GIT_CEILING_DIRECTORIES` set to its parent, so the configuration of a repository that CILogg
   happens to be started in cannot name another credential helper. Its standard input is `protocol=https`,
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
  the Installed tab, with one non-modal notice per new version. **Skip this version** hides a version
  until a newer one appears.
- **Auto-update** is a per-repository switch, off by default. When it is on, a check that finds an
  update downloads, verifies and installs it for plugins already installed from that repository: a
  running plugin is replaced when it stops (as today), a configuration pack at once. Auto-update stays
  within the installed major version; a new major version waits for a click. It never installs a
  new plugin, never enables a disabled one and keeps configuration (as updates do today). Each
  automatic update is journaled and announced in a notice; a failed one is reported once and
  retried at the next check.
- **Packs with local edits** are auto-updated only when the merge has no conflicts (section 4.6);
  otherwise they show "Update needs a merge" and wait for the user.

### 3.8 Commander, CLI, native MCP and Python

The existing `plugin_manage` action gains these operations; native MCP (`cilogg_execute`) and Python
use the same Commander implementation:

| Operation | Options | Result |
|---|---|---|
| `repositories` | none | Repositories in order, with `position`, `id`, `name`, `url`, `signIn`, `credentialSource`, `signedIn`, `enabled`, `autoUpdate`, `lastCheck`, `lastError`, `pluginCount`. |
| `repository_add` | `name`, `url`, `sign_in`, optional `branch`, `path`, `enabled`, `auto_update` | The new record. Rejects user information in the address and duplicate addresses. |
| `repository_update` | `repository` plus `name`, `enabled`, `auto_update`, `credential_source` | The changed record. The address, branch and path cannot be changed (section 3.1). |
| `repository_remove` | `repository` | Deletes the record and its stored PAT. Installed plugins keep running; their source becomes unavailable. |
| `repository_move` | `repository` and either `position` (1-based) or `direction` (`up`, `down`) | The repositories in their new order. Changes no installed plugin. |
| `repository_sign_in` | `repository`, `method` (`git-credential-manager` or `personal-access-token`); a PAT only from the GUI or the CLI (`--token-file` / `--token-stdin`) | A job; the result says whether the sign-in works. Outside the GUI it is a silent test (section 3.6). |
| `repository_sign_out` | `repository` | Deletes a stored PAT. It does not touch the Git credential manager, which Git also uses. |
| `catalog` | optional `repository` | One job; `plugins` in repository order (each with `repository` and `kind`) and `repositories` (per repository: state, error, entry count; `empty: true` for "No plugins in this repository"). The old `url` option still works: it adds that address as a repository with sign-in `none` if no repository has it, and fetches it. |
| `install`, `update` | `catalog_entry_id`, as today; `switch_source: true` to change the source; for packs with local edits `strategy` (`fail` by default, `keep_mine`, `take_theirs`) | A job. A pack update with conflicts and `strategy: fail` refuses and lists the conflicts (pack, kind, item, field). |
| `pack_edits` | `plugin` | The pack's local changes (kind, item, edited, added or deleted). |
| `pack_history` | `plugin`, optional `item` | The pack's versions and local commits. |
| `pack_reset` | `plugin`, optional `item` (kind and identity) | Restores the pack version of the item or of the whole pack, as a change that can itself be undone. |
| `pack_export_changes` | `plugin`, `output` | Writes a patch of the local changes in the pack's file formats. |
| `pack_propose` | `plugin`, `confirm: true`, optional `title` and `description` | Proposes the local changes as a pull request (section 4.8). Refused without `confirm: true`. Returns the pull request address, or why it could not be created. |
| `pack_export` | item selectors by kind and name or ID, `id`, `name`, `version`, `description`, `output` (folder or `.zip`) | Writes the pack; returns its path and contents. |
| `check_updates` | optional `repository` | A job listing `id`, `installed`, `available`, `repository`, `entry_id`. |

`get_plugins` adds `kind`, `contents` (packs), `source` (`repository`, `local` or `shipped`, the
repository ID and the installed version), `update` (the newest version from the source, if any) and
`localEdits` (the number of locally edited items of a pack).

### 3.9 In-app guide: pack a plugin, create a repository, use it

The guide is a new section of `DOCUMENTATION.md`, "Plugins: packing, repositories and updates", so
it ships in every build through the existing `documentation.html` resource and needs no network.
The Repositories dialog's **How to...** button and a new **Help > Plugin Repositories Guide** entry
open the documentation window at that section (`QTextBrowser::scrollToAnchor`); Help >
Documentation shows it with the rest. Its examples are generic (no organization names) and follow
the design as built. It has five parts:

1. **Pack a plugin.** The package layout (`plugin.json` at the root, the program or the content
   files beside it, no links or `..` paths), the `plugin.json` fields for process plugins (with a
   link to the [plugin SDK](plugin-sdk.md)) and for configuration packs (export the previews,
   actions, highlighters and filters from CILogg, add the Lua, write `kind: config-pack`), version
   numbers (raise the version for every change; a published version never changes), and building
   the zips, SHA-256 values and `plugins.json` with `python scripts/plugins/build_catalog.py`.
   **Export as Pack…** (section 4.7) writes a pack from the user's own filters, highlighters,
   actions and responses, previews and Lua or Python files.
2. **Create your own repository.**
   - Public: any HTTPS static host serves `plugins.json` and the zips, for example a GitHub
     repository (the `raw.githubusercontent.com` address of `plugins.json`, zips as files or release
     assets), GitHub Pages, or a company web server. Publish the zips first, then the catalog that
     names them.
   - Private: the Azure DevOps recipe of section 7 (a repository, or a folder of an existing private
     repository, with a pipeline that writes the catalog to its own branch), and what readers need
     (Read access, Git for Windows with its credential manager, or a PAT with Code (Read)).
3. **Use it in CILogg.** Repositories > Add (name, address, sign-in), the order and what it means,
   Fetch and Install on the Available tab, the Kind filter and the Packs… buttons, the update
   notice and Update button, editing pack items and updating a pack you edited (the merge dialog,
   Reset to Pack Version, section 4.6), auto-update, switching a plugin's source, removing a repository, and the same steps through the CLI
   (`cilogg command --action plugin_manage --plugin-operation repository_add --json-file ...`).
4. **Share your changes.** View Changes, Export Changes and Propose Changes (section 4.8), what
   the maintainer does with them (review, raise the version, let the pipeline publish), and how the
   next update then clears the local changes.
5. **Troubleshooting.** "Sign-in needed", "Not found", "No plugins in this repository",
   "SHA-256 mismatch", "Package identity differs from catalog", and entries hidden because the
   host is too old or the kind is not supported.

The developer-facing parts of `docs/plugin-sdk.md` ("Packages and catalog publication") are updated
to match and link to the guide.

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

### 4.2 Pack items in CILogg's lists

- Previews of enabled packs appear in the preview list with the pack's name. They can be edited in
  place (section 4.6) or copied.
- **Blocks stay inside their pack.** A pack preview resolves `source: block` references only from
  its own pack's `blocks`, and a user preview only from the user's. When previews are merged, a
  pack's blocks are renamed internally to `<pack-id>/<block>` and the pack's references are
  rewritten, so pack blocks never collide with the user's or another pack's.
- **Copy to My Previews** still makes an independent copy in `previews.json` together with every
  block the preview needs, directly or through other blocks. A copied block whose name is already
  used in the user's blocks gets a free name (`<block> (<pack name>)`, then a number), and the
  copy's references are rewritten to it. A copy has no link to the pack: updates never reach it. To
  keep receiving updates, edit the pack item in place instead.
- A user preview with the same name as a pack preview takes precedence, the way a user-installed
  plugin takes precedence over a shipped one. The pack preview then shows "Replaced by your item".
- **Between packs** there is no precedence: enabling a pack (or applying an update) that has a
  preview name already used by another enabled pack is refused, with a message naming both packs.
- **Quotas.** The JSON preview service gives up entirely above 128 previews or 4,096 blocks
  (`PreviewService::definitions()` returns nothing), and it also limits fields, depth and text.
  Enabling or updating a pack is refused if the user's previews and all enabled packs together
  would exceed any of these limits, so a pack can never switch previews off.
- **Exports.** Configuration exports read `PreviewManager::all()` and `blocks()` today, both for the
  live export and for the project snapshot (`configurationexport.cpp`). E16 makes both select only
  the user's own previews and blocks, so exports and project snapshots never contain pack items
  (edited or not). Export as Pack (section 4.7) is the way to share pack items.

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

### 4.5 Actions, responses, highlighters and filters

The rules of section 4.2 apply to the other content of a pack:

- **Shown with the pack's name, editable in place.** Pack actions and automatic responses appear in
  the Actions window, pack highlighter sets in the Highlighters dialog and pack predefined filters
  in the filters list, each with the pack's name. They can be edited in place (section 4.6), and
  **Copy to My Actions / Highlighters / Filters** makes an independent copy.
- **Identity.** Each item is identified within its pack: actions and responses by their `id` (only
  unique within one file, so they are namespaced `<pack-id>/<id>` next to the user's), highlighter
  sets and predefined filters by their `id` (a UUID), previews and blocks by their name. A user item
  with the same name takes precedence over the pack's, which shows "Replaced by your item". Two
  enabled packs with an item of the same identity or name are refused at enable or update time,
  with a message naming both packs.
- **Automatic responses start off.** An automatic response sends data to a device without a click,
  so every response a pack brings is installed switched off, and the user turns on each one (or all
  of the pack's responses at once, after a confirmation that lists them). Responses keep the
  existing coupling with Actions (decision D05). Pack actions only run when the user or a scenario
  runs them, as for the user's own actions.
- **User state is not content.** Whether an action or response is on, and which highlighter sets
  are active, is the user's state, kept per pack and item in the user's settings. It survives pack
  updates, is never a local edit and never causes a merge conflict.
- **Exports** and project snapshots contain only the user's own items, as for previews.
- **Limits.** The existing limits of the action, highlighter and filter stores apply to the user's
  items and all enabled packs together; enabling or updating a pack that would exceed one is
  refused with a message.

### 4.6 Local edits and merge on update

**Decided (Dmitry, 2026-11-17): edit in place with merge.** Process plugins (for example a CPF tool,
ADB or Syslog) are replaced whole on update. Pack content is text the user edits (preview JSON, Lua,
Python, actions and responses, highlighter and filter collections), so pack items are edited in
place, the installed version stays the merge base, and an update is a three-way merge that never
overwrites the edits or silently ignores the new version.

**The local pack store (decisions 8 and 9).** Each installed pack has a local Git
repository in the user's plugin data (`pack-store/<pack-id>/`). Users never see Git: the store is
internal, and CILogg reads a pack's effective content from it.

- Branch `upstream` gets one commit per installed version, exactly as shipped, with a message such
  as "Install 1.2.0 from Team packs". Branch `local` carries the user's edits as commits on top:
  CILogg commits when the user saves a pack item, with a readable message ("Edit filter 'Software
  or Reset'", "Delete highlighter set 'Radio'").
- **Canonical form.** Structured content is stored one item per file (`filters/<id>.json`,
  `highlights/<set-id>.json`, `actions/<id>.json`, `responses/<id>.json`, `previews/<name>.json`,
  `blocks/<name>.json`; unsafe characters of a name are encoded), as JSON with sorted keys and one
  member per line, and the rules of a highlighter set one per line. Unrelated edits then never touch
  the same lines. Lua and Python files are stored as they are. The canonical form converts to and
  from the pack's file formats without loss (round-trip tested), so a pack is still built from and
  exported to the normal files.
- **Update** commits the new version on `upstream` and merges `upstream` into `local`; Git finds
  the merge base. `.gitattributes` gives the structured item files a CILogg merge driver, run in
  process through libgit2's custom merge driver interface. It merges item by item and field by field with the rules below. Lua and Python
  files use Git's text merge. What stays unresolved opens the merge window in the GUI; from
  automation the strategy applies.
- **History for free.** View History (per pack or item), Revert to an earlier version, and
  **Reset to Pack Version** (per item or the whole pack, as a new `local` commit that restores the
  `upstream` content, so it can be undone).
- **Engine (decision 9).** libgit2 bundled and built without network support (no HTTPS or SSH
  transport, so it stays small; GPLv2 with the linking exception; supports custom merge drivers and
  `git_merge_file`), so packs work on computers without Git. E15 sign-in keeps using
  `git credential fill` when Git is present, as designed.
- **Rebuild.** A damaged store is rebuilt from the installed version: CILogg first exports the
  local edits it can still read (as a patch and as a pack), then creates a new store and re-applies
  them as one commit.
- **Not chosen (decision 8):** an overlay with base hashes and a merge implemented in CILogg. It
  would give no history and need its own base bookkeeping.

**Merge rules** (the merge driver) per item, with base = the installed version,
theirs = the new version and mine = the local edit:

- not edited locally: take theirs;
- edited locally, unchanged in the new version: keep mine;
- changed on both sides in different fields: merge the fields automatically; the same new value on
  both sides is not a conflict;
- the same field changed differently on both sides, or deleted on one side and changed on the other:
  a conflict;
- new in the new version: added; removed from the new version and not edited locally: removed.

Fields are the JSON members of an item (nested objects recursively). Arrays whose elements have an
identity (preview `fields` by `name`) merge per element; other arrays are single values. Ordered
lists without identities, such as the rules of a highlighter set, merge like text with one canonical
line per rule.

**Merge UI. Decided (Dmitry, 2026-11-17):**

- **Item merge window** for structured items (highlighters, filters, actions, responses, previews).
  No off-the-shelf tool understands these items, and a text merge of INI or JSON gives confusing
  conflicts (order, quoting, regex escapes). The window shows a tree, pack > item > field, each row
  marked added, removed, changed or conflict, with Base, Mine and Theirs columns and an editable
  Result. Per row: **Keep Mine**, **Take Theirs**, **Edit** and **Keep Both** (the new version stays
  the pack item, and mine becomes the user's own item, renamed "<name> (mine)"); for all rows:
  **Keep Mine for All** and **Take Theirs for All**. The decision itself is per field.
- **Three-pane text merge** for Lua and Python: Base, Mine and Theirs side by side, an editable
  Result below, and per-hunk buttons to take a side. The three-way text merge is libgit2's
  `git_merge_file` (the same xdiff code as `git merge-file`); tests check it against
  `git merge-file` results.
- **Open in external merge tool** (optional) for text files, configured like git mergetool: a
  command template with `$BASE`, `$LOCAL`, `$REMOTE` and `$MERGED`, and presets for Beyond Compare,
  KDiff3, WinMerge, Meld and P4Merge. CILogg reads `$MERGED` back and checks it (UTF-8, size limit,
  no conflict markers) before saving.
- Not used: embedding KDiff3 (needs KDE Frameworks), Meld (GTK and Python) or Qt Creator's diff
  editor (two-way only and tied to Creator).
- Nothing is applied until **Apply**. **Cancel** leaves the old version installed and the edits
  untouched. Apply installs the new version and the merge result in one step (staged first; a
  failure restores both). An item removed from the new version but kept with Keep Mine becomes the
  user's own item.

**Automation.** Commander, the CLI, native MCP, Python and scenarios never open a merge window.
`update` of a pack with conflicts reports them and refuses unless given a `strategy`: `fail` (the
default), `keep_mine` or `take_theirs`; merges without conflicts apply as in the GUI.

**Silent checks and auto-update** apply an update with local edits only when it merges without
conflicts. Otherwise the pack shows "Update needs a merge" and a notice offers to open the merge.

**Export as Pack** of an edited pack item exports its current content (section 4.7).

### 4.7 Export as Pack

**Export as Pack…** (on the Installed tab, and in the Highlighters, filters, Actions and preview
windows) shares a user's own items:

- The user picks items from their filters, highlighter sets, actions and responses, previews (with
  the blocks they need, collected automatically) and Lua or Python files, including pack items they
  edited (their current content), then gives an ID, a name, a version and a description.
- CILogg writes a pack folder or zip: `plugin.json` with `kind: config-pack` and a `content`
  object, and the content files in the formats CILogg imports and exports. Responses are exported
  with their content; on another computer they arrive switched off. Nothing from the user's
  preferences or session is included, and no path outside the pack is written.
- **Export Changes** (section 4.8) exports only what changed in an installed pack, as a patch.
- The output is ready for `scripts/plugins/build_catalog.py` (section 7) and can also be installed
  directly as a local zip. The guide (section 3.9) covers export, publishing to a public or private
  repository and installing elsewhere.
- Commander, the CLI, native MCP and Python get `pack_export` with the same choices (item
  selectors by kind and name or ID, the pack identity and the output path).

### 4.8 Sharing changes back (Contribute)

Users can send their edits and additions of an installed pack back to the people who maintain it
(decision 10).

- **Where a pack is authored.** A catalog entry of a pack may carry an optional `source_repository`
  object: the address of the Git repository where the pack is authored, the path of the pack in it
  and the branch, for example `packs/<pack>` on `main` in the section 7 layout. `build_catalog.py`
  writes it from `sources.json`. (The name differs from the `source` field of `get_plugins`, which
  is the repository a package was installed from.) Without it, a pack's changes can only be
  exported.
- **Seeing changes.** The Installed tab shows "Local changes" for a pack whose `local` branch has
  commits, and offers **View Changes** (what differs from the installed version, per item and field,
  and per line for text), **Export Changes** and **Propose Changes…**.
- **Export Changes** writes a patch of the `local` commits in the pack's file formats, or Export as
  Pack writes the pack with a raised version. It works for every repository, also plain HTTPS ones,
  and the user sends the result to the maintainer.
- **Propose Changes…** works when the pack's `source_repository` is on GitHub or Azure DevOps and the
  user is signed in there:
  - CILogg reads the pack folder at the tip of the source branch through the host's REST API. If the
    source moved past the installed version, the user's changes are merged onto it with the same
    rules and windows as an update.
  - It writes the changed files in the pack's formats as one commit on a new branch
    `cilogg/<user>/<pack>-<date>` and opens a pull request, both through the host's REST API (GitHub:
    the Git data and pull request APIs; Azure DevOps: the pushes and pull request APIs). No Git
    transport is needed, so libgit2 stays local-only.
  - Sign-in, credential scope and redirect rules are those of E15. A GitHub sign-in uses
    `git credential fill` for `github.com` (the Git Credential Manager supports GitHub) with a
    fine-grained token as the fallback, stored like a PAT.
  - It shows the link to the pull request.
  - Without write access (403) it says so and offers Export Changes. Forks are out of scope for the
    first slice.
- **Nothing is pushed without confirmation.** The confirmation shows the target repository, the
  branch name and the changed items. From Commander, the CLI, native MCP and Python, `pack_propose`
  is refused unless `confirm: true` is given.
- **After the merge.** When the pull request is merged and the pipeline publishes the new version,
  the next update sees the same change on both sides, and the pack's `local` branch has nothing
  left, so the pack shows no local changes.
- **Your own repository** works the same way, with the user as the maintainer.

## 5. Security

- Plugins are programs and Lua is code. The protection is that CILogg installs only from
  repositories the user added, only over HTTPS, with a SHA-256 check, and installing stays a user
  action unless the user turned auto-update on for that repository.
- The SHA-256 comes from the same repository as the package. It protects against a damaged or
  partial download and against a package changed after the catalog was written, not against a
  repository whose catalog and packages were both replaced. Package signing is not part of this
  design (decision 6).
- Update-source pinning (section 3.4) prevents a second repository from taking over a plugin ID.
- Credentials stay inside their scope (section 3.6). An unauthenticated catalog may list packages
  anywhere, but the credential of another repository is never attached to them.
- A pack's Lua is code with the user's permissions (section 4.3); nothing in this design sandboxes
  it. Pack previews follow the same validation and limits as user previews.
- Pack actions send data to devices only when run; pack automatic responses start switched off
  (section 4.5).
- Crash dumps can contain tokens that were in memory (section 3.6); they are not shared unless the
  user adds them.
- Propose Changes writes to a remote repository only after a confirmation that names the
  repository, the branch and the changed items (or `confirm: true` from automation), and only with
  the user's own sign-in under the E15 credential rules.

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

**Users** add the repository once (Plugins > Manage Plugins… > Repositories tab > Add, sign-in Azure
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
  refusing a PAT and never prompting; repository order (Move Up / Move Down and
  `repository_move` persist it, Browse groups by it, a new install of a plugin offered twice takes
  the higher repository, reordering changes no installed plugin and offers no update); an empty
  catalog shown as "No plugins in this repository", never as a bare job state; How to... and Help >
  Plugin Repositories Guide open the documentation at the guide's anchor; the one window's three
  tabs, the Kind filter (single-kind packs listed as their kind) and the Packs… buttons opening
  Available filtered to their kind. A local HTTPS test server
  answers catalog and package requests and records the headers.
- **E16:** pack manifest validation (`kind` read first, no process keys including `configuration`,
  `protocol_version: 1`, every Lua file listed, count and size limits, preview routes naming only
  pack previews); pack previews listed with the pack's name; Copy to My Previews with the block closure and renamed
  colliding blocks, unchanged after the pack is updated or removed; pack blocks resolved only inside
  their pack; user preview precedence; a preview-name collision between two packs refused; the
  aggregate preview quotas; exports and project snapshots without pack definitions; pack Lua in the
  profile snapshot only while enabled, copied, hashed and loaded in order, never written into the
  saved configuration, in a per-pack environment where two packs with the same global keep their
  own values; the 32-file and route limits; warm sessions discarded on update; JSON fallback
  without TShark; `python_decoders` listed and inactive; pack actions, highlighters and filters
  listed with the pack's name, Copy to My..., user precedence, collisions between packs refused, store limits,
  exports without pack items; pack automatic responses off after install and their on or off choice
  kept across an update. Local edits and merge: the installed version unchanged by edits; Reset to Pack Version; every three-way case per item (not edited,
  edited only locally, different fields, the same field, deleted against changed, added, removed)
  with the same new value on both sides not a conflict; per-element merge of identified arrays;
  highlighter rules and Lua and Python files by diff3 with and without conflicts; Keep Mine, Take
  Theirs, Edit, Keep Both and the for-all choices; Cancel and a failed apply leaving the old
  version and the edits; automation strategies `fail`, `keep_mine` and `take_theirs` and no dialog
  outside the GUI; auto-update skipping a conflicting pack and applying a clean merge. Local Git
  store: `upstream` and `local` commits for install, edit and update; the merge driver on every
  item-level case; canonical form round-trips for every kind; text merges matching
  `git merge-file`; View History, Revert and Reset to Pack Version; the store rebuilt from the
  installed version with the local edits re-applied; the external merge tool's result read back and
  refused with conflict markers. Contribute: a patch from Export Changes applied to a fresh store
  gives the same content; Propose Changes against a local fake GitHub and Azure DevOps host that
  records the branch, the commit and the pull request request, a source that moved past the
  installed version, the no-write-access path offering Export Changes, and `pack_propose` refused
  without `confirm: true`. Export as Pack:
  the selected items with the blocks they need, edited pack items exported with their current
  content, no preferences or session, responses arriving switched off, the output accepted by
  `build_catalog.py` and installable as a local zip, `pack_export` matching the dialog.
- Both: GUI, CLI, native MCP and Python give the same results; `[plugins]` and new `[workbench][E15]`
  / `[workbench][E16]` Catch2 cases; `tests/plugins` for the catalog script.

## 9. Slices

- **E15a:** the Plugins and Packs window (Installed, Available, Repositories) replacing Manage
  Plugins and Browse Plugins, the ordered repository table (Add, Edit, Remove, Move Up, Move Down,
  Enable), the Kind filter,
  migration, merged catalog grouped by repository order with sources, install-source pinning,
  status texts including "No plugins in this repository", `build_catalog.py`, the in-app guide
  (section 3.9), and Commander/CLI/MCP/Python operations for public repositories.
- **E15b:** Azure DevOps sign-in (credential manager, PAT store), credential scope and redirect
  rules.
- **E15c:** update check, notices, auto-update.
- **E16a:** configuration pack kind with JSON previews and Lua for TShark; the local pack store
  (libgit2, canonical form, merge driver, history, rebuild), the item merge window, the three-pane
  text merge, the optional external merge tool, Reset to Pack Version and the automation
  strategies, for previews, blocks and Lua or Python files.
- **E16b:** Python decoders, activated with MD03.
- **E16c:** actions with automatic responses (off until enabled), highlighters and predefined
  filters in packs, with their canonical forms, local edits and merge, and the Packs… buttons in
  their windows.
- **E16d:** Export as Pack (dialog and `pack_export`), View Changes and Export Changes
  (`pack_export_changes`), and their part of the guide.
- **E16e:** Propose Changes (`source_repository`, the GitHub and Azure DevOps REST flows, the GitHub
  sign-in, `pack_propose`) after E15b, and the guide's "Share your changes".

## 10. Decisions

All decided by Dmitry on 2026-11-17. He chose the editing model and the merge UI first, then
approved every remaining recommendation: "yes, approve all 11 recommended".

**Editing model and merge UI**

- **Local edits to pack items:** edit in place, with the installed version kept as the merge base
  and a three-way merge on update (section 4.6). Read-only pack items with only Copy to My... were
  not chosen.
- **Merge UI:** our own item merge window for structured items, a three-pane text merge for Lua and
  Python, and an optional external merge tool (section 4.6).

**The eleven design decisions**

1. **Sign-in:** the Git Credential Manager first, a personal access token as the fallback.
2. **Azure layout:** a Git repository with a pipeline that writes a `catalog` branch, not an Azure
   Artifacts feed.
3. **Updates:** notify only. Auto-update is a per-repository switch, off by default, and stays
   within the installed major version.
4. **PAT storage:** QtKeychain on all platforms.
5. **Python decoders:** carried in packs now and activated by MD03.
6. **Package signing:** not in E15; SHA-256 from an authenticated repository is the integrity check.
   Signing could be added later as a catalog key.
7. **Repository order:** a priority. Browse groups by it, and a new install of a plugin offered by
   several repositories takes the higher one, while installed plugins stay pinned to their source.
8. **Where local edits live:** a local Git store per pack, with `upstream` and `local` branches, the
   canonical one-item-per-file form and a CILogg merge driver. The overlay was not chosen.
9. **Git engine:** libgit2, bundled and built without network support. Not the `git` executable.
10. **Sharing changes back:** Export Changes for every pack, plus Propose Changes as a pull request
    through the GitHub and Azure DevOps REST APIs.
11. **Order of work:** E15 may start ahead of G02/G03, as E13a and R06 did; E16 follows E15.
