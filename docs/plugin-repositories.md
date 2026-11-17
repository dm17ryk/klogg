# Plugin repositories and preview packs (E15, E16) — proposed design

**Status:** proposed on 2026-11-17 for the user's approval (decision D16 in
[docs/workbench/decisions.md](workbench/decisions.md)). Nothing here is implemented. The tasks are
[E15](../plans/cilogg-workbench-2026-10-21/tasks/E15.md) (plugin repositories) and
[E16](../plans/cilogg-workbench-2026-10-21/tasks/E16.md) (preview packs). The open decisions are
listed at the end.

**Request (2026-11-17):** can CILogg install and update plugins from a private Azure DevOps
repository? An internal tool plugin must not be published publicly. Message previews (the JSON
preview definitions, Lua decoders and Python decoders) should be delivered and updated the same way,
possibly as plugins. Licensed protocol Lua sources cannot be public either.

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
- A new plugin kind, the **preview pack**, carries preview definitions (the JSON of `previews.json`),
  Lua decoders for TShark and Python decoders. Packs are versioned and updated from the same
  repositories. Their previews are read-only (copy one to edit it), and TShark gets the pack's Lua
  folder as a trusted path, because installing from a repository the user added is the trust
  decision.
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
  64-digit `sha256`, and adds a host-generated `entry_id`.
- **Browse Plugins** (`PluginUiController::browseDialog`) has one URL field, a Fetch button, a
  table (Plugin, Version, Description) and Install / Update Selected.
- Plugin `secret` configuration fields are kept in settings with current-user file permissions; the
  [plugin SDK](plugin-sdk.md) states that this is not encrypted credential storage. Repository
  credentials therefore do not reuse them.
- User previews live in `previews.json` beside the application settings
  (`PreviewRepository`: `version: 1`, `previews`, `blocks`).
- The TShark decoder configuration ([TShark previews](workbench/tshark-preview.md)) holds the
  `tshark` and `text2pcap` executables, up to 8 absolute Lua paths of at most 1 MiB each,
  `allowExternalLua`, the default dissector and the preview and protocol routes. Executable and Lua
  paths come only from these trusted settings.
- The application updater (`VersionChecker`) checks on a schedule, skips the check when version
  checking is off or CILogg runs in automation or headless mode, and retries a failed background
  check silently.

## 3. Plugin repositories (E15)

### 3.1 The repository record

Repositories are stored in the plugin `settings.ini`, next to the plugin data, so a portable copy
keeps its own list:

| Key | Meaning |
|---|---|
| `id` | Stable lowercase identifier, made from the name when the repository is added. |
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
- **Migration:** the first start with E15 turns the existing `catalogUrl` into the repository
  `default` (name "CILogg examples (GitHub)", sign-in `none`, enabled). An address the user had
  changed is kept. `catalogUrl` stays readable so an older CILogg copy that shares the folder keeps
  working.
- A controlled deployment can ship a portable copy whose `settings.ini` already lists its
  repositories. No company address is compiled into CILogg.

### 3.2 Catalog format

`plugins.json` stays at `schema_version: 1`. These keys are added; older CILogg versions ignore
them:

