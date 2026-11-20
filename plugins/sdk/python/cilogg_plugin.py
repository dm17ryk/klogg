"""CILogg process-plugin SDK 2.0.0, protocol 1. Python standard library only.

Version 2.0.0 adds capability negotiation (`Plugin.supports`, `host_limits`, `host_version`),
host queries answered on worker lanes (`@plugin.tooltip`, `@plugin.query`), tooltip
invalidation, registered icons, menus with dynamic submenus and commands
(`@plugin.menu_populate`, `@plugin.on_command`, `Plugin.update_menu`, `menu_item` and the other
menu builders), choices, check boxes, radio buttons, new field kinds, validation and field
completion (`choice`, `checkbox`, `radio_group`, `check_group`, `@plugin.complete`), rich tables
and element context menus (`cell`, the new `table`, `table_column` and `table_row` keys,
`select_rows`) and forward compatibility with newer hosts. See docs/plugin-sdk.md and
docs/plugin-sdk-advanced.md.
"""
from __future__ import annotations

import base64
import collections
import collections.abc
import json
import math
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

# Choices, check boxes, radio buttons, field kinds, validation and completion
# (docs/plugin-sdk-advanced.md, section 8; host features choices, toggles, field_kinds and
# validation).
FIELD_COMPLETE = "field/complete"
MAX_COMPLETION_ITEMS = 100
MAX_PATTERN_LENGTH = 256
MAX_INLINE_COLUMNS = 12
E14_FIELD_KINDS = ("text", "number", "choice", "multichoice", "boolean", "file_open", "file_save",
                   "directory")
# The kinds of host feature field_kinds, and the kind a host without it gets instead.
FIELD_KIND_FALLBACKS: Dict[str, str] = {
    "multiline": "text", "secret": "text", "integer": "number", "slider": "number",
    "color": "text", "date": "text", "time": "text", "datetime": "text",
}
FIELD_KINDS = E14_FIELD_KINDS + tuple(FIELD_KIND_FALLBACKS)
# The styles of choice and multichoice fields: a drop-down or an inline list.
CHOICE_STYLES: Dict[str, Tuple[str, ...]] = {
    "choice": ("dropdown", "radio"), "multichoice": ("dropdown", "checkboxes")}
ORIENTATIONS = ("vertical", "horizontal")

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
# Field and element keys of the E17c features (update() and patch() remove them per feature).
_CHOICE_OBJECT_KEYS = frozenset({"icon", "tooltip", "enabled"})  # inside choice objects
_CHOICE_MARKERS = ("separator", "section")
_CHOICE_FIELD_KEYS = frozenset({"indicator", "style", "searchable", "editable", "completion",
                                "completion_timeout_ms", "orientation", "columns"})
_EDITOR_CHOICE_KEYS = frozenset({"searchable", "indicator"})  # in table cell editors
_FIELD_KIND_KEYS = frozenset({"rows", "monospace", "step"})
_BOUND_KEYS = frozenset({"min", "max"})
_VALIDATION_KEYS = frozenset({"required", "pattern", "min", "max", "min_length", "max_length",
                              "error", "requires_valid"})
_TOGGLE_KEYS = frozenset({"tristate", "all_none"})
# A toggle element on a host without toggles: (field kind, inline style, caption key).
_TOGGLE_FIELDS: Dict[str, Tuple[str, Optional[str], str]] = {
    "checkbox": ("boolean", None, "text"), "radio_group": ("choice", "radio", "title"),
    "check_group": ("multichoice", "checkboxes", "title")}
_BOUNDED_KINDS = ("integer", "slider")  # their min and max belong to field_kinds

# Rich tables and element context menus (docs/plugin-sdk-advanced.md, section 10; host features
# rich_tables and element_menus).
CELL_TONES = TOOLTIP_TONES
COLUMN_ALIGNMENTS = ("left", "center", "right")
SORT_AS = ("text", "number", "natural")
TABLE_EVENTS = ("activated", "checked", "moved")  # besides selected and edited
MAX_TABLE_IDS = 1000  # ids the host uses in select, selected_ids, checked and moved
# Keys of the rich_tables feature (update() and patch() remove them on a host without it).
_RICH_COLUMN_KEYS = frozenset({"align", "hidden", "sortable", "sort_as", "min_width", "check",
                               "icon"})
_RICH_ROW_KEYS = frozenset({"icon", "checked", "drag"})
_RICH_TABLE_KEYS = frozenset({"header", "toggle_selection", "copy", "export", "reorderable",
                              "keep_columns"})
_SELECTION_PATCH_KEYS = frozenset({"select", "current", "scroll_to"})
_ELEMENT_MENU_KEYS = frozenset({"context_menu"})  # the element_menus feature


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
    """Conditions the host checks when a menu opens; the entry shows only when all hold. In an
    element menu `selection` and `min_lines`/`max_lines` count the selected rows, and `views`,
    `tab_kinds` and `file_patterns` never hold."""
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


class ElementMenuContext(TypedDict, total=False):
    """The `context` of an element menu entry (location "element", host feature
    element_menus): the clicked row and column (None when the click was not on a cell) and the
    selected row ids (at most 1000) with their count."""
    window_id: str
    view_id: str
    element_id: str
    row_id: Optional[str]
    column_id: Optional[str]
    selected_ids: List[str]
    selected_count: int


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

# Choices, check boxes, radio buttons, field kinds and validation (docs/plugin-sdk-advanced.md,
# section 8). A choice is a plain value or a Choice; separators and sections sit between them.
ChoiceValue = Union[str, int, float]


class _ChoiceRequired(TypedDict):
    value: ChoiceValue


class Choice(_ChoiceRequired, total=False):
    """A choice object; `icon`, `tooltip` and `enabled` need the host feature choices."""
    label: str
    icon: IconRef
    tooltip: str
    enabled: bool


class ChoiceSeparator(TypedDict):
    type: str  # separator


class ChoiceSection(TypedDict):
    type: str  # section
    label: str


ChoiceEntry = Union[ChoiceValue, Choice, ChoiceSeparator, ChoiceSection]
# A button's requires_valid: True (every field of the view) or a list of field ids.
RequiresValid = Union[bool, List[str]]


