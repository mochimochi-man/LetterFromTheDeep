param([switch]$Upload, [string]$Port = "COM21")
$ErrorActionPreference = "Stop"
$abyssProject = Split-Path -Parent $PSScriptRoot
$abyssCli = Join-Path $env:LOCALAPPDATA "Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
if (!(Test-Path -LiteralPath $abyssCli)) { $abyssCli = (Get-Command arduino-cli -ErrorAction Stop).Source }
# The Windows Xtensa linker needs an ASCII output path in this environment.
$abyssBuild = Join-Path $env:TEMP "abyssal-life-build"
$abyssFqbn = "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=custom,CPUFreq=240,CDCOnBoot=default,USBMode=hwcdc"
& $abyssCli --config-file (Join-Path $abyssProject "arduino-cli.yaml") compile --fqbn $abyssFqbn --build-path $abyssBuild --warnings all $abyssProject
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
if ($Upload) {
    # arduino-cli's own upload gives up part way through an image this size and leaves the
    # board unbootable, so the flash is written directly at a calmer rate.
    $abyssEsptool = Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA "Arduino15\packages\esp32\tools\esptool_py") -Filter esptool.exe -Recurse |
        Sort-Object FullName | Select-Object -Last 1
    if (!$abyssEsptool) { throw "esptool.exe not found" }
    $abyssBoot = Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA "Arduino15\packages\esp32\hardware\esp32") -Filter boot_app0.bin -Recurse |
        Sort-Object FullName | Select-Object -Last 1
    if (!$abyssBoot) { throw "boot_app0.bin not found" }
    $abyssName = Split-Path -Leaf $abyssProject
    & $abyssEsptool.FullName --chip esp32s3 --port $Port --baud 460800 --before default_reset --after hard_reset `
        write_flash -z --flash_mode dio --flash_freq 80m --flash_size 16MB `
        0x0 (Join-Path $abyssBuild "$abyssName.ino.bootloader.bin") `
        0x8000 (Join-Path $abyssBuild "$abyssName.ino.partitions.bin") `
        0xe000 $abyssBoot.FullName `
        0x10000 (Join-Path $abyssBuild "$abyssName.ino.bin")
    if ($LASTEXITCODE -ne 0) { throw "Upload failed ($LASTEXITCODE)" }
}
