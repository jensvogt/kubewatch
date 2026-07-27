# KubeWatch

A lightweight, native desktop client for browsing and managing Kubernetes clusters, built with Qt 6 and C++23.
KubeWatch wraps `kubectl` behind a fast, resizable, sortable/filterable table UI so you can inspect cluster state
without leaving a GUI, while still landing back on `kubectl`/`aws` for every actual cluster call.

## Features

- **Cluster navigation tree** — Pods, Deployments, StatefulSets, DaemonSets, Jobs, Services, Ingresses, ConfigMaps,
  Secrets, PersistentVolumeClaims, Nodes and Namespaces, grouped under Workloads / Service / Config and Storage /
  Cluster.
- **Context & namespace switcher** — pick the active kubeconfig context and namespace (or "All namespaces") from the
  toolbar; the selection is remembered across restarts.
- **Sortable, paginated, filterable tables** — every resource list supports column sorting, a configurable page size,
  and a name-prefix search box.
- **Health traffic light** — Pods, Deployments and Jobs show a red/orange/green status light at a glance (pod not
  running, fewer ready replicas than desired, job failed/suspended, etc.).
- **Resource detail dialogs** — double-click a row to open a Kubernetes-Dashboard-style detail view (Metadata,
  resource info, conditions, replica sets, events, ...) for Pods, Deployments, Jobs, Services, Ingresses, Nodes and
  Namespaces.
- **Row actions** — right-click a row for **Edit** (opens the live YAML in an editor and applies it via `kubectl` on
  save), **Delete** (with confirmation), and **Logs** (for Pods, and for Jobs via their owned Pods).
- **Live log panel** — a dockable log pane at the bottom of the window with auto-scroll toggle and copy-to-clipboard.
- **OneLogin / AWS SSO sign-in** — authenticates against OneLogin (SAML + MFA/OTP), then calls AWS STS
  `AssumeRoleWithSAML` to mint short-lived AWS credentials, which are wired into `kubectl`'s exec-based auth plugin
  for EKS clusters.
- **Auto-update checks** — polls a `version.txt` published on GitHub Pages, both periodically and on demand from the
  toolbar, with a one-click download link.
- **Light/dark icon themes** and a configurable page size via a local JSON config file.

## Requirements

- `kubectl`, configured with the context(s) you want to browse, available on `PATH`.
- `aws` CLI available on `PATH` if you use the OneLogin/AWS SSO sign-in flow (needed for `AssumeRoleWithSAML`).
- Windows, macOS or Linux with a Qt 6.10+ runtime (bundled by the installer builds).

## Installation

Download the latest installer/package for your platform from the
[GitHub Releases](https://github.com/jensvogt/kubewatch/releases) page (Windows/macOS/Linux builds are produced by
CI using the Qt Installer Framework). KubeWatch checks for new versions on startup and offers to download updates
from the same place.

## Building from source

Requirements: CMake 3.28+, a C++23 compiler (MSVC, Clang or GCC), and Qt 6 (Core, Gui, Widgets, Svg, Network).

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.y/<kit>
cmake --build build --target kubewatch
```

On Windows with MSVC, run this from a Developer Command Prompt (or after sourcing `vcvars64.bat`) so the standard
library headers resolve correctly. The build copies the required Qt runtime DLLs and platform/icon-engine/TLS
plugins next to the executable automatically.

## Configuration

On first launch, KubeWatch writes a small JSON config file to `~/.kubewatch/kubewatch.json`:

```json
{
  "ui": {
    "style-type": "dark",
    "page-size": 25,
    "busy-indicator-delay-ms": 500
  },
  "general": {
    "update-check-period": 86400
  }
}
```

| Key | Description |
|---|---|
| `ui.style-type` | Icon theme, `"dark"` or `"light"`. |
| `ui.page-size` | Default rows per page for resource tables. |
| `ui.busy-indicator-delay-ms` | How long a `kubectl` call must run before the busy overlay appears. |
| `general.update-check-period` | Seconds between background update checks. |

The last-used context, namespace and navigation selection are also persisted here automatically.

## Usage

1. On startup, sign in via the OneLogin dialog (or dismiss it if your kubeconfig context doesn't need it).
2. Pick a **Context** and **Namespace** from the toolbar.
3. Select a resource type from the tree on the left.
4. Use the search box above a table to filter by name prefix, click column headers to sort, and use the pager
   controls to move between pages.
5. Double-click a row to see full details, or right-click for **Edit** / **Delete** / **Logs**.
6. Watch the log panel at the bottom for a live feed of `kubectl` activity.

## Project layout

```
main.cpp                 Application/window wiring, navigation tree, context menus
include/, src/
  components/            Reusable paginated table widget
  tables/                One class per resource list (PodsTable, JobsTable, ...)
  dialogs/                Resource detail dialogs, edit/logs/login dialogs
  kubectl/                kubectl process wrapper, busy overlay
  onelogin/               OneLogin SAML + AWS STS login
  utils/                  Formatting helpers, config, icons, health-light indicator
installer/                Qt Installer Framework packaging config
resources/                Icons (light/dark) and Qt resource files
```

## Versioning & releases

KubeWatch uses [release-please](https://github.com/googleapis/release-please) with conventional commits to manage
versioning and `CHANGELOG.md`; merges to `master` that warrant a release are built and packaged for Windows, macOS
and Linux by GitHub Actions.
