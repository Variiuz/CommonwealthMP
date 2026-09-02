# CMP Reporter

Native Windows GUI Crash Reporter for CommonwealthMP made in Rust + egui.

## Build

```powershell
.\scripts\build-reporter.ps1
```

Output: `tools/cmp-reporter/dist/cmp-reporter.exe` (also `target/release/`).

Ship next to the plugin as `F4SE/Plugins/cmp-reporter.exe` (see `scripts/pack-mo2.ps1`).

## Run

```text
cmp-reporter.exe
cmp-reporter.exe --crash-txt <path> --crash-dmp <path> [--origin veh]
cmp-reporter.exe --report-dir <CMPReports\stamp>
cmp-reporter.exe --collect
```

After a CMP crash, the plugin launches the reporter with the live `CommonwealthMP.crash.txt` / `.dmp` paths. If the exe is missing, the old MessageBox still appears.


