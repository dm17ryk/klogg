"""CILogg process-plugin SDK 2.0.0, protocol 1. Python standard library only.

Version 2.0.0 adds capability negotiation (`Plugin.supports`, `host_limits`, `host_version`),
host queries answered on worker lanes (`@plugin.tooltip`, `@plugin.query`), tooltip
invalidation, registered icons, menus with dynamic submenus and commands
(`@plugin.menu_populate`, `@plugin.on_command`, `Plugin.update_menu`, `menu_item` and the other
menu builders) and forward compatibility with newer hosts. See docs/plugin-sdk.md and
docs/plugin-sdk-advanced.md.
"""
from __future__ import annotations

import base64
import collections
import collections.abc
import json
import os
import pathlib
import queue
import sys
import threading
import time
import uuid
from typing import (
    Any, Callable, Deque, Dict, FrozenSet, Iterable, List, Mapping, NamedTuple, Optional,
    Protocol, Sequence, Tuple, Type, TypedDict, Union, cast,
)

JsonObject = Dict[str, Any]
Action = Callable[["Job", JsonObject], Optional[JsonObject]]
ViewCallback = Callable[[JsonObject], None]

SDK_NAME = "cilogg_plugin.py"
SDK_VERSION = "2.0.0"
MAX_MESSAGE = 1024 * 1024
MAX_PENDING = 8 * MAX_MESSAGE
CANCEL_FLUSH_TIMEOUT = 1.0
VIEW_QUEUE_LIMIT = 1024
MESSAGE_BUTTONS = ("ok", "cancel", "yes", "no", "retry", "abort", "ignore", "close")

# Limits of a host that does not announce host_limits (the E13/E14 limits, unchanged since).
DEFAULT_HOST_LIMITS: Dict[str, int] = {
    "max_elements": 512, "max_table_cells": 50000, "max_text_chars": 4096,
    "max_message_bytes": MAX_MESSAGE,
}

# Host queries (docs/plugin-sdk-advanced.md, section 5).
TOOLTIP_RESOLVE = "tooltip/resolve"
QUERY_CANCEL = "query/cancel"
QUERY_DEFAULT_TIMEOUT_MS: Dict[str, int] = {
    "tooltip/resolve": 1500, "menu/populate": 1000, "tree/children": 10000, "field/complete": 500,
}
# Method -> (worker threads, latest wins). Other query methods get one FIFO worker each.
QUERY_LANES: Dict[str, Tuple[int, bool]] = {
    "tooltip/resolve": (1, True), "field/complete": (1, True),
    "menu/populate": (1, False), "tree/children": (2, False),
}
QUERY_QUEUE_LIMIT = 64
METHOD_NOT_FOUND = -32601
HANDLER_FAILED = -32000

TOOLTIP_TONES = ("normal", "ok", "warn", "error", "muted", "accent")
MAX_TOOLTIP_ROWS = 32
MAX_TOOLTIP_TTL_MS = 600000

# Menus, context menus and commands (docs/plugin-sdk-advanced.md, section 7).
MENU_POPULATE = "menu/populate"
UI_COMMAND = "ui/command"
MENU_UPDATE = "menu/update"
MENU_LOCATIONS = ("plugins", "menubar", "tab", "pane", "log", "element", "toolbar")
COMMAND_ORIGINS = ("menu", "shortcut", "toolbar", "status")
MAX_MENU_CACHE_MS = 60000

# Host methods the SDK handles itself; @plugin.query cannot take them over.
_CORE_HOST_METHODS = frozenset({
    "initialize", "shutdown", "invoke", "configure", "cancel", "stream/opened", "stream/ack",
    "stream/stop", "view/opened", "view/closed", "view/event", "ui/message_result", QUERY_CANCEL,
    UI_COMMAND,
})
_VIEW_NOTIFICATIONS = ("view/opened", "view/closed", "view/event", "ui/message_result")

# Element properties a host without the feature does not know; update() and patch() remove them.
_TOOLTIP_ELEMENT_KEYS = frozenset({"tooltip_provider", "tooltip_key"})
_TOOLTIP_COLUMN_KEYS = frozenset({"tooltip", "tooltip_provider", "tooltip_key"})
_TOOLTIP_ROW_KEYS = frozenset({"tooltip_provider", "tooltip_key"})
_ACCESSIBLE_KEYS = frozenset({"accessible_name", "accessible_description"})
_ROW_OPERATIONS = ("replace_rows", "update_rows", "insert_rows")
_SHORTCUT_KEYS = frozenset({"shortcut"})  # Not allowed in menu/populate answers.


# Message shapes (docs/plugin-sdk.md and docs/plugin-sdk-advanced.md). Keys of the
# `total=False` classes are optional.
class JsonRpcErrorObject(TypedDict):
    code: int
    message: str


class JsonRpcRequest(TypedDict):
    jsonrpc: str
    id: Union[str, int]
    method: str
    params: JsonObject


class JsonRpcNotification(TypedDict):
    jsonrpc: str
    method: str
    params: JsonObject


class HostLimits(TypedDict, total=False):
    max_elements: int
    max_table_cells: int
    max_text_chars: int
    max_message_bytes: int


class _InitializeParamsRequired(TypedDict):
    protocol_version: int
    plugin_id: str


class InitializeParams(_InitializeParamsRequired, total=False):
    configuration: JsonObject
    config_directory: str
    max_message_bytes: int
    max_pending_bytes: int
    host_features: List[str]
    host_feature_versions: Dict[str, int]
    host_version: str
    host_limits: HostLimits


class SdkInfo(TypedDict):
    name: str
    version: str


class _InitializeResultRequired(TypedDict):
    plugin_id: str
    protocol_version: int
    capabilities: List[str]
    actions: List[JsonObject]
    configuration: List[JsonObject]
    sdk: SdkInfo


class InitializeResult(_InitializeResultRequired, total=False):
    features: List[str]


# An icon reference: "icons/x.svg", "builtin:<name>", "status:<tone>", "image:<id>" or
# {"light": <ref>, "dark": <ref>}.
IconRef = Union[str, Dict[str, str]]


class _TooltipResolveRequired(TypedDict):
    view_id: str
    element_id: str


class TooltipResolveParams(_TooltipResolveRequired, total=False):
    window_id: str
    row_id: str
    column_id: str
    node_id: str
    tooltip_key: str
    timeout_ms: int
    context: JsonObject


class Tooltip(TypedDict, total=False):
    """An advanced tooltip; every part is plain text."""
    title: str
    text: str
    rows: List[List[str]]
    tone: str
    icon: IconRef
    footer: str
    ttl_ms: int


class _TooltipInvalidateRequired(TypedDict):
    view_id: str


class TooltipInvalidateParams(_TooltipInvalidateRequired, total=False):
    window_id: str
    element_id: str
    row_ids: List[str]
    node_ids: List[str]


class IconImage(TypedDict, total=False):
    """One registered image: exactly one of png (base64) or svg (text)."""
    png: str
    svg: str
    picture: bool
    mask: bool


class UiIconsParams(TypedDict, total=False):
    icons: Dict[str, IconImage]
    remove: List[str]


class QueryCancelParams(TypedDict):
    id: Union[str, int]


TooltipAnswer = Union[Tooltip, Mapping[str, Any], str, None]


class MenuWhen(TypedDict, total=False):
    """Conditions the host checks when a menu opens; the entry shows only when all hold."""
    views: List[str]  # main, filtered
    tab_kinds: List[str]  # file, serial, plugin_stream
    file_patterns: List[str]
    selection: str  # none, lines, any
    min_lines: int
    max_lines: int


class MenuEntry(TypedDict, total=False):
    """A menu entry (docs/plugin-sdk-advanced.md, section 7.3). An item does one of `action`
    (with `arguments`), `view` or `event`, or is a submenu (`items`, or `dynamic`)."""
    id: str
    type: str  # item (default), separator, section
    title: str
    icon: IconRef
    tooltip: str
    shortcut: Union[str, List[str]]
    enabled: bool
    visible: bool
    checkable: bool
    checked: bool
    group: str
    when: MenuWhen
    items: List[MenuEntry]
    dynamic: bool
    action: str
    arguments: JsonObject
    view: str
    event: bool


class _MenuRequired(TypedDict):
    id: str
    location: str
    items: List[MenuEntry]


class Menu(_MenuRequired, total=False):
    """One entry of the manifest's `menus`."""
    title: str
    icon: IconRef


class _MenuPopulateParamsRequired(TypedDict):
    menu_id: str
    item_id: str


class MenuPopulateParams(_MenuPopulateParamsRequired, total=False):
    location: str
    window_id: str
    context: JsonObject
    timeout_ms: int


class _MenuPopulateResultRequired(TypedDict):
    items: List[MenuEntry]


