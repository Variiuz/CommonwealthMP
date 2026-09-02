//! Archive crash reports and build support zips.

use crate::crash_parse::{parse_crash_txt, CrashReport};
use crate::paths::{self, ensure_dir, stamp_now};
use serde::{Deserialize, Serialize};
use std::fs::{self, File};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use walkdir::WalkDir;
use zip::write::SimpleFileOptions;
use zip::ZipWriter;

pub const REPORTER_VERSION: &str = env!("CARGO_PKG_VERSION");

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ReportMeta {
    pub reporter_version: String,
    pub created: String,
    pub origin: String,
    pub plugin_version: String,
    pub f4se_version: String,
    pub code_name: String,
    pub code_hex: String,
    pub why: String,
    pub where_addr: String,
    pub last_note: String,
    pub notes: Vec<String>,
    pub crash_txt: String,
    pub crash_dmp: String,
    pub fo4_hint: String,
}

#[derive(Debug, Clone)]
pub struct ArchivedReport {
    pub dir: PathBuf,
    pub meta: ReportMeta,
    pub parsed: CrashReport,
}

pub fn list_reports() -> Vec<PathBuf> {
    let Some(root) = paths::reports_root() else {
        return Vec::new();
    };
    let mut dirs: Vec<_> = fs::read_dir(&root)
        .into_iter()
        .flatten()
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .filter(|p| p.is_dir())
        .collect();
    dirs.sort_by(|a, b| b.cmp(a));
    dirs
}

pub fn load_report_dir(dir: &Path) -> Result<ArchivedReport, String> {
    let meta_path = dir.join("meta.json");
    let txt = dir.join("crash.txt");
    let parsed = if txt.exists() {
        parse_crash_txt(&txt)?
    } else {
        CrashReport::default()
    };
    let meta = if meta_path.exists() {
        let s = fs::read_to_string(&meta_path).map_err(|e| e.to_string())?;
        serde_json::from_str(&s).map_err(|e| e.to_string())?
    } else {
        meta_from_parsed(&parsed, "", "")
    };
    Ok(ArchivedReport {
        dir: dir.to_path_buf(),
        meta,
        parsed,
    })
}

fn meta_from_parsed(parsed: &CrashReport, txt: &str, dmp: &str) -> ReportMeta {
    ReportMeta {
        reporter_version: REPORTER_VERSION.to_string(),
        created: chrono::Local::now().to_rfc3339(),
        origin: parsed.origin.clone(),
        plugin_version: parsed.plugin_version.clone(),
        f4se_version: parsed.f4se_version.clone(),
        code_name: parsed.code_name.clone(),
        code_hex: parsed.code_hex.clone(),
        why: parsed.why.clone(),
        where_addr: if parsed.where_addr.is_empty() {
            parsed.rip.clone()
        } else {
            parsed.where_addr.clone()
        },
        last_note: parsed.last_note().to_string(),
        notes: parsed.last_notes.clone(),
        crash_txt: txt.to_string(),
        crash_dmp: dmp.to_string(),
        fo4_hint: String::new(),
    }
}

/// Copy live crash artifacts into CMPReports\<stamp>\ and write meta.json.
pub fn archive_crash(
    crash_txt: &Path,
    crash_dmp: Option<&Path>,
    origin_override: Option<&str>,
) -> Result<ArchivedReport, String> {
    let root = paths::reports_root().ok_or_else(|| "Documents/F4SE path not found".to_string())?;
    ensure_dir(&root).map_err(|e| e.to_string())?;
    let dir = root.join(stamp_now());
    ensure_dir(&dir).map_err(|e| e.to_string())?;

    let dest_txt = dir.join("crash.txt");
    fs::copy(crash_txt, &dest_txt).map_err(|e| format!("copy crash.txt: {e}"))?;

    let mut dest_dmp = PathBuf::new();
    if let Some(dmp) = crash_dmp {
        if dmp.exists() {
            dest_dmp = dir.join("crash.dmp");
            let _ = fs::copy(dmp, &dest_dmp);
        }
    } else if let Some(default_dmp) = paths::default_crash_dmp() {
        if default_dmp.exists() {
            dest_dmp = dir.join("crash.dmp");
            let _ = fs::copy(&default_dmp, &dest_dmp);
        }
    }

    let mut parsed = parse_crash_txt(&dest_txt)?;
    if let Some(o) = origin_override {
        if !o.is_empty() {
            parsed.origin = o.to_string();
        }
    }

    let mut meta = meta_from_parsed(
        &parsed,
        &dest_txt.display().to_string(),
        &dest_dmp.display().to_string(),
    );

    // Attach common extras into the report folder (not required)
    attach_extras(&dir, &mut meta)?;

    let meta_json = serde_json::to_string_pretty(&meta).map_err(|e| e.to_string())?;
    fs::write(dir.join("meta.json"), meta_json).map_err(|e| e.to_string())?;

    Ok(ArchivedReport {
        dir,
        meta,
        parsed,
    })
}