- `kind`: `process` (default, today's plugins) or `preview-pack` (section 4).
- `os` and `arch` may be `any` for packages that run everywhere (preview packs, pure-Python
  plugins). An older CILogg skips such entries because they match no platform name, so it never
  offers a package it cannot install.
- `min_host_version`: the oldest CILogg version that can use the package. Newer hosts hide entries
  they cannot use and say why.
- `released`, `homepage`, `license`: shown in the details pane.

The host-generated `entry_id` also covers the repository ID, so the same package listed by two
repositories gives two distinct entries.

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
- Versions are compared as semantic versions. An older version is never installed automatically;
  installing one is an explicit action.

### 3.5 Sign-in

**None.** As today: HTTPS with no credential.

**Azure DevOps.** For a repository on `dev.azure.com` (or the older `<organization>.visualstudio.com`
form):

1. **Address.** The user pastes the repository address
   (`https://dev.azure.com/<organization>/<project>/_git/<repository>`) or a file link copied from
   the web portal (`...?path=/plugins.json&version=GBcatalog`). CILogg derives the Items API
   address that returns the raw file:
   `https://dev.azure.com/<organization>/<project>/_apis/git/repositories/<repository>/items?path=<path>&versionDescriptor.versionType=branch&versionDescriptor.version=<branch>&download=true&api-version=7.1`.
   Package addresses in an Azure DevOps catalog may use either form; CILogg converts them the same
   way.
2. **Git Credential Manager (default).** CILogg runs `git credential fill` (arguments as a list,
   never a shell command line) and writes `protocol=https`, `host=dev.azure.com`,
   `path=<organization>/<project>/_git/<repository>` and `capability[]=authtype` to its standard
   input, the same request Git makes when it clones that repository. A user who already cloned the
   repository is signed in without a prompt. The result is used as follows:
   - `authtype=Bearer` with `credential=<token>` (Git 2.46 or newer): `Authorization: Bearer`.
   - `username`/`password` where the password is a Microsoft Entra access token (a JWT, which is
     what Git Credential Manager returns by default): `Authorization: Bearer <password>`.
   - Any other `username`/`password` (for example a PAT stored by the credential manager):
     `Authorization: Basic`.
   After a successful request CILogg runs `git credential approve`; after a 401 it runs
   `git credential reject` and, only when the user started the operation in the GUI, asks once more
   with `fill` (Git Credential Manager then shows its own sign-in window). This is the protocol Git
   itself follows.
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
  code is.
- **Scope.** A credential is attached only to requests whose scheme, host and port equal the
  repository's, and on `dev.azure.com` only below `https://dev.azure.com/<organization>/`. A
  package address outside that scope is downloaded without a credential.
- **Redirects.** Requests that carry a credential use manual redirect handling: at most 5 hops,
  HTTPS only; the credential stays only while the target remains inside the scope, and a redirect
  that leaves the scope continues without it. Requests without a credential keep today's policy.
- **Input.** In the GUI a PAT is typed into a password field. The CLI accepts it only from a file or
  standard input (`--token-file`, `--token-stdin`), never as an argument, so it does not end up in
  the shell history or the process list. Native MCP and Python cannot submit a PAT; they can start
  a credential manager sign-in and read the sign-in state.
- **Interactive prompts.** Only an operation the user starts in the GUI may show a sign-in window.
  Silent update checks and CLI/MCP/Python operations run `git credential fill` with
  `GIT_TERMINAL_PROMPT=0` and `GCM_INTERACTIVE=never` and report `sign_in_required` instead of
  prompting. A `git credential` call is stopped after 15 seconds when silent and 3 minutes when
  interactive.

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
  running plugin is replaced when it stops (as today), a preview pack at once. Auto-update stays
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
| `repository_update` | `repository` plus any of the add options | The changed record. |
| `repository_remove` | `repository` | Installed plugins keep running; their source becomes unavailable. |
| `repository_sign_in` | `repository`, `method` (`git-credential-manager` or `personal-access-token`), CLI only: `--token-file` / `--token-stdin` | A job; the result says whether the sign-in works. |
| `repository_sign_out` | `repository` | Deletes a stored PAT. It does not touch the Git credential manager, which Git also uses. |
| `catalog` | optional `repository` | One job; `plugins` (each with `repository` and `kind`) and `repositories` (per repository: state, error, entry count). The old `url` option still works: it sets the `default` repository's address and fetches it. |
| `install`, `update` | `catalog_entry_id`, as today; `switch_source: true` to change the source | A job. |
| `check_updates` | optional `repository` | A job listing `id`, `installed`, `available`, `repository`, `entry_id`. |

`get_plugins` adds `kind`, `source` (`repository`, `local` or `shipped`, the repository ID and the
installed version) and `update` (the newest version from the source, if any).

## 4. Preview packs (E16)

### 4.1 The pack

A preview pack is a plugin package whose `plugin.json` says `"kind": "preview-pack"`. It has no
executable and never starts a process:

```json
{
  "id": "org.example.protocol-previews",
  "name": "Example protocol previews",
  "version": "1.2.0",
  "kind": "preview-pack",
  "pack_format": 1,
  "previews": "previews.json",
  "lua": { "folder": "lua", "load": ["common.lua", "protocol.lua"] },
  "decoder_routes": {
    "previewRoutes": { "Protocol frame": { "dissector": "protocol" } },
    "protocolRoutes": {}
  },
  "python_decoders": [ { "id": "frame-summary", "module": "python/frame_summary.py" } ]
}
```

- `previews` is a file with the same format as the user's `previews.json` (`version: 1`,
  `previews`, `blocks`).
- `lua.load` lists the Lua files in loading order: at most 8 files of at most 1 MiB each, inside
  `lua.folder`.
- `decoder_routes` uses the format of the TShark decoder routes and may name only the pack's own
  previews.
- `python_decoders` is described in section 4.4.

Packs install, update, enable, disable and remove exactly like plugins, from the same repositories
or from a local zip. Operations that need a process (`restart`, actions, views) are rejected for
packs. Packs are platform independent, so their catalog entries use `os: any` and `arch: any`.

