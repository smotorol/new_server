param(
    [int]$Count = 100,
    [string]$Prefix = 'load',
    [string]$CharacterPrefix = 'lt_char_',
    [string]$Password = 'pw1',
    [int]$WorldIndex = 0,
    [Nullable[int]]$WorldCode = $null,
    [int]$CharacterIndex = 0,
    [string]$PortalRoute = '1,2,3,4',
    [string]$ClientTagPrefix = 'lt',
    [string]$ConfigPath = '',
    [string]$OutputCsv = '',
    [string]$SummaryJsonPath = '',
    [switch]$ValidateOnly,
    [switch]$DryRun,
    [bool]$ResetExistingPasswords = $true,
    [bool]$RepairExistingCharacters = $true
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Data

$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($ConfigPath)) {
    $ConfigPath = Join-Path $root 'config\account_server.ini'
}
if ([string]::IsNullOrWhiteSpace($OutputCsv)) {
    $OutputCsv = Join-Path $root 'config\loadtest_accounts.csv'
}
if ([string]::IsNullOrWhiteSpace($SummaryJsonPath)) {
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($OutputCsv)
    $SummaryJsonPath = Join-Path (Split-Path -Parent $OutputCsv) ($baseName + '.provision_summary.json')
}

function Read-IniFile {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        throw "INI file not found: $Path"
    }

    $ini = @{}
    $section = 'global'
    $ini[$section] = @{}

    foreach ($line in Get-Content $Path) {
        $trimmed = ([string]$line).Trim()
        if ([string]::IsNullOrWhiteSpace($trimmed)) { continue }
        if ($trimmed.StartsWith(';') -or $trimmed.StartsWith('#')) { continue }

        if ($trimmed.StartsWith('[') -and $trimmed.EndsWith(']')) {
            $section = $trimmed.Substring(1, $trimmed.Length - 2)
            if (-not $ini.ContainsKey($section)) {
                $ini[$section] = @{}
            }
            continue
        }

        $eqIndex = $trimmed.IndexOf('=')
        if ($eqIndex -lt 0) { continue }

        $key = $trimmed.Substring(0, $eqIndex).Trim()
        $value = $trimmed.Substring($eqIndex + 1).Trim()
        $ini[$section][$key] = $value
    }

    return $ini
}

function Write-JsonFile {
    param([string]$Path, $Data)

    $dir = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($dir)) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }

    $Data | ConvertTo-Json -Depth 8 | Set-Content -Path $Path -Encoding UTF8
}

function Write-CsvRows {
    param([string]$Path, $Rows)

    $dir = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($dir)) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }

    if ($null -eq $Rows -or @($Rows).Count -eq 0) {
        Set-Content -Path $Path -Value 'LoginId,Password,WorldIndex,CharacterIndex,PortalRoute,ClientTag' -Encoding UTF8
        return
    }

    $Rows | Export-Csv -Path $Path -Encoding UTF8 -NoTypeInformation
}

function New-SqlConnection {
    param(
        [string]$Dsn,
        [string]$UserId,
        [string]$PasswordText
    )

    if ([string]::IsNullOrWhiteSpace($Dsn)) {
        throw 'DSN is empty.'
    }

    $builder = New-Object System.Data.Odbc.OdbcConnectionStringBuilder
    $builder['DSN'] = $Dsn

    if (-not [string]::IsNullOrWhiteSpace($UserId)) {
        $builder['Uid'] = $UserId
    }
    if (-not [string]::IsNullOrWhiteSpace($PasswordText)) {
        $builder['Pwd'] = $PasswordText
    }

    $builder['Connection Timeout'] = 5

    $conn = New-Object System.Data.Odbc.OdbcConnection($builder.ConnectionString)
    $conn.Open()
    return $conn
}

function Convert-SqlTextToOdbc {
    param(
        [string]$Sql,
        [hashtable]$Parameters
    )

    if ($null -eq $Parameters -or $Parameters.Count -eq 0) {
        return [pscustomobject]@{
            Sql = $Sql
            Names = @()
        }
    }

    $names = New-Object System.Collections.Generic.List[string]
    $converted = [System.Text.RegularExpressions.Regex]::Replace(
        $Sql,
        '@[A-Za-z_][A-Za-z0-9_]*',
        {
            param($match)

            $token = $match.Value.Substring(1)
            if ($Parameters.ContainsKey($token)) {
                [void]$names.Add($token)
                return '?'
            }

            if ($Parameters.ContainsKey($match.Value)) {
                [void]$names.Add($match.Value)
                return '?'
            }

            return $match.Value
        }
    )

    return [pscustomobject]@{
        Sql = $converted
        Names = @($names)
    }
}

