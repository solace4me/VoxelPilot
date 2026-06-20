param(
    [string]$CudaArch
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

if (-not $CudaArch) {
    try {
        $capability = (& nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2>$null | Select-Object -First 1).Trim()
        if ($capability -match '^\d+\.\d+$') {
            $CudaArch = "sm_$($capability.Replace('.', ''))"
        }
    } catch {
        $CudaArch = $null
    }
}

$buildArgs = @()
if ($CudaArch) {
    $buildArgs += $CudaArch
}

& "$root\build_windows.bat" @buildArgs
exit $LASTEXITCODE