class ElementProperties(TypedDict, total=False):
    """Properties every view element takes."""
    id: str
    enabled: bool
    visible: bool
    tooltip: str
    tooltip_provider: bool
    tooltip_key: str
    accessible_name: str
    accessible_description: str


class FieldElement(ElementProperties, total=False):
    """A `field` element with the keys of E13/E14 and of the E17c features."""
    type: str  # field
    kind: str  # FIELD_KINDS
    label: str
    value: Any
    choices: List[ChoiceEntry]
    filter: str
    placeholder: str
    # Host feature choices (kinds choice and multichoice; completion also on text fields).
    indicator: str  # radio
    style: str  # CHOICE_STYLES
    searchable: bool
    editable: bool
    completion: bool
    orientation: str  # ORIENTATIONS, with an inline style
    columns: int  # 1..12, with an inline style
    # Host feature field_kinds.
    rows: int  # multiline
    monospace: bool  # multiline
    step: float  # integer, slider
    # Host feature validation (min and max also bound integer and slider fields).
    min: float
    max: float
    required: bool
    pattern: str  # a regular expression for the whole text, at most 256 characters
    min_length: int
    max_length: int
    error: str  # the plugin's message; "" clears it


class CheckboxElement(ElementProperties, total=False):
    """A `checkbox` element (host feature toggles); `value` None is partly checked."""
    type: str  # checkbox
    text: str
    value: Optional[bool]
    tristate: bool
    tone: str


class RadioGroupElement(ElementProperties, total=False):
    """A `radio_group` element (host feature toggles)."""
    type: str  # radio_group
    title: str
    choices: List[ChoiceEntry]
    value: ChoiceValue
    orientation: str
    columns: int


class CheckGroupElement(ElementProperties, total=False):
    """A `check_group` element (host feature toggles); `all_none` adds All and None links."""
    type: str  # check_group
    title: str
    choices: List[ChoiceEntry]
    value: List[ChoiceValue]
    orientation: str
    columns: int
    all_none: bool


SortValue = Union[str, int, float]


class _TableCellRequired(TypedDict):
    text: str


class TableCell(_TableCellRequired, total=False):
    """A cell object (host feature rich_tables); a cell may also be a plain string. On an older
    host update() and patch() send its text."""
    icon: IconRef
    tooltip: str
    tooltip_key: str
    tone: str  # CELL_TONES; wins over the row's tone
    bold: bool
    progress: float  # 0..1, a bar behind the text
    sort: SortValue  # the sort value instead of the text


class TableColumn(TypedDict, total=False):
    """A table column; the keys after `tooltip_key` need the host feature rich_tables."""
    id: str
    title: str
    width: int
    editable: bool
    tooltip: str
    tooltip_provider: bool
    tooltip_key: str
    align: str  # COLUMN_ALIGNMENTS
    hidden: bool
    sortable: bool
    sort_as: str  # SORT_AS
    min_width: int
    check: bool  # a check box bound to the row's `checked`
    icon: IconRef  # in the header


class TableRow(TypedDict, total=False):
    """A table row; `icon`, `checked` and `drag` need the host feature rich_tables."""
    id: str
    cells: List[Union[str, TableCell]]
    tone: str
    tooltip: str
    editors: JsonObject
    tooltip_provider: bool
    tooltip_key: str
    icon: IconRef  # in the first column
    checked: bool  # shown in every check column
    drag: Union[bool, JsonObject]  # accepted; used from E17g


class TableElement(ElementProperties, total=False):
    """A `table` element. `context_menu` needs the host feature element_menus, the keys before
    it rich_tables."""
    type: str  # table
    columns: List[TableColumn]
    rows: List[TableRow]
    sortable: bool
    filter_box: bool
    selection: str  # none, single, multi
    header: bool
    toggle_selection: bool
    copy: bool
    export: bool
    reorderable: bool
    keep_columns: bool
    context_menu: str


class _SelectionPatchRequired(TypedDict):
    id: str


class SelectionPatch(_SelectionPatchRequired, total=False):
    """A patch entry that sets a table's selection (host feature rich_tables); it never sends
    a `selected` event back."""
    select: List[str]
    current: str
    scroll_to: str


class TableActivatedValue(TypedDict):
    """The value of a table's `activated` event: a double-click or Enter on a cell that is not
    editable (sent only to plugins whose manifest declares `handles`)."""
    row_id: str
    column_id: str


class TableCheckedValue(TypedDict):
    """The value of a table's `checked` event: `row_id` is the clicked row, `row_ids` every
    row that changed (a Shift+click range, at most 1000)."""
    row_id: str
    row_ids: List[str]
    checked: bool


class TableMovedValue(TypedDict):
    """The value of a reorderable table's `moved` event: the dragged rows in shown order and
    the row they were dropped before (None: the end). The host does not move them: the plugin
    confirms with a patch."""
    row_ids: List[str]
    before: Optional[str]


class _ViewEventParamsRequired(TypedDict):
    view_id: str
    element_id: str
    type: str  # changed, clicked, selected, edited, activated, checked, moved


class ViewEventParams(_ViewEventParamsRequired, total=False):
    """A view/event. On a host with validation, the `changed` event of a field that declares a
    validation key (required, pattern, min, max, min_length, max_length or a non-empty error)
    carries `valid`; read it as optional, True when absent. Check boxes, radio groups and
    check groups send `changed` with their value. A table's `selected` event carries
    `selected_count` and, for plugins whose manifest declares `handles` on a host with
    rich_tables, `current` (the row with the keyboard focus, or None); `activated`, `checked`
    and `moved` carry TableActivatedValue, TableCheckedValue and TableMovedValue."""
    window_id: str
    value: Any
    inputs: JsonObject
    valid: bool
    truncated: bool
    selected_count: int
    current: Optional[str]
    row_id: str
    column_id: str
    previous: str


class _CompleteParamsRequired(TypedDict):
    view_id: str
    element_id: str
    text: str
    cursor: int


class CompleteParams(_CompleteParamsRequired, total=False):
    """A field/complete query: typing paused in a field with `completion`."""
    window_id: str
    timeout_ms: int
    context: JsonObject


class _CompletionItemRequired(TypedDict):
    value: str


class CompletionItem(_CompletionItemRequired, total=False):
    label: str
    icon: IconRef


class CompleteResult(TypedDict):
    """A field/complete answer: at most 100 items."""
    items: List[CompletionItem]