class MenuPopulateResult(_MenuPopulateResultRequired, total=False):
    """A menu/populate answer: entries without shortcuts; the host may reuse it for cache_ms."""
    cache_ms: int


class LogSelection(TypedDict, total=False):
    """`context.selection` of a log menu: 1-based [first, last] line ranges (at most 64), the
    selected line count, and in ui/command the selected text (at most 64 KiB)."""
    ranges: List[List[int]]
    count: int
    text: str
    truncated: bool


class _UiCommandParamsRequired(TypedDict):
    origin: str  # menu, shortcut, toolbar, status
    menu_id: str
    item_id: str


class UiCommandParams(_UiCommandParamsRequired, total=False):
    location: str
    window_id: str
    checked: bool
    context: JsonObject


class MenuItemChanges(TypedDict, total=False):
    title: str
    icon: IconRef
    tooltip: str
    enabled: bool
    visible: bool
    checked: bool


class MenuUpdateItem(TypedDict):
    id: str
    set: MenuItemChanges


class MenuUpdateParams(TypedDict):
    menu_id: str
    items: List[MenuUpdateItem]


MenuAnswer = Union[MenuPopulateResult, Sequence[MenuEntry], Sequence[Mapping[str, Any]],
                   Mapping[str, Any], None]


class LineReader(Protocol):
    def readline(self, size: int = -1, /) -> bytes: ...


class ByteWriter(Protocol):
    def write(self, data: bytes, /) -> int: ...

    def flush(self) -> None: ...


# Plugin view element builders (docs/plugin-views.md). Each returns a plain dict; arguments
# that are None are omitted. Element ids must be unique within one view. Every element also
# takes `tooltip`, `tooltip_provider`, `tooltip_key`, `accessible_name` and
# `accessible_description` as keyword properties; update() and patch() drop the ones the
# host does not know.
def _element(element_type: str, element_id: Optional[str], /, **props: Any) -> JsonObject:
    element: JsonObject = {"type": element_type}
    if element_id is not None:
        element["id"] = element_id
    element.update({key: value for key, value in props.items() if value is not None})
    return element


def column(*children: JsonObject, id: Optional[str] = None, **props: Any) -> JsonObject:
    return _element("column", id, children=list(children), **props)


def row(*children: JsonObject, id: Optional[str] = None, **props: Any) -> JsonObject:
    return _element("row", id, children=list(children), **props)


def group(title: str, *children: JsonObject, id: Optional[str] = None,
          collapsible: Optional[bool] = None, collapsed: Optional[bool] = None,
          **props: Any) -> JsonObject:
    return _element("group", id, title=title, collapsible=collapsible, collapsed=collapsed,
                    children=list(children), **props)


def label(id: str, text: str, tone: Optional[str] = None, bold: Optional[bool] = None,
          **props: Any) -> JsonObject:
    return _element("label", id, text=text, tone=tone, bold=bold, **props)


def badge(id: str, text: str, tone: Optional[str] = None, **props: Any) -> JsonObject:
    return _element("badge", id, text=text, tone=tone, **props)


def field(id: str, kind: str = "text", label: Optional[str] = None, value: Any = None,
          choices: Optional[Sequence[Any]] = None, filter: Optional[str] = None,
          placeholder: Optional[str] = None, **props: Any) -> JsonObject:
    return _element("field", id, kind=kind, label=label, value=value,
                    choices=list(choices) if choices is not None else None,
                    filter=filter, placeholder=placeholder, **props)


def button(id: str, label: str, action: Optional[str] = None,
           arguments: Optional[JsonObject] = None, event: Optional[bool] = None,
           tone: Optional[str] = None, confirm: Optional[JsonObject] = None,
           save_dialog: Optional[JsonObject] = None, **props: Any) -> JsonObject:
    return _element("button", id, label=label, action=action, arguments=arguments,
                    event=event, tone=tone, confirm=confirm, save_dialog=save_dialog, **props)


def progress(id: str, value: Optional[float] = None, busy: Optional[bool] = None,
             text: Optional[str] = None, **props: Any) -> JsonObject:
    return _element("progress", id, value=value, busy=busy, text=text, **props)


def table_column(id: str, title: Optional[str] = None, width: Optional[int] = None,
                 editable: Optional[bool] = None, tooltip: Optional[str] = None,
                 tooltip_provider: Optional[bool] = None,
                 tooltip_key: Optional[str] = None) -> JsonObject:
    """A table column. `tooltip` is the header tooltip; `tooltip_provider` asks the plugin
    for the tooltips of this column's cells (host feature tooltips)."""
    column_object: JsonObject = {"id": id, "title": title if title is not None else id}
    for key, value in (("width", width), ("editable", editable), ("tooltip", tooltip),
                       ("tooltip_provider", tooltip_provider), ("tooltip_key", tooltip_key)):
        if value is not None:
            column_object[key] = value
    return column_object


def table_row(id: str, cells: Sequence[Any], tone: Optional[str] = None,
              tooltip: Optional[str] = None, editors: Optional[JsonObject] = None,
              tooltip_provider: Optional[bool] = None,
              tooltip_key: Optional[str] = None) -> JsonObject:
    """A table row. `tooltip_key` (at most 128 characters) is part of the host's tooltip cache
    key: change it when the row's advanced tooltips change."""
    row_object: JsonObject = {"id": id, "cells": [str(cell) for cell in cells]}
    for key, value in (("tone", tone), ("tooltip", tooltip), ("editors", editors),
                       ("tooltip_provider", tooltip_provider), ("tooltip_key", tooltip_key)):
        if value is not None:
            row_object[key] = value
    return row_object


def editor(kind: str, choices: Optional[Sequence[Any]] = None, min: Optional[float] = None,
           max: Optional[float] = None, max_length: Optional[int] = None,
           placeholder: Optional[str] = None, value: Any = None) -> JsonObject:
    """A table cell editor: kind text, number, choice, multichoice or boolean (host feature view_edit).

    Pass it in table_row(editors={column_id: editor(...)}). value is the optional starting value
    (a list of checked values for multichoice)."""
    spec: JsonObject = {"kind": kind}
    for key, item in (("choices", list(choices) if choices is not None else None), ("min", min),
                      ("max", max), ("max_length", max_length), ("placeholder", placeholder),
                      ("value", value)):
        if item is not None:
            spec[key] = item
    return spec


def table(id: str, columns: Sequence[JsonObject], rows: Optional[Sequence[JsonObject]] = None,
          sortable: Optional[bool] = None, filter_box: Optional[bool] = None,
          selection: Optional[str] = None, **props: Any) -> JsonObject:
    return _element("table", id, columns=list(columns),
                    rows=list(rows) if rows is not None else None, sortable=sortable,
                    filter_box=filter_box, selection=selection, **props)


# Patch entries for Plugin.patch().
def set_props(element_id: str, **props: Any) -> JsonObject:
    return {"id": element_id, "set": props}


def replace_rows(table_id: str, rows: Sequence[JsonObject]) -> JsonObject:
    return {"id": table_id, "replace_rows": list(rows)}


def update_rows(table_id: str, rows: Sequence[JsonObject]) -> JsonObject:
    return {"id": table_id, "update_rows": list(rows)}


def insert_rows(table_id: str, rows: Sequence[JsonObject],
                before: Optional[str] = None) -> JsonObject:
    entry: JsonObject = {"id": table_id, "insert_rows": list(rows)}
    if before is not None:
        entry["before"] = before
    return entry


def remove_rows(table_id: str, row_ids: Sequence[str]) -> JsonObject:
    return {"id": table_id, "remove_rows": list(row_ids)}


# Advanced tooltips and icons (host features tooltips and icons).
def tooltip_content(text: Optional[str] = None, *, title: Optional[str] = None,
                    rows: Optional[Sequence[Sequence[Any]]] = None, tone: Optional[str] = None,
                    icon: Optional[IconRef] = None, footer: Optional[str] = None,
                    ttl_ms: Optional[int] = None) -> Tooltip:
    """Build an advanced tooltip answer. rows are (name, value) pairs (at most 32); ttl_ms is
    how long the host may cache the answer (0 to 600000, default 30 s on the host)."""
    if tone is not None and tone not in TOOLTIP_TONES:
        raise ValueError(f"Unknown tooltip tone: {tone}")
    if ttl_ms is not None and not 0 <= ttl_ms <= MAX_TOOLTIP_TTL_MS:
        raise ValueError(f"ttl_ms must be between 0 and {MAX_TOOLTIP_TTL_MS}")
    content: Tooltip = {}
    if title is not None:
        content["title"] = title
    if text is not None:
        content["text"] = text
    if rows is not None:
        pairs = [list(pair) for pair in rows]
        if len(pairs) > MAX_TOOLTIP_ROWS or any(len(pair) != 2 for pair in pairs):
            raise ValueError(f"Tooltip rows are at most {MAX_TOOLTIP_ROWS} (name, value) pairs")
        content["rows"] = [[str(name), str(value)] for name, value in pairs]
    if tone is not None:
        content["tone"] = tone
    if icon is not None:
        content["icon"] = icon
    if footer is not None:
        content["footer"] = footer
    if ttl_ms is not None:
        content["ttl_ms"] = ttl_ms
    return content


