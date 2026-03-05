Please address the comments from this code review:

## Overall Comments
- In `StartupSplashScreen::updateFromState` you call `repaint()` followed by `QCoreApplication::processEvents(ExcludeUserInputEvents)`, which can cause re-entrant event processing and janky startup; consider using `update()` (letting the normal event loop drive painting) and avoiding explicit `processEvents()` unless there is a concrete, measured need for it.
- The `StartupProgress` API allows callbacks to be invoked from arbitrary threads, but its only current consumer is a GUI splash screen that must run on the main thread; it would be safer to either constrain `StartupProgress` usage to the main thread (e.g., with an assertion or documentation) or move the thread-hopping logic into `StartupProgress` itself so callers cannot accidentally update UI from a worker thread.

## Individual Comments

### Comment 1
<location path="src/ui/src/mainwindow.cpp" line_range="2619-2628" />
<code_context>
+-    HighlighterSetCollection::getSynced();
++    StartupProgress::advance( tr( "Loading highlighters" ),
++                              tr( "Restoring and compiling highlighter sets" ) );
++    auto& highlighterCollection = HighlighterSetCollection::getSynced();
++    const auto highlighterSets = highlighterCollection.highlighterSets();
++    for ( const auto& highlighterSet : highlighterSets ) {
</code_context>
<issue_to_address>
**issue (performance):** Highlighters and the active set appear to be compiled twice during startup.

In the first loop you compile every `highlighter` and each `highlighterSet`. Then you get `currentActiveSet()`, recompile its highlighters, and call `activeSet.compile()` again. Unless `currentActiveSet()` returns a distinct copy, this double compilation is redundant and may hurt startup time for large configs. Consider compiling the active set only once (either exclude it from the first loop, or skip the second pass and only treat non‑active sets separately if needed).
</issue_to_address>
