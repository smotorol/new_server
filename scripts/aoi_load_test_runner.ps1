
param(
    [ValidateSet('WinForms', 'Bench')]
    [string]$ClientMode = 'WinForms',
    [int]$Clients = 100,
    [int]$DurationSeconds = 300,
    [int]$MoveIntervalMs = 250,
    [int]$PortalIntervalSeconds = 20,
    [int]$ReconnectIntervalSeconds = 0,
    [int]$WorldIndex = 0,
    [int]$CharacterIndex = 0,
    [int]$WanderRadius = 30,
    [int]$ClientLaunchSpacingMs = 50,
    [int]$MaxParallelLaunch = 25,
    [int]$MaxConcurrentConnect = 25,
    [int]$BenchProcessCount = 1,
    [string]$PortalRoute = '1,2,3,4',
    [string]$Configuration = 'Debug',
    [string]$ClientExePath = '',
    [string]$BenchExePath = '',
    [string]$RunId = '',
    [string]$AccountsCsv = '',
    [string]$LoginIdPrefix = 'load',
    [string]$Password = 'pw1',
    [switch]$NoServerStart,
    [switch]$NoAnalyzer,
    [switch]$SkipAnalyzer,
    [switch]$ValidateAccountsOnly,
    [switch]$Headless = $true,
    [switch]$StopServersOnExit,
    [int]$ClientShutdownGraceSeconds = 30,
    [switch]$ProvisionBeforeRun,
    [int]$ProvisionCount = 0,
    [string]$ProvisionPrefix = 'load',
    [string]$ProvisionCharacterPrefix = 'lt_char_',
    [switch]$ProvisionValidateOnly
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$outServers = Join-Path $root 'out\servers'
$loadTestRoot = Join-Path $root 'out\load_tests'
$timestamp = Get-Date -Format 'yyyy_MM_dd_HHmmss'
if ([string]::IsNullOrWhiteSpace($RunId)) { $RunId = "run_$timestamp" }
if ($SkipAnalyzer) { $NoAnalyzer = $true }

$runDir = Join-Path $loadTestRoot $RunId
$clientLogDir = Join-Path $runDir 'client_logs'
$clientResultDir = Join-Path $runDir 'client_results'
$benchProcessDir = Join-Path $runDir 'bench_processes'
$inputDir = Join-Path $runDir 'inputs'
$validationDir = Join-Path $runDir 'validation'
$allocationDir = Join-Path $runDir 'allocations'
$serverLogDir = Join-Path $runDir 'server_logs'
$serverArchiveDir = Join-Path $runDir 'server_logs_pre_run'
$analyzerDir = Join-Path $runDir 'analyzer'
$validationReportPath = Join-Path $validationDir 'validation_report.json'
$allocationPath = Join-Path $allocationDir 'bench_allocations.json'
$manifestPath = Join-Path $runDir 'run_manifest.json'
$summaryJsonPath = Join-Path $runDir 'run_summary.json'
$summaryTextPath = Join-Path $runDir 'run_summary.txt'
New-Item -ItemType Directory -Force -Path $runDir, $clientLogDir, $clientResultDir, $benchProcessDir, $inputDir, $validationDir, $allocationDir, $serverLogDir, $serverArchiveDir, $analyzerDir, $outServers | Out-Null

$requiredAccountColumns = @('LoginId', 'Password')
$optionalAccountColumns = @('WorldIndex', 'CharacterIndex', 'PortalRoute', 'ClientTag', 'DurationSeconds', 'MoveIntervalMs', 'PortalIntervalSeconds', 'ReconnectIntervalSeconds', 'WanderRadius')

function Write-JsonFile {
    param([string]$Path, $Data)
    $dir = Split-Path -Parent $Path
    if ($dir) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    $Data | ConvertTo-Json -Depth 10 | Set-Content -Path $Path -Encoding UTF8
}

function Write-TextFile {
    param([string]$Path, [string]$Text)
    $dir = Split-Path -Parent $Path
    if ($dir) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    Set-Content -Path $Path -Value $Text -Encoding UTF8
}

function Resolve-ClientExePath {
    param([string]$ConfiguredPath)
    if ($ConfiguredPath) { return (Resolve-Path $ConfiguredPath).Path }
    foreach ($candidate in @(
        (Join-Path $root 'tools\DummyClientWinForms\bin\Debug\DummyClientWinForms.exe'),
        (Join-Path $root 'tools\DummyClientWinForms\bin\Release\DummyClientWinForms.exe')
    )) {
        if (Test-Path $candidate) { return (Resolve-Path $candidate).Path }
    }
    throw 'DummyClientWinForms.exe not found. Pass -ClientExePath explicitly.'
}

function Resolve-BenchExePath {
    param([string]$ConfiguredPath)
    if ($ConfiguredPath) { return (Resolve-Path $ConfiguredPath).Path }
    foreach ($candidate in @(
        (Join-Path $root 'tools\BenchDummyClient\bin\Debug\BenchDummyClient.exe'),
        (Join-Path $root 'tools\BenchDummyClient\bin\Release\BenchDummyClient.exe')
    )) {
        if (Test-Path $candidate) { return (Resolve-Path $candidate).Path }
    }
    throw 'BenchDummyClient.exe not found. Pass -BenchExePath explicitly.'
}

function ConvertTo-SafeName {
    param([string]$Value)
    if ([string]::IsNullOrWhiteSpace($Value)) { return 'client' }
    return (($Value -replace '[^A-Za-z0-9._-]', '_').Trim('_'))
}

function Archive-ExistingLogs {
    param([string]$SourceDir, [string]$TargetDir)
    Get-ChildItem $SourceDir -File -ErrorAction SilentlyContinue | ForEach-Object {
        $destination = Join-Path $TargetDir $_.Name
        try {
            Move-Item -Path $_.FullName -Destination $destination -Force -ErrorAction Stop
        }
        catch {
            Write-Warning ("Archive-ExistingLogs skipped locked file: {0}" -f $_.FullName)
        }
    }
}

function Resolve-AnalyzerScriptPath {
    $path = Join-Path $root 'scripts\aoi_analyzer.py'
    if (-not (Test-Path $path)) { throw 'aoi_analyzer.py not found.' }
    return (Resolve-Path $path).Path
}

function Test-DirectoryWritable {
    param([string]$Path)
    try {
        $probe = Join-Path $Path '.write_probe.tmp'
        Set-Content -Path $probe -Value 'ok' -Encoding UTF8
        Remove-Item $probe -Force -ErrorAction SilentlyContinue
        return $true
    }
    catch {
        return $false
    }
}

function Copy-ArtifactIfDifferent {
    param(
        [string]$SourcePath,
        [string]$DestinationPath
    )

    if ([string]::IsNullOrWhiteSpace($SourcePath) -or [string]::IsNullOrWhiteSpace($DestinationPath)) {
        return
    }
    if (-not (Test-Path $SourcePath)) {
        return
    }

    $resolvedSource = [System.IO.Path]::GetFullPath($SourcePath)
    $resolvedDestination = [System.IO.Path]::GetFullPath($DestinationPath)
    if ([string]::Equals($resolvedSource, $resolvedDestination, [System.StringComparison]::OrdinalIgnoreCase)) {
        return
    }

    Copy-Item -Path $resolvedSource -Destination $resolvedDestination -Force
}

function Invoke-ProvisioningIfRequested {
    if (-not $ProvisionBeforeRun) { return $null }
    $provisionScript = Join-Path $root 'scripts\aoi_provision_loadtest_accounts.ps1'
    if (-not (Test-Path $provisionScript)) { throw 'Provisioning script not found.' }
    $count = if ($ProvisionCount -gt 0) { $ProvisionCount } else { $Clients }
    if (-not $AccountsCsv) { $script:AccountsCsv = Join-Path $root 'config\loadtest_accounts.csv' }
    $summaryOut = Join-Path $inputDir 'provision_summary.json'
    $args = @{
        Count = $count
        Prefix = $ProvisionPrefix
        CharacterPrefix = $ProvisionCharacterPrefix
        Password = $Password
        WorldIndex = $WorldIndex
        CharacterIndex = $CharacterIndex
        PortalRoute = $PortalRoute
        OutputCsv = $AccountsCsv
        SummaryJsonPath = $summaryOut
    }
    if ($ProvisionValidateOnly) { $args.ValidateOnly = $true }
    & $provisionScript @args
    return [pscustomobject]@{ script = $provisionScript; output_csv = $AccountsCsv; summary_json = $summaryOut; validate_only = [bool]$ProvisionValidateOnly }
}

function Build-BenchAllocations {
    param($Accounts, [int]$RequestedClients, [int]$RequestedProcesses)
    $allocations = @()
    $processCount = [Math]::Max(1, [Math]::Min($RequestedProcesses, $RequestedClients))
    $baseCount = [Math]::Floor($RequestedClients / $processCount)
    $remainder = $RequestedClients % $processCount
    $accountStart = 0
    for ($i = 0; $i -lt $processCount; $i++) {
        $sessionCount = $baseCount + $(if ($i -lt $remainder) { 1 } else { 0 })
        if ($sessionCount -le 0) { continue }
        $slice = @($Accounts | Select-Object -Skip $accountStart -First $sessionCount)
        $allocations += [pscustomobject]@{
            process_index = $i + 1
            process_label = ('bench_{0:D4}' -f ($i + 1))
            account_start_index = $accountStart
            session_count = $sessionCount
            first_login_id = if ($slice.Count -gt 0) { [string]$slice[0].LoginId } else { '' }
            last_login_id = if ($slice.Count -gt 0) { [string]$slice[$slice.Count - 1].LoginId } else { '' }
            login_ids = @($slice | ForEach-Object { [string]$_.LoginId })
        }
        $accountStart += $sessionCount
    }
    return ,$allocations
}
function Get-IntOrDefault {
    param($Value, [int]$Fallback)
    $parsed = 0
    if ($null -eq $Value -or [string]::IsNullOrWhiteSpace([string]$Value)) { return $Fallback }
    if ([int]::TryParse([string]$Value, [ref]$parsed)) { return $parsed }
    return $Fallback
}

function Validate-AndLoadAccounts {
    param(
        [int]$Count,
        [string]$CsvPath,
        [string]$Prefix,
        [string]$DefaultPassword,
        [int]$DefaultWorldIndex,
        [int]$DefaultCharacterIndex,
        [string]$DefaultPortalRoute,
        [int]$DefaultDurationSeconds,
        [int]$DefaultMoveIntervalMs,
        [int]$DefaultPortalIntervalSeconds,
        [int]$DefaultReconnectIntervalSeconds,
        [int]$DefaultWanderRadius
    )

    $result = [ordered]@{ valid = $true; source = if ($CsvPath) { 'csv' } else { 'generated' }; csv_path = $CsvPath; warnings = @(); errors = @(); row_count = 0; selected_count = 0; accounts = @() }
    if (-not $CsvPath) {
        for ($i = 0; $i -lt $Count; $i++) {
            $result.accounts += [pscustomobject]@{
                LoginId = ('{0}{1:D5}' -f $Prefix, ($i + 1))
                Password = $DefaultPassword
                WorldIndex = $DefaultWorldIndex
                CharacterIndex = $DefaultCharacterIndex
                PortalRoute = $DefaultPortalRoute
                ClientTag = ('client-{0:D5}' -f ($i + 1))
                DurationSeconds = $DefaultDurationSeconds
                MoveIntervalMs = $DefaultMoveIntervalMs
                PortalIntervalSeconds = $DefaultPortalIntervalSeconds
                ReconnectIntervalSeconds = $DefaultReconnectIntervalSeconds
                WanderRadius = $DefaultWanderRadius
            }
        }
        $result.row_count = $Count
        $result.selected_count = $Count
        return [pscustomobject]$result
    }

    if (-not (Test-Path $CsvPath)) {
        $result.valid = $false
        $result.errors += "AccountsCsv not found: $CsvPath"
        return [pscustomobject]$result
    }

    $rows = Import-Csv -Path $CsvPath
    $result.row_count = @($rows).Count
    if ($result.row_count -eq 0) {
        $result.valid = $false
        $result.errors += 'AccountsCsv is empty.'
        return [pscustomobject]$result
    }

    $columns = @($rows[0].PSObject.Properties.Name)
    foreach ($required in $requiredAccountColumns) {
        if ($columns -notcontains $required) {
            $result.valid = $false
            $result.errors += "AccountsCsv missing required column: $required"
        }
    }
    $duplicates = @($rows | Group-Object LoginId | Where-Object { $_.Count -gt 1 } | Select-Object -ExpandProperty Name)
    if ($duplicates.Count -gt 0) {
        $result.valid = $false
        $result.errors += ('Duplicate LoginId values are not allowed: ' + ($duplicates -join ', '))
    }
    if ($result.row_count -lt $Count) {
        $result.valid = $false
        $result.errors += "AccountsCsv row count ($($result.row_count)) is smaller than requested Clients ($Count)."
    }
    if (-not $result.valid) { return [pscustomobject]$result }

    $selected = @($rows | Select-Object -First $Count)
    $index = 0
    foreach ($row in $selected) {
        $index++
        $result.accounts += [pscustomobject]@{
            LoginId = [string]$row.LoginId
            Password = if ([string]::IsNullOrWhiteSpace([string]$row.Password)) { $DefaultPassword } else { [string]$row.Password }
            WorldIndex = Get-IntOrDefault $row.WorldIndex $DefaultWorldIndex
            CharacterIndex = Get-IntOrDefault $row.CharacterIndex $DefaultCharacterIndex
            PortalRoute = if ([string]::IsNullOrWhiteSpace([string]$row.PortalRoute)) { $DefaultPortalRoute } else { [string]$row.PortalRoute }
            ClientTag = if ([string]::IsNullOrWhiteSpace([string]$row.ClientTag)) { ('client-{0:D5}' -f $index) } else { [string]$row.ClientTag }
            DurationSeconds = Get-IntOrDefault $row.DurationSeconds $DefaultDurationSeconds
            MoveIntervalMs = Get-IntOrDefault $row.MoveIntervalMs $DefaultMoveIntervalMs
            PortalIntervalSeconds = Get-IntOrDefault $row.PortalIntervalSeconds $DefaultPortalIntervalSeconds
            ReconnectIntervalSeconds = Get-IntOrDefault $row.ReconnectIntervalSeconds $DefaultReconnectIntervalSeconds
            WanderRadius = Get-IntOrDefault $row.WanderRadius $DefaultWanderRadius
        }
    }
    $result.selected_count = @($result.accounts).Count
    return [pscustomobject]$result
}

function Build-RunManifestBase {
    param($AccountValidation, [string]$ExecutablePath)
    [ordered]@{
        run_id = $RunId
        started_at = (Get-Date).ToString('o')
        client_mode = $ClientMode
        executable = $ExecutablePath
        configuration = $Configuration
        requested_clients = $Clients
        duration_seconds = $DurationSeconds
        portal_route = $PortalRoute
        headless = [bool]$Headless
        accounts = $AccountValidation
        paths = [ordered]@{
            run_dir = $runDir
            inputs = $inputDir
            validation = $validationDir
            allocations = $allocationDir
            client_logs = $clientLogDir
            client_results = $clientResultDir
            bench_processes = $benchProcessDir
            server_logs = $serverLogDir
            server_logs_pre_run = $serverArchiveDir
            analyzer = $analyzerDir
            summary_json = $summaryJsonPath
            summary_txt = $summaryTextPath
            validation_report = $validationReportPath
            bench_allocations = $allocationPath
        }
        options = [ordered]@{
            no_server_start = [bool]$NoServerStart
            no_analyzer = [bool]$NoAnalyzer
            validate_accounts_only = [bool]$ValidateAccountsOnly
            client_launch_spacing_ms = $ClientLaunchSpacingMs
            max_parallel_launch = $MaxParallelLaunch
            max_concurrent_connect = $MaxConcurrentConnect
            bench_process_count = $BenchProcessCount
            provision_before_run = [bool]$ProvisionBeforeRun
            provision_count = $ProvisionCount
            provision_prefix = $ProvisionPrefix
            provision_validate_only = [bool]$ProvisionValidateOnly
            stop_servers_on_exit = [bool]$StopServersOnExit
        }
        pipeline_stages = @('validate_inputs','provision_optional','prepare_run_dir','start_servers_optional','launch_clients','monitor','collect_outputs','analyze_optional','aggregate_summary','finalize_manifest')
        exit_code_policy = [ordered]@{ success = 0; validation_failure = 1; process_launch_or_output_failure = 2; session_failure = 3; analyzer_failure = 4 }
    }
}

function Read-ClientResultFile {
    param([string]$Path)
    try { return Get-Content $Path -Raw | ConvertFrom-Json } catch { return $null }
}

function Read-BenchSummaryFile {
    param([string]$Path)
    try { return Get-Content $Path -Raw | ConvertFrom-Json } catch { return $null }
}

function Get-DateDiffSeconds {
    param([string]$StartText, [string]$EndText)
    if (-not $StartText -or -not $EndText) { return $null }
    try {
        $start = [DateTimeOffset]::Parse($StartText)
        $end = [DateTimeOffset]::Parse($EndText)
        return [Math]::Round(($end - $start).TotalSeconds, 3)
    }
    catch { return $null }
}
function Build-WinFormsSummary {
    param($LaunchedClients, $LaunchFailures, [string[]]$AnalyzerOutputs, [int]$AnalyzerExitCode)
    $resultsByIndex = @{}
    Get-ChildItem $clientResultDir -Filter 'client_result_*.json' -File -ErrorAction SilentlyContinue | ForEach-Object {
        $parsed = Read-ClientResultFile $_.FullName
        if ($parsed) { $resultsByIndex[[int]$parsed.client_index] = $parsed }
    }

    $statusCounts = @{}
    $failureReasonCounts = @{}
    $durations = New-Object System.Collections.Generic.List[double]
    $clients = @()
    foreach ($client in $LaunchedClients) {
        $result = $resultsByIndex[[int]$client.client_index]
        $processState = if ($client.process -and -not $client.process.HasExited) { 'still_running' } elseif ($client.process) { 'exited' } else { 'not_started' }
        $exitCode = if ($client.process -and $client.process.HasExited) { $client.process.ExitCode } else { $null }
        $finalStatus = if ($result) { [string]$result.final_status } elseif ($client.launch_failed) { 'launch_failed' } elseif ($processState -eq 'exited') { 'missing_result' } else { 'no_result' }
        $failureReason = if ($result) { [string]$result.failure_reason } elseif ($client.launch_failed) { [string]$client.failure_reason } else { '' }
        $success = if ($result) { [bool]$result.success } else { $false }
        if (-not $statusCounts.ContainsKey($finalStatus)) { $statusCounts[$finalStatus] = 0 }
        $statusCounts[$finalStatus]++
        if ($failureReason) {
            if (-not $failureReasonCounts.ContainsKey($failureReason)) { $failureReasonCounts[$failureReason] = 0 }
            $failureReasonCounts[$failureReason]++
        }
        $duration = if ($result) { Get-DateDiffSeconds ([string]$result.started_at_utc) ([string]$result.ended_at_utc) } else { $null }
        if ($null -ne $duration) { $durations.Add([double]$duration) }
        $clients += [pscustomobject]@{
            client_index = $client.client_index
            login_id = $client.login_id
            client_tag = $client.client_tag
            log_path = $client.log_path
            result_path = $client.result_path
            process_id = if ($client.process) { $client.process.Id } else { $null }
            process_exit_code = $exitCode
            process_state = $processState
            success = $success
            final_status = $finalStatus
            failure_reason = $failureReason
            duration_seconds = $duration
        }
    }
    foreach ($failure in $LaunchFailures) {
        $clients += [pscustomobject]@{
            client_index = $failure.client_index; login_id = $failure.login_id; client_tag = $failure.client_tag; log_path = $failure.log_path; result_path = $failure.result_path; process_id = $null; process_exit_code = $null; process_state = 'launch_failed'; success = $false; final_status = 'launch_failed'; failure_reason = [string]$failure.failure_reason; duration_seconds = $null }
    }
    [ordered]@{
        run_id = $RunId
        client_mode = 'WinForms'
        requested_clients = $Clients
        launched_clients = @($LaunchedClients).Count
        launch_failures = @($LaunchFailures).Count
        result_files_found = $resultsByIndex.Count
        success_count = @($clients | Where-Object { $_.success }).Count
        failure_count = @($clients | Where-Object { -not $_.success }).Count
        login_success_count = @($resultsByIndex.Values | Where-Object { $_.login_success }).Count
        enter_world_success_count = @($resultsByIndex.Values | Where-Object { $_.enter_world_success }).Count
        reconnect_success_count = @($resultsByIndex.Values | Where-Object { $_.reconnect_success }).Count
        disconnected_count = @($resultsByIndex.Values | Where-Object { $_.disconnected }).Count
        timeout_count = @($resultsByIndex.Values | Where-Object { $_.timeout }).Count
        missing_result_count = @($clients | Where-Object { $_.final_status -eq 'missing_result' -or $_.final_status -eq 'no_result' }).Count
        process_crash_count = @($clients | Where-Object { $_.process_state -eq 'exited' -and $_.process_exit_code -ne 0 -and -not $_.success }).Count
        average_duration_seconds = if ($durations.Count -gt 0) { [Math]::Round(($durations | Measure-Object -Average).Average, 3) } else { $null }
        analyzer_exit_code = $AnalyzerExitCode
        analyzer_outputs = $AnalyzerOutputs
        final_status_counts = @($statusCounts.GetEnumerator() | Sort-Object Name | ForEach-Object { [pscustomobject]@{ status = $_.Name; count = $_.Value } })
        failure_reason_counts = @($failureReasonCounts.GetEnumerator() | Sort-Object Name | ForEach-Object { [pscustomobject]@{ reason = $_.Name; count = $_.Value } })
        clients = @($clients | Sort-Object client_index)
    }
}

function Build-BenchSummary {
    param($LaunchedProcesses, $LaunchFailures, [string[]]$AnalyzerOutputs, [int]$AnalyzerExitCode)
    $statusCounts = @{}
    $failureReasonCounts = @{}
    $processes = @()
    $durations = New-Object System.Collections.Generic.List[double]
    $successCount = 0
    $failureCount = 0
    $loginSuccessCount = 0
    $enterWorldSuccessCount = 0
    $reconnectSuccessCount = 0
    $disconnectedCount = 0
    $timeoutCount = 0
    $missingSummaryCount = 0
    $summaryFilesFound = 0

    foreach ($proc in $LaunchedProcesses) {
        $summaryPath = Join-Path $proc.result_dir 'bench_summary.json'
        $summary = if (Test-Path $summaryPath) { Read-BenchSummaryFile $summaryPath } else { $null }
        if ($summary) {
            $summaryFilesFound++
            $successCount += [int]$summary.success_count
            $failureCount += [int]$summary.failure_count
            $loginSuccessCount += [int]$summary.login_success_count
            $enterWorldSuccessCount += [int]$summary.enter_world_success_count
            $reconnectSuccessCount += [int]$summary.reconnect_success_count
            $disconnectedCount += [int]$summary.disconnected_count
            $timeoutCount += [int]$summary.timeout_count
            if ($summary.average_duration_seconds -ne $null) { $durations.Add([double]$summary.average_duration_seconds) }
            foreach ($item in @($summary.final_status_counts)) {
                if (-not $statusCounts.ContainsKey([string]$item.status)) { $statusCounts[[string]$item.status] = 0 }
                $statusCounts[[string]$item.status] += [int]$item.count
            }
            foreach ($item in @($summary.failure_reason_counts)) {
                if (-not $failureReasonCounts.ContainsKey([string]$item.reason)) { $failureReasonCounts[[string]$item.reason] = 0 }
                $failureReasonCounts[[string]$item.reason] += [int]$item.count
            }
        } else {
            $missingSummaryCount++
        }
        $processes += [pscustomobject]@{
            process_index = $proc.process_index
            process_label = $proc.process_label
            account_start_index = $proc.account_start_index
            session_count = $proc.session_count
            result_dir = $proc.result_dir
            summary_path = $summaryPath
            summary_found = [bool]($summary)
            process_id = if ($proc.process) { $proc.process.Id } else { $null }
            process_exit_code = if ($proc.process -and $proc.process.HasExited) { $proc.process.ExitCode } else { $null }
            process_state = if ($proc.process -and -not $proc.process.HasExited) { 'still_running' } elseif ($proc.process) { 'exited' } else { 'not_started' }
            success_count = if ($summary) { [int]$summary.success_count } else { 0 }
            failure_count = if ($summary) { [int]$summary.failure_count } else { [int]$proc.session_count }
        }
    }
    foreach ($failure in $LaunchFailures) {
        $processes += [pscustomobject]@{ process_index = $failure.process_index; process_label = $failure.process_label; account_start_index = $failure.account_start_index; session_count = $failure.session_count; result_dir = $failure.result_dir; summary_path = ''; summary_found = $false; process_id = $null; process_exit_code = $null; process_state = 'launch_failed'; success_count = 0; failure_count = $failure.session_count }
        $failureCount += [int]$failure.session_count
    }
    [ordered]@{
        run_id = $RunId
        client_mode = 'Bench'
        requested_clients = $Clients
        launched_clients = ((@($LaunchedProcesses) | Measure-Object -Property session_count -Sum).Sum)
        launched_processes = @($LaunchedProcesses).Count
        launch_failures = @($LaunchFailures).Count
        result_files_found = $summaryFilesFound
        success_count = $successCount
        failure_count = $failureCount
        login_success_count = $loginSuccessCount
        enter_world_success_count = $enterWorldSuccessCount
        reconnect_success_count = $reconnectSuccessCount
        disconnected_count = $disconnectedCount
        timeout_count = $timeoutCount
        missing_result_count = $missingSummaryCount
        process_crash_count = @($processes | Where-Object { $_.process_state -eq 'exited' -and $_.process_exit_code -ne 0 -and -not $_.summary_found }).Count
        average_duration_seconds = if ($durations.Count -gt 0) { [Math]::Round(($durations | Measure-Object -Average).Average, 3) } else { $null }
        analyzer_exit_code = $AnalyzerExitCode
        analyzer_outputs = $AnalyzerOutputs
        final_status_counts = @($statusCounts.GetEnumerator() | Sort-Object Name | ForEach-Object { [pscustomobject]@{ status = $_.Name; count = $_.Value } })
        failure_reason_counts = @($failureReasonCounts.GetEnumerator() | Sort-Object Name | ForEach-Object { [pscustomobject]@{ reason = $_.Name; count = $_.Value } })
        processes = @($processes | Sort-Object process_index)
    }
}

function Get-BenchSummaryFoundCount {
    param($LaunchedProcesses)
    $count = 0
    foreach ($proc in @($LaunchedProcesses)) {
        if (Test-Path (Join-Path $proc.result_dir 'bench_summary.json')) {
            $count++
        }
    }
    return $count
}

function Build-RunSummaryText {
    param($Summary)
    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($line in @(
        "RunId: $($Summary.run_id)",
        "ClientMode: $($Summary.client_mode)",
        "RequestedClients: $($Summary.requested_clients)",
        "LaunchedClients: $($Summary.launched_clients)",
        "LaunchFailures: $($Summary.launch_failures)",
        "SuccessCount: $($Summary.success_count)",
        "FailureCount: $($Summary.failure_count)",
        "LoginSuccessCount: $($Summary.login_success_count)",
        "EnterWorldSuccessCount: $($Summary.enter_world_success_count)",
        "ReconnectSuccessCount: $($Summary.reconnect_success_count)",
        "DisconnectedCount: $($Summary.disconnected_count)",
        "TimeoutCount: $($Summary.timeout_count)",
        "MissingResultCount: $($Summary.missing_result_count)",
        "ProcessCrashCount: $($Summary.process_crash_count)",
        "AverageDurationSeconds: $($Summary.average_duration_seconds)",
        "AnalyzerExitCode: $($Summary.analyzer_exit_code)"
    )) { $lines.Add($line) }
    if ($Summary.PSObject.Properties.Name -contains 'launched_processes') { $lines.Add("LaunchedProcesses: $($Summary.launched_processes)") }
    $lines.Add('')
    $lines.Add('FinalStatusCounts:')
    foreach ($item in $Summary.final_status_counts) { $lines.Add("  - $($item.status): $($item.count)") }
    $lines.Add('')
    $lines.Add('FailureReasonCounts:')
    foreach ($item in $Summary.failure_reason_counts) { $lines.Add("  - $($item.reason): $($item.count)") }
    $lines.Add('')
    $lines.Add('AnalyzerOutputs:')
    foreach ($path in $Summary.analyzer_outputs) { $lines.Add("  - $path") }
    return ($lines -join [Environment]::NewLine)
}
if (-not $NoServerStart) {
    Archive-ExistingLogs -SourceDir $outServers -TargetDir $serverArchiveDir
}
else {
    Write-Host "Skipping pre-run server log archive because -NoServerStart was specified."
}

$provisioning = Invoke-ProvisioningIfRequested
if ($provisioning -and (Test-Path $provisioning.output_csv)) {
    Copy-ArtifactIfDifferent -SourcePath $provisioning.output_csv -DestinationPath (Join-Path $inputDir (Split-Path $provisioning.output_csv -Leaf))
}
if ($provisioning -and (Test-Path $provisioning.summary_json)) {
    Copy-ArtifactIfDifferent -SourcePath $provisioning.summary_json -DestinationPath (Join-Path $inputDir (Split-Path $provisioning.summary_json -Leaf))
}

$validationIssues = New-Object System.Collections.Generic.List[string]
$validationWarnings = New-Object System.Collections.Generic.List[string]
$clientExecutable = ''
$clientExe = ''
$benchExe = ''
$analyzerScript = ''
try {
    if ($ClientMode -eq 'Bench') {
        $benchExe = Resolve-BenchExePath -ConfiguredPath $BenchExePath
        $clientExecutable = $benchExe
    } else {
        $clientExe = Resolve-ClientExePath -ConfiguredPath $ClientExePath
        $clientExecutable = $clientExe
    }
} catch {
    $validationIssues.Add($_.Exception.Message) | Out-Null
}
if (-not $NoAnalyzer) {
    try { $analyzerScript = Resolve-AnalyzerScriptPath } catch { $validationIssues.Add($_.Exception.Message) | Out-Null }
}
if (-not (Test-DirectoryWritable -Path $runDir)) {
    $validationIssues.Add('Run directory is not writable.') | Out-Null
}
if ($ClientMode -eq 'Bench' -and $BenchProcessCount -gt $Clients) {
    $validationWarnings.Add('BenchProcessCount is greater than Clients; runner will clamp it.') | Out-Null
}

$accountValidation = Validate-AndLoadAccounts -Count $Clients -CsvPath $AccountsCsv -Prefix $LoginIdPrefix -DefaultPassword $Password -DefaultWorldIndex $WorldIndex -DefaultCharacterIndex $CharacterIndex -DefaultPortalRoute $PortalRoute -DefaultDurationSeconds $DurationSeconds -DefaultMoveIntervalMs $MoveIntervalMs -DefaultPortalIntervalSeconds $PortalIntervalSeconds -DefaultReconnectIntervalSeconds $ReconnectIntervalSeconds -DefaultWanderRadius $WanderRadius
if ($AccountsCsv -and (Test-Path $AccountsCsv)) {
    Copy-ArtifactIfDifferent -SourcePath $AccountsCsv -DestinationPath (Join-Path $inputDir (Split-Path $AccountsCsv -Leaf))
}
$manifest = Build-RunManifestBase -AccountValidation $accountValidation -ExecutablePath $clientExecutable
$validationReport = [ordered]@{
    run_id = $RunId
    client_mode = $ClientMode
    valid = ($validationIssues.Count -eq 0 -and $accountValidation.valid)
    issues = @($validationIssues) + @($accountValidation.errors)
    warnings = @($validationWarnings) + @($accountValidation.warnings)
    executable = $clientExecutable
    analyzer_script = $analyzerScript
    accounts_csv = $AccountsCsv
    requested_clients = $Clients
    selected_accounts = $accountValidation.selected_count
    provisioning = $provisioning
}
Write-JsonFile -Path $validationReportPath -Data $validationReport
$manifest.validation_report = $validationReportPath
$manifest.provisioning = $provisioning
Write-JsonFile -Path $manifestPath -Data $manifest

if (-not $validationReport.valid) {
    $messages = @($validationReport.issues | ForEach-Object { "ERROR: $_" }) + @($validationReport.warnings | ForEach-Object { "WARN: $_" })
    Write-TextFile -Path $summaryTextPath -Text ($messages -join [Environment]::NewLine)
    exit 1
}

if ($ValidateAccountsOnly) {
    $validationSummary = [ordered]@{ run_id = $RunId; client_mode = $ClientMode; validation_only = $true; valid = $true; accounts_csv = $AccountsCsv; row_count = $accountValidation.row_count; selected_count = $accountValidation.selected_count; warnings = $validationReport.warnings; validation_report = $validationReportPath }
    Write-JsonFile -Path $summaryJsonPath -Data $validationSummary
    Write-TextFile -Path $summaryTextPath -Text "AccountsCsv validation passed. rows=$($accountValidation.row_count) selected=$($accountValidation.selected_count) mode=$ClientMode"
    exit 0
}

if (-not $NoServerStart) {
    & (Join-Path $root 'scripts\run_all_servers.ps1') -Configuration $Configuration -NoBuild
    Start-Sleep -Seconds 4
}

$launchedClients = @()
$launchedBenchProcesses = @()
$launchFailures = @()
$benchAllocations = @()

if ($ClientMode -eq 'Bench') {
    $benchAllocations = Build-BenchAllocations -Accounts $accountValidation.accounts -RequestedClients $Clients -RequestedProcesses $BenchProcessCount
    Write-JsonFile -Path $allocationPath -Data $benchAllocations
    foreach ($alloc in $benchAllocations) {
        $processIndex = [int]$alloc.process_index
        $processLabel = [string]$alloc.process_label
        $accountStart = [int]$alloc.account_start_index
        $sessionCount = [int]$alloc.session_count
        $processDir = Join-Path $benchProcessDir $processLabel
        New-Item -ItemType Directory -Force -Path $processDir | Out-Null
        $argList = @(
            '--accounts-csv', $AccountsCsv,
            '--account-start-index', $accountStart,
            '--session-count', $sessionCount,
            '--duration-sec', $DurationSeconds,
            '--move-interval-ms', $MoveIntervalMs,
            '--portal-interval-sec', $PortalIntervalSeconds,
            '--reconnect-interval-sec', $ReconnectIntervalSeconds,
            '--launch-spacing-ms', $ClientLaunchSpacingMs,
            '--max-concurrent-connect', $MaxConcurrentConnect,
            '--result-dir', $processDir,
            '--run-id', $RunId,
            '--process-label', $processLabel,
            '--random-seed', $processIndex
        )
        try {
            $process = Start-Process -FilePath $benchExe -ArgumentList $argList -PassThru -WindowStyle Hidden
            $launchedBenchProcesses += [pscustomobject]@{ process_index = $processIndex; process_label = $processLabel; account_start_index = $accountStart; session_count = $sessionCount; result_dir = $processDir; process = $process }
        }
        catch {
            $launchFailures += [pscustomobject]@{ process_index = $processIndex; process_label = $processLabel; account_start_index = $accountStart; session_count = $sessionCount; result_dir = $processDir; failure_reason = ('bench_launch_failed: ' + $_.Exception.Message) }
        }
        Start-Sleep -Milliseconds $ClientLaunchSpacingMs
    }
}
else {
    for ($i = 0; $i -lt $Clients; $i++) {
        $account = $accountValidation.accounts[$i]
        $safeLoginId = ConvertTo-SafeName -Value $account.LoginId
        $clientIndex = $i + 1
        $clientLogPath = Join-Path $clientLogDir ('client_{0:D5}_{1}.log' -f $clientIndex, $safeLoginId)
        $clientResultPath = Join-Path $clientResultDir ('client_result_{0:D5}_{1}.json' -f $clientIndex, $safeLoginId)
        $argList = @(
            '--auto', '--client-index', $clientIndex, '--run-id', $RunId,
            '--login-id', $account.LoginId, '--password', $account.Password,
            '--duration-sec', $account.DurationSeconds, '--move-interval-ms', $account.MoveIntervalMs,
            '--portal-interval-sec', $account.PortalIntervalSeconds, '--reconnect-interval-sec', $account.ReconnectIntervalSeconds,
            '--world-index', $account.WorldIndex, '--character-index', $account.CharacterIndex,
            '--wander-radius', $account.WanderRadius, '--portal-route', $account.PortalRoute,
            '--random-seed', $clientIndex, '--client-log-path', $clientLogPath,
            '--client-result-path', $clientResultPath, '--client-tag', $account.ClientTag
        )
        if ($Headless) { $argList += '--headless' }
        try {
            $process = Start-Process -FilePath $clientExe -ArgumentList $argList -PassThru -WindowStyle Hidden
            $launchedClients += [pscustomobject]@{ client_index = $clientIndex; login_id = $account.LoginId; client_tag = $account.ClientTag; log_path = $clientLogPath; result_path = $clientResultPath; process = $process }
        }
        catch {
            $launchFailures += [pscustomobject]@{ client_index = $clientIndex; login_id = $account.LoginId; client_tag = $account.ClientTag; log_path = $clientLogPath; result_path = $clientResultPath; failure_reason = ('launch_failed: ' + $_.Exception.Message) }
        }
        Start-Sleep -Milliseconds $ClientLaunchSpacingMs
        if ($MaxParallelLaunch -gt 0 -and (($clientIndex % $MaxParallelLaunch) -eq 0)) { Start-Sleep -Milliseconds ([Math]::Max(500, $ClientLaunchSpacingMs * 5)) }
    }
}

$benchLaunchSpreadSeconds = if ($ClientMode -eq 'Bench' -and @($benchAllocations).Count -gt 0) {
    [int][Math]::Ceiling(((@($benchAllocations | Measure-Object -Property session_count -Maximum).Maximum - 1) * [Math]::Max(0, $ClientLaunchSpacingMs)) / 1000.0)
} else { 0 }
$testEndAt = (Get-Date).AddSeconds($DurationSeconds + $(if ($ClientMode -eq 'Bench') { 15 + $benchLaunchSpreadSeconds } else { 10 }))
while ((Get-Date) -lt $testEndAt) {
    $alive = if ($ClientMode -eq 'Bench') { @($launchedBenchProcesses | Where-Object { $_.process -and -not $_.process.HasExited }).Count } else { @($launchedClients | Where-Object { $_.process -and -not $_.process.HasExited }).Count }
    if ($ClientMode -eq 'Bench') {
        $summaryFound = Get-BenchSummaryFoundCount -LaunchedProcesses $launchedBenchProcesses
        if ($summaryFound -ge @($launchedBenchProcesses).Count -and @($launchedBenchProcesses).Count -gt 0) { break }
    }
    if ($alive -eq 0) { break }
    Start-Sleep -Seconds 1
}

if ($ClientShutdownGraceSeconds -gt 0) {
    $graceUntil = (Get-Date).AddSeconds($ClientShutdownGraceSeconds)
    while ((Get-Date) -lt $graceUntil) {
        $alive = if ($ClientMode -eq 'Bench') { @($launchedBenchProcesses | Where-Object { $_.process -and -not $_.process.HasExited }).Count } else { @($launchedClients | Where-Object { $_.process -and -not $_.process.HasExited }).Count }
        if ($ClientMode -eq 'Bench') {
            $summaryFound = Get-BenchSummaryFoundCount -LaunchedProcesses $launchedBenchProcesses
            if ($summaryFound -ge @($launchedBenchProcesses).Count -and @($launchedBenchProcesses).Count -gt 0) { break }
        }
        if ($alive -eq 0) { break }
        Start-Sleep -Seconds 1
    }
}

foreach ($proc in @($launchedBenchProcesses | ForEach-Object { $_.process }) + @($launchedClients | ForEach-Object { $_.process })) {
    if ($proc -and -not $proc.HasExited) { try { Stop-Process -Id $proc.Id -Force -ErrorAction Stop } catch {} }
}

$analyzerExitCode = 0
$analyzerOutputs = @()
if (-not $NoAnalyzer) {
    & py -3 (Join-Path $root 'scripts\aoi_analyzer.py') $outServers --report-prefix "aoi_analysis_$RunId"
    $analyzerExitCode = $LASTEXITCODE
}

Get-ChildItem $outServers -File -ErrorAction SilentlyContinue | ForEach-Object { Copy-Item -Path $_.FullName -Destination (Join-Path $serverLogDir $_.Name) -Force }
$analyzerOutputs = @(Get-ChildItem $serverLogDir -File -Filter "aoi_analysis_$RunId*" -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
foreach ($path in $analyzerOutputs) { Copy-Item -Path $path -Destination (Join-Path $analyzerDir (Split-Path $path -Leaf)) -Force }

$summary = if ($ClientMode -eq 'Bench') { Build-BenchSummary -LaunchedProcesses $launchedBenchProcesses -LaunchFailures $launchFailures -AnalyzerOutputs $analyzerOutputs -AnalyzerExitCode $analyzerExitCode } else { Build-WinFormsSummary -LaunchedClients $launchedClients -LaunchFailures $launchFailures -AnalyzerOutputs $analyzerOutputs -AnalyzerExitCode $analyzerExitCode }
Write-JsonFile -Path $summaryJsonPath -Data $summary
Write-TextFile -Path $summaryTextPath -Text (Build-RunSummaryText -Summary $summary)
$manifest.finished_at = (Get-Date).ToString('o')
$manifest.summary_json = $summaryJsonPath
$manifest.summary_txt = $summaryTextPath
$manifest.run_verdict = $summary.verdict
$manifest.exit_code_policy = $summary.exit_code_policy
$manifest.analyzer_outputs = $analyzerOutputs
if ($ClientMode -eq 'Bench') {
    $manifest.bench_process_dir = $benchProcessDir
    $manifest.bench_allocation_file = $allocationPath
    $manifest.bench_summary_files = @(Get-ChildItem $benchProcessDir -Recurse -Filter 'bench_summary.json' -File -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
    $manifest.bench_processes = @($launchedBenchProcesses | ForEach-Object {
        [pscustomobject]@{ process_index = $_.process_index; process_label = $_.process_label; account_start_index = $_.account_start_index; session_count = $_.session_count; result_dir = $_.result_dir; process_id = if ($_.process) { $_.process.Id } else { $null } }
    })
}
else {
    $manifest.client_result_files = @(Get-ChildItem $clientResultDir -Filter 'client_result_*.json' -File -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
    $manifest.client_launches = @($launchedClients | ForEach-Object {
        [pscustomobject]@{ client_index = $_.client_index; login_id = $_.login_id; client_tag = $_.client_tag; log_path = $_.log_path; result_path = $_.result_path; process_id = if ($_.process) { $_.process.Id } else { $null } }
    })
}
$manifest.launch_failures = $launchFailures
Write-JsonFile -Path $manifestPath -Data $manifest

if ($StopServersOnExit) {
    Get-Process account_server_d,world_server_d,zone_server_d,login_server_d -ErrorAction SilentlyContinue | Stop-Process -Force
}

$summary.verdict = if ($summary.launch_failures -gt 0 -or $summary.missing_result_count -gt 0 -or $summary.process_crash_count -gt 0) { 'process_output_failures_present' } elseif ($summary.failure_count -gt 0) { 'session_failures_present' } else { 'success' }
$summary.exit_code_policy = [ordered]@{ success = 0; validation_failure = 1; process_launch_or_output_failure = 2; session_failure = 3; analyzer_failure = 4 }
Write-JsonFile -Path $summaryJsonPath -Data $summary
Write-TextFile -Path $summaryTextPath -Text (Build-RunSummaryText -Summary $summary)
Write-Host "AOI load test complete. RunDir=$runDir Mode=$ClientMode"
if (-not $NoAnalyzer -and $analyzerExitCode -ne 0) { exit 4 }
if ($summary.launch_failures -gt 0 -or $summary.missing_result_count -gt 0 -or $summary.process_crash_count -gt 0) { exit 2 }
if ($summary.failure_count -gt 0) { exit 3 }
exit 0