def png_icon(data: bytes, *, picture: bool = False) -> IconImage:
    """A register_icons() entry from PNG bytes; picture=True for the image element."""
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError("Not PNG data")
    entry: IconImage = {"png": base64.b64encode(data).decode("ascii")}
    if picture:
        entry["picture"] = True
    return entry


def svg_icon(text: str, *, picture: bool = False, mask: bool = False) -> IconImage:
    """A register_icons() entry from SVG text. mask=True marks a monochrome icon that the host
    recolours with the theme's text colour. The host refuses references other than data: URIs."""
    if "<svg" not in text:
        raise ValueError("Not SVG text")
    entry: IconImage = {"svg": text}
    if picture:
        entry["picture"] = True
    if mask:
        entry["mask"] = True
    return entry


def image_ref(icon_id: str) -> str:
    """The icon reference of an image registered with register_icons()."""
    return f"image:{icon_id}"


def builtin_ref(name: str) -> str:
    """The icon reference of a built-in host icon such as info, warning or device."""
    return f"builtin:{name}"


def status_ref(tone: str) -> str:
    """The icon reference of a coloured status dot (normal, ok, warn, error, muted, accent)."""
    return f"status:{tone}"


def themed_ref(light: str, dark: str) -> Dict[str, str]:
    """Different icon references for light and dark themes."""
    return {"light": light, "dark": dark}


# Menus (host features menus and shortcuts; docs/plugin-sdk-advanced.md, section 7). The
# builders return plain dicts for plugin.json `menus` and for menu/populate answers.
def _shortcut_keys(shortcut: Union[str, Sequence[str], None]) -> Union[str, List[str], None]:
    if shortcut is None:
        return None
    keys = [shortcut] if isinstance(shortcut, str) else list(shortcut)
    if not 1 <= len(keys) <= 2 or not all(isinstance(key, str) and key for key in keys):
        raise ValueError('A shortcut is a key sequence such as "Ctrl+Alt+D", or a list of two')
    return shortcut if isinstance(shortcut, str) else keys


def menu_item(id: str, title: str, *, icon: Optional[IconRef] = None,
              tooltip: Optional[str] = None, enabled: Optional[bool] = None,
              visible: Optional[bool] = None, checkable: Optional[bool] = None,
              checked: Optional[bool] = None, group: Optional[str] = None,
              event: Optional[bool] = None, action: Optional[str] = None,
              arguments: Optional[Mapping[str, Any]] = None, view: Optional[str] = None,
              items: Optional[Sequence[Mapping[str, Any]]] = None, dynamic: Optional[bool] = None,
              when: Optional[Mapping[str, Any]] = None,
              shortcut: Union[str, Sequence[str], None] = None) -> MenuEntry:
    """A menu entry. It does exactly one of `action` (with `arguments`), `view` or `event=True`
    (the host sends ui/command), or opens a submenu: `items`, or `dynamic=True` (filled by
    menu/populate), which takes no shortcut or check mark. `checkable` entries with the same
    `group` are exclusive (radio items); `when` holds the host's conditions (MenuWhen);
    `shortcut` is a key sequence or a list of two (not in menu/populate answers, where the SDK
    removes it). Raises ValueError for an entry the host would refuse for its shape."""
    if not id or not title:
        raise ValueError("A menu entry needs an id and a title")
    if "/" in id:
        raise ValueError(f"A menu entry id has no '/': {id}")
    targets = [name for name, value in (("action", action), ("view", view), ("event", event))
               if value is not None and value is not False]
    if items is not None or dynamic:
        if items is not None and dynamic:
            raise ValueError(f"Menu entry {id} has static items or is dynamic, not both")
        if targets or shortcut is not None or checkable:
            raise ValueError(f"Submenu {id} has no action, view, event, shortcut or check mark")
    elif len(targets) != 1:
        raise ValueError(f"Menu entry {id} needs exactly one of action, view or event")
    if arguments is not None and action is None:
        raise ValueError("Menu entry arguments need an action")
    if (group is not None or checked) and not checkable:
        raise ValueError(f"A checked or grouped menu entry is checkable: {id}")
    entry: JsonObject = {"id": id, "title": title}
    for key, value in (
            ("icon", icon), ("tooltip", tooltip), ("shortcut", _shortcut_keys(shortcut)),
            ("enabled", enabled), ("visible", visible), ("checkable", checkable),
            ("checked", checked), ("group", group), ("when", None if when is None else dict(when)),
            ("items", None if items is None else list(items)), ("dynamic", dynamic),
            ("action", action), ("arguments", None if arguments is None else dict(arguments)),
            ("view", view), ("event", event or None)):
        if value is not None:
            entry[key] = value
    return cast(MenuEntry, entry)


def menu_separator(*, id: Optional[str] = None) -> MenuEntry:
    """A separator line between menu entries."""
    entry: MenuEntry = {"type": "separator"}
    if id is not None:
        entry["id"] = id
    return entry


def menu_section(title: str, *, id: Optional[str] = None) -> MenuEntry:
    """A titled separator."""
    entry: MenuEntry = {"type": "section", "title": title}
    if id is not None:
        entry["id"] = id
    return entry


def menu(id: str, location: str, items: Sequence[Mapping[str, Any]], title: Optional[str] = None,
         icon: Optional[IconRef] = None) -> Menu:
    """A plugin.json `menus` entry. location is plugins (the plugin's submenu of Plugins, titled
    `title`), menubar, tab, pane, log, element or toolbar (MENU_LOCATIONS)."""
    if not id:
        raise ValueError("A menu needs an id")
    if "/" in id or id in ("action", "view"):
        raise ValueError(f"A menu id has no '/' and is not action or view: {id}")
    if location not in MENU_LOCATIONS:
        raise ValueError(f"Unknown menu location: {location}")
    result: Menu = {"id": id, "location": location,
                    "items": [cast(MenuEntry, dict(item)) for item in items]}
    if title is not None:
        result["title"] = title
    if icon is not None:
        result["icon"] = icon
    return result


def menu_set(item_id: str, *, title: Optional[str] = None, icon: Optional[IconRef] = None,
             tooltip: Optional[str] = None, enabled: Optional[bool] = None,
             visible: Optional[bool] = None, checked: Optional[bool] = None) -> MenuUpdateItem:
    """A Plugin.update_menu() entry: the new state of one declared entry."""
    if not item_id:
        raise ValueError("A menu update needs the entry id")
    changes: JsonObject = {key: value for key, value in (
        ("title", title), ("icon", icon), ("tooltip", tooltip), ("enabled", enabled),
        ("visible", visible), ("checked", checked)) if value is not None}
    return {"id": item_id, "set": cast(MenuItemChanges, changes)}


def _without_shortcuts(entry: Any) -> Any:
    """A menu/populate entry without `shortcut` keys (also in its submenu), copied only when
    something changes."""
    if not isinstance(entry, dict):
        return entry
    result = _without(entry, _SHORTCUT_KEYS)
    if "items" in result:
        items = _map_list(result["items"], _without_shortcuts)
        if items is not result["items"]:
            result = {**result, "items": items}
    return result


def _cache_ms(value: object) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise TypeError("cache_ms is a number of milliseconds")
    return max(0, min(MAX_MENU_CACHE_MS, int(value)))


def _menu_answer(answer: object) -> Optional[JsonObject]:
    """The menu/populate result of a @plugin.menu_populate handler's return value."""
    if answer is None:
        return None
    result: JsonObject
    if isinstance(answer, collections.abc.Mapping):
        result = dict(answer)
    elif isinstance(answer, collections.abc.Sequence) and not isinstance(answer, (str, bytes)):
        result = {"items": answer}
    else:
        raise TypeError("A menu/populate handler returns a list of entries, a dict with items "
                        "and cache_ms, or None")
    items = result.get("items")
    if not isinstance(items, collections.abc.Sequence) or isinstance(items, (str, bytes)):
        raise TypeError("A menu/populate answer needs a list of items")
    result["items"] = [_without_shortcuts(entry) for entry in items]
    if result.get("cache_ms") is not None:
        result["cache_ms"] = _cache_ms(result["cache_ms"])
    else:
        result.pop("cache_ms", None)
    return result


class Cancelled(Exception):
    """The host cancelled this job, query or stream, or the plugin is stopping."""