function Add-SqlParameters {
    param(
        [System.Data.Odbc.OdbcCommand]$Command,
        [string[]]$OrderedParameterNames,
        [hashtable]$Parameters
    )

    if ($null -eq $OrderedParameterNames -or $OrderedParameterNames.Count -eq 0) {
        return
    }

    foreach ($key in $OrderedParameterNames) {
        $value = $null
        if ($Parameters.ContainsKey($key)) {
            $value = $Parameters[$key]
        }
        elseif ($Parameters.ContainsKey('@' + $key)) {
            $value = $Parameters['@' + $key]
        }
        else {
            throw "Missing parameter value for '$key'."
        }

        $p = $Command.CreateParameter()

        if ($null -eq $value) {
            $p.Value = [DBNull]::Value
        }
        elseif ($value -is [bool]) {
            $p.OdbcType = [System.Data.Odbc.OdbcType]::Bit
            $p.Value = $value
        }
        elseif ($value -is [byte]) {
            $p.OdbcType = [System.Data.Odbc.OdbcType]::TinyInt
            $p.Value = [int]$value
        }
        elseif ($value -is [int16]) {
            $p.OdbcType = [System.Data.Odbc.OdbcType]::SmallInt
            $p.Value = $value
        }
        elseif ($value -is [int32]) {
            $p.OdbcType = [System.Data.Odbc.OdbcType]::Int
            $p.Value = $value
        }
        elseif ($value -is [int64]) {
            $p.OdbcType = [System.Data.Odbc.OdbcType]::BigInt
            $p.Value = $value
        }
        elseif ($value -is [datetime]) {
            $p.OdbcType = [System.Data.Odbc.OdbcType]::DateTime
            $p.Value = $value
        }
        elseif ($value -is [string]) {
            $p.OdbcType = [System.Data.Odbc.OdbcType]::NVarChar
            $p.Value = $value
        }
        else {
            $p.Value = $value
        }

        [void]$Command.Parameters.Add($p)
    }
}

function Invoke-SqlScalar {
    param(
        [System.Data.Odbc.OdbcConnection]$Connection,
        [string]$Sql,
        [hashtable]$Parameters
    )

    $parsed = Convert-SqlTextToOdbc -Sql $Sql -Parameters $Parameters
    $command = $Connection.CreateCommand()
    $command.CommandText = $parsed.Sql
    Add-SqlParameters -Command $command -OrderedParameterNames $parsed.Names -Parameters $Parameters
    return $command.ExecuteScalar()
}

function Invoke-SqlNonQuery {
    param(
        [System.Data.Odbc.OdbcConnection]$Connection,
        [string]$Sql,
        [hashtable]$Parameters
    )

    $parsed = Convert-SqlTextToOdbc -Sql $Sql -Parameters $Parameters
    $command = $Connection.CreateCommand()
    $command.CommandText = $parsed.Sql
    Add-SqlParameters -Command $command -OrderedParameterNames $parsed.Names -Parameters $Parameters
    return $command.ExecuteNonQuery()
}

function Invoke-SqlReaderRows {
    param(
        [System.Data.Odbc.OdbcConnection]$Connection,
        [string]$Sql,
        [hashtable]$Parameters
    )

    $parsed = Convert-SqlTextToOdbc -Sql $Sql -Parameters $Parameters
    $command = $Connection.CreateCommand()
    $command.CommandText = $parsed.Sql
    Add-SqlParameters -Command $command -OrderedParameterNames $parsed.Names -Parameters $Parameters

    $reader = $command.ExecuteReader()
    try {
        $rows = @()
        while ($reader.Read()) {
            $row = [ordered]@{}
            for ($i = 0; $i -lt $reader.FieldCount; $i++) {
                $name = $reader.GetName($i)
                $row[$name] = if ($reader.IsDBNull($i)) { $null } else { $reader.GetValue($i) }
            }
            $rows += [pscustomobject]$row
        }
        return $rows
    }
    finally {
        $reader.Close()
    }
}

function New-RandomBytes {
    param([int]$Count)

    $bytes = New-Object byte[] $Count
    $rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
    try {
        $rng.GetBytes($bytes)
    }
    finally {
        $rng.Dispose()
    }
    return $bytes
}

