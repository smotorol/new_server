param(
    [string]$LegacyRoot = 'G:\Programing\Work\12sky1\12sky1',
    [string]$ZoneCsvRoot = 'G:\Programing\Work\new_server\data_src\zone_csv',
    [string]$TargetRoot = 'G:\Programing\Work\new_server\resources'
)

$ErrorActionPreference = 'Stop'
$mapsRoot = Join-Path $TargetRoot 'maps'
$zonesRoot = Join-Path $TargetRoot 'zones'
$runtimeRoot = Join-Path $TargetRoot 'runtime'
New-Item -ItemType Directory -Force -Path $mapsRoot, $zonesRoot, $runtimeRoot | Out-Null

$maps = Import-Csv (Join-Path $ZoneCsvRoot 'maps.csv') | Where-Object { [int]$_.map_id -ge 1 -and [int]$_.map_id -le 9 }
$portal = Import-Csv (Join-Path $ZoneCsvRoot 'portal_regions.csv')
$npcs = Import-Csv (Join-Path $ZoneCsvRoot 'summon_npc_regions.csv')
$monsters = Import-Csv (Join-Path $ZoneCsvRoot 'summon_monster_regions.csv')
$safe = Import-Csv (Join-Path $ZoneCsvRoot 'safe_regions.csv')
$special = Import-Csv (Join-Path $ZoneCsvRoot 'special_regions.csv')

foreach ($map in $maps) {
    $mapId = [int]$map.map_id
    $zoneId = [int]$map.zone_id
    $mapDir = Join-Path $mapsRoot ('map_{0:d3}' -f $mapId)
    $zoneDir = Join-Path $zonesRoot ('zone_{0:d3}' -f $zoneId)
    New-Item -ItemType Directory -Force -Path $mapDir, $zoneDir | Out-Null

    $wmName = 'Z{0:d3}.WM' -f $mapId
    $legacyWm = Join-Path $LegacyRoot ('data\' + $wmName)
    if (Test-Path $legacyWm) {
        Copy-Item $legacyWm (Join-Path $mapDir 'base.wm') -Force
    }

    @{ map_id = $mapId; zone_id = $zoneId; coordinate_unit_meters = 1 } | ConvertTo-Json | Set-Content (Join-Path $mapDir 'metadata.json')
    @{ zone_id = $zoneId; map_id = $mapId; zone_type = 'field'; default_channel_policy = 'least_loaded' } | ConvertTo-Json | Set-Content (Join-Path $zoneDir 'zone.json')
    @{ zone_id = $zoneId; maps = @($mapId) } | ConvertTo-Json | Set-Content (Join-Path $zoneDir 'map_bindings.json')

    ($portal | Where-Object { [int]$_.zone_id -eq $zoneId -and [int]$_.map_id -eq $mapId }) | Export-Csv (Join-Path $zoneDir 'portal.csv') -NoTypeInformation
    ($npcs | Where-Object { [int]$_.zone_id -eq $zoneId -and [int]$_.map_id -eq $mapId }) | Export-Csv (Join-Path $zoneDir 'npc_spawn.csv') -NoTypeInformation
    ($monsters | Where-Object { [int]$_.zone_id -eq $zoneId -and [int]$_.map_id -eq $mapId }) | Export-Csv (Join-Path $zoneDir 'monster_spawn.csv') -NoTypeInformation
    ($safe | Where-Object { [int]$_.zone_id -eq $zoneId -and [int]$_.map_id -eq $mapId }) | Export-Csv (Join-Path $zoneDir 'safe_zone.csv') -NoTypeInformation
    ($special | Where-Object { [int]$_.zone_id -eq $zoneId -and [int]$_.map_id -eq $mapId }) | Export-Csv (Join-Path $zoneDir 'special_region.csv') -NoTypeInformation
}

Copy-Item (Join-Path $TargetRoot 'zone_runtime.bin') (Join-Path $runtimeRoot 'zone_runtime.bin') -Force
Write-Host 'Imported maps 1-9 into resources/maps and resources/zones.'
