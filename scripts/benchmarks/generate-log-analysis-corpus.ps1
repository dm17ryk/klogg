param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [ValidateRange(1, 10000000)]
    [int]$Lines = 100000,
    [ValidateRange(1, 4096)]
    [int]$LongLineKiB = 256,
    [ValidateRange(1, 1024)]
    [int]$FileCount = 64
)

$ErrorActionPreference = 'Stop'
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $outputPath) {
    throw "Choose a new corpus directory; the destination already exists: $outputPath"
}
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
$utf16 = [Text.UnicodeEncoding]::new($false, $true)
$invariant = [Globalization.CultureInfo]::InvariantCulture
$manifestFiles = [Collections.Generic.List[object]]::new()

function Write-CorpusFile {
    param([string]$Name, [Text.Encoding]$Encoding, [scriptblock]$Content)

    $path = Join-Path $outputPath $Name
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path)) | Out-Null
    $stream = [IO.FileStream]::new($path, [IO.FileMode]::CreateNew)
    $writer = [IO.StreamWriter]::new($stream, $Encoding)
    $writer.NewLine = "`n"
    try {
        & $Content $writer
    }
    finally {
        $writer.Dispose()
    }
    $file = Get-Item -LiteralPath $path
    $manifestFiles.Add([ordered]@{
        path = $Name.Replace('\', '/')
        bytes = $file.Length
        sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    })
    Write-Verbose "Generated $Name ($($file.Length) bytes)"
}

$recordContent = {
    param($writer)
    for ($i = 0; $i -lt $Lines; ++$i) {
        $level = if (($i % 10) -eq 0) { 'ERROR' } else { 'INFO' }
        $writer.WriteLine([string]::Format($invariant,
            '{0:D10} {1} device=CP heap={2} rssi={3} state={4}',
            [object[]]@($i, $level, (65536 - ($i % 4096)), (-40 - ($i % 40)), ($i % 7))))
    }
}
Write-CorpusFile 'ascii.log' ([Text.Encoding]::ASCII) $recordContent
Write-CorpusFile 'utf16le.log' $utf16 $recordContent
Write-CorpusFile 'unicode.log' $utf8 {
    param($writer)
    $unicodeText = [char]0x05E9 + [string][char]0x05DC + [char]0x05D5 + [char]0x05DD +
        ' ' + [char]::ConvertFromUtf32(0x1F600)
    for ($i = 0; $i -lt $Lines; ++$i) {
        $writer.WriteLine(('{0:D10} INFO message={1}' -f $i, $unicodeText))
    }
}
Write-CorpusFile 'long-lines.log' $utf8 {
    param($writer)
    $payload = ('0123456789abcdef' * ($LongLineKiB * 64))
    for ($i = 0; $i -lt 8; ++$i) {
        $writer.WriteLine("LONG $i $payload")
    }
}
Write-CorpusFile 'structured.jsonl' $utf8 {
    param($writer)
    for ($i = 0; $i -lt $Lines; ++$i) {
        $writer.WriteLine(('{{"seq":{0},"timestamp":{1},"device":"CP","heap":{2}}}' -f
            $i.ToString($invariant), (1700000000000L + $i).ToString($invariant),
            (65536 - ($i % 4096)).ToString($invariant)))
    }
}
Write-CorpusFile 'partial-final-line.log' $utf8 {
    param($writer)
    $writer.WriteLine('INFO complete')
    $writer.Write('ERROR incom')
}
for ($fileIndex = 0; $fileIndex -lt $FileCount; ++$fileIndex) {
    Write-CorpusFile ('folder/{0:D4}.log' -f $fileIndex) $utf8 {
        param($writer)
        for ($i = 0; $i -lt 32; ++$i) {
            $writer.WriteLine("file=$fileIndex seq=$i MATCH")
        }
    }
}
$manifest = [ordered]@{
    schemaVersion = 1
    generator = 'generate-log-analysis-corpus.ps1'
    lines = $Lines
    errorLines = [long][Math]::Ceiling($Lines / 10.0)
    folderMatches = $FileCount * 32
    longLineKiB = $LongLineKiB
    files = $manifestFiles.ToArray()
}
$manifestPath = Join-Path $outputPath 'manifest.json'
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 6), $utf8)
Write-Output $manifestPath