function Convert-BytesToHex {
    param([byte[]]$Bytes)

    if ($null -eq $Bytes) {
        return ''
    }

    return ([System.BitConverter]::ToString($Bytes)).Replace('-', '')
}

function Get-Sha256SaltPasswordBytes {
    param(
        [byte[]]$SaltBytes,
        [string]$PlainPassword
    )

    $encoding = [System.Text.Encoding]::UTF8
    $passwordBytes = $encoding.GetBytes($PlainPassword)
    $buffer = New-Object byte[] ($SaltBytes.Length + $passwordBytes.Length)

    [Array]::Copy($SaltBytes, 0, $buffer, 0, $SaltBytes.Length)
    [Array]::Copy($passwordBytes, 0, $buffer, $SaltBytes.Length, $passwordBytes.Length)

    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return $sha.ComputeHash($buffer)
    }
    finally {
        $sha.Dispose()
    }
}

function Format-LoadIndexValue {
    param([int]$Index)
    return ('{0:D5}' -f $Index)
}

function Resolve-AccountLoginId {
    param([int]$Index)
    return ($Prefix + (Format-LoadIndexValue -Index $Index))
}

function Resolve-CharacterBaseName {
    param([int]$Index)
    return ($CharacterPrefix + (Format-LoadIndexValue -Index $Index))
}

function Resolve-ClientTag {
    param([int]$Index)
    return ('{0}-{1}' -f $ClientTagPrefix, (Format-LoadIndexValue -Index $Index))
}

function Resolve-WorldCodeFromConfig {
    param($IniData, [int]$Index)

    if ($WorldCode.HasValue) {
        return $WorldCode.Value
    }

    if (-not $IniData.ContainsKey('World')) {
        throw 'account_server.ini is missing [World] section.'
    }

    $worldSection = $IniData['World']
    $key = 'WorldID' + $Index
    if (-not $worldSection.ContainsKey($key)) {
        throw "account_server.ini is missing [World]::$key"
    }

    return [int]$worldSection[$key]
}

function Resolve-GameDsnName {
    param($IniData, [int]$Index, [string]$DefaultDsn)

    if (-not $IniData.ContainsKey('World')) {
        throw 'account_server.ini is missing [World] section.'
    }

    $worldSection = $IniData['World']
    $key = 'DBName' + $Index

    if ($worldSection.ContainsKey($key)) {
        return [string]$worldSection[$key]
    }

    return $DefaultDsn
}

function Get-AccountRow {
    param(
        [System.Data.Odbc.OdbcConnection]$Connection,
        [string]$LoginId
    )

    $rows = Invoke-SqlReaderRows -Connection $Connection -Sql @"
SELECT TOP (1)
    [account_id],
    [login_id],
    [account_state],
    [is_deleted],
    [password_hash],
    [password_salt]
FROM [auth].[account]
WHERE [login_id] = @login_id
ORDER BY [account_id] DESC
"@ -Parameters @{ login_id = $LoginId }

    if ($rows.Count -gt 0) {
        return $rows[0]
    }

    return $null
}

function Test-AccountPasswordMatch {
    param(
        $AccountRow,
        [string]$PlainPassword
    )

    if ($null -eq $AccountRow) { return $false }
    if ($null -eq $AccountRow.password_hash -or $null -eq $AccountRow.password_salt) { return $false }

    $storedHash = [byte[]]$AccountRow.password_hash
    $storedSalt = [byte[]]$AccountRow.password_salt

    if ($storedHash.Length -eq 0) { return $false }

    $saltPasswordHash = Get-Sha256SaltPasswordBytes -SaltBytes $storedSalt -PlainPassword $PlainPassword
    return [System.Linq.Enumerable]::SequenceEqual($saltPasswordHash, $storedHash)
}

