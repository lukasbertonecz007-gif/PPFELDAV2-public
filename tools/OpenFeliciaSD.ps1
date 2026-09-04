<#!
Jednoducha sprava SD karty OpenFelicia pres USB/BT Serial.
Pouziti: OpenFeliciaSD.cmd [-Port COM4]
Uzivatel vybira libovolny dostupny seriovy port.
#>
[CmdletBinding()]
param(
  [string]$Port,
  [switch]$Check
)

$ErrorActionPreference = 'Stop'

function Get-Crc32 {
  param([byte[]]$Bytes)
  # PowerShell jinak mezivysledek -bxor prevede na zaporne Int32.
  [uint64]$crc = 4294967295
  [uint64]$mask = 4294967295
  [uint64]$polynomial = 3988292384
  foreach ($byte in $Bytes) {
    $crc = [uint64](($crc -bxor [uint64]$byte) -band $mask)
    for ($bit = 0; $bit -lt 8; $bit++) {
      if (($crc -band 1) -ne 0) {
        $crc = [uint64]((($crc -shr 1) -bxor $polynomial) -band $mask)
      } else {
        $crc = [uint64](($crc -shr 1) -band $mask)
      }
    }
  }
  return [uint32](($crc -bxor $mask) -band $mask)
}

function ConvertTo-Hex {
  param([byte[]]$Bytes)
  return ([BitConverter]::ToString($Bytes)).Replace('-', '')
}

function ConvertFrom-Hex {
  param([string]$Text)
  if ([string]::IsNullOrWhiteSpace($Text) -or ($Text.Length % 2) -ne 0 -or $Text -notmatch '^[0-9A-Fa-f]+$') {
    throw "Neplatna hexadecimalni data SDFS."
  }
  [byte[]]$bytes = New-Object byte[] ($Text.Length / 2)
  for ($i = 0; $i -lt $bytes.Length; $i++) {
    $bytes[$i] = [Convert]::ToByte($Text.Substring($i * 2, 2), 16)
  }
  return $bytes
}

function Read-SdfsLine {
  param([int]$TimeoutMs = 5000)
  $until = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
  while ([DateTime]::UtcNow -lt $until) {
    try {
      $line = $script:Serial.ReadLine().Trim()
      if ($line.StartsWith('SDFS ')) { return $line }
    } catch [System.TimeoutException] {
      continue
    }
  }
  throw "Budik neodpovedel na SDFS do $TimeoutMs ms. Zkontroluj port, kabel/BT a ze je nahrany firmware se spravou SD."
}

function Send-Sdfs {
  param([string]$Command)
  $script:Serial.WriteLine($Command)
}

function Wait-Sdfs {
  param([scriptblock]$Match, [int]$TimeoutMs = 5000)
  $until = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
  while ([DateTime]::UtcNow -lt $until) {
    $left = [Math]::Max(100, [int]($until - [DateTime]::UtcNow).TotalMilliseconds)
    $line = Read-SdfsLine -TimeoutMs $left
    if (& $Match $line) { return $line }
    if ($line -match '^SDFS (ERROR|GET ERROR|PUT ERROR|RM ERROR)') { throw $line }
  }
  throw 'SDFS odpoved nedorazila vcas.'
}

function Get-SdfsInfoAndList {
  Send-Sdfs 'sd.info'
  Write-Host (Wait-Sdfs { param($x) $x -like 'SDFS INFO *' })
  Send-Sdfs 'sd.ls'
  $line = Wait-Sdfs { param($x) $x -eq 'SDFS LS BEGIN' }
  Write-Host $line
  do {
    $line = Read-SdfsLine -TimeoutMs 5000
    Write-Host $line
  } while ($line -ne 'SDFS LS END')
}

