//! Opt-in local diagnostics for support zips (specs, processes, FO4 load order).

use serde::Serialize;
use std::fs::{self, File};
use std::io::Write;
use std::path::{Path, PathBuf};
use sysinfo::{Disks, System};
use walkdir::WalkDir;

#[derive(Serialize)]
struct SystemSnapshot {
    reporter_note: &'static str,
    os_name: String,
    os_version: String,
    kernel: String,
    hostname: String,
    cpu_brand: String,
    cpu_cores_logical: usize,
    cpu_cores_physical: usize,
    memory_total_mb: u64,
    memory_available_mb: u64,
    memory_used_mb: u64,
    disks: Vec<DiskSnap>,
    gpus: Vec<String>,
    fo4_install: Option<String>,
    fo4_running: bool,
    username: String,
}

#[derive(Serialize)]
struct DiskSnap {
    name: String,
    mount: String,
    total_gb: u64,
    available_gb: u64,
    fs: String,
}

pub fn attach_diagnostics(dir: &Path) -> Vec<String> {
    let mut attached = Vec::new();
    let mut sys = System::new_all();
    sys.refresh_all();

    let fo4_install = resolve_fo4_install(&sys);
    let fo4_running = fo4_process_paths(&sys).next().is_some();

    if write_system_json(dir, &sys, fo4_install.as_deref(), fo4_running).is_ok() {
        attached.push("system.json".into());
    }
    if write_processes(dir, &sys).is_ok() {
        attached.push("processes.txt".into());
    }
    attached.extend(copy_load_order_files(dir));
    attached.extend(copy_mo2_modlists(dir, fo4_install.as_deref()));
    if let Some(ref install) = fo4_install {
        if write_f4se_plugins_list(dir, install).is_ok() {
            attached.push("f4se_plugins.txt".into());
        }
        let _ = fs::write(dir.join("fo4_install.txt"), format!("{install}\n"));
        attached.push("fo4_install.txt".into());
    }
    attached
}

fn write_system_json(
    dir: &Path,
    sys: &System,
    fo4_install: Option<&str>,
    fo4_running: bool,
) -> Result<(), String> {
    let disks = Disks::new_with_refreshed_list();
    let disk_snaps: Vec<_> = disks
        .list()
        .iter()
        .map(|d| DiskSnap {
            name: d.name().to_string_lossy().into_owned(),
            mount: d.mount_point().display().to_string(),
            total_gb: d.total_space() / (1024 * 1024 * 1024),
            available_gb: d.available_space() / (1024 * 1024 * 1024),
            fs: String::from_utf8_lossy(d.file_system().as_encoded_bytes()).into_owned(),
        })
        .collect();

    let snap = SystemSnapshot {
        os_name: System::name().unwrap_or_else(|| "unknown".into()),
        os_version: System::os_version().unwrap_or_else(|| "unknown".into()),
        kernel: System::kernel_version().unwrap_or_else(|| "unknown".into()),
        hostname: System::host_name().unwrap_or_else(|| "unknown".into()),
        cpu_brand: sys
            .cpus()
            .first()
            .map(|c| c.brand().to_string())
            .unwrap_or_else(|| "unknown".into()),
        cpu_cores_logical: sys.cpus().len(),
        cpu_cores_physical: System::physical_core_count().unwrap_or(0),
        memory_total_mb: sys.total_memory() / (1024 * 1024),
        memory_available_mb: sys.available_memory() / (1024 * 1024),
        memory_used_mb: sys.used_memory() / (1024 * 1024),
        disks: disk_snaps,
        gpus: list_gpus(),
        fo4_install: fo4_install.map(|s| s.to_string()),
        fo4_running,
        username: std::env::var("USERNAME").unwrap_or_default(),
    };

    let json = serde_json::to_string_pretty(&snap).map_err(|e| e.to_string())?;
    fs::write(dir.join("system.json"), json).map_err(|e| e.to_string())
}

fn list_gpus() -> Vec<String> {
    // Prefer WMI via PowerShell; empty on failure.
    let out = std::process::Command::new("powershell")
        .args([
            "-NoProfile",
            "-Command",
            "Get-CimInstance Win32_VideoController | Select-Object -ExpandProperty Name",
        ])
        .output();
    match out {
        Ok(o) if o.status.success() => String::from_utf8_lossy(&o.stdout)
            .lines()
            .map(|l| l.trim().to_string())
            .filter(|l| !l.is_empty())
            .collect(),
        _ => Vec::new(),
    }
}