function Ensure-Account {
    param(
        [System.Data.Odbc.OdbcConnection]$Connection,
        [string]$LoginId,
        [string]$PlainPassword,
        [bool]$AllowPasswordReset,
        [bool]$WhatIfMode
    )

    if ($WhatIfMode) {
        $existingId = Invoke-SqlScalar -Connection $Connection -Sql @"
SELECT TOP (1) CAST([account_id] AS BIGINT)
FROM [auth].[account]
WHERE [login_id] = @login_id
ORDER BY [account_id] DESC
"@ -Parameters @{
            login_id = $LoginId
        }

        if ($null -ne $existingId -and $existingId -ne [DBNull]::Value) {
            return [pscustomobject]@{
                action = 'reused'
                repaired = $false
                account_id = [int64]$existingId
            }
        }

        return [pscustomobject]@{
            action = 'create'
            repaired = $false
            account_id = $null
        }
    }

    $salt = New-RandomBytes -Count 16
    $hash = Get-Sha256SaltPasswordBytes -SaltBytes $salt -PlainPassword $PlainPassword
    $saltHex = Convert-BytesToHex -Bytes $salt
    $hashHex = Convert-BytesToHex -Bytes $hash

    $resultRows = Invoke-SqlReaderRows -Connection $Connection -Sql @"
SET NOCOUNT ON;

DECLARE @existing_account_id BIGINT;

SELECT TOP (1)
    @existing_account_id = CAST([account_id] AS BIGINT)
FROM [auth].[account]
WHERE [login_id] = @login_id
ORDER BY [account_id] DESC;

IF @existing_account_id IS NOT NULL
BEGIN
    SELECT
        CAST(@existing_account_id AS BIGINT) AS account_id,
        CAST(N'reused' AS NVARCHAR(20)) AS action;
    RETURN;
END

BEGIN TRY
    INSERT INTO [auth].[account]
    (
        [login_id],
        [password_hash],
        [password_salt],
        [account_state],
        [is_deleted]
    )
    VALUES
    (
        @login_id,
        CONVERT(VARBINARY(32), @password_hash_hex, 2),
        CONVERT(VARBINARY(16), @password_salt_hex, 2),
        1,
        0
    );

    SELECT
        CAST(SCOPE_IDENTITY() AS BIGINT) AS account_id,
        CAST(N'created' AS NVARCHAR(20)) AS action;
END TRY
BEGIN CATCH
    IF ERROR_NUMBER() IN (2601, 2627)
    BEGIN
        SELECT TOP (1)
            CAST([account_id] AS BIGINT) AS account_id,
            CAST(N'reused' AS NVARCHAR(20)) AS action
        FROM [auth].[account]
        WHERE [login_id] = @login_id
        ORDER BY [account_id] DESC;
    END
    ELSE
    BEGIN
        THROW;
    END
END CATCH
"@ -Parameters @{
        login_id          = $LoginId
        password_hash_hex = $hashHex
        password_salt_hex = $saltHex
    }

    if ($null -eq $resultRows -or $resultRows.Count -eq 0) {
        throw "Ensure-Account did not return account row for login_id='$LoginId'."
    }

    $result = $resultRows[0]
    $action = [string]$result.action
    $accountId = [int64]$result.account_id

    if ($action -eq 'reused') {
        $row = Get-AccountRow -Connection $Connection -LoginId $LoginId
        $needsRepair = $false
        $passwordMismatch = $false
        $didRepair = $false

        if ($null -ne $row) {
            $needsRepair = ([int]$row.account_state -ne 1) -or ([bool]$row.is_deleted)
            $passwordMismatch = -not (Test-AccountPasswordMatch -AccountRow $row -PlainPassword $PlainPassword)
        }

        if ($null -ne $row -and ($needsRepair -or ($AllowPasswordReset -and $passwordMismatch))) {
            $repairSalt = New-RandomBytes -Count 16
            $repairHash = Get-Sha256SaltPasswordBytes -SaltBytes $repairSalt -PlainPassword $PlainPassword
            $repairSaltHex = Convert-BytesToHex -Bytes $repairSalt
            $repairHashHex = Convert-BytesToHex -Bytes $repairHash

            $updated = Invoke-SqlNonQuery -Connection $Connection -Sql @"
UPDATE [auth].[account]
SET [password_hash] = CONVERT(VARBINARY(32), @password_hash_hex, 2),
    [password_salt] = CONVERT(VARBINARY(16), @password_salt_hex, 2),
    [account_state] = 1,
    [is_deleted] = 0,
    [updated_at_utc] = SYSUTCDATETIME()
WHERE [account_id] = @account_id
"@ -Parameters @{
                account_id        = $accountId
                password_hash_hex = $repairHashHex
                password_salt_hex = $repairSaltHex
            }

            $didRepair = ($updated -gt 0)
        }

        return [pscustomobject]@{
            action = $(if ($didRepair) { 'repaired' } else { 'reused' })
            repaired = $didRepair
            account_id = $accountId
        }
    }

    return [pscustomobject]@{
        action = 'created'
        repaired = $false
        account_id = $accountId
    }
}
function Get-LiveCharacters {
    param(
        [System.Data.Odbc.OdbcConnection]$Connection,
        [int64]$AccountId,
        [int]$ResolvedWorldCode
    )

    return Invoke-SqlReaderRows -Connection $Connection -Sql @"
SELECT
    [char_id],
    [char_name],
    [slot_no],
    [char_state],
    [level],
    [gold],
    [zone_id],
    [hp],
    [mp]
FROM [game].[character]
WHERE [account_id] = @account_id
  AND [world_code] = @world_code
  AND [char_state] = 1
ORDER BY [slot_no] ASC, [char_id] ASC
"@ -Parameters @{
        account_id = $AccountId
        world_code = $ResolvedWorldCode
    }
}

