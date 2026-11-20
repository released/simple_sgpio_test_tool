param([Parameter(Mandatory=$true)][string]$HubRoot,
      [ValidateSet('Release','Debug')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
. (Join-Path $HubRoot 'scripts/invoke_hub_test.ps1')
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$result=Invoke-HubOwnedTest -ProjectRoot $projectRoot -Purpose 'SGPIO synthetic contract, worker, overlapped pipe and MFC lifetime tests' -MaxBytes 100MB -Action {
    param($run)
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs=& $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $vcvars=Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
    $shared=@('src/core/bridge_service.cpp','src/core/sgpio_model.cpp','src/hid/hid_bridge_client.cpp','src/hid/overlapped_io.cpp','src/hid/hid_scan.cpp')
    $ui=@('src/main_frame.cpp','src/main_frame_bridge.cpp','src/config/ini_manager.cpp','src/core/app_state.cpp','src/core/pin_usage_registry.cpp','src/core/text_utils.cpp','src/log/logger.cpp','src/ui/sgpio_tab.cpp','src/ui/sgpio_monitor_tab.cpp')
    $commands=@('@echo off',('call "'+$vcvars+'" >nul'),('cd /d "'+$run.Work+'"'),
                ('set "TEMP='+$run.Work+'"'),('set "TMP='+$run.Work+'"'))
    $flags=if($Configuration -eq 'Debug'){'/MTd /Od /D_DEBUG'}else{'/MT /O2 /DNDEBUG'}
    foreach($test in @('bridge_contract_tests','bridge_ui_tests')) {
        $sources=@($shared)+@('tests/'+$test+'.cpp')
        if($test -eq 'bridge_ui_tests') {$sources+=@($ui)}
        $arguments=($sources | ForEach-Object {'"'+(Join-Path $projectRoot $_)+'"'}) -join ' '
        $commands+=('cl /nologo /std:c++17 /EHsc /W4 /WX '+$flags+' /D_UNICODE /DUNICODE /D_AFX_NO_MFC_CONTROLS_IN_DIALOGS /Fe:'+ $test+'.exe '+$arguments+' /link /SUBSYSTEM:CONSOLE setupapi.lib hid.lib cfgmgr32.lib')
        $commands+='if errorlevel 1 exit /b %errorlevel%'
        $commands+=($test+'.exe')
        $commands+='if errorlevel 1 exit /b %errorlevel%'
    }
    $commands+='exit /b 0'
    $batch=Join-Path $run.Work 'compile-tests.cmd'
    [IO.File]::WriteAllLines($batch,$commands,[Text.Encoding]::Default)
    Register-HubArtifact -Run $run -Path $batch -Producer 'test compiler command generator'
    $log=Join-Path $run.Work 'tests.log'
    $err=Join-Path $run.Work 'tests.err'
    $process=Start-Process -FilePath $env:ComSpec -ArgumentList @('/d','/c',('"'+$batch+'"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput $log -RedirectStandardError $err
    $null=$process.Handle
    Register-HubTestProcess -Run $run -ProcessId $process.Id
    if (!$process.WaitForExit(60000)) { throw 'Synthetic test process exceeded 60 seconds; preserve active run.' }
    Get-Content -LiteralPath $log | ForEach-Object { Write-Host ([string]$_) }
    Get-Content -LiteralPath $err | ForEach-Object { Write-Host ([string]$_) }
    if($process.ExitCode -ne 0) { throw "Synthetic check failed: $($process.ExitCode)" }
} -CollectArtifacts {
    param($run)
    # A fresh isolated cl invocation and test-owned GUI are the only producers.
    Register-HubArtifactTree -Run $run -Producer 'isolated cl and synthetic test executables'
} -Evidence {
    param($run,$outcome)
    if($outcome -ne 'PASS') {
        foreach($name in @('tests.log','tests.err')) {
            $path=Join-Path $run.Work $name
            if(Test-Path -LiteralPath $path){Get-Content -LiteralPath $path | ForEach-Object {Write-Host $_}}
        }
    }
}
$result | Select-Object Success,Outcome,CleanupRemoved,Errors | ConvertTo-Json -Depth 3
if(!$result.Success){throw 'Synthetic verification or registered cleanup failed.'}