fn attach_extras(dir: &Path, meta: &mut ReportMeta) -> Result<(), String> {
    if let Some(log) = paths::f4se_log() {
        if log.exists() {
            let _ = fs::copy(&log, dir.join("CommonwealthMP.log"));
        }
    }
    if let Some(f4se) = paths::f4se_dir() {
        let ini = f4se.join("CommonwealthMP.ini");
        if ini.exists() {
            let _ = fs::copy(&ini, dir.join("CommonwealthMP.ini"));
        }
    }
    for cand in paths::live_dump_candidates() {
        if cand.exists() {
            let name = cand
                .file_name()
                .map(|n| n.to_string_lossy().into_owned())
                .unwrap_or_else(|| "live_dump.txt".into());
            let _ = fs::copy(&cand, dir.join(name));
            break;
        }
    }
    let diag = crate::diagnostics::attach_diagnostics(dir);
    if !diag.is_empty() {
        meta.notes.extend(diag);
    }
    Ok(())
}

/// Manual collect bundle (no crash required).
pub fn collect_bundle(extra_paths: &[PathBuf]) -> Result<PathBuf, String> {
    let root = paths::reports_root().ok_or_else(|| "Documents/F4SE path not found".to_string())?;
    ensure_dir(&root).map_err(|e| e.to_string())?;
    let dir = root.join(format!("collect-{}", stamp_now()));
    ensure_dir(&dir).map_err(|e| e.to_string())?;

    let mut notes = Vec::new();
    if let Some(log) = paths::f4se_log() {
        if log.exists() {
            let _ = fs::copy(&log, dir.join("CommonwealthMP.log"));
            notes.push("log".into());
        }
    }
    if let Some(txt) = paths::default_crash_txt() {
        if txt.exists() {
            let _ = fs::copy(&txt, dir.join("crash.txt"));
            notes.push("crash.txt".into());
        }
    }
    if let Some(dmp) = paths::default_crash_dmp() {
        if dmp.exists() {
            let _ = fs::copy(&dmp, dir.join("crash.dmp"));
            notes.push("crash.dmp".into());
        }
    }
    for cand in paths::live_dump_candidates() {
        if cand.exists() {
            let name = cand
                .file_name()
                .map(|n| n.to_string_lossy().into_owned())
                .unwrap_or_else(|| "live_dump.txt".into());
            let _ = fs::copy(&cand, dir.join(&name));
            notes.push(name);
            break;
        }
    }
    for p in extra_paths {
        if p.exists() {
            if let Some(name) = p.file_name() {
                let _ = fs::copy(p, dir.join(name));
                notes.push(name.to_string_lossy().into_owned());
            }
        }
    }
    notes.extend(crate::diagnostics::attach_diagnostics(&dir));

    let meta = ReportMeta {
        reporter_version: REPORTER_VERSION.to_string(),
        created: chrono::Local::now().to_rfc3339(),
        origin: "collect".into(),
        plugin_version: String::new(),
        f4se_version: String::new(),
        code_name: String::new(),
        code_hex: String::new(),
        why: "Manual support bundle".into(),
        where_addr: String::new(),
        last_note: notes.join(", "),
        notes,
        crash_txt: String::new(),
        crash_dmp: String::new(),
        fo4_hint: String::new(),
    };
    fs::write(
        dir.join("meta.json"),
        serde_json::to_string_pretty(&meta).map_err(|e| e.to_string())?,
    )
    .map_err(|e| e.to_string())?;

    zip_report_dir(&dir)
}

pub fn zip_report_dir(dir: &Path) -> Result<PathBuf, String> {
    let zip_path = dir.with_extension("zip");
    // If dir is .../CMPReports/stamp, zip beside it as stamp.zip
    let zip_path = if zip_path
        .extension()
        .and_then(|e| e.to_str())
        == Some("zip")
        && dir.extension().is_none()
    {
        // with_extension on a dir path like `...\20260101-120000` yields `...\20260101-120000.zip`
        zip_path
    } else {
        dir.parent()
            .unwrap_or(Path::new("."))
            .join(format!(
                "{}.zip",
                dir.file_name()
                    .and_then(|n| n.to_str())
                    .unwrap_or("cmp-report")
            ))
    };

    let file = File::create(&zip_path).map_err(|e| e.to_string())?;
    let mut zip = ZipWriter::new(file);
    let opts = SimpleFileOptions::default().compression_method(zip::CompressionMethod::Deflated);

    let base = dir;
    for entry in WalkDir::new(dir).into_iter().filter_map(|e| e.ok()) {
        let path = entry.path();
        if path.is_dir() {
            continue;
        }
        let rel = path.strip_prefix(base).unwrap_or(path);
        let name = rel.to_string_lossy().replace('\\', "/");
        zip.start_file(name, opts).map_err(|e| e.to_string())?;
        let mut f = File::open(path).map_err(|e| e.to_string())?;
        let mut buf = Vec::new();
        f.read_to_end(&mut buf).map_err(|e| e.to_string())?;
        zip.write_all(&buf).map_err(|e| e.to_string())?;
    }
    zip.finish().map_err(|e| e.to_string())?;
    Ok(zip_path)
}

pub fn delete_report(dir: &Path) -> Result<(), String> {
    if dir.exists() {
        fs::remove_dir_all(dir).map_err(|e| e.to_string())?;
    }
    let zip = dir.with_extension("zip");
    if zip.exists() {
        let _ = fs::remove_file(zip);
    }
    Ok(())
}