### 4.2 Read-only previews

- Previews of enabled packs appear in the preview list with a lock and the pack name. They cannot
  be edited or deleted. **Copy to My Previews** makes an editable copy in `previews.json`.
- A user preview with the same name as a pack preview takes precedence, the way a user-installed
  plugin takes precedence over a shipped one. The pack preview then shows "Replaced by your copy".
- Exporting previews exports only the user's previews. A pack update never changes the user's
  copies.

### 4.3 Lua for TShark

- While a pack is enabled, its Lua files are added to the TShark decoder profile as trusted
  scripts. Installing the pack from a repository the user added, and enabling it, is the trust
  decision; the per-file "I trust and approve" checkbox stays for the user's own Lua files.
- The `tshark` and `text2pcap` executables remain user settings. A pack cannot set or change an
  executable path, and preview requests and imported JSON still cannot name Lua or executables.
- Lua runs from the installed pack folder. The pack's `lua` folder is added to the Lua search path
  so that `require` finds the pack's own modules. Packs load in ID order, then the user's own
  scripts. Two scripts that register the same protocol fail visibly, as TShark reports them.
- The profile limit grows from 8 Lua files to 32 in total, the user's own 8 included, with at most
  8 per pack and 1 MiB per file. Enabling a pack that would exceed 32 is refused with a message.
- Enabling, disabling or updating a pack discards the warm TShark sessions, so the next preview
  loads the current scripts.
- `decoder_routes` are merged below the user's routes: a user route with the same key wins.
- If the decoder is disabled or TShark is not configured, the pack's Lua is idle, previews fall
  back to JSON, and the Plugins window shows "Needs TShark (Preview Decoder Settings)".

### 4.4 Python decoders

CILogg has no Python decoder backend for the preview pane yet: the Python helper that ships today
only carries data between CILogg and TShark. Task MD03 plans the supervised decoder contract. Until
it exists, a pack's `python_decoders` are installed and updated with the pack and listed as
"Requires a newer CILogg"; nothing runs them. MD03 activates them without changing the pack format.
A Python decoder that is needed sooner can be shipped as an ordinary process plugin (a `converter`
or `tool`).

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
- A pack's Lua cannot reach executables, and pack previews follow the same validation and limits
  as user previews.

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
  packs/<pack>/           preview packs authored here: plugin.json, previews.json, lua/, python/
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
trigger: { branches: { include: [ main ] } }
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
pipeline does not depend on the public repository at build time. The exact YAML is checked on its
first run. The project's build service account needs Read on the source repositories and Contribute
on the catalog repository. Users need only Read on the catalog repository.

**Users** add the repository once (Plugins > Manage Plugins > Repositories > Add, sign-in Azure
DevOps, address `https://dev.azure.com/<org>/<project>/_git/<repository>`) and are signed in through
the Git Credential Manager they already use for that organization.

## 8. Tests

- **E15:** catalog merge with one repository failing; migration of a changed `catalogUrl`; entry IDs
  that differ per repository; update only from the install source and the explicit source switch;
  update check deadline, automation skip and auto-update within the major version; a fake
  `git credential` helper (Bearer, JWT password, Basic, failure, timeout, approve/reject calls);
  PAT through a fake credential store; the credential absent from settings, logs, journal, job
  results and problem reports; no credential outside the scope or after a redirect that leaves it;
  203 and HTML replies treated as sign-in needed; the CLI refusing a token argument; MCP refusing a
  PAT. A local HTTPS test server answers catalog and package requests and records the headers.
- **E16:** pack manifest validation (no executable, Lua count and size, routes naming only pack
  previews); read-only pack previews and Copy to My Previews; user preview precedence; pack Lua in
  the TShark profile only while enabled; warm sessions discarded on update; JSON fallback without
  TShark; `python_decoders` listed and inactive.
- Both: GUI, CLI, native MCP and Python give the same results; `[plugins]` and new `[workbench][E15]`
  / `[workbench][E16]` Catch2 cases; `tests/plugins` for the catalog script.

## 9. Slices

- **E15a:** repository list, migration, merged catalog with sources, install-source pinning,
  `build_catalog.py`, Commander/CLI/MCP/Python operations for public repositories.
- **E15b:** Azure DevOps sign-in (credential manager, PAT store), credential scope and redirect
  rules.
- **E15c:** update check, notices, auto-update.
- **E16a:** preview pack kind with JSON previews and Lua for TShark.
- **E16b:** Python decoders, activated with MD03.

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
