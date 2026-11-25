# Plugin repositories and configuration packs (E15, E16) — accepted design

**Status:** accepted on 2026-11-17; the user approved every decision in section 10 (decision D16 in
[docs/workbench/decisions.md](workbench/decisions.md)). Slice E15a (public repositories, the
Plugins and Packs window, the merged catalog, install-source pinning, `build_catalog.py` and the
in-app guide) is merged (PR https://github.com/dm17ryk/klogg/pull/122); slice E15b (Azure DevOps
sign-in, the token store, credential scope and redirects, request origin) is merged (PR
https://github.com/dm17ryk/klogg/pull/124); slice E15c (update checks, notices, Skip this version,
auto-update) is merged (PR https://github.com/dm17ryk/klogg/pull/125), so E15 is complete. E16a is
delivered in two parts (decided on 2026-11-19): E16a1 (the pack kind, previews, Lua and routes for
TShark) is in review in PR https://github.com/dm17ryk/klogg/pull/128 and E16a2 (the local pack
store with edit in place and merge) follows; the
other E16 slices are planned. The tasks are
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
  credential store. The deletion is listed in the plugin settings in the same write that removes the
  repository (`pendingTokenDeletions`, store keys only); a deletion the store refuses, or one cut off
  by an exit, is tried again at the next repository operation and the next start.
- **Changing `credentialSource`** of a checked repository sets its state to "Sign-in needed": the
  last check used the other source, so the repository counts as signed in only after a check with
  the new one.
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
   wait on a console nor start another prompt program. Each run starts in a new, empty folder of
   its own under the temporary folder (removed afterwards) with `GIT_CEILING_DIRECTORIES` set to its
   parent, so neither a repository that CILogg happens to be started in nor a `.git` placed in the
   temporary folder can name another credential helper. Its standard input is `protocol=https`,
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
   shows its sign-in window). The same rule applies to a catalog and to a package download. Silent
   checks and CLI/MCP/Python operations never reject; they mark the repository "Sign-in needed".