CompleteAnswer = Union[CompleteResult, Sequence[CompletionItem], Sequence[str],
                       Sequence[Mapping[str, Any]], Mapping[str, Any], None]


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


def _check_layout(orientation: Optional[str], columns: Optional[int]) -> None:
    if orientation is not None and orientation not in ORIENTATIONS:
        raise ValueError(f"orientation is vertical or horizontal, not {orientation!r}")
    if columns is not None and (isinstance(columns, bool) or not isinstance(columns, int)
                                or not 1 <= columns <= MAX_INLINE_COLUMNS):
        raise ValueError(f"columns is a whole number from 1 to {MAX_INLINE_COLUMNS}")


def _check_choice_keys(kind: str, indicator: Optional[str], style: Optional[str],
                       searchable: Optional[bool], editable: Optional[bool],
                       completion: Optional[bool]) -> None:
    if indicator is not None and (indicator != "radio" or kind != "choice"):
        raise ValueError('indicator is "radio", on a choice field')
    if style is not None and style not in CHOICE_STYLES.get(kind, ()):
        raise ValueError(f"A {kind} field has no style {style!r}")
    if searchable and kind not in CHOICE_STYLES:
        raise ValueError("searchable is for choice and multichoice fields")
    if editable and kind != "choice":
        raise ValueError("editable is for choice fields")
    if completion and not (kind == "text" or (kind == "choice" and editable)):
        raise ValueError("completion is for text fields and editable choice fields")


def _check_bounds(low: Optional[float], high: Optional[float], name: str) -> None:
    if low is not None and high is not None and low > high:
        raise ValueError(f"min{name} is greater than max{name}")


def _is_number(value: Any, whole: bool) -> bool:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return False
    if isinstance(value, float) and not math.isfinite(value):
        return False
    return not whole or float(value).is_integer()


def _check_field_values(kind: str, pattern: Optional[str], minimum: Optional[float],
                        maximum: Optional[float], step: Optional[float], rows: Optional[int],
                        min_length: Optional[int], max_length: Optional[int]) -> None:
    if pattern is not None and (not isinstance(pattern, str) or len(pattern) > MAX_PATTERN_LENGTH):
        raise ValueError(f"pattern is a regular expression of at most {MAX_PATTERN_LENGTH} "
                         "characters")
    # integer and slider fields take whole bounds and steps (the host would truncate them);
    # rows and length rules are always whole.
    discrete = kind in ("integer", "slider")
    for name, value, whole in (("min", minimum, discrete), ("max", maximum, discrete),
                               ("step", step, discrete), ("rows", rows, True),
                               ("min_length", min_length, True),
                               ("max_length", max_length, True)):
        if value is not None and not _is_number(value, whole):
            raise ValueError(f"{name} is a {'whole' if whole else 'finite'} number")
    _check_bounds(minimum, maximum, "")
    _check_bounds(min_length, max_length, "_length")
    for name, value in (("min_length", min_length), ("max_length", max_length)):
        if value is not None and value < 0:
            raise ValueError(f"{name} is not negative")
    if step is not None and step <= 0:
        raise ValueError("step is positive")
    if rows is not None and rows < 1:
        raise ValueError("rows is at least 1")


def field(id: str, kind: str = "text", label: Optional[str] = None, value: Any = None,
          choices: Optional[Sequence[Any]] = None, filter: Optional[str] = None,
          placeholder: Optional[str] = None, *, indicator: Optional[str] = None,
          style: Optional[str] = None, searchable: Optional[bool] = None,
          editable: Optional[bool] = None, completion: Optional[bool] = None,
          orientation: Optional[str] = None, columns: Optional[int] = None,
          rows: Optional[int] = None, monospace: Optional[bool] = None,
          min: Optional[float] = None, max: Optional[float] = None,
          step: Optional[float] = None, required: Optional[bool] = None,
          pattern: Optional[str] = None, min_length: Optional[int] = None,
          max_length: Optional[int] = None, error: Optional[str] = None,
          **props: Any) -> JsonObject:
    """A form field (FieldElement). `kind` is one of FIELD_KINDS; choices are plain values or
    choice(), choice_separator() and choice_section() entries.

    Keyword keys of the E17c host features (update() and patch() remove what the host lacks):
    choices: `indicator="radio"` and `style` ("dropdown", "radio" for choice, "checkboxes" for
    multichoice) with `orientation` and `columns`, `searchable`, `editable` (choice) and
    `completion` (text, or an editable choice; answered by @plugin.complete); field_kinds:
    `rows` and `monospace` (multiline), `step` (integer, slider); validation: `required`,
    `pattern` (whole text, at most 256 characters), `min`/`max` (which also bound integer and
    slider fields), `min_length`/`max_length` and `error` ("" clears it). Raises ValueError
    for a combination the host refuses."""
    _check_choice_keys(kind, indicator, style, searchable, editable, completion)
    _check_layout(orientation, columns)
    _check_field_values(kind, pattern, min, max, step, rows, min_length, max_length)
    return _element("field", id, kind=kind, label=label, value=value,
                    choices=list(choices) if choices is not None else None,
                    filter=filter, placeholder=placeholder, indicator=indicator, style=style,
                    searchable=searchable or None, editable=editable or None,
                    completion=completion or None, orientation=orientation, columns=columns,
                    rows=rows, monospace=monospace, min=min, max=max, step=step,
                    required=required, pattern=pattern, min_length=min_length,
                    max_length=max_length, error=error, **props)


def _requires_valid(value: Union[bool, Sequence[str], None]) -> Optional[RequiresValid]:
    if value is None or isinstance(value, bool):
        return True if value else None  # False: no requirement
    if isinstance(value, str) or not all(isinstance(item, str) and item for item in value):
        raise ValueError("requires_valid is True or a list of field ids")
    return list(value)


def button(id: str, label: str, action: Optional[str] = None,
           arguments: Optional[JsonObject] = None, event: Optional[bool] = None,
           tone: Optional[str] = None, confirm: Optional[JsonObject] = None,
           save_dialog: Optional[JsonObject] = None, *,
           requires_valid: Union[bool, Sequence[str], None] = None,
           **props: Any) -> JsonObject:
    """A button. `requires_valid` (host feature validation) keeps it disabled until every
    field of the view (True) or the listed fields are valid."""
    return _element("button", id, label=label, action=action, arguments=arguments,
                    event=event, tone=tone, confirm=confirm, save_dialog=save_dialog,
                    requires_valid=_requires_valid(requires_valid), **props)