function Get-UsedSlots {
    param(
        [System.Data.Odbc.OdbcConnection]$Connection,
        [int64]$AccountId,
        [int]$ResolvedWorldCode
    )

    $rows = Invoke-SqlReaderRows -Connection $Connection -Sql @"
SELECT [slot_no]
FROM [game].[character]
WHERE [account_id] = @account_id
  AND [world_code] = @world_code
  AND [char_state] <> 9
ORDER BY [slot_no] ASC
"@ -Parameters @{
        account_id = $AccountId
        world_code = $ResolvedWorldCode
    }

    return @($rows | ForEach-Object { [int]$_.slot_no })
}

function Resolve-AvailableSlot {
    param([int[]]$UsedSlots)

    for ($slot = 0; $slot -lt 32; $slot++) {
        if ($UsedSlots -notcontains $slot) {
            return $slot
        }
    }

    throw 'No free character slots remain for this account/world.'
}

function Test-CharacterNameInUse {
    param(
        [System.Data.Odbc.OdbcConnection]$Connection,
        [int]$ResolvedWorldCode,
        [string]$CharacterName
    )

    $value = Invoke-SqlScalar -Connection $Connection -Sql @"
SELECT COUNT(1)
FROM [game].[character]
WHERE [world_code] = @world_code
  AND [char_name] = @char_name
  AND [char_state] <> 9
"@ -Parameters @{
        world_code = $ResolvedWorldCode
        char_name  = $CharacterName
    }

    return ([int]$value) -gt 0
}

function Resolve-AvailableCharacterName {
    param(
        [System.Data.Odbc.OdbcConnection]$Connection,
        [int]$ResolvedWorldCode,
        [string]$BaseName
    )

    if (-not (Test-CharacterNameInUse -Connection $Connection -ResolvedWorldCode $ResolvedWorldCode -CharacterName $BaseName)) {
        return $BaseName
    }

    for ($suffix = 1; $suffix -le 999; $suffix++) {
        $tail = '_' + $suffix.ToString()
        $maxBaseLength = [Math]::Max(1, 20 - $tail.Length)
        $candidate = $BaseName.Substring(0, [Math]::Min($BaseName.Length, $maxBaseLength)) + $tail

        if (-not (Test-CharacterNameInUse -Connection $Connection -ResolvedWorldCode $ResolvedWorldCode -CharacterName $candidate)) {
            return $candidate
        }
    }

    throw "Could not find a unique character name for base '$BaseName'."
}

function Ensure-CharacterWallet {
    param(
        [System.Data.Odbc.OdbcConnection]$Connection,
        [int64]$CharId,
        [int64]$Gold,
        [bool]$WhatIfMode
    )

    $exists = [int](Invoke-SqlScalar -Connection $Connection -Sql @"
SELECT COUNT(1)
FROM [game].[character_wallet]
WHERE [char_id] = @char_id
"@ -Parameters @{ char_id = $CharId })

    if ($exists -gt 0) {
        return $false
    }

    if ($WhatIfMode) {
        return $true
    }

    $null = Invoke-SqlNonQuery -Connection $Connection -Sql @"
INSERT INTO [game].[character_wallet]
(
    [char_id], [gold], [cash_free], [cash_paid], [mileage]
)
VALUES
(
    @char_id, @gold, 0, 0, 0
)
"@ -Parameters @{
        char_id = $CharId
        gold    = [Math]::Max(0, [int64]$Gold)
    }

    return $true
}

