//! Parse CommonwealthMP.crash.txt produced by the F4SE plugin.

use serde::{Deserialize, Serialize};
use std::path::Path;

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct CrashReport {
    pub origin: String,
    pub timestamp: String,
    pub plugin_version: String,
    pub f4se_version: String,
    pub code_name: String,
    pub code_hex: String,
    pub why: String,
    pub where_addr: String,
    pub rip: String,
    pub last_notes: Vec<String>,
    pub stack: Vec<String>,
    pub modules: Vec<String>,
    pub minidump_path: String,
    pub raw: String,
}

impl CrashReport {
    pub fn summary(&self) -> String {
        let notes = if self.last_notes.is_empty() {
            "-".to_string()
        } else {
            self.last_notes.join(" | ")
        };
        format!(
            "Oooops?!\n\
             Seems like the game crashed.\n\
             Why: {}\n\
             Where: {}\n\
             Code: {} {}\n\
             Notes: {}\n\
             Origin: {}\n\
             Plugin: {}  F4SE: {}\n",
            blank(&self.why),
            blank(&self.where_addr),
            blank(&self.code_name),
            blank(&self.code_hex),
            notes,
            blank(&self.origin),
            blank(&self.plugin_version),
            blank(&self.f4se_version),
        )
    }

    pub fn last_note(&self) -> &str {
        self.last_notes.last().map(|s| s.as_str()).unwrap_or("-")
    }
}

fn blank(s: &str) -> &str {
    if s.is_empty() {
        "-"
    } else {
        s
    }
}

pub fn parse_crash_txt(path: &Path) -> Result<CrashReport, String> {
    let raw = std::fs::read_to_string(path).map_err(|e| format!("read {}: {e}", path.display()))?;
    Ok(parse_crash_text(&raw))
}

pub fn parse_crash_text(raw: &str) -> CrashReport {
    let mut report = CrashReport {
        raw: raw.to_string(),
        ..Default::default()
    };

    let mut in_notes = false;
    let mut in_stack = false;
    let mut in_modules = false;

    for line in raw.lines() {
        if line.starts_with("CommonwealthMP crash origin=") {
            // CommonwealthMP crash origin=veh 2026-09-02 12:00:00 tid=1234
            let rest = line.trim_start_matches("CommonwealthMP crash origin=");
            let mut parts = rest.splitn(2, ' ');
            report.origin = parts.next().unwrap_or("").to_string();
            if let Some(ts) = parts.next() {
                // drop tid=...
                let ts = ts.split(" tid=").next().unwrap_or(ts).trim();
                report.timestamp = ts.to_string();
            }
            in_notes = false;
            in_stack = false;
            in_modules = false;
            continue;
        }
        if line.starts_with("plugin ") {
            let rest = line.trim_start_matches("plugin ");
            if let Some((plug, f4se)) = rest.split_once(" f4se ") {
                report.plugin_version = plug.trim().to_string();
                report.f4se_version = f4se.trim().to_string();
            } else {
                report.plugin_version = rest.trim().to_string();
            }
            continue;
        }
        if line.starts_with("code ") {
            let rest = line.trim_start_matches("code ").trim();
            let mut parts = rest.split_whitespace();
            report.code_name = parts.next().unwrap_or("").to_string();
            report.code_hex = parts.next().unwrap_or("").to_string();
            continue;
        }
        if line.starts_with("why ") {
            report.why = line.trim_start_matches("why ").trim().to_string();
            continue;
        }
        if line.starts_with("addr ") {
            report.where_addr = line.trim_start_matches("addr ").trim().to_string();
            continue;
        }
        if line.starts_with("rip ") {
            // rip MODULE+RVA rsp ... (first token after rip)
            let rest = line.trim_start_matches("rip ").trim();
            report.rip = rest
                .split_whitespace()
                .next()
                .unwrap_or(rest)
                .to_string();
            if report.where_addr.is_empty() {
                report.where_addr = report.rip.clone();
            }
            continue;
        }
        if line.starts_with("minidump ") {
            report.minidump_path = line.trim_start_matches("minidump ").trim().to_string();
            continue;
        }
        if line.starts_with("notes") {
            in_notes = true;
            in_stack = false;
            in_modules = false;
            continue;
        }
        if line.trim() == "stack:" {
            in_stack = true;
            in_notes = false;
            in_modules = false;
            continue;
        }
        if line.starts_with("modules:") || line.trim() == "modules" {
            in_modules = true;
            in_notes = false;
            in_stack = false;
            continue;
        }
        if in_notes {
            let t = line.trim();
            if t.is_empty() {
                continue;
            }
            // notes often look like "  [n] text" or plain
            report.last_notes.push(t.trim_start_matches([' ', '-', '*']).to_string());
            continue;
        }
        if in_stack {
            let t = line.trim();
            if t.is_empty() {
                continue;
            }
            if t.starts_with("modules") {
                in_stack = false;
                in_modules = true;
                continue;
            }
            report.stack.push(t.to_string());
            continue;
        }
        if in_modules {
            let t = line.trim();
            if t.is_empty() {
                continue;
            }
            report.modules.push(t.to_string());
        }
    }

    // Heuristic: if notes section missing, look for "Doing" style lines in raw
    if report.last_notes.is_empty() {
        for line in raw.lines() {
            if line.contains("note") && line.contains(':') {
                if let Some((_, v)) = line.split_once(':') {
                    let v = v.trim();
                    if !v.is_empty() {
                        report.last_notes.push(v.to_string());
                    }
                }
            }
        }
    }

    report
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_sample() {
        let sample = r#"CommonwealthMP crash origin=veh 2026-09-02 12:00:00 tid=1234
plugin 0.6.7 f4se 0.7.9
code ACCESS_VIOLATION C0000005
addr CommonwealthMP.dll+1A2B
why Null pointer. The game read address 0 (nothing was there).
rip CommonwealthMP.dll+1A2B rsp 0000000000123456 rbp 0000000000123400 rax 0 rcx 0 rdx 0
notes (newest last):
  pose
  ghosts
  task
stack:
  CommonwealthMP.dll+1A2B
  Fallout4.exe+ABCDEF
modules:
  CommonwealthMP.dll base=180000000 size=200000
minidump C:\Users\x\Documents\My Games\Fallout4\F4SE\CommonwealthMP.dmp
"#;
        let r = parse_crash_text(sample);
        assert_eq!(r.origin, "veh");
        assert_eq!(r.code_name, "ACCESS_VIOLATION");
        assert!(r.why.contains("Null pointer"));
        assert_eq!(r.last_notes.len(), 3);
        assert!(r.stack.len() >= 2);
        assert!(!r.minidump_path.is_empty());
    }
}