def checkbox(id: str, text: str, value: Optional[bool] = False, *,
             tristate: Optional[bool] = None, tone: Optional[str] = None,
             **props: Any) -> JsonObject:
    """A check box (CheckboxElement, host feature toggles). value is True, False or, with
    tristate=True, None (partly checked); `changed` events and `inputs` carry it. On a host
    without toggles update() sends a boolean field captioned `text` (None becomes False)."""
    if value is None and not tristate:
        raise ValueError("A partly checked box (value None) needs tristate=True")
    element = _element("checkbox", id, text=text, tristate=tristate, tone=tone, **props)
    element["value"] = value
    return element


def radio_group(id: str, choices: Sequence[Any], value: Any = None, *,
                title: Optional[str] = None, orientation: Optional[str] = None,
                columns: Optional[int] = None, **props: Any) -> JsonObject:
    """Radio buttons (RadioGroupElement, host feature toggles) in an optional titled frame;
    the value is the chosen choice value. On a host without toggles update() sends a choice
    field captioned `title`."""
    _check_layout(orientation, columns)
    return _element("radio_group", id, title=title, choices=list(choices), value=value,
                    orientation=orientation, columns=columns, **props)


def check_group(id: str, choices: Sequence[Any], value: Optional[Sequence[Any]] = None, *,
                title: Optional[str] = None, orientation: Optional[str] = None,
                columns: Optional[int] = None, all_none: Optional[bool] = None,
                **props: Any) -> JsonObject:
    """Check boxes (CheckGroupElement, host feature toggles); the value is the list of checked
    choice values, and `all_none` adds All and None links. On a host without toggles update()
    sends a multichoice field captioned `title`."""
    _check_layout(orientation, columns)
    if isinstance(value, (str, bytes)):
        raise ValueError("The value of a check group is a list of choice values")
    return _element("check_group", id, title=title, choices=list(choices),
                    value=list(value) if value is not None else None,
                    orientation=orientation, columns=columns, all_none=all_none, **props)


def choice(value: ChoiceValue, label: Optional[str] = None, *, icon: Optional[IconRef] = None,
           tooltip: Optional[str] = None, enabled: Optional[bool] = None) -> Choice:
    """A choice object for `choices` lists. `icon`, `tooltip` and `enabled` need the host
    feature choices; on an older host update() sends {value, label}, or the plain value."""
    # NaN and infinities are no JSON numbers: the host would refuse the whole frame.
    if isinstance(value, bool) or not (isinstance(value, str) or _is_number(value, False)):
        raise ValueError("A choice value is a string or a finite number")
    entry: Choice = {"value": value}
    if label is not None:
        entry["label"] = label
    if icon is not None:
        entry["icon"] = icon
    if tooltip is not None:
        entry["tooltip"] = tooltip
    if enabled is not None:
        entry["enabled"] = enabled
    return entry


def choice_separator() -> ChoiceSeparator:
    """A line between choices (left out on a host without the choices feature)."""
    return {"type": "separator"}


def choice_section(label: str) -> ChoiceSection:
    """A section title between choices (left out on a host without the choices feature)."""
    if not label:
        raise ValueError("A choice section needs a label")
    return {"type": "section", "label": label}


def completion_item(value: str, label: Optional[str] = None, *,
                    icon: Optional[IconRef] = None) -> CompletionItem:
    """One entry of a field/complete answer: the text to insert, and an optional label and
    icon for the completion list."""
    if not isinstance(value, str):
        raise ValueError("A completion value is a string")
    item: CompletionItem = {"value": value}
    if label is not None:
        item["label"] = label
    if icon is not None:
        item["icon"] = icon
    return item


def progress(id: str, value: Optional[float] = None, busy: Optional[bool] = None,
             text: Optional[str] = None, **props: Any) -> JsonObject:
    return _element("progress", id, value=value, busy=busy, text=text, **props)


def _check_one_of(name: str, value: Optional[str], allowed: Sequence[str]) -> None:
    if value is not None and value not in allowed:
        raise ValueError(f"{name} is one of {', '.join(allowed)}, not {value!r}")


def cell(text: Any, *, icon: Optional[IconRef] = None, tooltip: Optional[str] = None,
         tooltip_key: Optional[str] = None, tone: Optional[str] = None,
         bold: Optional[bool] = None, progress: Optional[float] = None,
         sort: Optional[SortValue] = None) -> TableCell:
    """A cell object for table_row() (host feature rich_tables): `text` (other values are
    converted with str()), an `icon`, a plain `tooltip` (wins over the row's), a `tooltip_key`
    for the advanced tooltip cache, a `tone` (CELL_TONES; wins over the row's), `bold`, a
    `progress` bar from 0 to 1 behind the text and a `sort` value (number or text) used instead
    of the text. On an older host update() and patch() send the text alone."""
    _check_one_of("tone", tone, CELL_TONES)
    if progress is not None and (not _is_number(progress, False) or not 0 <= progress <= 1):
        raise ValueError("progress is a number from 0 to 1")
    if sort is not None and not (_is_number(sort, False) or isinstance(sort, str)):
        raise ValueError("sort is a number or a string")
    result: TableCell = {"text": text if isinstance(text, str) else str(text)}
    if icon is not None:
        result["icon"] = icon
    if tooltip is not None:
        result["tooltip"] = tooltip
    if tooltip_key is not None:
        result["tooltip_key"] = tooltip_key
    if tone is not None:
        result["tone"] = tone
    if bold is not None:
        result["bold"] = bold
    if progress is not None:
        result["progress"] = progress
    if sort is not None:
        result["sort"] = sort
    return result