function Get-SdfsFile {
  param([string]$Alias, [string]$OutputDirectory)
  Send-Sdfs "sd.get $Alias"
  $begin = Wait-Sdfs { param($x) $x -like 'SDFS GET BEGIN *' }
  $parts = $begin -split ' '
  if ($parts.Count -ne 6 -or $parts[3] -ne $Alias) { throw "Neocekavany zacatek prenosu: $begin" }
  [uint32]$expectedSize = [uint32]$parts[4]
  [uint32]$expectedCrc = [Convert]::ToUInt32($parts[5], 16)
  New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
  $temporary = Join-Path $OutputDirectory "$Alias.part"
  $output = Join-Path $OutputDirectory "$Alias.txt"
  $writer = [System.IO.File]::Open($temporary, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
  try {
    [uint32]$offset = 0
    while ($true) {
      $line = Read-SdfsLine -TimeoutMs 45000
      if ($line -like 'SDFS GET DATA *') {
        $data = $line -split ' '
        if ($data.Count -ne 5) { throw "Neplatny datovy ramec: $line" }
        [uint32]$remoteOffset = [uint32]$data[3]
        if ($remoteOffset -ne $offset) { throw "Chyba poradi dat: cekano $offset, prislo $remoteOffset" }
        [byte[]]$bytes = ConvertFrom-Hex $data[4]
        $writer.Write($bytes, 0, $bytes.Length)
        $offset += [uint32]$bytes.Length
        continue
      }
      if ($line -like 'SDFS GET END *') {
        $end = $line -split ' '
        if ($end.Count -ne 5 -or $end[3] -ne $Alias) { throw "Neplatny konec prenosu: $line" }
        [uint32]$endCrc = [Convert]::ToUInt32($end[4], 16)
        if ($endCrc -ne $expectedCrc -or $offset -ne $expectedSize) { throw 'Nesedi velikost nebo CRC z budiku.' }
        break
      }
      if ($line -match '^SDFS GET ERROR|^SDFS ERROR') { throw $line }
    }
  } finally {
    $writer.Close()
  }
  [byte[]]$received = [System.IO.File]::ReadAllBytes($temporary)
  if ($received.Length -ne $expectedSize -or (Get-Crc32 $received) -ne $expectedCrc) {
    Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
    throw "Kontrolni soucet stazeneho souboru $Alias nesedi; soubor nebyl ponechan."
  }
  Move-Item -LiteralPath $temporary -Destination $output -Force
  Write-Host "Stazeno a overeno: $output"
}

function Get-AllSdfsFiles {
  $folder = Join-Path $PSScriptRoot ("SD-stazeno_vse_" + (Get-Date -Format 'yyyyMMdd_HHmmss'))
  $aliases = @('config', 'configbak', 'consumption', 'consumptionbak', 'service', 'servicebak', 'system', 'error', 'errorbak')
  $downloaded = 0
  foreach ($alias in $aliases) {
    try {
      Get-SdfsFile $alias $folder
      $downloaded++
    } catch {
      if ($_.Exception.Message -like '*FILE_NOT_FOUND*') {
        Write-Host "Soubor $alias na SD neni, preskakuji."
      } else {
        throw
      }
    }
  }
  if ($downloaded -eq 0) { throw 'Na SD nebyl nalezen zadny podporovany soubor.' }
  Write-Host "Hromadne stazeni hotove: $downloaded souboru v $folder" -ForegroundColor Green
}

function Get-UploadBytes {
  param([string]$Path)
  [string]$text = [System.Text.Encoding]::UTF8.GetString([System.IO.File]::ReadAllBytes($Path)).TrimStart([char]0xFEFF)
  [string[]]$filtered = @($text -split "`r?`n" | Where-Object { $_ -notmatch '^\s*crc32\s*=' })
  [string]$normalized = (($filtered -join "`n").TrimEnd([char[]]@([char]13, [char]10))) + "`n"
  return [System.Text.UTF8Encoding]::new($false).GetBytes($normalized)
}

function Send-SdfsFile {
  param([string]$Alias, [string]$Path)
  if ($Alias -notin @('config', 'consumption', 'service')) { throw 'Nahravat lze pouze config, consumption nebo service.' }
  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Soubor neexistuje: $Path" }
  $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
  $backup = "$Path.before-upload_$stamp"
  Copy-Item -LiteralPath $Path -Destination $backup -ErrorAction Stop
  [byte[]]$bytes = Get-UploadBytes $Path
  if ($bytes.Length -eq 0 -or $bytes.Length -gt 16384) { throw 'Po uprave ma soubor neplatnou velikost (1..16384 B).' }
  [uint32]$crc = Get-Crc32 $bytes
  Write-Host "PC zaloha: $backup"
  Send-Sdfs "sd.put $Alias $($bytes.Length) $($crc.ToString('X8')) potvrdit"
  $ready = Wait-Sdfs { param($x) $x -like "SDFS PUT READY $Alias *" }
  Write-Host $ready
  for ([uint32]$offset = 0; $offset -lt $bytes.Length; $offset += 16) {
    $count = [Math]::Min(16, $bytes.Length - $offset)
    [byte[]]$chunk = New-Object byte[] $count
    [Array]::Copy($bytes, [int]$offset, $chunk, 0, $count)
    Send-Sdfs "sd.data $offset $(ConvertTo-Hex $chunk)"
  }
  Send-Sdfs 'sd.end'
  $result = Wait-Sdfs { param($x) $x -like "SDFS PUT OK $Alias RESTART_REQUIRED" } 15000
  Write-Host $result -ForegroundColor Green
  Write-Warning 'RESTART JE NUTNY: do restartu firmware neuklada automaticky, aby import neprepsal. Restart provadej jen kdyz je auto zaparkovane.'
}

function Remove-SdfsLog {
  param([string]$Alias)
  if ($Alias -notin @('error', 'errorbak', 'system')) { throw 'Smazat lze pouze error, errorbak nebo system.' }
  $confirm = Read-Host "Napis SMAZAT pro smazani $Alias"
  if ($confirm -ne 'SMAZAT') { Write-Host 'Zruseno.'; return }
  Send-Sdfs "sd.rm $Alias potvrdit"
  Write-Host (Wait-Sdfs { param($x) $x -like 'SDFS RM OK*' }) -ForegroundColor Yellow
}

function Show-AvailableSerialPorts {
  # Win32_SerialPort muze vynechat FTDI port, i kdyz ho SerialPort i PlatformIO vidi.
  $labels = @{}
  try {
    Get-PnpDevice -Class Ports -PresentOnly | ForEach-Object {
      if ($_.FriendlyName -match '\((COM\d+)\)') {
        $labels[$Matches[1].ToUpperInvariant()] = $_.FriendlyName
      }
    }
  } catch {
    # Popis je jen pomocny; samotny seznam bere .NET primo z dostupnych portu.
  }

  $ports = [System.IO.Ports.SerialPort]::GetPortNames() |
    Sort-Object { [int](($_ -replace '[^0-9]', '')) }
  if (-not $ports) {
    Write-Host '  Zadny seriovy port nebyl nalezen.'
    return
  }
  foreach ($serialPortName in $ports) {
    $description = if ($labels.ContainsKey($serialPortName)) { $labels[$serialPortName] } else { 'seriovy port' }
    Write-Host "  $serialPortName - $description"
  }
}

if ($Check) { Write-Host 'Syntax OpenFeliciaSD.ps1 je v poradku.'; exit 0 }

function Normalize-PortName {
  param([string]$Value)
  if ([string]::IsNullOrWhiteSpace($Value)) { return $null }
  $normalized = $Value.Trim().ToUpperInvariant()
  if ($normalized -match '^\d+$') { $normalized = "COM$normalized" }
  if ($normalized -notmatch '^COM\d+$') { return $null }
  return $normalized
}

function Select-AndOpenSerialPort {
  param([string]$InitialPort)
  $candidate = Normalize-PortName $InitialPort
  if ($InitialPort -and -not $candidate) {
    Write-Host "Neplatny port '$InitialPort'. Napis COM4 nebo jen 4." -ForegroundColor Yellow
  }
  while ($true) {
    if (-not $candidate) {
      Write-Host 'Dostupne seriove porty:'
      Show-AvailableSerialPorts
      $candidate = Normalize-PortName (Read-Host 'Zadej port budiku (napr. COM4 nebo jen 4)')
      if (-not $candidate) {
        Write-Host 'Neplatny zapis portu. Zkus to znovu; program zustava otevreny.' -ForegroundColor Yellow
        continue
      }
    }

    $serial = [System.IO.Ports.SerialPort]::new($candidate, 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $serial.Encoding = [System.Text.Encoding]::ASCII
    $serial.NewLine = "`n"
    $serial.ReadTimeout = 200
    $serial.WriteTimeout = 3000
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    try {
      $serial.Open()
      return $serial
    } catch {
      Write-Host "Port $candidate nelze otevrit: $($_.Exception.Message)" -ForegroundColor Red
      $serial.Dispose()
      $candidate = $null
      [void](Read-Host 'Stiskni Enter a vyber port znovu')
    }
  }
}

$script:Serial = Select-AndOpenSerialPort $Port

try {
  Start-Sleep -Milliseconds 250
  $script:Serial.DiscardInBuffer()
  $script:Serial.DiscardOutBuffer()
  $running = $true
  while ($running) {
    Write-Host ''
    Write-Host '1 - stav a seznam SD'
    Write-Host '2 - stahnout soubor'
    Write-Host '3 - stahnout vsechny podporovane soubory'
    Write-Host '4 - nahrat config/consumption/service'
    Write-Host '5 - smazat log/system'
    Write-Host '6 - restart budiku (pouze zaparkovane auto)'
    Write-Host '0 - konec'
    try {
      switch (Read-Host 'Volba') {
        '1' { Get-SdfsInfoAndList }
        '2' {
          $alias = Read-Host 'Soubor: config, configbak, consumption, consumptionbak, service, servicebak, system, error, errorbak'
          $folder = Join-Path $PSScriptRoot ("SD-stazeno_" + (Get-Date -Format 'yyyyMMdd_HHmmss'))
          Get-SdfsFile $alias $folder
        }
        '3' { Get-AllSdfsFiles }
        '4' {
          $alias = Read-Host 'Cil: config, consumption nebo service'
          $path = Read-Host 'Cesta k souboru z PC'
          Send-SdfsFile $alias $path
        }
        '5' { Remove-SdfsLog (Read-Host 'Smazat: error, errorbak nebo system') }
        '6' {
          $confirm = Read-Host 'Napis RESTART pro restart zaparkovaneho budiku'
          if ($confirm -eq 'RESTART') { Send-Sdfs 'sd.reboot potvrdit'; Write-Host (Wait-Sdfs { param($x) $x -eq 'SDFS REBOOTING' }) }
        }
        '0' { $running = $false }
        default { Write-Host 'Neplatna volba.' -ForegroundColor Yellow }
      }
    } catch {
      Write-Host "CHYBA: $($_.Exception.Message)" -ForegroundColor Red
      [void](Read-Host 'Stiskni Enter pro navrat do menu')
    }
  }
} finally {
  if ($script:Serial -and $script:Serial.IsOpen) { $script:Serial.Close() }
  if ($script:Serial) { $script:Serial.Dispose() }
}