class Job:
    def __init__(self, plugin: Plugin, identifier: str, context: JsonObject) -> None:
        self.plugin = plugin
        self.id = identifier
        self.context = context
        self.cancelled = threading.Event()

    def check_cancelled(self) -> None:
        if self.cancelled.is_set() or self.plugin.stopping.is_set():
            raise Cancelled("Job cancelled")

    def progress(self, value: float) -> None:
        self.check_cancelled()
        self.plugin.notify("progress", {"job_id": self.id, "value": value})

    def panel(self, identifier: str, title: str, elements: List[JsonObject]) -> None:
        self.check_cancelled()
        self.plugin.notify("panel/update", {
            "job_id": self.id,
            "panel": {"id": identifier, "title": title, "elements": elements},
        })

    def stream(self) -> Stream:
        self.check_cancelled()
        return Stream(self)


class Stream:
    def __init__(self, job: Job) -> None:
        self.plugin = job.plugin
        self.job = job
        self.id = str(uuid.uuid4())
        self.sequence = 0
        self.closed = threading.Event()
        self.opened = threading.Event()
        self.ack = threading.Event()
        with self.plugin.lock:
            if len(self.plugin.streams) >= 128:
                raise RuntimeError("Too many streams")
            self.plugin.streams[self.id] = self
        try:
            self.plugin.notify("stream/open", {"job_id": job.id, "stream_id": self.id})
            self._wait(self.opened)
        except BaseException:
            self.close()
            raise

    def _wait(self, event: threading.Event) -> None:
        self.job.check_cancelled()
        while not event.wait(0.1):
            self.job.check_cancelled()
            if self.closed.is_set():
                raise Cancelled("Stream stopped")
        self.job.check_cancelled()
        if self.closed.is_set():
            raise Cancelled("Stream stopped")

    def write(self, records: Iterable[str], *, finish_on_cancel: bool = False) -> None:
        """Write a bounded batch; optionally finish it within one second of cancellation."""
        cancellation_deadline: Optional[float] = None

        def check() -> None:
            nonlocal cancellation_deadline
            if finish_on_cancel and self.job.cancelled.is_set() and not self.plugin.stopping.is_set():
                if cancellation_deadline is None:
                    cancellation_deadline = time.monotonic() + CANCEL_FLUSH_TIMEOUT
                if time.monotonic() >= cancellation_deadline:
                    raise Cancelled("Pending batch flush timed out")
            else:
                self.job.check_cancelled()
            if self.closed.is_set():
                raise Cancelled("Stream stopped")

        check()
        params = {"stream_id": self.id, "sequence": self.sequence, "records": list(records)}
        size = len(self.plugin.encode({"jsonrpc": "2.0", "method": "stream/data", "params": params}))
        with self.plugin.credit:
            while self.plugin.pending_bytes + size > MAX_PENDING:
                check()
                self.plugin.credit.wait(0.1)
            self.plugin.pending_bytes += size
        try:
            self.ack.clear()
            check()
            self.plugin.notify("stream/data", params)
            while not self.ack.wait(0.1):
                check()
            check()
            self.sequence += 1
        finally:
            with self.plugin.credit:
                self.plugin.pending_bytes -= size
                self.plugin.credit.notify_all()

    def close(self) -> None:
        try:
            if not self.closed.is_set():
                self.closed.set()
                self.plugin.notify("stream/close", {"stream_id": self.id})
        finally:
            with self.plugin.lock:
                self.plugin.streams.pop(self.id, None)

    def __enter__(self) -> Stream:
        return self

    def __exit__(self, *_args: Any) -> None:
        self.close()


class Message:
    """An open host message box; the result arrives as ui/message_result."""

    def __init__(self, plugin: Plugin, identifier: str,
                 callback: Optional[ViewCallback]) -> None:
        self.plugin = plugin
        self.id = identifier
        self.callback = callback
        self.result: Optional[JsonObject] = None
        self.done = threading.Event()

    def wait(self, timeout: Optional[float] = None) -> Optional[JsonObject]:
        """Block for the result: {message_id, button, timed_out}; None on timeout or shutdown."""
        deadline = None if timeout is None else time.monotonic() + timeout
        while not self.done.wait(0.1):
            if self.plugin.stopping.is_set():
                return None
            if deadline is not None and time.monotonic() >= deadline:
                return None
        return self.result

    def close(self) -> None:
        """Close the box; the result reports button "closed"."""
        self.plugin.notify("ui/close_message", {"message_id": self.id})


class View:
    """Convenience wrapper that binds a declared view id."""

    def __init__(self, plugin: Plugin, identifier: str) -> None:
        self.plugin = plugin
        self.id = identifier

    def update(self, root: JsonObject, window_id: Optional[str] = None) -> None:
        self.plugin.update(self.id, root, window_id)

    def patch(self, element_id: Optional[str] = None, window_id: Optional[str] = None,
              changes: Optional[Sequence[JsonObject]] = None, **props: Any) -> None:
        entries = list(changes or [])
        if element_id is not None:
            entries.append(set_props(element_id, **props))
        self.plugin.patch(self.id, entries, window_id)

    def show(self, window_id: Optional[str] = None, **overrides: Any) -> None:
        self.plugin.show_view(self.id, window_id, **overrides)

    def hide(self, window_id: Optional[str] = None) -> None:
        self.plugin.hide_view(self.id, window_id)

    def invalidate_tooltips(self, element_id: Optional[str] = None,
                            row_ids: Optional[Sequence[str]] = None,
                            node_ids: Optional[Sequence[str]] = None,
                            window_id: Optional[str] = None) -> bool:
        return self.plugin.invalidate_tooltips(self.id, element_id, row_ids, node_ids, window_id)


def _optional_text(value: Any) -> Optional[str]:
    return value if isinstance(value, str) else None


def _query_timeout_ms(method: str, value: Any) -> Optional[float]:
    if isinstance(value, (int, float)) and not isinstance(value, bool) and value > 0:
        return float(value)
    default = QUERY_DEFAULT_TIMEOUT_MS.get(method)
    return None if default is None else float(default)


def _id_key(identifier: Any) -> str:
    return json.dumps(identifier, sort_keys=True)


class Query:
    """A request from the host that a query handler answers (docs/plugin-sdk.md).

    The handler's return value is the answer. `deadline` is the time.monotonic() value after
    which the host stops waiting (None when the host gave no timeout and the method has no
    default); `cancelled` is set by query/cancel, by a newer query in a latest-wins lane and
    when the plugin stops, and then no answer is sent."""

    def __init__(self, plugin: Plugin, identifier: Any, method: str, params: JsonObject,
                 received: Optional[float] = None) -> None:
        self.plugin = plugin
        self.id = identifier
        self.method = method
        self.params = params
        self.received = time.monotonic() if received is None else received
        self.timeout_ms = _query_timeout_ms(method, params.get("timeout_ms"))
        self.deadline: Optional[float] = (
            None if self.timeout_ms is None else self.received + self.timeout_ms / 1000.0)
        context = params.get("context")
        self.context: JsonObject = context if isinstance(context, dict) else {}
        self.cancelled = threading.Event()
        self.lock = threading.Lock()

    def remaining(self) -> float:
        """Seconds left until the deadline (0 when passed, infinity without a deadline)."""
        if self.deadline is None:
            return float("inf")
        return max(0.0, self.deadline - time.monotonic())

    def expired(self) -> bool:
        return self.deadline is not None and time.monotonic() > self.deadline

    def check_cancelled(self) -> None:
        """Raise Cancelled when nobody waits for the answer any more."""
        if self.cancelled.is_set() or self.plugin.stopping.is_set():
            raise Cancelled("Query cancelled")

    def cancel(self) -> None:
        with self.lock:
            self.cancelled.set()


class TooltipQuery(Query):
    """A tooltip/resolve query: which element (and row, column or node) the pointer rests on."""

    @property
    def view_id(self) -> str:
        return _optional_text(self.params.get("view_id")) or ""

    @property
    def element_id(self) -> str:
        return _optional_text(self.params.get("element_id")) or ""

    @property
    def window_id(self) -> Optional[str]:
        return _optional_text(self.params.get("window_id"))

    @property
    def row_id(self) -> Optional[str]:
        return _optional_text(self.params.get("row_id"))

    @property
    def column_id(self) -> Optional[str]:
        return _optional_text(self.params.get("column_id"))

    @property
    def node_id(self) -> Optional[str]:
        return _optional_text(self.params.get("node_id"))

    @property
    def tooltip_key(self) -> Optional[str]:
        return _optional_text(self.params.get("tooltip_key"))


def _is_line(value: object) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and value >= 1


