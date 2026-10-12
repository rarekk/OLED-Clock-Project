<#
.SYNOPSIS
    Windows helper for building / flashing the ESP32 OLED clock via Docker.

.DESCRIPTION
    Docker Desktop on Windows cannot pass a COM port into a container, so this
    script uses the container for building and a host-side esptool for flashing.
    If you prefer WSL2, attach the board with usbipd-win and use the Makefile.

.PARAMETER Action
    build | menuconfig | flash | monitor | erase | shell

.PARAMETER Port
    Serial port for flashing, e.g. COM5.

.EXAMPLE
    pwsh scripts/clock.ps1 build
    pwsh scripts/clock.ps1 flash -Port COM5
#>
param(
    [Parameter(Position = 0)]
    [ValidateSet('build', 'menuconfig', 'flash', 'monitor', 'erase', 'shell')]
    [string]$Action = 'build',

    [string]$Port = 'COM5'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$compose = Join-Path $root 'docker/docker-compose.yml'
$idf = @('compose', '-f', $compose, 'run', '--rm', 'idf')

function Invoke-Idf {
    param([string[]]$IdfArgs)
    & docker @idf @IdfArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

switch ($Action) {
    'build'      { Invoke-Idf @('idf.py', 'build') }
    'menuconfig' { Invoke-Idf @('idf.py', 'menuconfig') }
    'shell'      { & docker compose -f $compose run --rm idf bash }
    'monitor'    { Invoke-Idf @('idf.py', '-p', $Port, 'monitor') }
    'erase'      { Invoke-Idf @('idf.py', '-p', $Port, 'erase-flash') }
    'flash' {
        # Build inside the container, then flash from the host.
        Invoke-Idf @('idf.py', 'build')

        Write-Host "Flashing $root\build to $Port from the host..." -ForegroundColor Cyan
        python -m esptool --chip esp32 -p $Port -b 460800 `
            --before default_reset --after hard_reset `
            write_flash --flash_mode dio --flash_freq 40m --flash_size detect `
            0x1000  (Join-Path $root 'build/bootloader/bootloader.bin') `
            0x8000  (Join-Path $root 'build/partition_table/partition-table.bin') `
            0x10000 (Join-Path $root 'build/oled_clock_project.bin')

        Write-Host "Done. Open a monitor with: pwsh scripts/clock.ps1 monitor" -ForegroundColor Green
    }
}
