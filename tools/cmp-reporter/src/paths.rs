//! Path helpers for F4SE Documents folder and CMPReports.

use std::path::{Path, PathBuf};

pub fn documents_dir() -> Option<PathBuf> {
    std::env::var_os("USERPROFILE")
        .map(PathBuf::from)
        .map(|p| p.join("Documents"))
}

pub fn f4se_dir() -> Option<PathBuf> {
    documents_dir().map(|d| d.join("My Games").join("Fallout4").join("F4SE"))
}

pub fn reports_root() -> Option<PathBuf> {
    f4se_dir().map(|d| d.join("CMPReports"))
}

pub fn ensure_dir(path: &Path) -> std::io::Result<()> {
    std::fs::create_dir_all(path)
}

pub fn default_crash_txt() -> Option<PathBuf> {
    f4se_dir().map(|d| d.join("CommonwealthMP.crash.txt"))
}

pub fn default_crash_dmp() -> Option<PathBuf> {
    f4se_dir().map(|d| d.join("CommonwealthMP.dmp"))
}

pub fn f4se_log() -> Option<PathBuf> {
    f4se_dir().map(|d| d.join("CommonwealthMP.log"))
}

pub fn live_dump_candidates() -> Vec<PathBuf> {
    let mut out = Vec::new();
    if let Some(dir) = f4se_dir() {
        out.push(dir.join("CommonwealthMP.dump.txt"));
        out.push(dir.join("CommonwealthMP.live.txt"));
        // cmp_dump may use a timestamped name; pick newest matching
        if let Ok(entries) = std::fs::read_dir(&dir) {
            let mut dumps: Vec<_> = entries
                .filter_map(|e| e.ok())
                .map(|e| e.path())
                .filter(|p| {
                    p.file_name()
                        .and_then(|n| n.to_str())
                        .map(|n| {
                            n.starts_with("CommonwealthMP")
                                && (n.contains("dump") || n.contains("live"))
                                && n.ends_with(".txt")
                                && n != "CommonwealthMP.crash.txt"
                                && n != "CommonwealthMP.log"
                        })
                        .unwrap_or(false)
                })
                .collect();
            dumps.sort_by_key(|p| std::fs::metadata(p).and_then(|m| m.modified()).ok());
            if let Some(last) = dumps.pop() {
                out.push(last);
            }
        }
    }
    out
}

pub fn stamp_now() -> String {
    chrono::Local::now().format("%Y%m%d-%H%M%S").to_string()
}
