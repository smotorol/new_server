param(
    [string]$Configuration = 'Debug',
    [switch]$NoBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$configRoot = Join-Path $root 'config'
$outRoot = Join-Path $root 'out\servers'
New-Item -ItemType Directory -Force -Path $outRoot | Out-Null

$servers = Get-Content (Join-Path $configRoot 'servers.json') | ConvertFrom-Json
$binRoot = Join-Path $root (Join-Path 'Bin' $Configuration)
$ps = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'

if (-not $NoBuild) {
    & 'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build (Join-Path $root 'build_vs') --config $Configuration --target account_server world_server zone_server login_server -- /m:1 /p:UseMultiToolTask=false /p:CL_MPCount=1
}

function Start-ServerProcess {
    param(
        [string]$Name,
        [string]$ExePath,
        [hashtable]$EnvMap,
        [string]$WorkingDirectory
    )

    $stdout = Join-Path $outRoot ($Name + '.stdout.log')
    $stderr = Join-Path $outRoot ($Name + '.stderr.log')
    $envAssignments = @('$env:DC_SERVERS_CONFIG_PATH=''' + (Join-Path $configRoot 'servers.json') + '''')
    foreach ($key in $EnvMap.Keys) {
        $envAssignments += ('$env:' + $key + '=''' + $EnvMap[$key] + '''')
    }
    $command = ($envAssignments -join '; ') + '; & ''' + $ExePath + ''''
    Start-Process -FilePath $ps -ArgumentList '-NoProfile','-Command',$command -WorkingDirectory $WorkingDirectory -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru | Out-Null
}

Start-ServerProcess -Name 'account_server' -ExePath (Join-Path $binRoot 'account_server_d.exe') -EnvMap @{ DC_ACCOUNT_CONFIG_PATH = (Join-Path $configRoot 'account_server.ini') } -WorkingDirectory $root
Start-Sleep -Milliseconds 500
Start-ServerProcess -Name 'world_server' -ExePath (Join-Path $binRoot 'world_server_d.exe') -EnvMap @{ DC_WORLD_CONFIG_PATH = (Join-Path $configRoot 'world_server.ini') } -WorkingDirectory $root
Start-Sleep -Milliseconds 500

foreach ($zone in $servers.zone_servers) {
    foreach ($channel in $zone.channels) {
        $name = ('zone_server_{0}_ch{1}' -f $zone.id, $channel)
        $ini = Join-Path $configRoot ($name + '.ini')
        Start-ServerProcess -Name $name -ExePath (Join-Path $binRoot 'zone_server_d.exe') -EnvMap @{ DC_ZONE_CONFIG_PATH = $ini } -WorkingDirectory $root
        Start-Sleep -Milliseconds 250
    }
}

Start-Sleep -Milliseconds 500
Start-ServerProcess -Name 'login_server' -ExePath (Join-Path $binRoot 'login_server_d.exe') -EnvMap @{ DC_LOGIN_CONFIG_PATH = (Join-Path $configRoot 'login_server.ini') } -WorkingDirectory $root

Write-Host 'All servers launched.'
Write-Host ('Logs: ' + $outRoot)