3. **Personal access token (fallback).** When Git is not installed, the credential manager is
   missing or the user prefers it, **Sign In** asks for a PAT with the Code (Read) scope. It is
   stored in the operating system credential store through QtKeychain (Windows Credential Manager,
   macOS Keychain, Secret Service or KWallet on Linux; the Linux builds include the libsecret
   headers for the Secret Service) under `CILogg plugin repository <profile>/<id>/<address>`, where
   `<profile>` and `<address>` are short SHA-256 hashes of the plugin data folder and of the
   repository's organization, project, repository, branch and path. An installed and a portable
   copy, or two portable copies, that give the same ID to a repository therefore never share or
   overwrite a token, and neither does a later repository with another address. Without a usable
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
- **As built (E15c).** `PluginUpdateChecker` runs the check 60 s after the start of an interactive
  GUI process and then every hour; each run fetches only the enabled repositories whose last
  successful check is 24 hours old (or that have no saved catalog) and that the check has not
  tried in the last 24 hours, so a failing repository is tried once a day. The setting is
  **Options > Update > Check plugin repositories for plugin updates once a day**
  (`versionchecker.plugins`, on by default). The last catalog of each repository is kept as
  published in `<plugin data>/catalog-cache/<id>-<address hash>.json` and read again at start for
  the running CILogg version. Per plugin, the plugin settings keep `skippedUpdate` (Skip this
  version hides that version and older ones), `announcedUpdate` (the newest version shown in a
  notice, so each version gets one notice across restarts; it only goes up) and
  `autoUpdateFailed` (the version whose failure was reported); removing the plugin clears all
  three, and an install or update by the user clears the failure. Auto-update installs the newest
  version within the installed major version even when a newer major version exists, and
  announces the major version; skipping the major version does not stop it (for auto-update a
  skip covers older versions of the skipped version's own major version only). It never stops a
  running plugin: the staged package waits until the plugin stops (Disable, Restart, a crash),
  and is dropped if CILogg exits first; the next start's check installs it again. An install,
  update or remove the user starts replaces a pending automatic update of the same plugin
  (the update is tried again at the next check if the user's operation does not install it);
  cancelling a waiting update removes its staged package. A version announced while no notice
  can be shown (automation mode) is not announced again, but it is listed under Updates (n).
  More than three new versions at once give one notice that lists them.

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
| `check_updates` | optional `repository` | A job listing `id`, `installed`, `available`, `repository`, `entry_id` (as built also `name`, `repositoryName`, `skipped`, `majorChange`, `automatic`), with the per-repository results. It fetches the repositories whatever their last check, announces new versions and starts automatic updates like the silent check. |
| `skip_update` | plugin ID and `version` | Skip this version (added in E15c): hides that version, and older ones, from notices and auto-update until a newer one appears. Update still installs it. |
| `set_source` | plugin ID and `path` (the absolute path of a folder or ZIP) | Repoints a plugin installed from a folder or ZIP: the path must hold a `plugin.json` with the same ID. |
| `reinstall` | plugin ID | Installs a plugin installed from a folder or ZIP again from the recorded path, with the checks of an install from that path. |

`get_plugins` adds `kind`, `contents` (packs), `source` (`repository`, `local` or `shipped`, the
repository ID and the installed version; for `local`, the recorded `path` and `pathKind`, `folder`
or `zip`), `update` (the newest version from the source, if any) and
`localEdits` (the number of locally edited items of a pack). As built, `update` is `null` or an
object with `version`, `entry_id`, `repository`, `repositoryName`, `skipped` and `majorChange`.

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
  A route goes to a TShark `dissector` or, with `{ "python": "<pack-id>/<decoder-id>" }`, to one of
  the pack's own `python_decoders` (section 4.4).
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

A pack's `python_decoders` run in the preview's **Python decoder backend** (slice MD03b, issue
#159), a narrow part of task MD03 like MD03a was for TShark and Lua. The pack format is unchanged:
each item is `{ "id", "module" }`, the module a `.py` file inside the pack. The full MD03 decoder
contract (datasets, conversations, scans) stays planned; this backend uses the existing
PreviewService result version 1. A Python decoder that needs more than a preview can still be
shipped as an ordinary process plugin (a `converter` or `tool`).

**Entry point.** CILogg calls the module's `decode(payload, context)`:

- `payload` is `bytes`: the preview's buffer (its `bufferCapture`, decoded by its buffer type or the
  request's encoding), or the assembled message for a protocol route; 1 byte to 64 KiB.
- `context` is a `dict`: `preview` (the preview's name, empty for a protocol route), `protocol` (the
  assembled protocol, or empty), `inputEncoding` (how the log text became `payload`: `utf8`, `hex`,
  `base64`, `bin` or `bytes`), `direction` (`unknown` until a source reports one; `rx` and `tx` are
  reserved) and `decoder` (`<pack-id>/<decoder-id>`).
- It returns the rows, or an object `{ "fields": [rows], "messageCompleteness", "messageIntegrity" }`.
  A row is `{ "name", "value", "raw", "pos", "size", "depth", "attributes" }`: only `name` is
  required; `value` is text, a number (integers exactly), a boolean or `None`; `raw` is `bytes`
  (shown as hex) or text; `pos` and `size` are integers or text, offsets in `payload`; `depth`
  starts at 0 and grows by at most one from row to row (preorder); `attributes` is an object of at
  most 64 values. `messageCompleteness` is `complete`, `incomplete` or `not_reported`,
  `messageIntegrity` `valid`, `invalid` or `not_reported` (the default); they are independent of the
  result's own `complete`, which only says whether every row was kept.
- It returns `None` when the bytes are not its message ("not dispatched").
- A module's suffix may be `.py` in any case, as the pack's manifest check takes it.

**Result.** PreviewService result version 1 with `backend: "python_decoder"`, `decoder`,
`offsetSpace: "decoder_payload"`, `moduleSha256` (the session copy that decoded) and `enginePid`
(metadata only). Rows keep the MD03a limits: 512 rows, depth 32 and 64 Ki UTF-16 units per text,
then `limited` with the reason `rendered_field_limit`, `depth_limit` or `field_text_limit`; the
whole response stays within 128 KiB (`response_limit`, an exact prefix of the rows). A failure is
`decoder_failed` (the function raised), `decoder_load_failed` (the module did not import or has no
`decode`) or `invalid_decoder_result`, with `detail` naming the exception type and the file and line
in the pack, never the message text or the data.

**Worker.** One supervised Python worker per preview session (the preview owner, as for TShark), at
most two warm, on the MD03a transport (`message_decoder_python_worker.py`, framed JSON over stdio).
The host starts it in a process group (Unix) or job object (Windows) of its own before sending
anything, so ending it ends its descendants; it is not a sandbox. Deadlines are the preview's: 3 s
by default, 10 s at most, from the request's start. A deadline, a cancellation, a crash or a
protocol error ends the worker and fails that request; the next request starts a new worker. A
decoder failure fails only its request; the worker and its modules stay. `print()` and a child
process's output go to the worker's diagnostics, never into the protocol, and are bounded at 64
KiB per request. The interpreter is chosen as for plugins (#141): `CILOGG_SCRIPT_PYTHON`, a bundled
interpreter, then `python.exe` before `python3.exe` and launcher scripts on `PATH`. It runs with
`-I -S`, so decoders use the standard library only.

**Routing.** A decoder route names `{ "python": "<pack-id>/<decoder-id>" }` instead of a
`dissector`, in `previewRoutes` (a preview name) or `protocolRoutes` (an assembled protocol, with an
optional `inputEncoding`). A pack's routes name its own decoders; the user's routes (Preview Decoder
Settings, where the decoder column takes `python:<pack-id>/<decoder-id>`) may name any pack's. In
Automatic mode a Python route has the place a TShark route has: an assembled protocol's route
first, then routed previews before other matching JSON previews, then the TShark default dissector,
then JSON. A Python route applies while its decoder's pack is enabled, whether TShark is configured
or not; a route whose decoder is not loaded does not apply. Automatic mode falls back to JSON only
when the decoder returns `None` (`fallbackReason: "decoder_not_dispatched"`, `decoderAttempts:
["python_decoder", "json_preview"]`); a failure, a deadline or a limit stays visible. Explicit JSON
ignores the routes; explicit TShark, or a dissector named in the request, never uses a Python route.

**Loading and isolation.** When a session starts, the worker copies every enabled pack's listed
modules into its own folder, at most 1 MiB each and 64 in all (the pack check refuses a pack that
would exceed 64), and hashes them; later changes of the pack's files reach the next session. Each
pack is a package of its own whose path is its copy, so a module imports the pack's other listed
modules relatively (`from . import common`) and two packs' modules of the same name never collide.
Only listed modules are part of a pack (its store keeps only them), so a helper module is listed
too. Each folder of a listed module is a package of the pack, also when its name has a `.`; a
module and a folder of the same name (`sub.py` beside `sub/`) are refused when the session starts.
Enabling, disabling or updating a pack starts new sessions.

**Trust.** Pack Python is local code with the user's permissions. The install, enable and update
confirmations of pack Lua cover it ("contains Python code that runs on this computer"; "Lua and
Python code" for both), and auto-update of such a pack is the same trust decision as for a process
plugin. A repository install or update asks from the catalog entry's `contents`, so staging
refuses a pack package whose entry leaves out its `lua` or `python_decoders` ("The package has
python_decoders, which its catalog entry's contents do not list"); `build_catalog.py` lists them
from the manifest.

**Capability.** The host feature `python_decoders` (version 1) says this CILogg runs pack Python
decoders: `get_plugins` reports it in `hostFeatures`, and per pack `pythonDecodersActive` (the pack
is enabled, has Python decoders and the host has the feature). The Installed tab shows
"Python decoders: N", "(inactive)" while the pack is disabled, and "requires a newer CILogg" on a
host without the feature; a pack can also say `"requires": { "host_features": ["python_decoders"] }`
so that an older CILogg lists it as requiring a newer one. The example
`plugins/examples/python-decoder-pack` shows a decoder with a helper module and its route.

**As built (E16a1).** `Manifest::parse` reads `kind` first and checks a pack with
`validatePackManifest` (`src/plugins/configpack.*`); `Manifest::read` then checks its files
(`validatePackFiles`): listed files exist without links within 1 MiB, every file of the Lua folder
is listed, and preview routes name the pack's own previews. `PluginManager` refuses `restart`,
`configure` and plugin actions for packs, and asks the application's pack check
(`setPackCheck`) before a pack is enabled or a new version of an enabled pack is installed.
`PreviewPackRegistry` (`src/ui`) follows the manager: it reads each enabled pack once per version,
gives its previews to `PreviewManager::setPackPreviews` (kept apart from the user's
`previews.json`, merged by `effective()`), and its Lua and routes to
`PreviewDecoderSettings::setPacks`, which adds them to the session snapshot only (`packLua`, routes
below the user's) and starts new sessions. The preview worker copies each pack's files and loads
them through a generated loader with one environment per pack (Lua 5.1 `setfenv` or 5.2+ `load`
with an environment). The pack check refuses a shared preview name or route key, merged route and
Lua limits, and merged previews past the service limits (`PreviewService::definitionsFor`); if the
user's own previews grow past the limits later, evaluation drops the packs' previews instead of
all previews. Every applied change, also a new version whose Lua keeps its file names, starts new
TShark sessions. Pack files are checked again whenever the pack is read, and a file or the Lua
folder reached through a link or junction is refused. A user preview with a pack preview's name
replaces it together with its route; **Copy to My Previews** also copies the pack's route into the
user's routes, and a renamed copy (`<block> (<pack name>)`) keeps the generic `_block` fallback.
E16a2 adds edit in place and merge (section 4.6). A pack preview's
on/off choice is kept in the user's settings (`previewPacks/enabled`). Every GUI path asks before
pack Lua can run (`pluginsui::confirmPackLua`): Enable, and an install or update that replaces an
enabled pack with a version that has Lua (Installed tab Update, Available tab Install / Update and
Switch source, Install Folder and Install ZIP), judged from the new version's catalog `contents`
or manifest. Two enabled packs never share a route, even one the user's route hides, and a pack's
block names `<pack-id>/` are its own: its blocks win there, and enabling it is refused while a
block of the user's uses that namespace.

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

**As built (E16c1, predefined filters).** E16c is delivered as three PRs (decided on 2026-11-24):
E16c1 predefined filters, E16c2 highlighter sets, E16c3 actions with automatic responses. A
pack's `filters` file is the INI file **Export** writes (group `PredefinedFiltersCollection`,
version 1 to 3); `packcanonical::fromFilters` refuses one that is not exactly that (a filter
without a unique `id`, a name or a pattern, a `regex` that is neither true nor false, any other
key, more than 4,096 filters), so nothing in it is lost on the way to the store. In the store each
filter is `filters/<id>.json` (`filter`, `id`, `name`, `regex`) with the list order in
`filters/order.txt`; filters merge field by field like previews (the `id` is the identity: no
resolution or edit changes it), the order as text, and `toFilters` writes the content folder's
filters file back with QSettings (version 3). An edit, a merge result or a reset must leave every
filter one CILogg reads back the same, within 4,096 filters and 1 MiB (`filtersInvalid`); a version
without a filters file takes the filter items with it (`droppedEdits`), and `pack_reset` refuses a
filter there. `PreviewPackRegistry` reads each enabled pack's filters and hands them to
`PackFilters` as `ListedFilter`s (the filter with its pack's ID and name; `PredefinedFilter` itself
is unchanged), apart from the user's `PredefinedFiltersCollection` (never saved, exported or put
in a project snapshot); `effectivePredefinedFilters` lists the user's filters, then the packs'
whose id the user has not taken. The pack check refuses a filter id another enabled pack has and
more than 4,096 filters together; the user's own filters are not limited, and when they grow past
that the packs' filters give way in the lists (`packFiltersGiveWay`, shown as "not listed" in the
dialog), as pack previews do. No other content file may be the filters file, which CILogg writes
(`checkReservedPaths`), and an order with CR LF line ends keeps its order. The search bar's filter list shows pack filters as
"<name> (<pack name>)" and follows `PackFilters::changed`; Commander's predefined filters carry
`packId` and `packName`. The Predefined Filters dialog lists the pack's filters after the user's
with a Source column ("replaced by your filter" when a filter of the user's has the id), read
only, with **Copy to My Filters** (a filter of the user's with the same id), **Edit...** (the
pack item editor on `filters/<id>.json`) and **Packs...** (the Plugins and Packs window over the
dialog). `get_plugins` counts a pack's `filters`.

**As built (E16c2, highlighter sets).** A pack's `highlights` file is the INI file **Export**
writes (group `HighlighterSetCollection`, version 1 or 2; each set `HighlighterSet` version 1 to 3);
`packcanonical::fromHighlights` refuses one that is not exactly that (a set without a unique `id`
or a name, a counted rule without its `regexp`, a value CILogg would not read back, a list where
one value goes (an unquoted comma), a `color_variance` outside 0 to 100, any other key, more than
4,096 sets or 4,096 rules in a set) and drops the exported `active_sets` and color
labels, which are the user's. Rule members an older export lacks take the defaults CILogg reads
them with (the store's set text carries all eight); requiring every counted rule's pattern keeps
the work bounded by the file's size, so a count alone cannot make millions of rules. In the store each set is one text file,
`highlights/<id>.set`: an `id` line, a `name` line and one `rule` line per rule (the rule as JSON
with sorted keys), with the set order in `highlights/order.txt`. A set merges as text (diff3), so
edits of different rules merge and edits of the same or adjacent lines conflict; Keep Mine or Take
Theirs keeps a whole set, so no rule is left without its set. `toHighlights` writes the content
folder's highlighters file back with QSettings. An edit, a merge result or a reset must leave every
set one CILogg reads back the same (`highlightsInvalid`, also in `contentInvalid`); `pack_edit`
canonicalizes a set and refuses to remove one (a pack's sets go with the pack), and a version
without a highlighters file takes the set items with it. `PreviewPackRegistry` reads each enabled
pack's sets as CILogg reads its own (`HighlighterSetCollection::retrieveFromStorage` on a copy)
and hands them to `PackHighlighters` as `ListedHighlighterSet`s (the set with its pack's ID and
name); the user's `HighlighterSetCollection` holds the user's sets only (saved, exported and put in
project snapshots), and `listedSets()` lists them, then the packs' whose id the user has not taken.
The active sets stay the user's: `activeSetIds` keeps pack set ids, also while the pack is
disabled, and the combined highlighting (`updateCombinedSet`) follows `listedSets()`, so an
active pack set highlights with its new rules as soon as the pack changes. The pack check refuses
a set id another enabled pack has and more than 4,096 sets together; the user's own sets are not
limited, and when they grow past that the packs' sets give way (shown as "not listed" in the
dialog). No other content file may be the highlighters file (`checkReservedPaths`). The
Highlighters menu shows pack sets as "<name> (<pack name>)" and follows `PackHighlighters::changed`;
the Highlighters dialog lists them after the user's, read only (the set editor disabled), marked
"replaced by your set" when a set of the user's has the id, with **Copy to My Highlighters** (a
set of the user's with the same id, which stays active if the pack's was; removing that copy lists
the pack's set again, still active if the copy was), **Edit...** (the pack
item editor on `highlights/<id>.set`) and **Packs...**. `get_plugins` counts a pack's
`highlighterSets`. Stores written before E16c kept the filters and highlighters files as they
are, under `files/<path>`: every read of a store commit converts such a file to its items, with the
commit's own manifest and any edit made to the file (`packcanonical::fromLegacyDocuments`), so
the merge base, the local edits and the content folder see one form and the next commit is in it.
A file that is not exactly what Export writes (an older export without ids) stays as it is and the
content folder gets it back unchanged, never an empty file in its place. `PreviewPackRegistry`
then leaves out only that file's items (`Contribution::warnings`): the pack's previews, Lua,
decoders and other items still apply, enabling or updating it is refused with the reason, and
`get_plugins` reports it in `contentWarnings`. The pack check counts only the pack items that are
listed (a filter or set of the user's with a pack item's id replaces it), as the lists do.

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
  or Reset'", "Edit highlighter set 'Radio'").
- **Canonical form.** Structured content is stored one item per file (`filters/<id>.json`,
  `highlights/<set-id>.set`, `actions/<id>.json`, `responses/<id>.json`, `previews/<name>.json`,
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

**As built (E16a2), for previews, blocks and Lua or Python files.** libgit2 1.9.1 is built by CPM
without network, SSH or HTTPS support (`klogg_libgit2`, linked by `klogg_plugins` only).
`PackStore` (`src/plugins/packstore.*`) keeps a bare repository `pack-store/<pack-id>.git` with
the branches `upstream` and `local`, and writes the effective content to
`pack-store/<pack-id>/current` in the pack's own layout (staged in `current.new`, then renamed);
`PreviewPackRegistry` reads packs from there through `PluginManager::packContentDirectory`.
Commits are built from an in-memory index, so the store has no work tree. `packcanonical`
converts between the pack layout and the canonical form: `previews/<name>.json`,
`blocks/<name>.json`, `previews/order.txt` (evaluation order), `previews/meta.json` and
`files/<path>`, names percent-encoded outside `[a-z0-9._-]`, JSON indented with sorted keys.
The merge is done in process by `PackStore::planUpdate` with `packmerge` (`mergeJson` field by
field with `fields/@name/...` element paths, `mergeText` with `git_merge_file` in diff3 style
labelled "your edit", "installed version" and "new version") rather than through
`.gitattributes` and a registered merge driver: the outcome is the same, and the merge commit on
`local` has the new `upstream` commit as its second parent. Both commits are made before either
branch moves, so an update is in the store whole or not at all. The store moves before the
package folder is replaced: a store that fails leaves the installed version, and a package commit
that fails puts the store back (`PackStore::rollback`), so the installed version and the
effective content never disagree. Enabling a pack checks its effective content (with the edits),
and an edit, Revert or Reset of an enabled pack passes the same check before it is committed
(`PackStore::planRestore` gives what a reset would leave); the registry keys
each pack on the store's `local` commit as well as its files, so every edit reloads it. The
store carries every file the manifest lists, also actions, highlights and filters (as files
until E16c), and refuses a previews document with unnamed, duplicate or malformed items instead
of cleaning it up. A file that is not UTF-8 text is never edited as text: `pack_read` refuses
it and a conflict on it is resolved with `take` (a whole side, byte for byte); text from the
external editor that is not UTF-8 is refused. Keep Both copies the user's preview from the
pack's effective content, so it works for disabled packs too. A merge result has exactly the
files the new manifest lists (a file it dropped goes, also with Keep Mine; the job result names
dropped edited files in `droppedEdits` and a warning), so a resolution never removes a listed
file or the preview order, and the merge window offers no removal for them. For an enabled pack
the pack check runs on what would load: the merge result, written to the store's check folder,
when there are edits (an item an edit deleted stays deleted, so another pack may use its name),
otherwise the new version as shipped. The previews settings (`previews/meta.json`) are listed in
Pack Files but not edited or reverted on their own; an update takes the new version's, so they
never conflict and no resolution names them. A job keeps at most 4 MiB of conflict texts (jobs are kept, up to
256): previews and blocks first, each file's sides once; a file past that is listed with
`omitted` and merged as a whole side, the merge window records the whole side chosen (a file
that is not UTF-8 may read the same for different bytes), and an automatic update keeps no
texts. An item with more than 32 conflicting fields, or any item past 1,024 listed conflicts, is
listed as one whole-item conflict. Resolutions come back with the result's `revision` (the
`local` commit the merge was shown for; resolutions without it are refused); after another edit
they are dropped and the merge is asked for again, so a stale choice never overwrites a newer
edit, and the GUI then drops the earlier merge's Keep Both too (the new merge's choices
replace it). The merge window sends a whole side as `take` only when a whole-side button chose
it (the editor shows CRLF as LF), and a text result of a CRLF file keeps CRLF. Every install
or update writes a journal first (`<pack-id>.update.json`: the branch tips before it) and
removes it once the package folder is replaced or the store put back; a journal still there
when the pack is next read (at startup too) is finished: if the package folder already holds
what the store's `upstream` holds and `upstream` moved on from the journal's tip, the update had
completed and only the journal goes (an update moves `upstream` first and an undo puts it back
last, `local` first, so an undo that stopped part-way is done again; while a journal that could
not be acted on is there, the pack loads its installed folder, never content of a version that
was not installed, as the content folder is replaced before its revision is recorded; a journal
that was settled, the update finished or put back, but could not be removed keeps the content
folder loading; a store that is gone is never taken as put back, and an install that fails
before it writes its own journal leaves an earlier one in place);
otherwise CILogg ended part-way (between the branch moves or before the package swap, whatever
the versions) and the store is put back to those tips, so a later update merges anew. A journal
is read strictly (a missing or malformed field, the tips included, is never taken as "there was
no store", which would remove it), and a store that is on disk but does not open now (another program holds a
file) stops the operation instead of being taken as missing, while the pack keeps loading its
content folder (never the installed folder, which lacks the edits). A merge's `revision` covers
the edits and the incoming package (its files and manifest), so choices are not applied to
another package either, and a stale revision installs nothing, also when that package would
merge cleanly. A whole-item conflict carries only the base (its sides are in `items` once), a
`take` resolution works for any item, and the merge window resolves a preview or block too
large to show as a whole side instead of refusing it. The previews document as written (one
member per line) must stay within 1 MiB: a package, merge result or edit that would pass it is
refused, as a minified document just under the limit grows when it is written again. A copy into the user's previews (Copy to My Previews, Keep Both after an
update) that would take them and the enabled packs past the preview limits is refused before it
is saved, as the service would otherwise drop the packs' previews. Such a copy takes the decoder
route the preview decodes with (the user's own route for it, else the pack's) as the user's
route for the copy; the route is saved first (a route under a name no preview has decodes
nothing) and taken back if the copy fails, so a copy whose route cannot be added (the user's
preview routes at their limit of 128, or another route of the user's already under the copy's
name, which it would decode with) is not made, never left with a wrong route, and Keep Both
checks the routes of all its copies at Apply. A copy under another name (Keep Both's
"<name> (mine)") never takes the name of a preview of any installed pack, enabled or not (its
content and its installed version, read even when they do not load now), which it would replace, and keeps its source preview's on/off state (as applied while that pack is
enabled, otherwise its definition and the choice saved for it). Arrays of named elements
(preview fields) merge their order too: a side that
keeps the installed order leaves the order to the other, an element one side added keeps its
place, and two different reorderings, or an element both sides added in different places (after
another element of the installed version, or in another order among such elements), are one
conflict of the list. A copied block is renamed also when an enabled
pack's block has its name (a pack block literally named `<other-pack-id>/<name>`), which would
replace it. A field resolution's path stays within 4,096 characters and 128 segments; a conflict
on a deeper field is listed as a whole-item conflict. A merge result must also stay within the
preview limits (128 previews, 4,096 blocks): keeping an edited preview the new version removes,
beside the version's own, can pass them. It is checked once the conflicts are settled (an open
conflict holds the user's side), also for a disabled pack. A previews document over the
preview limits (128 previews, 4,096 blocks), which could never be enabled, is refused at
install, and item names are checked with a hash set. Keep Both is for a preview as a whole: a field choice of
one of its rows takes the item out of it. Removing a
pack removes the repository and the content folder each on its own; a store still on disk when
the pack is installed again (a removal that could not finish) is set aside as
`<name>.orphaned-<time>`, never merged into the new install. `pack_edit` takes exactly one
of `content` and `remove`, and no content file of a pack may be its `plugin.json` or previews
file, which CILogg writes itself, or differ from another only in case. The content folder's
`local` commit is recorded beside it (`current.revision`), and what loads follows that record:
a pack operation that writes the folder again (a repair after an edit whose files could not be
written), also one that only reads, tells the listeners. The folder is written into a staging
folder that is cleared first (retried like a rename): one that cannot be cleared stops the write,
so a stale file is never published. A folder left behind by a replace
that failed (a rename refused while a file is held; it is retried for about 1.5 s) is written
again by the next pack operation, enable or update.
Edits change or delete the pack's own previews and blocks and change its listed files and
preview order; they add no items. An item named `meta` is stored as `%6Deta.json`, so it never
takes the previews settings' file. A revert never brings back a file the installed version's
manifest no longer lists (the content folder would not have it), and its result stays within the
preview limits like an edit's. libgit2 is always CILogg's own build (static, local only), also
when packaging asks CPM for the system's packages. Resolutions name only the pack's items in their canonical
spelling, and the content folder is written only with relative paths inside it. A whole-pack
Revert stays within the installed version (items revert one by one across versions), and a
Revert names a commit from the pack's history. A tree that cannot be read whole is never
committed; it marks the store as damaged. `commitWhenStopped` merges before the
package commit (`preparePackInstall`) and updates the store just before it (`finishPackInstall`); with
conflicts left open the job fails with `needsMerge`, `conflicts` and `items`, the installed
version stays, and `plugins/<id>/needsMerge` keeps the version for the Installed tab. The GUI
(`src/ui/packeditors.*`) runs install and update through `runPackAwareInstall`, which opens the
merge window and runs the request again with `resolutions`; Keep Both takes the user's preview
as it is at Apply (the copy's name must be free then, and its decoder route must fit the
user's routes) and copies it as "<name> (mine)" only once
that update is installed, so a failed update leaves no copy behind (a copy that fails after it is
a warning; the version stays in the pack's history). An automatic update
that needs a merge is reported by its own notice and retried at the next check. A damaged store
(a repository libgit2 cannot read, a missing branch or object; a file the system refuses to open
is not damage) is rebuilt when CILogg reads the pack, before a pack operation and before an
update merges, unless its content folder cannot be read whole (then nothing is moved, the error
names the folder, and the pack's operations and updates fail until it is repaired or the pack
removed, so no edit is dropped): the damaged repository is kept aside as `<pack-id>.git.damaged-<time>` (instead of
exporting a patch, which arrives with E16d), and the content folder's files come back as one
`local` commit; the content folder is replaced only after both commits. A content folder whose
repository is gone is rebuilt the same way, never reset to the installed version. libgit2 keeps
paths inside a repository under MAX_PATH on Windows whatever `core.longpaths` says, so when
`pack-store/<pack-id>.git` would be longer than 180 characters (a deep portable folder) the
repository goes to `%LOCALAPPDATA%\cilogg-pack-stores\<hash of the data folder and pack>.git`;
the content folder stays in the data folder. A content folder that
could not be written after its commit is written again the next time the pack is read. libgit2's
owner check is off for this private store. The external merge tool starts from the merge with
its conflict markers, as git mergetool does, so a tool that fails or returns without saving gives
a result that is refused. Pack operations refuse options they do
not take. Removing a pack removes its store. The external merge tool command is an application
setting (`packs/mergeTool`), asked for on first use.

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
  without TShark; `python_decoders` run by the Python decoder backend (MD03b: the worker's deadline,
  crash restart, limits and the end of its descendants, routing and the JSON fallback only when not
  dispatched, one import namespace per pack, the trust prompts, the `python_decoders` host feature
  and an end-to-end pack preview); pack actions, highlighters and filters
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
  strategies, for previews, blocks and Lua or Python files. Delivered as two PRs (2026-11-19):
  **E16a1** the pack kind, previews, Lua and routes, and **E16a2** the store and merge.
- **E16b:** Python decoders, activated by the MD03b Python preview backend (section 4.4; a plan
  change approved on 2026-11-24, instead of waiting for the full MD03 contract).
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
5. **Python decoders:** carried in packs now and activated by MD03 (as built: the MD03b Python
   preview backend, approved by Dmitry on 2026-11-24, section 4.4).
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
