[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo',
    [switch] $Package
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Package-Windows.ps1 requires CI environment"
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "Packaging script requires a 64-bit system to build and run."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The packaging script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Package {
    trap {
        Write-Error $_
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."
    $BuildSpecFile = "${ProjectRoot}/buildspec.json"

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach( $Utility in $UtilityFunctions ) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    $BuildSpec = Get-Content -Path ${BuildSpecFile} -Raw | ConvertFrom-Json
    $ProductName = $BuildSpec.name
    $ProductVersion = $BuildSpec.version

    $OutputName = "${ProductName}-${ProductVersion}-windows-${Target}"

    $RemoveArgs = @{
        ErrorAction = 'SilentlyContinue'
        Path = @(
            "${ProjectRoot}/release/${ProductName}-*-windows-*.zip"
            "${ProjectRoot}/release/${ProductName}-*-windows-*.exe"
        )
    }

    Remove-Item @RemoveArgs

    Log-Group "Archiving ${ProductName}..."
    $CompressArgs = @{
        Path = (Get-ChildItem -Path "${ProjectRoot}/release/${Configuration}" -Exclude "${OutputName}*.*")
        CompressionLevel = 'Optimal'
        DestinationPath = "${ProjectRoot}/release/${OutputName}.zip"
        Verbose = ($Env:CI -ne $null)
    }
    Compress-Archive -Force @CompressArgs

    if ($Package) {
        Log-Group "Building NSIS installer..."
        $BuildDir = "${ProjectRoot}/build_${Target}"
        try {
            Invoke-External cmake --build $BuildDir --config $Configuration --target package
        } catch {
            $NsisLog = Get-ChildItem -Path "${BuildDir}/_CPack_Packages" -Recurse -Filter "NSISOutput.log" -ErrorAction SilentlyContinue |
                Sort-Object LastWriteTime -Descending |
                Select-Object -First 1

            if ($NsisLog) {
                Log-Group "NSIS output log"
                Get-Content -Path $NsisLog.FullName
            }

            throw
        }

        Log-Group "Publishing installer .exe..."
        $InstallerExe = Get-ChildItem -Path $BuildDir -Recurse -Filter "${ProductName}-${ProductVersion}-windows-${Target}*.exe" -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1

        if ($InstallerExe) {
            $ExeOutputName = "${ProductName}-${ProductVersion}-windows-${Target}-Installer.exe"
            Copy-Item -Path $InstallerExe.FullName -Destination "${ProjectRoot}/release/${ExeOutputName}" -Force
            Write-Host "  => Installer .exe packaged: ${ExeOutputName}" -ForegroundColor Green
        } else {
            Write-Warning "NSIS installer .exe not found in build directory. Skipping .exe publishing."
        }
    } else {
        Write-Host "  => Installer packaging disabled. ZIP artifact only." -ForegroundColor Yellow
    }

    Log-Group
}

Package