function Repair-CharacterDefaults {
    param(
        [System.Data.Odbc.OdbcConnection]$Connection,
        [int64]$CharId,
        [bool]$WhatIfMode
    )

    $needsRepair = Invoke-SqlScalar -Connection $Connection -Sql @"
SELECT COUNT(1)
FROM [game].[character]
WHERE [char_id] = @char_id
  AND (
        [level] < 1
     OR [gold] < 1000
     OR [zone_id] < 1
     OR [hp] < 100
     OR [mp] < 100
  )
"@ -Parameters @{ char_id = $CharId }

    if ([int]$needsRepair -eq 0) {
        return $false
    }

    if ($WhatIfMode) {
        return $true
    }

    $null = Invoke-SqlNonQuery -Connection $Connection -Sql @"
UPDATE [game].[character]
SET [level] = CASE WHEN [level] < 1 THEN 1 ELSE [level] END,
    [gold] = CASE WHEN [gold] < 1000 THEN 1000 ELSE [gold] END,
    [zone_id] = CASE WHEN [zone_id] < 1 THEN 1 ELSE [zone_id] END,
    [hp] = CASE WHEN [hp] < 100 THEN 100 ELSE [hp] END,
    [mp] = CASE WHEN [mp] < 100 THEN 100 ELSE [mp] END,
    [updated_at_utc] = SYSUTCDATETIME()
WHERE [char_id] = @char_id
"@ -Parameters @{ char_id = $CharId }

    return $true
}

function Create-CharacterViaProcedure {
    param(
        [System.Data.Odbc.OdbcConnection]$Connection,
        [int64]$AccountId,
        [int]$ResolvedWorldCode,
        [string]$CharacterName,
        [int]$SlotNo,
        [bool]$WhatIfMode
    )

    if ($WhatIfMode) {
        return $null
    }

    $safeCharName = $CharacterName.Replace("'", "''")

    $sql = @"
DECLARE @out_char_id BIGINT;
EXEC [game].[usp_create_character]
    @account_id = $AccountId,
    @world_code = $ResolvedWorldCode,
    @char_name = N'$safeCharName',
    @slot_no = $SlotNo,
    @char_id = @out_char_id OUTPUT;
SELECT @out_char_id;
"@

    $command = $Connection.CreateCommand()
    $command.CommandText = $sql
    $result = $command.ExecuteScalar()

    if ($null -eq $result -or $result -eq [DBNull]::Value) {
        throw 'usp_create_character did not return char_id.'
    }

    return [int64]$result
}

function Ensure-Character {
    param(
        [System.Data.Odbc.OdbcConnection]$Connection,
        [int64]$AccountId,
        [int]$ResolvedWorldCode,
        [string]$CharacterBaseName,
        [bool]$WhatIfMode,
        [bool]$AllowRepair
    )

    $liveCharacters = @(Get-LiveCharacters -Connection $Connection -AccountId $AccountId -ResolvedWorldCode $ResolvedWorldCode)
    if ($liveCharacters.Count -gt 0) {
        $selected = $liveCharacters[0]
        $repaired = $false

        if ($AllowRepair) {
            if (Repair-CharacterDefaults -Connection $Connection -CharId ([int64]$selected.char_id) -WhatIfMode $WhatIfMode) {
                $repaired = $true
            }
            if (Ensure-CharacterWallet -Connection $Connection -CharId ([int64]$selected.char_id) -Gold ([int64]$selected.gold) -WhatIfMode $WhatIfMode) {
                $repaired = $true
            }
        }

        $action = 'reused'
        if ($repaired) {
            if ($WhatIfMode) {
                $action = 'repair'
            }
            else {
                $action = 'repaired'
            }
        }

        return [pscustomobject]@{
            action = $action
            repaired = $repaired
            char_id = [int64]$selected.char_id
            char_name = [string]$selected.char_name
            slot_no = [int]$selected.slot_no
        }
    }

    $usedSlots = @(Get-UsedSlots -Connection $Connection -AccountId $AccountId -ResolvedWorldCode $ResolvedWorldCode)
    $slotNo = Resolve-AvailableSlot -UsedSlots $usedSlots
    $resolvedName = Resolve-AvailableCharacterName -Connection $Connection -ResolvedWorldCode $ResolvedWorldCode -BaseName $CharacterBaseName
    $charId = Create-CharacterViaProcedure -Connection $Connection -AccountId $AccountId -ResolvedWorldCode $ResolvedWorldCode -CharacterName $resolvedName -SlotNo $slotNo -WhatIfMode $WhatIfMode

    if (-not $WhatIfMode) {
        $null = Invoke-SqlNonQuery -Connection $Connection -Sql @"
UPDATE [game].[character]
SET [gold] = CASE WHEN [gold] < 1000 THEN 1000 ELSE [gold] END,
    [zone_id] = CASE WHEN [zone_id] < 1 THEN 1 ELSE [zone_id] END,
    [hp] = CASE WHEN [hp] < 100 THEN 100 ELSE [hp] END,
    [mp] = CASE WHEN [mp] < 100 THEN 100 ELSE [mp] END,
    [updated_at_utc] = SYSUTCDATETIME()
WHERE [char_id] = @char_id
"@ -Parameters @{ char_id = $charId }

        $null = Ensure-CharacterWallet -Connection $Connection -CharId $charId -Gold 1000 -WhatIfMode $false
    }

    $action = 'created'
    if ($WhatIfMode) {
        $action = 'create'
    }

    return [pscustomobject]@{
        action = $action
        repaired = $false
        char_id = $charId
        char_name = $resolvedName
        slot_no = $slotNo
    }
}