def table_column(id: str, title: Optional[str] = None, width: Optional[int] = None,
                 editable: Optional[bool] = None, tooltip: Optional[str] = None,
                 tooltip_provider: Optional[bool] = None,
                 tooltip_key: Optional[str] = None, *, align: Optional[str] = None,
                 hidden: Optional[bool] = None, sortable: Optional[bool] = None,
                 sort_as: Optional[str] = None, min_width: Optional[int] = None,
                 check: Optional[bool] = None, icon: Optional[IconRef] = None) -> JsonObject:
    """A table column (TableColumn). `tooltip` is the header tooltip; `tooltip_provider` asks
    the plugin for the tooltips of this column's cells (host feature tooltips).

    Keys of the host feature rich_tables (update() and patch() remove them on an older host):
    `align` (left, center, right), `hidden` (hidden at first; the header menu shows it),
    `sortable` (False: this column of a sortable table does not sort), `sort_as` (text,
    number, natural), `min_width` (pixels), `check` (a check box bound to the row's `checked`,
    next to the cell text) and a header `icon`."""
    _check_one_of("align", align, COLUMN_ALIGNMENTS)
    _check_one_of("sort_as", sort_as, SORT_AS)
    if min_width is not None and (not isinstance(min_width, int) or isinstance(min_width, bool)
                                  or min_width < 0):
        raise ValueError("min_width is a whole number of pixels, at least 0")
    column_object: JsonObject = {"id": id, "title": title if title is not None else id}
    for key, value in (("width", width), ("editable", editable), ("tooltip", tooltip),
                       ("tooltip_provider", tooltip_provider), ("tooltip_key", tooltip_key),
                       ("align", align), ("hidden", hidden), ("sortable", sortable),
                       ("sort_as", sort_as), ("min_width", min_width), ("check", check),
                       ("icon", icon)):
        if value is not None:
            column_object[key] = value
    return column_object


def _row_cell(value: Any) -> Any:
    """A cell object stays a (copied) dict; any other value becomes its str()."""
    return dict(value) if isinstance(value, collections.abc.Mapping) else str(value)


def table_row(id: str, cells: Sequence[Any], tone: Optional[str] = None,
              tooltip: Optional[str] = None, editors: Optional[JsonObject] = None,
              tooltip_provider: Optional[bool] = None,
              tooltip_key: Optional[str] = None, *, icon: Optional[IconRef] = None,
              checked: Optional[bool] = None,
              drag: Union[bool, Mapping[str, Any], None] = None) -> JsonObject:
    """A table row (TableRow). Cells are strings (other values are converted with str()) or
    cell() objects. `tooltip_key` (at most 128 characters) is part of the host's tooltip cache
    key: change it when the row's advanced tooltips change.

    Keys of the host feature rich_tables (update() and patch() remove them, and send cell
    objects as their text, on an older host): `icon` (in the first column), `checked` (shown
    in every check column; a row without it is unchecked) and `drag` (accepted; used from
    E17g)."""
    row_object: JsonObject = {"id": id, "cells": [_row_cell(value) for value in cells]}
    for key, value in (("tone", tone), ("tooltip", tooltip), ("editors", editors),
                       ("tooltip_provider", tooltip_provider), ("tooltip_key", tooltip_key),
                       ("icon", icon), ("checked", checked),
                       ("drag", dict(drag) if isinstance(drag, collections.abc.Mapping) else drag)):
        if value is not None:
            row_object[key] = value
    return row_object


def editor(kind: str, choices: Optional[Sequence[Any]] = None, min: Optional[float] = None,
           max: Optional[float] = None, max_length: Optional[int] = None,
           placeholder: Optional[str] = None, value: Any = None, *,
           searchable: Optional[bool] = None, indicator: Optional[str] = None) -> JsonObject:
    """A table cell editor: kind text, number, choice, multichoice or boolean (host feature view_edit).

    Pass it in table_row(editors={column_id: editor(...)}). value is the optional starting value
    (a list of checked values for multichoice). With the host feature choices, `searchable`
    (choice, multichoice) filters the open list as the user types and `indicator="radio"`
    (choice) draws radio marks; update() and patch() remove both on an older host."""
    if searchable and kind not in CHOICE_STYLES:
        raise ValueError("searchable is for choice and multichoice editors")
    if indicator is not None and (indicator != "radio" or kind != "choice"):
        raise ValueError('indicator is "radio", on a choice editor')
    spec: JsonObject = {"kind": kind}
    for key, item in (("choices", list(choices) if choices is not None else None), ("min", min),
                      ("max", max), ("max_length", max_length), ("placeholder", placeholder),
                      ("value", value), ("searchable", searchable or None),
                      ("indicator", indicator)):
        if item is not None:
            spec[key] = item
    return spec


def table(id: str, columns: Sequence[JsonObject], rows: Optional[Sequence[JsonObject]] = None,
          sortable: Optional[bool] = None, filter_box: Optional[bool] = None,
          selection: Optional[str] = None, *, header: Optional[bool] = None,
          toggle_selection: Optional[bool] = None, context_menu: Optional[str] = None,
          copy: Optional[bool] = None, export: Optional[bool] = None,
          reorderable: Optional[bool] = None, keep_columns: Optional[bool] = None,
          **props: Any) -> JsonObject:
    """A table (TableElement). `selection` is none, single or multi.

    Keys of the host feature rich_tables (update() and patch() remove them on an older host):
    `header` (False hides the header: a list), `toggle_selection` (with selection "multi":
    every click adds or removes a row), `copy` (default True: Copy and Copy All), `export`
    (Export CSV...), `reorderable` (rows can be dragged; the plugin gets `moved` and confirms
    with a patch) and `keep_columns` (default True: the host keeps widths, order, hidden
    columns and the sort). `context_menu` (host feature element_menus) names a plugin.json
    menu with location "element" whose entries follow the host's Copy entries."""
    if toggle_selection and selection != "multi":
        raise ValueError('toggle_selection needs selection "multi"')
    if context_menu is not None and (not isinstance(context_menu, str) or not context_menu
                                     or "/" in context_menu):
        raise ValueError(f"context_menu is the id of a menu with location element: "
                         f"{context_menu!r}")
    return _element("table", id, columns=list(columns),
                    rows=list(rows) if rows is not None else None, sortable=sortable,
                    filter_box=filter_box, selection=selection, header=header,
                    toggle_selection=toggle_selection, context_menu=context_menu, copy=copy,
                    export=export, reorderable=reorderable, keep_columns=keep_columns, **props)


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