fn write_processes(dir: &Path, sys: &System) -> Result<(), String> {
    let mut rows: Vec<(String, u32, u64)> = sys
        .processes()
        .iter()
        .map(|(pid, p)| {
            (
                p.name().to_string_lossy().into_owned(),
                pid.as_u32(),
                p.memory() / (1024 * 1024),
            )
        })
        .collect();
    rows.sort_by(|a, b| a.0.to_lowercase().cmp(&b.0.to_lowercase()));

    let mut fo4_related = Vec::new();
    let mut rest = Vec::new();
    for row in rows {
        if is_fo4_related(&row.0) {
            fo4_related.push(row);
        } else {
            rest.push(row);
        }
    }

    let mut f = File::create(dir.join("processes.txt")).map_err(|e| e.to_string())?;
    writeln!(
        f,
        "# CMP Reporter process snapshot\n# name\\tpid\\tmemory_mb\n"
    )
    .map_err(|e| e.to_string())?;
    writeln!(f, "## Fallout / F4SE / MO2 related").map_err(|e| e.to_string())?;
    if fo4_related.is_empty() {
        writeln!(f, "(none running)").map_err(|e| e.to_string())?;
    } else {
        for (name, pid, mb) in &fo4_related {
            writeln!(f, "{name}\t{pid}\t{mb}").map_err(|e| e.to_string())?;
        }
    }
    writeln!(f, "\n## All processes").map_err(|e| e.to_string())?;
    for (name, pid, mb) in &rest {
        writeln!(f, "{name}\t{pid}\t{mb}").map_err(|e| e.to_string())?;
    }
    Ok(())
}

fn is_fo4_related(name: &str) -> bool {
    let n = name.to_lowercase();
    n.contains("fallout")
        || n.contains("f4se")
        || n.contains("modorganizer")
        || n.contains("mo2")
        || n == "cmp-reporter.exe"
        || n.contains("commonwealth")
}

fn fo4_process_paths(sys: &System) -> impl Iterator<Item = PathBuf> + '_ {
    sys.processes().values().filter_map(|p| {
        let name = p.name().to_string_lossy().to_lowercase();
        if name == "fallout4.exe" || name == "f4se_loader.exe" {
            p.exe().map(|e| e.to_path_buf())
        } else {
            None
        }
    })
}

fn resolve_fo4_install(sys: &System) -> Option<String> {
    for exe in fo4_process_paths(sys) {
        if let Some(parent) = exe.parent() {
            return Some(parent.display().to_string());
        }
    }
    registry_fo4_path().or_else(steam_fo4_hint)
}

fn registry_fo4_path() -> Option<String> {
    use winreg::enums::*;
    use winreg::RegKey;
    let hklm = RegKey::predef(HKEY_LOCAL_MACHINE);
    for sub in [
        r"SOFTWARE\WOW6432Node\Bethesda Softworks\Fallout4",
        r"SOFTWARE\Bethesda Softworks\Fallout4",
    ] {
        if let Ok(key) = hklm.open_subkey(sub) {
            if let Ok(path) = key.get_value::<String, _>("installed path") {
                let p = PathBuf::from(path.trim_end_matches(['\\', '/']));
                if p.is_dir() {
                    return Some(p.display().to_string());
                }
            }
        }
    }
    None
}

fn steam_fo4_hint() -> Option<String> {
    // Common default; skip exhaustive libraryfolder.vdf parse for v1.
    let candidates = [
        r"C:\Program Files (x86)\Steam\steamapps\common\Fallout 4",
        r"C:\Program Files\Steam\steamapps\common\Fallout 4",
        r"D:\SteamLibrary\steamapps\common\Fallout 4",
        r"E:\SteamLibrary\steamapps\common\Fallout 4",
    ];
    for c in candidates {
        let p = Path::new(c);
        if p.join("Fallout4.exe").exists() {
            return Some(c.to_string());
        }
    }
    None
}