function Build-CsvRow {
    param(
        [int]$Index,
        [string]$LoginId,
        [string]$PlainPassword
    )

    return [pscustomobject]@{
        LoginId = $LoginId
        Password = $PlainPassword
        WorldIndex = $WorldIndex
        CharacterIndex = $CharacterIndex
        PortalRoute = $PortalRoute
        ClientTag = (Resolve-ClientTag -Index $Index)
    }
}

$ini = Read-IniFile -Path $ConfigPath
if (-not $ini.ContainsKey('Database')) {
    throw 'account_server.ini is missing [Database] section.'
}

$databaseSection = $ini['Database']
$authDsnName = [string]$databaseSection['DNS']
$authUserId = [string]$databaseSection['ID']
$authPassword = [string]$databaseSection['PW']

$resolvedWorldCode = Resolve-WorldCodeFromConfig -IniData $ini -Index $WorldIndex
$gameDsnName = Resolve-GameDsnName -IniData $ini -Index $WorldIndex -DefaultDsn $authDsnName
$gameUserId = if ($ini['World'].ContainsKey('DB_ID')) { [string]$ini['World']['DB_ID'] } else { $authUserId }
$gamePassword = if ($ini['World'].ContainsKey('DB_PW')) { [string]$ini['World']['DB_PW'] } else { $authPassword }

$summary = [ordered]@{
    started_at_utc = (Get-Date).ToUniversalTime().ToString('o')
    config_path = $ConfigPath
    output_csv = $OutputCsv
    summary_json = $SummaryJsonPath
    dry_run = [bool]$DryRun
    validate_only = [bool]$ValidateOnly
    auth_dsn = $authDsnName
    game_dsn = $gameDsnName
    world_index = $WorldIndex
    world_code = $resolvedWorldCode
    requested = $Count
    created_accounts = 0
    reused_accounts = 0
    repaired_accounts = 0
    created_characters = 0
    reused_characters = 0
    repaired_characters = 0
    failed = 0
    rows_exported = 0
    failures = @()
    rows = @()
}

$authConn = $null
$gameConn = $null