class _MenuContext:
    """What a menu query or command reads from its `context`."""

    context: JsonObject

    def _selection_value(self, key: str) -> object:
        selection = self.context.get("selection")
        return selection.get(key) if isinstance(selection, dict) else None

    @property
    def line(self) -> Optional[int]:
        """context.line of a log menu: the 1-based line under the pointer, or None."""
        line = self.context.get("line")
        return line if isinstance(line, int) and _is_line(line) else None

    @property
    def selection(self) -> Optional[LogSelection]:
        """context.selection of a log menu (ranges, count; text and truncated in a command)."""
        selection = self.context.get("selection")
        return cast(LogSelection, selection) if isinstance(selection, dict) else None

    @property
    def ranges(self) -> List[Tuple[int, int]]:
        """The selected [first, last] line ranges (1-based, at most 64); [] without a selection."""
        ranges = self._selection_value("ranges")
        if not isinstance(ranges, list):
            return []
        return [(int(pair[0]), int(pair[1])) for pair in ranges
                if isinstance(pair, (list, tuple)) and len(pair) == 2
                and _is_line(pair[0]) and _is_line(pair[1]) and pair[0] <= pair[1]]

    @property
    def line_count(self) -> int:
        """The number of selected lines: `count` (all ranges, also beyond the 64 reported), else
        the lines of `ranges`."""
        count = self._selection_value("count")
        if isinstance(count, int) and not isinstance(count, bool) and count >= 0:
            return count
        return sum(last - first + 1 for first, last in self.ranges)


class MenuQuery(Query, _MenuContext):
    """A menu/populate query: a dynamic submenu is about to show."""

    @property
    def menu_id(self) -> str:
        return _optional_text(self.params.get("menu_id")) or ""

    @property
    def item_id(self) -> str:
        return _optional_text(self.params.get("item_id")) or ""

    @property
    def location(self) -> Optional[str]:
        return _optional_text(self.params.get("location"))

    @property
    def window_id(self) -> Optional[str]:
        return _optional_text(self.params.get("window_id"))


class UiCommand(_MenuContext):
    """A ui/command: the user picked an `event` menu entry (origin "menu") or pressed its
    shortcut ("shortcut"); toolbars and status items come later ("toolbar", "status")."""

    def __init__(self, plugin: Plugin, params: JsonObject) -> None:
        self.plugin = plugin
        self.params = params
        context = params.get("context")
        self.context = context if isinstance(context, dict) else {}

    @property
    def origin(self) -> str:
        return _optional_text(self.params.get("origin")) or ""

    @property
    def menu_id(self) -> str:
        return _optional_text(self.params.get("menu_id")) or ""

    @property
    def item_id(self) -> str:
        return _optional_text(self.params.get("item_id")) or ""

    @property
    def location(self) -> Optional[str]:
        return _optional_text(self.params.get("location"))

    @property
    def window_id(self) -> Optional[str]:
        return _optional_text(self.params.get("window_id"))

    @property
    def checked(self) -> Optional[bool]:
        """The new check state of a checkable entry; None for other entries."""
        checked = self.params.get("checked")
        return checked if isinstance(checked, bool) else None

    @property
    def text(self) -> Optional[str]:
        """The selected text of a log entry (at most 64 KiB), or None."""
        return _optional_text(self._selection_value("text"))

    @property
    def truncated(self) -> bool:
        """True when the host cut the selected text at 64 KiB."""
        return self._selection_value("truncated") is True


QueryHandler = Callable[[Query], Any]
TooltipHandler = Callable[[TooltipQuery], TooltipAnswer]
MenuPopulateHandler = Callable[[MenuQuery], MenuAnswer]
CommandHandler = Callable[[UiCommand], None]
_QUERY_CLASSES: Dict[str, Type[Query]] = {TOOLTIP_RESOLVE: TooltipQuery, MENU_POPULATE: MenuQuery}


def _tooltip_answer(answer: object) -> Optional[JsonObject]:
    if answer is None:
        return None
    if isinstance(answer, str):
        return {"text": answer}
    if isinstance(answer, collections.abc.Mapping):
        return dict(answer)
    raise TypeError("A tooltip handler returns a dict, a string or None")


class _QueryLane:
    """The worker threads of one query method (QUERY_LANES)."""

    def __init__(self, plugin: Plugin, method: str, workers: int, latest_wins: bool) -> None:
        self.plugin = plugin
        self.method = method
        self.workers = workers
        self.latest_wins = latest_wins
        self.condition = threading.Condition()
        self.waiting: Deque[Query] = collections.deque()
        self.threads: List[threading.Thread] = []
        self.closed = False

    def submit(self, query: Query) -> Optional[List[Query]]:
        """Queue a query. Returns the waiting queries it replaced, or None when it was refused."""
        with self.condition:
            if self.closed:
                return None
            replaced: List[Query] = []
            if self.latest_wins:
                replaced = list(self.waiting)
                self.waiting.clear()
            elif len(self.waiting) >= QUERY_QUEUE_LIMIT:
                return None
            self.waiting.append(query)
            if len(self.threads) < self.workers:
                thread = threading.Thread(
                    target=self._run, daemon=True,
                    name=f"plugin-query-{self.method}-{len(self.threads) + 1}")
                self.threads.append(thread)
                thread.start()
            self.condition.notify()
        return replaced

    def remove(self, query: Query) -> None:
        with self.condition:
            try:
                self.waiting.remove(query)
            except ValueError:
                pass

    def close(self) -> List[threading.Thread]:
        with self.condition:
            self.closed = True
            self.waiting.clear()
            self.condition.notify_all()
            return list(self.threads)

    def _run(self) -> None:
        while True:
            with self.condition:
                while not self.waiting and not self.closed:
                    self.condition.wait()
                if self.closed:
                    return
                query = self.waiting.popleft()
            self.plugin._run_query(query)


class _StripRules(NamedTuple):
    element: FrozenSet[str]
    column: FrozenSet[str]
    row: FrozenSet[str]


def _without(item: Any, keys: FrozenSet[str]) -> Any:
    if not isinstance(item, dict) or keys.isdisjoint(item):
        return item
    return {key: value for key, value in item.items() if key not in keys}


def _map_list(items: Any, convert: Callable[[Any], Any]) -> Any:
    """Convert every item; return the original list when nothing changed."""
    if not isinstance(items, list):
        return items
    converted = [convert(item) for item in items]
    return items if all(new is old for new, old in zip(converted, items)) else converted


def _compatible_element(element: Any, rules: _StripRules) -> Any:
    """Return the element (or a `set` dict of patch properties) without the properties the
    host does not know, copying only what changes."""
    if not isinstance(element, dict):
        return element
    result = _without(element, rules.element)
    changes: JsonObject = {}
    for key, convert in (("children", lambda child: _compatible_element(child, rules)),
                         ("columns", lambda item: _without(item, rules.column)),
                         ("rows", lambda item: _without(item, rules.row))):
        if key in result:
            value = _map_list(result[key], convert)
            if value is not result[key]:
                changes[key] = value
    if changes:
        result = {**result, **changes}
    return result


def _compatible_patch_entry(entry: Any, rules: _StripRules) -> Any:
    if not isinstance(entry, dict):
        return entry
    changes: JsonObject = {}
    if "set" in entry:
        properties = _compatible_element(entry["set"], rules)
        if properties is not entry["set"]:
            changes["set"] = properties
    for operation in _ROW_OPERATIONS:
        if operation in entry:
            rows = _map_list(entry[operation], lambda item: _without(item, rules.row))
            if rows is not entry[operation]:
                changes[operation] = rows
    return {**entry, **changes} if changes else entry