fn copy_load_order_files(dir: &Path) -> Vec<String> {
    let mut attached = Vec::new();
    let mut roots = Vec::new();
    if let Some(local) = std::env::var_os("LOCALAPPDATA") {
        roots.push(PathBuf::from(local).join("Fallout4"));
    }
    if let Some(docs) = crate::paths::documents_dir() {
        roots.push(docs.join("My Games").join("Fallout4"));
    }
    for root in roots {
        for name in ["plugins.txt", "loadorder.txt"] {
            let src = root.join(name);
            if src.exists() {
                let dest_name = format!(
                    "{}_{}",
                    root.file_name()
                        .and_then(|n| n.to_str())
                        .unwrap_or("fo4"),
                    name
                );
                if fs::copy(&src, dir.join(&dest_name)).is_ok() {
                    attached.push(dest_name);
                }
            }
        }
        // Prefs / custom ini help with resolution and control maps.
        for name in ["Fallout4.ini", "Fallout4Prefs.ini", "Fallout4Custom.ini"] {
            let src = root.join(name);
            if src.exists() {
                if fs::copy(&src, dir.join(name)).is_ok() {
                    if !attached.iter().any(|a| a == name) {
                        attached.push(name.to_string());
                    }
                }
            }
        }
    }
    attached
}

fn copy_mo2_modlists(dir: &Path, fo4_install: Option<&str>) -> Vec<String> {
    let mut attached = Vec::new();
    let mut search_roots = Vec::new();

    if let Some(local) = std::env::var_os("LOCALAPPDATA") {
        search_roots.push(PathBuf::from(local).join("ModOrganizer"));
    }
    if let Some(install) = fo4_install {
        let fo4 = Path::new(install);
        if let Some(parent) = fo4.parent() {
            search_roots.push(parent.to_path_buf());
            search_roots.push(parent.join("Mod Organizer 2"));
            search_roots.push(parent.join("MO2"));
        }
        search_roots.push(fo4.join("ModOrganizer"));
    }
    // Portable MO2 often sits next to common modding folders.
    for extra in [
        r"C:\Modding\MO2",
        r"C:\Games\MO2",
        r"D:\Modding\MO2",
        r"D:\Games\MO2",
    ] {
        search_roots.push(PathBuf::from(extra));
    }

    let mut found = Vec::new();
    for root in search_roots {
        if !root.exists() {
            continue;
        }
        for entry in WalkDir::new(&root)
            .max_depth(6)
            .into_iter()
            .filter_map(|e| e.ok())
        {
            let path = entry.path();
            if path
                .file_name()
                .and_then(|n| n.to_str())
                .map(|n| n.eq_ignore_ascii_case("modlist.txt"))
                .unwrap_or(false)
            {
                found.push(path.to_path_buf());
            }
        }
    }

    found.sort();
    found.dedup();
    // Prefer newest few to avoid packing dozens of old profiles.
    found.sort_by_key(|p| fs::metadata(p).and_then(|m| m.modified()).ok());
    let newest: Vec<_> = found.into_iter().rev().take(3).collect();

    for (i, src) in newest.iter().enumerate() {
        let profile = src
            .parent()
            .and_then(|p| p.file_name())
            .and_then(|n| n.to_str())
            .unwrap_or("profile");
        let dest = format!("mo2_modlist_{i}_{profile}.txt");
        if fs::copy(src, dir.join(&dest)).is_ok() {
            attached.push(dest);
        }
    }
    attached
}

fn write_f4se_plugins_list(dir: &Path, fo4_install: &str) -> Result<(), String> {
    let plugins = Path::new(fo4_install).join("Data").join("F4SE").join("Plugins");
    let mut f = File::create(dir.join("f4se_plugins.txt")).map_err(|e| e.to_string())?;
    writeln!(f, "# {}", plugins.display()).map_err(|e| e.to_string())?;
    if !plugins.is_dir() {
        writeln!(f, "(directory not found)").map_err(|e| e.to_string())?;
        return Ok(());
    }
    let mut entries: Vec<_> = fs::read_dir(&plugins)
        .map_err(|e| e.to_string())?
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .collect();
    entries.sort();
    for path in entries {
        let meta = fs::metadata(&path).ok();
        let size = meta.as_ref().map(|m| m.len()).unwrap_or(0);
        let name = path
            .file_name()
            .map(|n| n.to_string_lossy().into_owned())
            .unwrap_or_default();
        writeln!(f, "{name}\t{size}").map_err(|e| e.to_string())?;
    }
    Ok(())
}
