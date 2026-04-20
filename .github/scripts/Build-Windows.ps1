[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "A 64-bit system is required to build the project."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The obs-studio PowerShell build script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Build {
    trap {
        Pop-Location -Stack BuildTemp -ErrorAction 'SilentlyContinue'
        Write-Error $_
        Log-Group
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."

    # --- Smart Path Detection (v1.4) ---
    $vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        # Using -products * to find BuildTools as well as full VS
        $script:vsPath = & $vswhere -latest -products * -property installationPath
        if ($script:vsPath) {
            Write-Host "  =>   Found Visual Studio: $script:vsPath" -ForegroundColor Green
            $msbuildPath = Get-ChildItem -Path "$script:vsPath" -Recurse -Filter "MSBuild.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($msbuildPath) {
                $msbuildDir = Split-Path -Path $msbuildPath.FullName
                if ($env:PATH -notlike "*$msbuildDir*") { $env:PATH = "$msbuildDir;$env:PATH" }
            }
        }
    }

    $ManualPaths = @(
        "C:\Program Files\CMake\bin",
        "C:\Program Files\PowerShell\7",
        "C:\Qt\6.11.0\msvc2022_64\bin",
        "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Tools\MSVC\*\bin\Hostx64\x64"
    )
    foreach ($Path in $ManualPaths) {
        if ((Test-Path $Path) -and ($env:PATH -notlike "*$Path*")) { 
            $env:PATH = "$Path;$env:PATH" 
        }
    }
    # -----------------------------------

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach($Utility in $UtilityFunctions) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    $BuildSpec = Get-Content -Path "$ProjectRoot/buildspec.json" -Raw | ConvertFrom-Json
    $ProductName = $BuildSpec.name
    $ProductVersion = $BuildSpec.version

    Push-Location -Stack BuildTemp
    Ensure-Location $ProjectRoot

    # --- Clean Slate (v1.5) ---
    $BuildDir = "$ProjectRoot/build_${Target}"
    if (Test-Path $BuildDir) {
        Write-Host "  =>   Checking for running OBS instances..." -ForegroundColor Yellow
        $obsProcess = Get-Process -Name obs64 -ErrorAction SilentlyContinue
        if ($obsProcess) {
            Write-Error "OBS Studio is currently running. Please close OBS before building to release file locks on the plugin DLLs."
            exit 2
        }

        Write-Host "  =>   Cleaning old build files..." -ForegroundColor Yellow
        
        # Retry logic for directory removal (handles transient locks)
        $retryCount = 0
        $maxRetries = 3
        while ($retryCount -lt $maxRetries) {
            try {
                Remove-Item -Recurse -Force $BuildDir
                break
            } catch {
                $retryCount++
                if ($retryCount -eq $maxRetries) {
                    Write-Error "Could not clean build directory after $maxRetries attempts. The directory or a file within it is locked by another process: $($_.Exception.Message)"
                    exit 2
                }
                Write-Warning "Build directory is locked. Retrying in 1 second... (Attempt $retryCount/$maxRetries)"
                Start-Sleep -Seconds 1
            }
        }
    }

    $CmakeArgs = @('--preset', "windows-ci-${Target}")
    # Local developer fix: Disable "Warnings as Errors" so vendor libraries (like whisper.cpp) 
    # which have minor warnings can still be compiled successfully.
    $CmakeArgs += "-DCMAKE_COMPILE_WARNING_AS_ERROR=OFF"
    
    # Force Static libraries: This avoids a known crash in cmake.exe (-1073741819) 
    # during "Auto build dll exports" on some Windows environments.
    $CmakeArgs += @("-DBUILD_SHARED_LIBS=OFF", "-DWHISPER_SHAREDLIB=OFF")

    # Force UI features enabled for local build (overriding CI defaults)
    $CmakeArgs += @("-DENABLE_QT=ON", "-DENABLE_FRONTEND_API=ON")

    if ($script:vsPath) {
        $CmakeArgs += "-DCMAKE_GENERATOR_INSTANCE=$script:vsPath"
        # If it's VS 2019, we must override the 2022 generator in the preset
        if ($script:vsPath -like "*2019*") {
            $CmakeArgs += @("-G", "Visual Studio 16 2019")
        }
    }

    $CmakeBuildArgs = @('--build')
    $CmakeInstallArgs = @()

    if ( $DebugPreference -eq 'Continue' ) {
        $CmakeArgs += ('--debug-output')
        $CmakeBuildArgs += ('--verbose')
        $CmakeInstallArgs += ('--verbose')
    }

    $CmakeBuildArgs += @(
        '--preset', "windows-${Target}"
        '--config', $Configuration
        '--parallel'
        '--', '/consoleLoggerParameters:Summary', '/noLogo'
    )

    $CmakeInstallArgs += @(
        '--install', "build_${Target}"
        '--prefix', "${ProjectRoot}/release/${Configuration}"
        '--config', $Configuration
    )

    Log-Group "Configuring ${ProductName}..."
    Invoke-External cmake @CmakeArgs

    Log-Group "Building ${ProductName}..."
    Invoke-External cmake @CmakeBuildArgs

    Log-Group "Installing ${ProductName}..."
    Invoke-External cmake @CmakeInstallArgs

    Pop-Location -Stack BuildTemp
    Log-Group
}

Build