class Plugin:
    def __init__(self, manifest_path: Union[str, os.PathLike[str], Mapping[str, Any]] = "plugin.json") -> None:
        """Read plugin.json (the host starts the plugin in its folder) or take a manifest dict."""
        if isinstance(manifest_path, collections.abc.Mapping):
            self.manifest: JsonObject = dict(manifest_path)
        else:
            self.manifest = json.loads(pathlib.Path(manifest_path).read_text(encoding="utf-8"))
        self.actions: Dict[str, Action] = {}
        self.jobs: Dict[str, Job] = {}
        self.streams: Dict[str, Stream] = {}
        self.workers: Dict[str, threading.Thread] = {}
        self.configuration: JsonObject = {}
        self.config_directory: Optional[str] = None
        self.host_features: List[str] = []
        self.host_feature_versions: Dict[str, int] = {}
        self.host_version: Optional[str] = None
        self.host_limits: Dict[str, int] = dict(DEFAULT_HOST_LIMITS)
        self.features: List[str] = []
        self.lock = threading.RLock()
        self.output_lock = threading.Lock()
        self.output: Optional[ByteWriter] = None
        self.credit = threading.Condition()
        self.pending_bytes = 0
        self.stopping = threading.Event()
        self.initialized = False
        self.view_callbacks: Dict[str, List[ViewCallback]] = {
            "view/opened": [], "view/closed": [], "view/event": []}
        self.messages: Dict[str, Message] = {}
        self.view_queue: queue.Queue[Optional[Tuple[List[ViewCallback], JsonObject]]] = (
            queue.Queue(maxsize=VIEW_QUEUE_LIMIT))
        self.view_worker: Optional[threading.Thread] = None
        self.query_handlers: Dict[str, QueryHandler] = {}
        self.tooltip_handlers: Dict[Tuple[str, Optional[str]], TooltipHandler] = {}
        self.menu_populate_handlers: Dict[Tuple[str, Optional[str]], MenuPopulateHandler] = {}
        self.command_handlers: Dict[Tuple[Optional[str], Optional[str]], CommandHandler] = {}
        self.queries: Dict[str, Query] = {}
        self.lanes: Dict[str, _QueryLane] = {}

    def action(self, identifier: str) -> Callable[[Action], Action]:
        def register(callback: Action) -> Action:
            self.actions[identifier] = callback
            return callback
        return register

    # Capability negotiation (docs/plugin-sdk-advanced.md, section 4).
    def supports(self, feature: str, version: int = 1) -> bool:
        """True when the host announced `feature` at `version` or newer. A host that sends no
        host_feature_versions has every announced feature at version 1."""
        if feature not in self.host_features:
            return False
        return self.host_feature_versions.get(feature, 1) >= version

    # Host queries (docs/plugin-sdk-advanced.md, section 5).
    def query(self, method: str) -> Callable[[QueryHandler], QueryHandler]:
        """Register handler(query) -> answer for a host query method such as tree/children.

        The handler runs on the method's lane, never on the reader, a job or a view callback
        thread. Its return value (JSON) is the result; an exception answers error -32000.
        Register handlers before serve(): the initialize result lists them in `features`, and
        plugin.json must list the method (and query/cancel) in `handles`."""
        if not method or method in _CORE_HOST_METHODS:
            raise ValueError(f"Not a query method: {method!r}")

        def register(handler: QueryHandler) -> QueryHandler:
            self.query_handlers[method] = handler
            return handler
        return register

    def tooltip(self, view_id: str,
                element_id: Optional[str] = None) -> Callable[[TooltipHandler], TooltipHandler]:
        """Register handler(TooltipQuery) for the advanced tooltips of one element, or of any
        element of the view when element_id is None (an element's own handler wins).

        The handler returns a dict (see tooltip_content()), a plain string (the tooltip text)
        or None (no advanced tooltip: the static one is shown)."""
        if not view_id:
            raise ValueError("A tooltip handler needs a view id")

        def register(handler: TooltipHandler) -> TooltipHandler:
            self.tooltip_handlers[(view_id, element_id)] = handler
            return handler
        return register

    # Menus and commands (docs/plugin-sdk-advanced.md, section 7).
    def menu_populate(self, menu_id: str, item_id: Optional[str] = None
                      ) -> Callable[[MenuPopulateHandler], MenuPopulateHandler]:
        """Register handler(MenuQuery) that fills the `dynamic` entry item_id of a menu, or
        every dynamic entry of the menu when item_id is None (an entry's own handler wins; a
        @plugin.query("menu/populate") handler is the last fallback).

        The handler runs on the menu/populate lane and returns a list of entries (menu_item(),
        menu_separator(), menu_section()), a dict {"items": [...], "cache_ms": n} or None
        (nothing to show). The SDK removes `shortcut` keys from the
        entries and keeps cache_ms within 0..60000 ms; any other return value answers error
        -32000. plugin.json must list menu/populate (and query/cancel) in `handles`."""
        if not menu_id or item_id == "":
            raise ValueError("A menu/populate handler needs a menu id (and a non-empty entry id)")

        def register(handler: MenuPopulateHandler) -> MenuPopulateHandler:
            self.menu_populate_handlers[(menu_id, item_id)] = handler
            return handler
        return register

    def on_command(self, menu_id: Optional[str] = None,
                   item_id: Optional[str] = None) -> Callable[[CommandHandler], CommandHandler]:
        """Register handler(UiCommand) for the `event` entries of a menu (or of every menu when
        menu_id is None), or for one entry. The most specific handler wins: menu and entry,
        then the entry in any menu, then the menu, then the handler without ids.

        Handlers run in arrival order on the worker thread of the view callbacks, never on the
        protocol reader. plugin.json must list ui/command in `handles`."""
        if menu_id == "" or item_id == "":
            raise ValueError("Menu and entry ids of a command handler are None or non-empty")

        def register(handler: CommandHandler) -> CommandHandler:
            self.command_handlers[(menu_id, item_id)] = handler
            return handler
        return register

    def update_menu(self, menu_id: str,
                    items: Union[Sequence[Mapping[str, Any]], Mapping[str, Mapping[str, Any]]]
                    ) -> bool:
        """Change declared entries of a menu in every window (open menus change in place):
        a list of menu_set(item_id, title=, icon=, tooltip=, enabled=, visible=, checked=)
        entries, or a dict {item_id: {"title": ..., ...}}. Returns False (and sends nothing) on
        a host without the menus feature."""
        if not menu_id:
            raise ValueError("A menu update needs the menu id")
        pairs: List[Tuple[Any, Any]] = (
            list(items.items()) if isinstance(items, collections.abc.Mapping)
            else [(item.get("id"), item.get("set")) if isinstance(item, collections.abc.Mapping)
                  else (None, None) for item in items])
        entries: List[MenuUpdateItem] = []
        for item_id, changes in pairs:
            if not isinstance(item_id, str) or not item_id:
                raise ValueError("Every menu update names an entry id")
            if not isinstance(changes, collections.abc.Mapping):
                raise ValueError(f"The changes of menu entry {item_id} are not a dict")
            entries.append({"id": item_id, "set": cast(MenuItemChanges, dict(changes))})
        params: MenuUpdateParams = {"menu_id": menu_id, "items": entries}
        if not self.supports("menus"):
            return False
        self.notify(MENU_UPDATE, params)
        return True

    def handled_methods(self) -> List[str]:
        """The host-to-plugin methods the registered handlers answer (`features`)."""
        methods = set(self.query_handlers)
        if self.tooltip_handlers:
            methods.add(TOOLTIP_RESOLVE)
        if self.menu_populate_handlers:
            methods.add(MENU_POPULATE)
        if methods:
            methods.add(QUERY_CANCEL)
        if self.command_handlers:
            methods.add(UI_COMMAND)
        return sorted(methods)

    def invalidate_tooltips(self, view_id: str, element_id: Optional[str] = None,
                            row_ids: Optional[Sequence[str]] = None,
                            node_ids: Optional[Sequence[str]] = None,
                            window_id: Optional[str] = None) -> bool:
        """Drop the host's cached advanced tooltips of a view, an element, or some of its rows
        or nodes. Returns False (and sends nothing) on a host without the tooltips feature."""
        if (row_ids is not None or node_ids is not None) and element_id is None:
            raise ValueError("row_ids and node_ids need the element_id of their table or tree")
        params: TooltipInvalidateParams = {"view_id": view_id}
        if window_id:
            params["window_id"] = window_id
        if element_id is not None:
            params["element_id"] = element_id
        if row_ids is not None:
            params["row_ids"] = [str(item) for item in row_ids]
        if node_ids is not None:
            params["node_ids"] = [str(item) for item in node_ids]
        if not self.supports("tooltips"):
            return False
        self.notify("tooltip/invalidate", params)
        return True

    def register_icons(self, icons: Mapping[str, IconImage],
                       remove: Optional[Sequence[str]] = None) -> bool:
        """Register images (png_icon(), svg_icon()) for the plugin's lifetime and remove others;
        use them as image_ref(id). Returns False (and sends nothing) on a host without the
        icons feature."""
        entries: Dict[str, IconImage] = {}
        for icon_id, entry in icons.items():
            if not isinstance(icon_id, str) or not icon_id:
                raise ValueError("Icon ids are non-empty strings")
            if ("png" in entry) == ("svg" in entry):
                raise ValueError(f"Icon {icon_id} needs exactly one of png or svg")
            entries[icon_id] = entry
        params: UiIconsParams = {"icons": entries}
        if remove:
            params["remove"] = [str(item) for item in remove]
        if not self.supports("icons"):
            return False
        self.notify("ui/icons", params)
        return True

    # Plugin views and message boxes (docs/plugin-views.md).
    @property
    def supports_views(self) -> bool:
        return "views" in self.host_features

    @property
    def supports_view_edit(self) -> bool:
        return "view_edit" in self.host_features

    def on_view_opened(self, callback: ViewCallback) -> ViewCallback:
        """Register callback(params) for view/opened {view_id, window_id, context}."""
        self.view_callbacks["view/opened"].append(callback)
        return callback

    def on_view_closed(self, callback: ViewCallback) -> ViewCallback:
        """Register callback(params) for view/closed {view_id, window_id}."""
        self.view_callbacks["view/closed"].append(callback)
        return callback

    def on_view_event(self, callback: ViewCallback) -> ViewCallback:
        """Register callback(params) for view/event {view_id, window_id, element_id, type,
        value, inputs}. Callbacks run in order on one worker thread, never the reader."""
        self.view_callbacks["view/event"].append(callback)
        return callback

    def view(self, identifier: str) -> View:
        return View(self, identifier)

    def _strip_rules(self) -> Optional[_StripRules]:
        element: FrozenSet[str] = frozenset()
        column_keys: FrozenSet[str] = frozenset()
        row_keys: FrozenSet[str] = frozenset()
        if not self.supports("tooltips"):
            element |= _TOOLTIP_ELEMENT_KEYS
            column_keys |= _TOOLTIP_COLUMN_KEYS
            row_keys |= _TOOLTIP_ROW_KEYS
        if not self.supports("views", 2):
            element |= _ACCESSIBLE_KEYS
        if not (element or column_keys or row_keys):
            return None
        return _StripRules(element, column_keys, row_keys)

    def update(self, view_id: str, root: JsonObject, window_id: Optional[str] = None) -> None:
        """Replace a view's element tree (in every window without window_id)."""
        rules = self._strip_rules()
        params: JsonObject = {
            "view_id": view_id, "root": root if rules is None else _compatible_element(root, rules)}
        if window_id:
            params["window_id"] = window_id
        self.notify("view/update", params)

    def patch(self, view_id: str, changes: Sequence[JsonObject],
              window_id: Optional[str] = None) -> None:
        """Apply set_props()/row operation entries to existing elements."""
        rules = self._strip_rules()
        entries = list(changes)
        if rules is not None:
            entries = [_compatible_patch_entry(entry, rules) for entry in entries]
        params: JsonObject = {"view_id": view_id, "patch": entries}
        if window_id:
            params["window_id"] = window_id
        self.notify("view/update", params)

    def show_view(self, view_id: str, window_id: Optional[str] = None, *,
                  area: Optional[str] = None, floating: Optional[bool] = None,
                  title: Optional[str] = None, focus: Optional[bool] = None) -> None:
        """Open or raise a declared view (default: the most recently active window)."""
        params = _element("view", None, view_id=view_id, window_id=window_id or None,
                          area=area, floating=floating, title=title, focus=focus)
        del params["type"]
        self.notify("view/show", params)

    def hide_view(self, view_id: str, window_id: Optional[str] = None) -> None:
        params: JsonObject = {"view_id": view_id}
        if window_id:
            params["window_id"] = window_id
        self.notify("view/hide", params)

    def message(self, text: str, title: str = "", icon: str = "info",
                buttons: Sequence[str] = ("ok",), default: Optional[str] = None,
                timeout_ms: Optional[int] = None, modal: bool = True,
                window_id: Optional[str] = None, callback: Optional[ViewCallback] = None,
                message_id: Optional[str] = None) -> Message:
        """Show a host message box. Use the returned Message.wait() or a result callback."""
        unknown = [name for name in buttons if name not in MESSAGE_BUTTONS]
        if unknown or not buttons:
            raise ValueError(f"Unsupported message buttons: {unknown or 'none'}")
        identifier = message_id or str(uuid.uuid4())
        handle = Message(self, identifier, callback)
        with self.lock:
            self.messages[identifier] = handle
        params = _element("message", None, message_id=identifier, title=title, text=text[:4096],
                          icon=icon, buttons=list(buttons), default=default,
                          timeout_ms=timeout_ms, modal=modal, window_id=window_id or None)
        del params["type"]
        try:
            self.notify("ui/message", params)
        except BaseException:
            with self.lock:
                self.messages.pop(identifier, None)
            raise
        return handle

    def _queue_view_callback(self, callbacks: List[ViewCallback], params: JsonObject) -> None:
        if not callbacks:
            return
        with self.lock:
            if self.view_worker is None:
                self.view_worker = threading.Thread(target=self._run_view_callbacks,
                                                    name="plugin-view-callbacks", daemon=True)
                self.view_worker.start()
        try:
            self.view_queue.put_nowait((list(callbacks), params))
        except queue.Full:
            print("Plugin view callback queue is full; event dropped", file=sys.stderr, flush=True)

    def _run_view_callbacks(self) -> None:
        while True:
            item = self.view_queue.get()
            if item is None:
                return
            callbacks, params = item
            for callback in callbacks:
                try:
                    callback(params)
                except Exception as error:  # A callback failure never stops the protocol.
                    print(f"Plugin view callback failed: {error}", file=sys.stderr, flush=True)

    def _view_notification(self, method: str, params: JsonObject) -> None:
        if method == "ui/message_result":
            with self.lock:
                handle = self.messages.pop(str(params.get("message_id")), None)
            if handle:
                handle.result = params
                handle.done.set()
                if handle.callback:
                    self._queue_view_callback([handle.callback], params)
            return
        self._queue_view_callback(self.view_callbacks[method], params)

    @staticmethod
    def encode(message: Mapping[str, Any]) -> bytes:
        encoded = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        if len(encoded) > MAX_MESSAGE:
            raise ValueError("Plugin message exceeds 1 MiB; split it into smaller batches")
        return encoded + b"\n"

    def send(self, message: Mapping[str, Any]) -> None:
        encoded = self.encode(message)
        with self.output_lock:
            output: ByteWriter = self.output if self.output is not None else sys.stdout.buffer
            output.write(encoded)
            output.flush()

    def notify(self, method: str, params: Mapping[str, Any]) -> None:
        self.send({"jsonrpc": "2.0", "method": method, "params": params})

    def _send_error(self, request_id: Any, code: int, text: str) -> None:
        self.send({"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": text}})

    def _invoke(self, request_id: str, params: JsonObject) -> None:
        identifier = params["job_id"]
        job = Job(self, identifier, params.get("context", {}))
        with self.lock:
            if len(self.jobs) >= 128:
                self._send_error(request_id, HANDLER_FAILED, "Too many jobs")
                return
            self.jobs[identifier] = job

        def run() -> None:
            try:
                job.check_cancelled()
                result = self.actions[params["action"]](job, params.get("arguments", {}))
                job.check_cancelled()
                self.send({"jsonrpc": "2.0", "id": request_id, "result": result or {}})
            except Exception as error:
                print(f"Plugin job {identifier}: {error}", file=sys.stderr, flush=True)
                if not self.stopping.is_set():
                    self._send_error(request_id, HANDLER_FAILED, str(error))
            finally:
                with self.lock:
                    self.jobs.pop(identifier, None)
                    self.workers.pop(identifier, None)

        worker = threading.Thread(target=run, name=f"plugin-job-{identifier}", daemon=True)
        with self.lock:
            self.workers[identifier] = worker
        worker.start()

    def _has_query_handler(self, method: str) -> bool:
        return (method in self.query_handlers
                or (method == TOOLTIP_RESOLVE and bool(self.tooltip_handlers))
                or (method == MENU_POPULATE and bool(self.menu_populate_handlers)))

    def _submit_query(self, identifier: Any, method: str, params: JsonObject) -> None:
        if self.stopping.is_set():
            return
        query = _QUERY_CLASSES.get(method, Query)(self, identifier, method, params)
        with self.lock:
            key = _id_key(identifier)
            previous = self.queries.get(key)
            self.queries[key] = query
            lane = self.lanes.get(method)
            if lane is None:
                workers, latest_wins = QUERY_LANES.get(method, (1, False))
                lane = self.lanes[method] = _QueryLane(self, method, workers, latest_wins)
        if previous is not None:  # The host reused a pending id: only the newest gets an answer.
            previous.cancel()
            self._lane_remove(previous)
        replaced = lane.submit(query)
        if replaced is None:
            self._forget_query(query)
            if not self.stopping.is_set():
                self._send_error(identifier, HANDLER_FAILED, "Too many waiting queries")
            return
        for older in replaced:  # Latest wins: a replaced query gets no answer.
            older.cancel()
            self._forget_query(older)

    def _lane_remove(self, query: Query) -> None:
        with self.lock:
            lane = self.lanes.get(query.method)
        if lane is not None:
            lane.remove(query)

    def _forget_query(self, query: Query) -> None:
        with self.lock:
            key = _id_key(query.id)
            if self.queries.get(key) is query:
                del self.queries[key]

    def _cancel_query(self, identifier: Any) -> None:
        with self.lock:
            query = self.queries.pop(_id_key(identifier), None)
        if query is not None:
            query.cancel()
            self._lane_remove(query)

    def _tooltip_handler(self, query: TooltipQuery) -> Optional[Callable[[TooltipQuery], object]]:
        specific = self.tooltip_handlers.get((query.view_id, query.element_id))
        if specific is not None:
            return specific
        general = self.tooltip_handlers.get((query.view_id, None))
        if general is not None:
            return general
        return self.query_handlers.get(TOOLTIP_RESOLVE)

    def _answer_query(self, query: Query) -> Any:
        if isinstance(query, TooltipQuery):
            handler = self._tooltip_handler(query)
            return None if handler is None else _tooltip_answer(handler(query))
        if isinstance(query, MenuQuery):
            populate = (self.menu_populate_handlers.get((query.menu_id, query.item_id))
                        or self.menu_populate_handlers.get((query.menu_id, None)))
            if populate is not None:
                return _menu_answer(populate(query))
        query_handler = self.query_handlers.get(query.method)
        return None if query_handler is None else query_handler(query)

    def _command_handler(self, command: UiCommand) -> Optional[CommandHandler]:
        menu_id, item_id = command.menu_id or None, command.item_id or None
        for key in ((menu_id, item_id), (None, item_id), (menu_id, None), (None, None)):
            handler = self.command_handlers.get(key)
            if handler is not None:
                return handler
        return None

    def _command(self, params: JsonObject) -> None:
        """Queue a ui/command for its handler on the view-callback worker."""
        if not self.command_handlers:
            return  # Not announced in `features`: a host does not send it.
        command = UiCommand(self, params)
        handler = self._command_handler(command)
        if handler is None:
            print(f"cilogg_plugin: no ui/command handler for menu {command.menu_id!r} entry "
                  f"{command.item_id!r}", file=sys.stderr, flush=True)
            return

        def run(_params: JsonObject) -> None:
            try:
                handler(command)
            except Exception as error:  # A handler failure never stops the protocol.
                print(f"Plugin command {command.menu_id}/{command.item_id} failed: {error}",
                      file=sys.stderr, flush=True)
        self._queue_view_callback([run], params)

    def _run_query(self, query: Query) -> None:
        try:
            # Dropped without an answer: cancelled, replaced, stopping, or too late to start.
            if query.cancelled.is_set() or self.stopping.is_set() or query.expired():
                return
            response: JsonObject
            try:
                response = {"jsonrpc": "2.0", "id": query.id, "result": self._answer_query(query)}
                self.encode(response)  # The answer must be JSON and fit into one message.
            except Exception as error:
                if not query.cancelled.is_set():
                    print(f"Plugin query {query.method} {query.id}: {error}", file=sys.stderr,
                          flush=True)
                response = {"jsonrpc": "2.0", "id": query.id, "error": {
                    "code": HANDLER_FAILED, "message": str(error) or type(error).__name__}}
            with query.lock:
                if query.cancelled.is_set() or self.stopping.is_set():
                    return
                try:
                    self.send(response)
                except Exception as error:  # The host may already be gone.
                    print(f"Plugin query {query.method} {query.id}: {error}", file=sys.stderr,
                          flush=True)
        finally:
            self._forget_query(query)

    def _warn_unlisted_handles(self) -> None:
        handles = self.manifest.get("handles")
        listed = {name for name in handles if isinstance(name, str)} if isinstance(handles, list) else set()
        missing = [method for method in self.handled_methods() if method not in listed]
        if missing:
            print("cilogg_plugin: warning: plugin.json \"handles\" does not list "
                  f"{', '.join(missing)}; CILogg sends these methods only to plugins that list "
                  "them", file=sys.stderr, flush=True)

    def _initialize(self, request_id: Any, params: JsonObject) -> None:
        if self.initialized or params.get("protocol_version") != 1 or params.get("plugin_id") != self.manifest["id"]:
            raise ValueError("Incompatible initialization")
        self.configuration = params.get("configuration", {})
        self.config_directory = params.get("config_directory")
        self.host_features = [str(name) for name in params.get("host_features", [])]
        versions = params.get("host_feature_versions")
        self.host_feature_versions = {
            str(name): version for name, version in versions.items()
            if isinstance(version, int) and not isinstance(version, bool) and version >= 1
        } if isinstance(versions, dict) else {}
        self.host_version = _optional_text(params.get("host_version")) or None
        limits = params.get("host_limits")
        self.host_limits = dict(DEFAULT_HOST_LIMITS)
        if isinstance(limits, dict):
            self.host_limits.update({
                str(name): value for name, value in limits.items()
                if isinstance(value, int) and not isinstance(value, bool) and value > 0})
        self.features = self.handled_methods()
        self.initialized = True
        result: InitializeResult = {
            "plugin_id": self.manifest["id"], "protocol_version": 1,
            "capabilities": self.manifest["capabilities"],
            "actions": self.manifest.get("actions", []),
            "configuration": self.manifest.get("configuration", []),
            "sdk": {"name": SDK_NAME, "version": SDK_VERSION},
        }
        if self.features:
            result["features"] = list(self.features)
        self.send({"jsonrpc": "2.0", "id": request_id, "result": result})

    def _dispatch(self, message: JsonObject) -> None:
        method = message.get("method")
        params = message.get("params", {})
        if not isinstance(method, str) or not isinstance(params, dict):
            raise ValueError("Malformed host JSON-RPC")
        is_request = "id" in message
        if method == "initialize":
            self._initialize(message["id"], params)
        elif method == "shutdown":
            self.stopping.set()
        elif not self.initialized:
            raise ValueError("Plugin has not initialized")
        elif method == "invoke":
            self._invoke(message["id"], params)
        elif method == "configure":
            self.configuration = params["configuration"]
        elif method == "cancel":
            with self.lock:
                job = self.jobs.get(params["job_id"])
                if job:
                    job.cancelled.set()
        elif method in ("stream/opened", "stream/ack", "stream/stop"):
            with self.lock:
                stream = self.streams.get(params["stream_id"])
                if stream:
                    if method == "stream/opened":
                        stream.opened.set()
                    elif method == "stream/ack" and params.get("sequence") == stream.sequence:
                        stream.ack.set()
                    elif method == "stream/stop":
                        stream.closed.set()
        elif method in _VIEW_NOTIFICATIONS:
            self._view_notification(method, params)
        elif method == UI_COMMAND and not is_request:
            self._command(params)
        elif method == QUERY_CANCEL:
            self._cancel_query(params.get("id"))
        elif is_request and self._has_query_handler(method):
            self._submit_query(message["id"], method, params)
        elif is_request:
            # Forward compatibility: a newer host's request is refused, never fatal.
            self._send_error(message["id"], METHOD_NOT_FOUND, "Method not found")
        # A newer host's notification is ignored.

    def serve(self, stdin: Optional[LineReader] = None, stdout: Optional[ByteWriter] = None) -> None:
        """Run the protocol until shutdown or end of input (default: this process's stdin and
        stdout; a test host can pass binary streams)."""
        if stdout is not None:
            self.output = stdout
        source: LineReader = stdin if stdin is not None else sys.stdin.buffer
        self._warn_unlisted_handles()
        try:
            while not self.stopping.is_set():
                line = source.readline(MAX_MESSAGE + 2)
                if not line:
                    break
                if len(line.rstrip(b"\r\n")) > MAX_MESSAGE or not line.endswith(b"\n"):
                    raise ValueError("Oversized or truncated host message")
                message = json.loads(line)
                if not isinstance(message, dict) or message.get("jsonrpc") != "2.0":
                    raise ValueError("Malformed host JSON-RPC")
                self._dispatch(message)
        except Exception as error:
            print(f"Plugin protocol failed: {error}", file=sys.stderr, flush=True)
        finally:
            self.stopping.set()
            with self.lock:
                for job in self.jobs.values():
                    job.cancelled.set()
                for stream in self.streams.values():
                    stream.closed.set()
                workers = list(self.workers.values())
                view_worker = self.view_worker
                for handle in self.messages.values():
                    handle.done.set()
                self.messages.clear()
                queries = list(self.queries.values())
                self.queries.clear()
                lanes = list(self.lanes.values())
            for pending in queries:
                pending.cancel()
            for lane in lanes:
                workers.extend(lane.close())
            with self.credit:
                self.credit.notify_all()
            deadline = time.monotonic() + 2.8
            for worker in workers:
                worker.join(max(0.0, deadline - time.monotonic()))
            if view_worker is not None:
                try:
                    self.view_queue.put(None, timeout=max(0.0, deadline - time.monotonic()))
                    view_worker.join(max(0.0, deadline - time.monotonic()))
                except queue.Full:
                    pass
