$ErrorActionPreference = 'Stop';

$packageArgs = @{
  packageName    = 'cilogg'
  unzipLocation  = "$(Split-Path -parent $MyInvocation.MyCommand.Definition)"
  fileType       = 'exe'
  url64bit       = 'https://github.com/dm17ryk/klogg/releases/download/v22.06/cilogg-22.06.0.1289-Win-x64-Qt5-setup.exe'
  checksum64     = '2a287c26fdbda57afb1d30c2d8fdd7db23960fe15957ef1ebb113b339a6c7d62'
  checksumType64 = 'sha256'
  url            = 'https://github.com/dm17ryk/klogg/releases/download/v22.06/cilogg-22.06.0.1289-Win-x86-Qt5-setup.exe'
  checksum       = '4baf7299a5efca6d809191271e1b3518147faef72fd56144270086b2f24f939c'
  checksumType   = 'sha256'
  softwareName   = 'cilogg'
  silentArgs     = '/S'
  validExitCodes = @(0)
}

Install-ChocolateyPackage @packageArgs