# SPDX-License-Identifier: Apache-2.0
# Run from an activated ESP-IDF v6.0.1 PowerShell terminal.
[CmdletBinding()]
param(
    [ValidateSet('Configure', 'Build', 'Flash', 'WiFi', 'TTS')]
    [string]$Action = 'Build',
    [string]$Port
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$espRoot = Join-Path $repoRoot 'esp32'
$buildRoot = Join-Path $espRoot 'build-muse-seeed-xiao-s3-147'
$tokenPath = Join-Path $repoRoot '.sdk-token'
$utf8 = [Text.UTF8Encoding]::new($false)

if ($Action -eq 'Configure') {
    $secret = Read-Host 'Your Muse SDK Token (input hidden)' -AsSecureString
    $handle = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secret)
    try { $token = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($handle) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($handle) }
    if ($token -notmatch '^mgst_[A-Za-z0-9._~-]+$') { throw 'Invalid SDK token format.' }
    [IO.File]::WriteAllText($tokenPath, $token + "`n", $utf8)
    $token = $null
    Write-Host 'SDK token saved locally; its value has not been printed.'
    return
}

if ($Action -in @('WiFi', 'TTS')) {
    $name = if ($Action -eq 'TTS') { 'tts' } else { 'wifi' }
    $configPath = Join-Path $repoRoot ('.' + $name + '-config.json')
    if (-not (Test-Path -LiteralPath $configPath)) {
        Copy-Item -LiteralPath (Join-Path $repoRoot ('.' + $name + '-config.example.json')) -Destination $configPath
        throw ('Fill the empty fields in ' + $configPath + ', save, then run again. Do not share its contents.')
    }
    if (-not $Port) {
        $Port = (& python (Join-Path $PSScriptRoot 'ports.py') xiao147).Trim()
        if ($LASTEXITCODE -ne 0) { throw 'Select the XIAO USB serial port explicitly with -Port.' }
    }
    & python (Join-Path $PSScriptRoot ('xiao147_' + $name + '.py')) --port $Port --config $configPath
    if ($LASTEXITCODE -ne 0) { throw 'Device configuration failed; no credentials were printed.' }
    return
}

if (-not $env:IDF_PATH) { throw 'Activate an ESP-IDF v6.0.1 PowerShell terminal first.' }
$idf = Join-Path $env:IDF_PATH 'tools/idf.py'
$version = & python $idf --version
if ($LASTEXITCODE -ne 0 -or $version -notmatch '^ESP-IDF v6\.0\.1(?:$|[- ])') {
    throw 'This profile requires ESP-IDF v6.0.1.'
}
if (-not (Test-Path -LiteralPath $tokenPath)) { throw 'Run -Action Configure to save your own SDK token first.' }
$token = [IO.File]::ReadAllText($tokenPath).Trim().TrimStart([char]0xfeff)
if ($token -notmatch '^mgst_[A-Za-z0-9._~-]+$') { throw 'Invalid local SDK token format.' }
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null
$defaultsPath = Join-Path $buildRoot 'sdkconfig.token'
[IO.File]::WriteAllText($defaultsPath, 'CONFIG_GADGET_SDK_TOKEN="' + $token + '"' + "`n", $utf8)
$configPath = Join-Path $buildRoot 'sdkconfig'
if (Test-Path -LiteralPath $configPath) {
    $config = [IO.File]::ReadAllText($configPath)
    $pattern = '(?m)^(?:CONFIG_GADGET_SDK_TOKEN=[^\r\n]*|# CONFIG_GADGET_SDK_TOKEN is not set)'
    $updated = [regex]::Replace($config, $pattern, 'CONFIG_GADGET_SDK_TOKEN="' + $token + '"')
    if ($updated -eq $config -and $config -notmatch '(?m)^CONFIG_GADGET_SDK_TOKEN=') {
        $updated += "`nCONFIG_GADGET_SDK_TOKEN=`"" + $token + "`"`n"
    }
    if ($updated -ne $config) { [IO.File]::WriteAllText($configPath, $updated, $utf8) }
}
$token = $null
$defaults = 'sdkconfig.defaults;devices/sdkconfig.muse;devices/sdkconfig.muse-seeed-xiao-s3-147;' + $defaultsPath
$idfArgs = @('--no-ccache', '-C', $espRoot, '-B', $buildRoot, '-DIDF_TARGET=esp32s3',
    "-DSDKCONFIG=$configPath", "-DSDKCONFIG_DEFAULTS=$defaults")
& python $idf @idfArgs build
if ($LASTEXITCODE -ne 0) { throw 'Firmware build failed.' }
if ($Action -eq 'Flash') {
    if (-not $Port) {
        $Port = (& python (Join-Path $PSScriptRoot 'ports.py') xiao147).Trim()
        if ($LASTEXITCODE -ne 0) { throw 'Select the XIAO USB serial port explicitly with -Port.' }
    }
    & python $idf @idfArgs -p $Port flash
    if ($LASTEXITCODE -ne 0) { throw 'Firmware flash failed.' }
}