def select_rows(table_id: str, row_ids: Optional[Sequence[str]] = None, *,
                current: Optional[str] = None, scroll_to: Optional[str] = None) -> JsonObject:
    """A selection patch entry (SelectionPatch, host feature rich_tables): `row_ids` replace
    the selection (an empty list clears it; unknown ids are ignored, at most 1000 used),
    `current` sets the row with the keyboard focus and `scroll_to` scrolls a row into view.
    The host sends no `selected` event for it. On an older host patch() leaves it out."""
    if isinstance(row_ids, str):
        raise ValueError("row_ids is a list of row ids")
    if row_ids is None and current is None and scroll_to is None:
        raise ValueError("A selection patch needs row_ids, current or scroll_to")
    entry: JsonObject = {"id": table_id}
    if row_ids is not None:
        entry["select"] = [str(row_id) for row_id in row_ids]
    if current is not None:
        entry["current"] = current
    if scroll_to is not None:
        entry["scroll_to"] = scroll_to
    return entry


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
    `title`), menubar, tab, pane, log, element or toolbar (MENU_LOCATIONS). An `element` menu
    is the context menu of the tables that name it in `context_menu` (host feature
    element_menus); its entries get an ElementMenuContext."""
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


def _completion_entry(item: object) -> JsonObject:
    if isinstance(item, str):
        return {"value": item}
    if isinstance(item, collections.abc.Mapping) and isinstance(item.get("value"), str):
        return dict(item)
    raise TypeError("A completion item is a string or a dict with a string value")


def _complete_answer(answer: object) -> Optional[JsonObject]:
    """The field/complete result of a @plugin.complete handler's return value: at most
    MAX_COMPLETION_ITEMS items, each {value, label?, icon?}."""
    if answer is None:
        return None
    result: JsonObject
    if isinstance(answer, collections.abc.Mapping):
        result = dict(answer)
    elif isinstance(answer, collections.abc.Sequence) and not isinstance(answer, (str, bytes)):
        result = {"items": answer}
    else:
        raise TypeError("A field/complete handler returns a list of items, a dict with items "
                        "or None")
    items = result.get("items")
    if not isinstance(items, collections.abc.Sequence) or isinstance(items, (str, bytes)):
        raise TypeError("A field/complete answer needs a list of items")
    result["items"] = [_completion_entry(item) for item in list(items)[:MAX_COMPLETION_ITEMS]]
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

    # An element menu's context (ElementMenuContext); None or empty in other locations.
    @property
    def view_id(self) -> Optional[str]:
        """The view of the element whose context menu opened."""
        return _optional_text(self.context.get("view_id"))

    @property
    def element_id(self) -> Optional[str]:
        """The element (a table) whose context menu opened."""
        return _optional_text(self.context.get("element_id"))

    @property
    def row_id(self) -> Optional[str]:
        """The clicked row; None when the click was not on a row."""
        return _optional_text(self.context.get("row_id"))

    @property
    def column_id(self) -> Optional[str]:
        """The clicked column; None when the click was not on a cell."""
        return _optional_text(self.context.get("column_id"))

    @property
    def selected_ids(self) -> List[str]:
        """The selected row ids (at most 1000); [] without a selection."""
        ids = self.context.get("selected_ids")
        return [item for item in ids if isinstance(item, str)] if isinstance(ids, list) else []

    @property
    def selected_count(self) -> int:
        """The number of selected rows: `selected_count`, else the length of selected_ids."""
        count = self.context.get("selected_count")
        if isinstance(count, int) and not isinstance(count, bool) and count >= 0:
            return count
        return len(self.selected_ids)


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


class CompleteQuery(Query):
    """A field/complete query: typing paused in a field with `completion` (default timeout
    500 ms). `text` is the field's text and `cursor` the cursor position in it."""

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
    def text(self) -> str:
        return _optional_text(self.params.get("text")) or ""

    @property
    def cursor(self) -> int:
        """The cursor position, within 0..len(text); the end of the text when not given."""
        cursor = self.params.get("cursor")
        if isinstance(cursor, bool) or not isinstance(cursor, int):
            return len(self.text)
        return max(0, min(len(self.text), cursor))


class UiCommand(_MenuContext):
    """A ui/command: the user picked an `event` menu entry (origin "menu") or pressed its
    shortcut ("shortcut"); toolbars and status items come later ("toolbar", "status"). An
    entry of an element menu (location "element") also has view_id, element_id, row_id,
    column_id, selected_ids and selected_count."""

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
CompleteHandler = Callable[[CompleteQuery], CompleteAnswer]
_QUERY_CLASSES: Dict[str, Type[Query]] = {
    TOOLTIP_RESOLVE: TooltipQuery, MENU_POPULATE: MenuQuery, FIELD_COMPLETE: CompleteQuery}


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
    """What update() and patch() change for the host: keys removed from every element, table
    column and row, and whether the host has each E17c and E17d feature."""
    element: FrozenSet[str]
    column: FrozenSet[str]
    row: FrozenSet[str]
    choices: bool = True
    toggles: bool = True
    field_kinds: bool = True
    validation: bool = True
    rich_tables: bool = True
    element_menus: bool = True

    def table_keys(self) -> FrozenSet[str]:
        """The table keys the host does not know."""
        keys: FrozenSet[str] = frozenset()
        if not self.rich_tables:
            keys |= _RICH_TABLE_KEYS
        if not self.element_menus:
            keys |= _ELEMENT_MENU_KEYS
        return keys


# A patch entry that update rules leave out entirely (a selection patch on an older host).
_DROPPED: Any = object()


# Element id -> (type, field kind) of a view's last update(), for patches.
_ElementKinds = Dict[str, Tuple[str, Optional[str]]]


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


def _compatible_choices(choices: Any) -> Any:
    """A choices list for a host without the choices feature: no separators or sections, and
    choice objects as {value, label} (or the plain value)."""
    if not isinstance(choices, list):
        return choices
    converted: List[Any] = []
    for item in choices:
        if isinstance(item, dict) and item.get("type") in _CHOICE_MARKERS:
            continue
        entry = _without(item, _CHOICE_OBJECT_KEYS)
        converted.append(entry["value"] if isinstance(entry, dict) and set(entry) == {"value"}
                         else entry)
    unchanged = len(converted) == len(choices) and all(
        new is old for new, old in zip(converted, choices))
    return choices if unchanged else converted


def _with_compatible_choices(props: JsonObject) -> JsonObject:
    if "choices" not in props:
        return props
    choices = _compatible_choices(props["choices"])
    return props if choices is props["choices"] else {**props, "choices": choices}


