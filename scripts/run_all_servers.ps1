param(
    [string]$Configuration = 'Debug',
    [switch]$NoBuild
)

$ErrorActionPreference = 'Stop'

# project root = parent of scripts/
$root = Split-Path -Parent $PSScriptRoot
$configRoot = Join-Path $root 'config'
$outRoot = Join-Path $root 'out\servers'
$wrapperRoot = Join-Path $outRoot 'wrappers'

New-Item -ItemType Directory -Force -Path $outRoot | Out-Null
New-Item -ItemType Directory -Force -Path $wrapperRoot | Out-Null

$serversJson = Join-Path $configRoot 'servers.json'
if (-not (Test-Path $serversJson)) {
    throw "Missing servers.json: $serversJson"
}

$servers = Get-Content $serversJson -Raw | ConvertFrom-Json

$binRoot = Join-Path $root (Join-Path 'Bin' $Configuration)
$psExe = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'

function Assert-FileExists {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Label
    )

    if (-not (Test-Path $Path)) {
        throw "Missing ${Label}: ${Path}"
    }
}

Assert-FileExists -Path (Join-Path $configRoot 'account_server.ini') -Label 'account_server.ini'
Assert-FileExists -Path (Join-Path $configRoot 'world_server.ini')   -Label 'world_server.ini'
Assert-FileExists -Path (Join-Path $configRoot 'login_server.ini')   -Label 'login_server.ini'

if (-not $NoBuild) {
    $cmakeExe = 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (-not (Test-Path $cmakeExe)) {
        throw "Missing cmake.exe: $cmakeExe"
    }

    & $cmakeExe `
        --build (Join-Path $root 'build_vs') `
        --config $Configuration `
        --target account_server world_server zone_server login_server `
        -- /m:1 /p:UseMultiToolTask=false /p:CL_MPCount=1

    if ($LASTEXITCODE -ne 0) {
        throw "Build failed with exit code $LASTEXITCODE"
    }
}

function New-WrapperScript {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$ExePath,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][hashtable]$EnvMap
    )

    Assert-FileExists -Path $ExePath -Label "$Name exe"

    $logPath = Join-Path $outRoot ($Name + '.log')
    $wrapperPath = Join-Path $wrapperRoot ($Name + '.ps1')

    $envLines = @()
    $escapedServersJson = $serversJson -replace "'", "''"
    $envLines += "`$env:DC_SERVERS_CONFIG_PATH = '$escapedServersJson'"

    foreach ($key in $EnvMap.Keys) {
        $val = [string]$EnvMap[$key]
        $escapedVal = $val -replace "'", "''"
        $envLines += "`$env:${key} = '$escapedVal'"
    }

    $escapedWorkingDirectory = $WorkingDirectory -replace "'", "''"
    $escapedExePath = $ExePath -replace "'", "''"
    $escapedLogPath = $logPath -replace "'", "''"

    $content = @"
`$ErrorActionPreference = 'Continue'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
`$OutputEncoding = [System.Text.UTF8Encoding]::new()

$($envLines -join "`r`n")

Set-Location '$escapedWorkingDirectory'

Write-Host '[$Name] starting...'
Write-Host '[$Name] exe: $escapedExePath'
Write-Host '[$Name] cwd: $escapedWorkingDirectory'
Write-Host '[$Name] log: $escapedLogPath'

& '$escapedExePath' 2>&1 |
    Tee-Object -FilePath '$escapedLogPath' -Append |
    Out-Host

Write-Host '[$Name] process exited with code' `$LASTEXITCODE
Write-Host '[$Name] press Enter to close...'
[void](Read-Host)
"@

    Set-Content -Path $wrapperPath -Value $content -Encoding UTF8
    return $wrapperPath
}

function Start-ServerWindow {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$ExePath,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][hashtable]$EnvMap
    )

    $wrapper = New-WrapperScript -Name $Name -ExePath $ExePath -WorkingDirectory $WorkingDirectory -EnvMap $EnvMap

    Start-Process `
        -FilePath $psExe `
        -WorkingDirectory $WorkingDirectory `
        -ArgumentList @(
            '-NoProfile',
            '-ExecutionPolicy', 'Bypass',
            '-File', $wrapper
        ) `
        | Out-Null
}

# account
Start-ServerWindow `
    -Name 'account_server' `
    -ExePath (Join-Path $binRoot 'account_server_d.exe') `
    -WorkingDirectory $root `
    -EnvMap @{
        DC_ACCOUNT_CONFIG_PATH = (Join-Path $configRoot 'account_server.ini')
    }

Start-Sleep -Milliseconds 700

# world
Start-ServerWindow `
    -Name 'world_server' `
    -ExePath (Join-Path $binRoot 'world_server_d.exe') `
    -WorkingDirectory $root `
    -EnvMap @{
        DC_WORLD_CONFIG_PATH = (Join-Path $configRoot 'world_server.ini')
    }

Start-Sleep -Milliseconds 700

# zone servers
foreach ($zone in $servers.zone_servers) {
    foreach ($channel in $zone.channels) {
        $name = "zone_server_$($zone.id)_ch$channel"
        $ini = Join-Path $configRoot ($name + '.ini')
        Assert-FileExists -Path $ini -Label "$name ini"

        Start-ServerWindow `
            -Name $name `
            -ExePath (Join-Path $binRoot 'zone_server_d.exe') `
            -WorkingDirectory $root `
            -EnvMap @{
                DC_ZONE_CONFIG_PATH = $ini
            }

        Start-Sleep -Milliseconds 400
    }
}

Start-Sleep -Milliseconds 700

# login
Start-ServerWindow `
    -Name 'login_server' `
    -ExePath (Join-Path $binRoot 'login_server_d.exe') `
    -WorkingDirectory $root `
    -EnvMap @{
        DC_LOGIN_CONFIG_PATH = (Join-Path $configRoot 'login_server.ini')
    }

Write-Host ''
Write-Host 'All servers launched.'
Write-Host "Logs folder : $outRoot"
Write-Host "Wrapper dir : $wrapperRoot"
Write-Host ''
Write-Host 'Each server opens in its own PowerShell console window.'
Write-Host 'Console output is also saved to out\servers\*.log'