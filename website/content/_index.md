---
title: "About"
type: docs
---

## Faster log explorer

_CILogg_ is an open source multi-platform GUI application to search through all kinds of text log files using regular expressions. It has started as fork of [glogg project](https://glogg.bonnefon.org/) created by [Nicolas Bonnefon](https://github.com/nickbnf), and has evolved into a separate project with a lot of new features and improvements.

_CILogg_ is designed to:

 - be very fast
 - handle huge log files
 - provide a clear view of the matches even in heavily cluttered files.

Here is what _CILogg_ looks like:

<div id="gallery" style="display:none;">
<img src="/screenshots/mainwindow.png" alt="CILogg main window" />
<img src="/screenshots/dark.png" alt="CILogg dark theme" />
<img src="/screenshots/scratchpad.png" alt="CILogg scratchpad" />
<img src="/screenshots/highlighters.png" alt="CILogg highlighters configuration" />
</div>

## Features
{{< columns >}}
 _CILogg_ inherited a lot of features from _glogg_

 - Runs on Unix-like systems, Windows and Mac thanks to Qt5
 - Displays search results separately from original file
 - Supports Perl-compatible regular expressions
 - Colorizes the log and search results
 - Displays a context view of where in the log the lines of interest are
 - Is fast and reads the file directly from disk, without loading it into memory
 - Watches for file changes on disk and reloads it (kind of like tail)

<--->

_CILogg_ improves and brings more

 - Is optimized for modern CPUs with multiple cores and SIMD instructions
 - Has portable version (no need to install)
 - Understands a lot of text encodings and detects many of them automatically
 - Allows to perform search in a portion of huge log file
 - Supports multiple sets of text highlight rules with more sophisticated match options
 - Provides many small features to make life easier (tab renaming, favorite files list, archive extraction,
 scratchpad etc.)
 

{{< /columns >}}

## Downloads

Latest stable version:

[ ![GitHub Release](https://img.shields.io/github/v/release/dm17ryk/klogg?label=GitHub%20Release&style=for-the-badge)](https://github.com/dm17ryk/klogg/releases/latest)
[ ![Chocolatey](https://img.shields.io/chocolatey/v/cilogg?style=for-the-badge)](https://chocolatey.org/packages/cilogg)
[ ![Homebrew](https://img.shields.io/homebrew/cask/v/cilogg?style=for-the-badge)](https://formulae.brew.sh/cask/cilogg)


Latest development builds can be downloaded from releases on Github: 

{{< button href="https://github.com/dm17ryk/klogg/releases/tag/continuous-win" >}}Windows{{< /button >}}
{{< button href="https://github.com/dm17ryk/klogg/releases/tag/continuous-linux" >}}Linux{{< /button >}}
{{< button href="https://github.com/dm17ryk/klogg/releases/tag/continuous-osx" >}}Mac{{< /button >}}