def _toggle_as_field(props: JsonObject, element_type: str, whole: bool) -> JsonObject:
    """A checkbox, radio_group or check_group (or the `set` properties of its patch) as the
    field a host without toggles shows: boolean, choice or multichoice, captioned by the
    element's text or title."""
    kind, style, caption = _TOGGLE_FIELDS[element_type]
    result: JsonObject = {}
    if whole:
        result["type"] = "field"
        if "id" in props:
            result["id"] = props["id"]
        result["kind"] = kind
    if caption in props:
        result["label"] = props[caption]
    if whole and style is not None:
        result["style"] = style  # removed again on a host without choices
    for key, value in props.items():
        if key in ("type", "id", "tone", caption) or key in _TOGGLE_KEYS:
            continue
        # A partly checked box (None) shows as unchecked.
        result[key] = (value is True) if key == "value" and kind == "boolean" else value
    return result


def _compatible_field(props: JsonObject, kind: Optional[str], rules: _StripRules) -> JsonObject:
    """A field (or the `set` properties of its patch) without the E17c keys the host lacks,
    with a new kind replaced by its FIELD_KIND_FALLBACKS kind."""
    current = props.get("kind", kind)
    remove: FrozenSet[str] = frozenset()
    if not rules.choices:
        remove |= _CHOICE_FIELD_KEYS
    if not rules.field_kinds:
        remove |= _FIELD_KIND_KEYS
    if not rules.validation:
        # min and max of integer and slider fields belong to field_kinds.
        bounded = rules.field_kinds and current in _BOUNDED_KINDS
        remove |= _VALIDATION_KEYS - _BOUND_KEYS if bounded else _VALIDATION_KEYS
    result: JsonObject = _without(props, remove)
    if not rules.field_kinds and result.get("kind") in FIELD_KIND_FALLBACKS:
        result = {**result, "kind": FIELD_KIND_FALLBACKS[result["kind"]]}
    return result if rules.choices else _with_compatible_choices(result)


def _compatible_unknown(props: JsonObject, rules: _StripRules) -> JsonObject:
    """The `set` properties of a patch of an element that no update() showed: only the keys no
    other element type uses are removed."""
    remove: FrozenSet[str] = frozenset()
    if not rules.choices:
        remove |= _CHOICE_FIELD_KEYS - {"columns"}  # also table columns
    if not rules.field_kinds:
        remove |= _FIELD_KIND_KEYS - {"rows"}  # also table rows
    if not rules.validation:
        remove |= _VALIDATION_KEYS - _BOUND_KEYS  # also integer and slider bounds
    if not rules.toggles:
        remove |= _TOGGLE_KEYS
    remove |= rules.table_keys()  # no other element type has them
    result: JsonObject = _without(props, remove)
    return result if rules.choices else _with_compatible_choices(result)


def _cell_text(value: Any) -> Any:
    """A cell for a host without rich_tables: a cell object becomes its text."""
    if not isinstance(value, dict):
        return value
    text = value.get("text")
    if isinstance(text, str):
        return text
    return "" if text is None else json.dumps(text, ensure_ascii=False)


def _compatible_editors(editors: Any) -> Any:
    """Cell editors for a host without choices: no searchable or indicator keys, and choices
    an older host knows."""
    if not isinstance(editors, dict):
        return editors
    converted = {column_id: _with_compatible_choices(_without(spec, _EDITOR_CHOICE_KEYS))
                 if isinstance(spec, dict) else spec
                 for column_id, spec in editors.items()}
    if all(converted[column_id] is editors[column_id] for column_id in editors):
        return editors
    return converted


def _compatible_row(row: Any, rules: _StripRules) -> Any:
    result = _without(row, rules.row)
    if not isinstance(result, dict):
        return result
    changes: JsonObject = {}
    if not rules.rich_tables and "cells" in result:
        cells = _map_list(result["cells"], _cell_text)
        if cells is not result["cells"]:
            changes["cells"] = cells
    if not rules.choices and "editors" in result:
        editors = _compatible_editors(result["editors"])
        if editors is not result["editors"]:
            changes["editors"] = editors
    return {**result, **changes} if changes else result


def _compatible_props(props: Any, rules: _StripRules, element_type: Optional[str],
                      kind: Optional[str], whole: bool) -> Any:
    """An element (whole=True) or the `set` properties of a patch of an element of
    element_type (None: not known) without what the host does not know, copying only what
    changes."""
    if not isinstance(props, dict):
        return props
    result = _without(props, rules.element)
    if element_type in _TOGGLE_FIELDS and not rules.toggles:
        result = _toggle_as_field(result, element_type, whole)
        element_type, kind = "field", _TOGGLE_FIELDS[element_type][0]
    if element_type == "field":
        result = _compatible_field(result, kind, rules)
    elif element_type in _TOGGLE_FIELDS and not rules.choices:
        result = _with_compatible_choices(result)
    elif element_type == "button" and not rules.validation:
        result = _without(result, frozenset({"requires_valid"}))
    elif element_type == "table":
        result = _without(result, rules.table_keys())
    elif element_type is None:
        result = _compatible_unknown(result, rules)
    changes: JsonObject = {}
    for key, convert in (("children", lambda child: _compatible_element(child, rules)),
                         ("columns", lambda item: _without(item, rules.column)),
                         ("rows", lambda item: _compatible_row(item, rules))):
        if key in result:
            value = _map_list(result[key], convert)
            if value is not result[key]:
                changes[key] = value
    if changes:
        result = {**result, **changes}
    return result


def _compatible_element(element: Any, rules: _StripRules) -> Any:
    """Return the element without the properties the host does not know (and an E17c element
    as the field an older host knows), copying only what changes."""
    if not isinstance(element, dict):
        return element
    element_type = element.get("type")
    return _compatible_props(element, rules, element_type if isinstance(element_type, str) else "",
                             None, True)