try {
    $authConn = New-SqlConnection -Dsn $authDsnName -UserId $authUserId -PasswordText $authPassword
    $gameConn = New-SqlConnection -Dsn $gameDsnName -UserId $gameUserId -PasswordText $gamePassword

    for ($index = 1; $index -le $Count; $index++) {
        $loginId = Resolve-AccountLoginId -Index $index
        $characterBaseName = Resolve-CharacterBaseName -Index $index

        $rowStatus = [ordered]@{
            index = $index
            login_id = $loginId
            requested_character_base_name = $characterBaseName
            account_action = ''
            character_action = ''
            account_id = $null
            char_id = $null
            char_name = ''
            slot_no = $null
            success = $false
            failure_reason = ''
        }

        try {
            $existingAccount = Get-AccountRow -Connection $authConn -LoginId $loginId

            if ($ValidateOnly) {
                if ($null -eq $existingAccount) {
                    throw 'account_not_found'
                }
                if ([int]$existingAccount.account_state -ne 1 -or [bool]$existingAccount.is_deleted) {
                    throw 'account_not_active'
                }
                if (-not (Test-AccountPasswordMatch -AccountRow $existingAccount -PlainPassword $Password)) {
                    throw 'password_mismatch'
                }

                $rowStatus.account_action = 'validated'
                $rowStatus.account_id = [int64]$existingAccount.account_id
                $summary.reused_accounts++

                $charRows = @(Get-LiveCharacters -Connection $gameConn -AccountId ([int64]$rowStatus.account_id) -ResolvedWorldCode $resolvedWorldCode)
                if ($charRows.Count -eq 0) {
                    throw 'live_character_not_found'
                }

                $selected = $charRows[0]
                $rowStatus.character_action = 'validated'
                $rowStatus.char_id = [int64]$selected.char_id
                $rowStatus.char_name = [string]$selected.char_name
                $rowStatus.slot_no = [int]$selected.slot_no
                $summary.reused_characters++
            }
            else {
                if ($DryRun -and $null -eq $existingAccount) {
                    $rowStatus.account_action = 'create'
                    $rowStatus.character_action = 'create'
                    $rowStatus.char_name = Resolve-AvailableCharacterName -Connection $gameConn -ResolvedWorldCode $resolvedWorldCode -BaseName $characterBaseName
                    $rowStatus.slot_no = 0
                    $summary.created_accounts++
                    $summary.created_characters++
                }
                else {
                    $accountResult = Ensure-Account -Connection $authConn -LoginId $loginId -PlainPassword $Password -AllowPasswordReset $ResetExistingPasswords -WhatIfMode:$DryRun
                    $rowStatus.account_action = $accountResult.action
                    $rowStatus.account_id = $accountResult.account_id

                    switch ($accountResult.action) {
                        'created'  { $summary.created_accounts++ }
                        'reused'   { $summary.reused_accounts++ }
                        'repaired' { $summary.repaired_accounts++ }
                        'repair'   { $summary.repaired_accounts++ }
                        'create'   { $summary.created_accounts++ }
                    }

                    $characterResult = Ensure-Character -Connection $gameConn -AccountId ([int64]$rowStatus.account_id) -ResolvedWorldCode $resolvedWorldCode -CharacterBaseName $characterBaseName -WhatIfMode:$DryRun -AllowRepair:$RepairExistingCharacters
                    $rowStatus.character_action = $characterResult.action
                    $rowStatus.char_id = $characterResult.char_id
                    $rowStatus.char_name = $characterResult.char_name
                    $rowStatus.slot_no = $characterResult.slot_no

                    switch ($characterResult.action) {
                        'created'  { $summary.created_characters++ }
                        'reused'   { $summary.reused_characters++ }
                        'repaired' { $summary.repaired_characters++ }
                        'repair'   { $summary.repaired_characters++ }
                        'create'   { $summary.created_characters++ }
                    }
                }
            }

            $rowStatus.success = $true
            $summary.rows += [pscustomobject]$rowStatus
            $summary.rows_exported++
        }
        catch {
            $summary.failed++
            $rowStatus.failure_reason = $_.Exception.Message
            $summary.failures += [pscustomobject]@{
                index = $index
                login_id = $loginId
                reason = $_.Exception.Message
            }
            $summary.rows += [pscustomobject]$rowStatus
        }
    }
}
catch {
    $summary.failed++
    $summary.failures += [pscustomobject]@{
        index = 0
        login_id = ''
        reason = $_.Exception.Message
    }
}
finally {
    if ($null -ne $authConn) { $authConn.Dispose() }
    if ($null -ne $gameConn) { $gameConn.Dispose() }
}

$exportRows = @(
    $summary.rows |
    Where-Object { $_.success } |
    Sort-Object index |
    ForEach-Object {
        Build-CsvRow -Index ([int]$_.index) -LoginId ([string]$_.login_id) -PlainPassword $Password
    }
)

Write-CsvRows -Path $OutputCsv -Rows $exportRows
$summary.rows_exported = $exportRows.Count
$summary.finished_at_utc = (Get-Date).ToUniversalTime().ToString('o')
Write-JsonFile -Path $SummaryJsonPath -Data $summary

Write-Host ("AOI provisioning complete. requested={0} created_accounts={1} reused_accounts={2} repaired_accounts={3} created_characters={4} reused_characters={5} repaired_characters={6} failed={7} output={8}" -f $summary.requested, $summary.created_accounts, $summary.reused_accounts, $summary.repaired_accounts, $summary.created_characters, $summary.reused_characters, $summary.repaired_characters, $summary.failed, $OutputCsv)

if ($summary.failed -gt 0) {
    exit 2
}

exit 0
