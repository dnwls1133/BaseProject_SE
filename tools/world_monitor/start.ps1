param(
    [int]$Port = 8765,
    [string]$SnapshotDir = ""
)

$monitorScript = Join-Path $PSScriptRoot "serve.py"
$monitorArguments = @($monitorScript, "--port", "$Port")
if ($SnapshotDir)
{
    $monitorArguments += @("--snapshot-dir", $SnapshotDir)
}

$pythonLauncher = Get-Command py -ErrorAction SilentlyContinue
if ($pythonLauncher)
{
    & $pythonLauncher.Source -3 @monitorArguments
    exit $LASTEXITCODE
}

$bundledPython = Join-Path $env:USERPROFILE ".cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if (Test-Path -LiteralPath $bundledPython)
{
    & $bundledPython @monitorArguments
    exit $LASTEXITCODE
}

$pythonExecutable = Get-Command python -ErrorAction SilentlyContinue
if ($pythonExecutable)
{
    & $pythonExecutable.Source @monitorArguments
    exit $LASTEXITCODE
}

Write-Error "Python 3를 찾지 못했습니다. Python 3 설치 후 serve.py를 실행하세요."
exit 1