def _compatible_patch_entry(entry: Any, rules: _StripRules, kinds: _ElementKinds) -> Any:
    """A patch entry the host knows, or _DROPPED for a selection patch it does not know."""
    if not isinstance(entry, dict):
        return entry
    if not rules.rich_tables and not _SELECTION_PATCH_KEYS.isdisjoint(entry):
        entry = _without(entry, _SELECTION_PATCH_KEYS)
        if set(entry) <= {"id"}:
            return _DROPPED
    changes: JsonObject = {}
    if "set" in entry:
        identifier = entry.get("id")
        element_type, kind = (kinds.get(identifier, (None, None)) if isinstance(identifier, str)
                              else (None, None))
        properties = _compatible_props(entry["set"], rules, element_type, kind, False)
        if properties is not entry["set"]:
            changes["set"] = properties
    for operation in _ROW_OPERATIONS:
        if operation in entry:
            rows = _map_list(entry[operation], lambda item: _compatible_row(item, rules))
            if rows is not entry[operation]:
                changes[operation] = rows
    return {**entry, **changes} if changes else entry


def _element_kinds(root: Any) -> _ElementKinds:
    """Every element id of a view tree with its type and field kind; the first element with an
    id wins, as on the host."""
    found: _ElementKinds = {}
    pending: List[Any] = [root]
    while pending:
        element = pending.pop()
        if not isinstance(element, dict):
            continue
        identifier, element_type, kind = element.get("id"), element.get("type"), element.get("kind")
        if isinstance(identifier, str) and isinstance(element_type, str):
            found.setdefault(identifier, (element_type, kind if isinstance(kind, str) else None))
        children = element.get("children")
        if isinstance(children, list):
            pending.extend(reversed(children))
    return found


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
        self.complete_handlers: Dict[Tuple[str, Optional[str]], CompleteHandler] = {}
        self.queries: Dict[str, Query] = {}
        self.lanes: Dict[str, _QueryLane] = {}
        # The element types of each view's last update() on an older host, for patch(), by
        # (view id, window id); "" is an update sent to every window.
        self._view_elements: Dict[Tuple[str, str], _ElementKinds] = {}

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

    # Field completion (docs/plugin-sdk-advanced.md, section 8.2).
    def complete(self, view_id: str, element_id: Optional[str] = None
                 ) -> Callable[[CompleteHandler], CompleteHandler]:
        """Register handler(CompleteQuery) for the completion of one field with `completion`
        (a text field or an editable choice field), or of every other such field of the view
        when element_id is None (an element's own handler wins; a
        @plugin.query("field/complete") handler is the last fallback, its result unchanged).

        The handler runs on the field/complete lane (latest wins: a newer query replaces a
        waiting one; default timeout 500 ms) and returns a list of items (strings or
        completion_item()), a dict {"items": [...]} or None (no completions). The SDK keeps
        the first 100 items; any other return value answers error -32000. plugin.json must
        list field/complete (and query/cancel) in `handles`."""
        if not view_id or element_id == "":
            raise ValueError("A completion handler needs a view id (and a non-empty element id)")

        def register(handler: CompleteHandler) -> CompleteHandler:
            self.complete_handlers[(view_id, element_id)] = handler
            return handler
        return register

    def handled_methods(self) -> List[str]:
        """The host-to-plugin methods the registered handlers answer (`features`)."""
        methods = set(self.query_handlers)
        if self.tooltip_handlers:
            methods.add(TOOLTIP_RESOLVE)
        if self.menu_populate_handlers:
            methods.add(MENU_POPULATE)
        if self.complete_handlers:
            methods.add(FIELD_COMPLETE)
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
        choices, toggles, field_kinds, validation, rich_tables, element_menus = (
            self.supports(name) for name in ("choices", "toggles", "field_kinds", "validation",
                                             "rich_tables", "element_menus"))
        if not rich_tables:
            column_keys |= _RICH_COLUMN_KEYS
            row_keys |= _RICH_ROW_KEYS
        if not (element or column_keys or row_keys) and choices and toggles and field_kinds \
                and validation and rich_tables and element_menus:
            return None
        return _StripRules(element, column_keys, row_keys, choices, toggles, field_kinds,
                           validation, rich_tables, element_menus)

    def update(self, view_id: str, root: JsonObject, window_id: Optional[str] = None) -> None:
        """Replace a view's element tree (in every window without window_id). On an older host
        the tree is sent without what the host does not know (see _strip_rules), E17c elements
        as the fields it knows and table cell objects as their text; the caller's tree is not
        changed."""
        rules = self._strip_rules()
        if rules is not None:
            elements = _element_kinds(root)  # patch() converts by the element's type
            with self.lock:
                if not window_id:  # every window now shows this tree
                    for key in [key for key in self._view_elements if key[0] == view_id]:
                        del self._view_elements[key]
                self._view_elements[(view_id, window_id or "")] = elements
        params: JsonObject = {
            "view_id": view_id, "root": root if rules is None else _compatible_element(root, rules)}
        if window_id:
            params["window_id"] = window_id
        self.notify("view/update", params)

    def _patch_elements(self, view_id: str, window_id: str) -> _ElementKinds:
        """The element types a patch() for this window converts by (the caller holds lock): the
        window's own tree, else the tree sent to every window; a patch for every window uses
        the types of all of the view's trees."""
        for key in ((view_id, window_id), (view_id, "")):
            if key in self._view_elements:
                return self._view_elements[key]
        merged: _ElementKinds = {}
        if not window_id:
            for (view, _window), kinds in self._view_elements.items():
                if view == view_id:
                    merged.update(kinds)
        return merged

    def patch(self, view_id: str, changes: Sequence[JsonObject],
              window_id: Optional[str] = None) -> None:
        """Apply set_props(), row operation and select_rows() entries to existing elements. On
        an older host they are converted like update() converts the elements of the view's
        last tree; a host without rich_tables gets no selection patch (and no frame when
        nothing else is left)."""
        rules = self._strip_rules()
        entries = list(changes)
        if rules is not None:
            with self.lock:
                elements = self._patch_elements(view_id, window_id or "")
            converted = [_compatible_patch_entry(entry, rules, elements) for entry in entries]
            entries = [entry for entry in converted if entry is not _DROPPED]
            if converted and not entries:
                return
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
                or (method == MENU_POPULATE and bool(self.menu_populate_handlers))
                or (method == FIELD_COMPLETE and bool(self.complete_handlers)))

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
        if isinstance(query, CompleteQuery):
            complete = (self.complete_handlers.get((query.view_id, query.element_id))
                        or self.complete_handlers.get((query.view_id, None)))
            if complete is not None:
                return _complete_answer(complete(query))
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
