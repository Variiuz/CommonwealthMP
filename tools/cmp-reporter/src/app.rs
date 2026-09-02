//! CMP Reporter GUI (eframe / egui).

use crate::bundle::{self, ArchivedReport};
use crate::crash_parse::CrashReport;
use crate::paths;
use eframe::egui::{self, Color32, RichText, Vec2};
use std::path::PathBuf;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Tab {
    Crash,
    History,
    Collect,
}

pub struct ReporterApp {
    tab: Tab,
    status: String,
    crash: Option<ArchivedReport>,
    history: Vec<PathBuf>,
    selected_history: Option<usize>,
    history_detail: Option<ArchivedReport>,
    collect_extras: String,
    last_zip: Option<PathBuf>,
}

impl ReporterApp {
    pub fn new(
        cc: &eframe::CreationContext<'_>,
        initial_crash: Option<ArchivedReport>,
        start_collect: bool,
        boot_status: String,
    ) -> Self {
        let mut style = (*cc.egui_ctx.style()).clone();
        style.visuals.dark_mode = true;
        style.visuals.panel_fill = Color32::from_rgb(18, 20, 22);
        style.visuals.window_fill = Color32::from_rgb(24, 26, 28);
        style.visuals.override_text_color = Some(Color32::from_rgb(230, 228, 220));
        style.visuals.widgets.inactive.bg_fill = Color32::from_rgb(40, 44, 48);
        style.visuals.widgets.hovered.bg_fill = Color32::from_rgb(55, 60, 66);
        style.visuals.widgets.active.bg_fill = Color32::from_rgb(70, 110, 90);
        style.visuals.selection.bg_fill = Color32::from_rgb(55, 95, 75);
        cc.egui_ctx.set_style(style);

        let tab = if start_collect {
            Tab::Collect
        } else if initial_crash.is_some() {
            Tab::Crash
        } else {
            Tab::History
        };

        let mut status = boot_status;
        if status.is_empty() {
            if start_collect {
                status = "Read".into();
            } else if initial_crash.is_some() {
                status = "Crash archived.".into();
            }
        }

        Self {
            tab,
            status,
            crash: initial_crash,
            history: bundle::list_reports(),
            selected_history: None,
            history_detail: None,
            collect_extras: String::new(),
            last_zip: None,
        }
    }

    fn refresh_history(&mut self) {
        self.history = bundle::list_reports();
        if let Some(i) = self.selected_history {
            if i >= self.history.len() {
                self.selected_history = None;
                self.history_detail = None;
            }
        }
    }

    fn copy_text(&mut self, text: &str) {
        match arboard::Clipboard::new() {
            Ok(mut cb) => {
                if cb.set_text(text.to_string()).is_ok() {
                    self.status = "Copied to clipboard.".into();
                } else {
                    self.status = "Clipboard write failed.".into();
                }
            }
            Err(e) => self.status = format!("Clipboard unavailable: {e}"),
        }
    }

    fn save_zip_for(&mut self, dir: &PathBuf) {
        match bundle::zip_report_dir(dir) {
            Ok(zip) => {
                self.status = format!("Saved {}", zip.display());
                self.last_zip = Some(zip);
            }
            Err(e) => self.status = format!("Zip failed: {e}"),
        }
    }

    fn draw_crash_tab(&mut self, ui: &mut egui::Ui) {
        ui.heading(RichText::new("Crash").size(22.0).strong());
        ui.add_space(6.0);

        let Some(report) = self.crash.clone() else {
            ui.label("No crash loaded...");
            if ui.button("Load crash.txt").clicked() {
                if let Some(txt) = paths::default_crash_txt() {
                    if txt.exists() {
                        match bundle::archive_crash(&txt, paths::default_crash_dmp().as_deref(), None)
                        {
                            Ok(arch) => {
                                self.status = format!("Archived {}", arch.dir.display());
                                self.crash = Some(arch);
                                self.refresh_history();
                            }
                            Err(e) => self.status = e,
                        }
                    } else {
                        self.status = format!("Missing {}", txt.display());
                    }
                }
            }
            return;
        };

        draw_report_body(ui, &report.parsed, &report.meta.why);

        ui.add_space(12.0);
        ui.horizontal(|ui| {
            if ui
                .add(egui::Button::new(RichText::new("Save support zip").strong()).min_size(Vec2::new(160.0, 32.0)))
                .clicked()
            {
                self.save_zip_for(&report.dir);
            }
            if ui.button("Open folder").clicked() {
                let _ = open::that(&report.dir);
            }
            if ui.button("Copy summary").clicked() {
                self.copy_text(&report.parsed.summary());
            }
        });

        if let Some(zip) = &self.last_zip {
            ui.add_space(6.0);
            ui.label(RichText::new(format!("Zip: {}", zip.display())).weak());
            if ui.button("Open zip location").clicked() {
                if let Some(parent) = zip.parent() {
                    let _ = open::that(parent);
                }
            }
        }
    }

    fn draw_history_tab(&mut self, ui: &mut egui::Ui) {
        ui.heading(RichText::new("History").size(22.0).strong());
        ui.add_space(4.0);
        ui.horizontal(|ui| {
            if ui.button("Refresh").clicked() {
                self.refresh_history();
            }
            if let Some(root) = paths::reports_root() {
                if ui.button("Open CMPReports").clicked() {
                    let _ = std::fs::create_dir_all(&root);
                    let _ = open::that(&root);
                }
            }
        });
        ui.add_space(8.0);

        egui::ScrollArea::vertical()
            .max_height(180.0)
            .show(ui, |ui| {
                if self.history.is_empty() {
                    ui.label("No archived reports yet.");
                    return;
                }
                for (i, dir) in self.history.iter().enumerate() {
                    let name = dir
                        .file_name()
                        .and_then(|n| n.to_str())
                        .unwrap_or("?");
                    let selected = self.selected_history == Some(i);
                    if ui
                        .selectable_label(selected, name)
                        .clicked()
                    {
                        self.selected_history = Some(i);
                        match bundle::load_report_dir(dir) {
                            Ok(r) => {
                                self.history_detail = Some(r);
                                self.status.clear();
                            }
                            Err(e) => {
                                self.history_detail = None;
                                self.status = e;
                            }
                        }
                    }
                }
            });

        ui.separator();
        if let Some(detail) = self.history_detail.clone() {
            draw_report_body(ui, &detail.parsed, &detail.meta.why);
            ui.add_space(8.0);
            ui.horizontal(|ui| {
                if ui.button("Save support zip").clicked() {
                    self.save_zip_for(&detail.dir);
                }
                if ui.button("Open folder").clicked() {
                    let _ = open::that(&detail.dir);
                }
                if ui.button("Load in Crash tab").clicked() {
                    self.crash = Some(detail.clone());
                    self.tab = Tab::Crash;
                }
                if ui.button("Delete").clicked() {
                    if let Err(e) = bundle::delete_report(&detail.dir) {
                        self.status = e;
                    } else {
                        self.history_detail = None;
                        self.selected_history = None;
                        self.refresh_history();
                        self.status = "Deleted.".into();
                    }
                }
            });
        }
    }

    fn draw_collect_tab(&mut self, ui: &mut egui::Ui) {
        ui.heading(RichText::new("Collect").size(22.0).strong());
        ui.label("Build a support bundle without crashing. Includes CMP log/ini, crash files if present, system.json, process list, FO4 plugins/loadorder, MO2 modlist (if found), and F4SE Plugins listing.");
        ui.add_space(8.0);
        ui.label("Extra files (one path per line, optional):");
        ui.add(
            egui::TextEdit::multiline(&mut self.collect_extras)
                .desired_rows(4)
                .desired_width(f32::INFINITY),
        );
        ui.add_space(8.0);
        if ui
            .add(egui::Button::new(RichText::new("Collect support zip").strong()).min_size(Vec2::new(180.0, 32.0)))
            .clicked()
        {
            let extras: Vec<PathBuf> = self
                .collect_extras
                .lines()
                .map(|l| l.trim())
                .filter(|l| !l.is_empty())
                .map(PathBuf::from)
                .collect();
            match bundle::collect_bundle(&extras) {
                Ok(zip) => {
                    self.status = format!("Collected {}", zip.display());
                    self.last_zip = Some(zip);
                    self.refresh_history();
                }
                Err(e) => self.status = e,
            }
        }
        if let Some(zip) = &self.last_zip {
            ui.add_space(6.0);
            ui.label(format!("Zip: {}", zip.display()));
            if ui.button("Open zip location").clicked() {
                if let Some(parent) = zip.parent() {
                    let _ = open::that(parent);
                }
            }
        }
    }
}

fn draw_report_body(ui: &mut egui::Ui, parsed: &CrashReport, why_fallback: &str) {
    let why = if parsed.why.is_empty() {
        why_fallback
    } else {
        parsed.why.as_str()
    };
    let where_s = if parsed.where_addr.is_empty() {
        parsed.rip.as_str()
    } else {
        parsed.where_addr.as_str()
    };

    ui.group(|ui| {
        ui.label(RichText::new("Why").strong().color(Color32::from_rgb(200, 160, 90)));
        ui.label(why);
        ui.add_space(6.0);
        ui.label(RichText::new("Where").strong());
        ui.monospace(if where_s.is_empty() { "-" } else { where_s });
        ui.add_space(4.0);
        ui.horizontal(|ui| {
            ui.label(RichText::new("Code").strong());
            ui.monospace(format!(
                "{} {}",
                blank(&parsed.code_name),
                blank(&parsed.code_hex)
            ));
        });
        ui.horizontal(|ui| {
            ui.label(RichText::new("Doing").strong());
            ui.label(parsed.last_note());
        });
        ui.horizontal(|ui| {
            ui.label(RichText::new("Origin").strong());
            ui.label(blank(&parsed.origin));
        });
        ui.horizontal(|ui| {
            ui.label(RichText::new("Versions").strong());
            ui.label(format!(
                "plugin {}  |  f4se {}",
                blank(&parsed.plugin_version),
                blank(&parsed.f4se_version)
            ));
        });
    });

    if !parsed.last_notes.is_empty() {
        ui.add_space(8.0);
        ui.collapsing("Recent notes", |ui| {
            for n in &parsed.last_notes {
                ui.monospace(n);
            }
        });
    }
    if !parsed.stack.is_empty() {
        ui.collapsing("Stack", |ui| {
            egui::ScrollArea::vertical().max_height(120.0).show(ui, |ui| {
                for s in &parsed.stack {
                    ui.monospace(s);
                }
            });
        });
    }
    if !parsed.modules.is_empty() {
        ui.collapsing(format!("Modules ({})", parsed.modules.len()), |ui| {
            egui::ScrollArea::vertical().max_height(120.0).show(ui, |ui| {
                for m in parsed.modules.iter().take(40) {
                    ui.monospace(m);
                }
                if parsed.modules.len() > 40 {
                    ui.label(format!("… {} more", parsed.modules.len() - 40));
                }
            });
        });
    }
}

fn blank(s: &str) -> &str {
    if s.is_empty() {
        "-"
    } else {
        s
    }
}

impl eframe::App for ReporterApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        egui::TopBottomPanel::top("brand").show(ctx, |ui| {
            ui.add_space(8.0);
            ui.horizontal(|ui| {
                ui.heading(RichText::new("CommonwealthMP: Crash Reporter").size(28.0).strong());
                ui.add_space(12.0);
                ui.label(
                    RichText::new("CommonwealthMP Crash Reporter").color(Color32::from_rgb(200, 160, 90))
                        .weak()
                        .size(14.0),
                );
            });
            ui.add_space(4.0);
            ui.horizontal(|ui| {
                for (tab, label) in [
                    (Tab::Crash, "Crash"),
                    (Tab::History, "History"),
                    (Tab::Collect, "Collect"),
                ] {
                    if ui
                        .selectable_label(self.tab == tab, RichText::new(label).size(15.0))
                        .clicked()
                    {
                        self.tab = tab;
                    }
                }
            });
            ui.add_space(4.0);
            ui.separator();
        });

        egui::TopBottomPanel::bottom("status").show(ctx, |ui| {
            ui.horizontal(|ui| {
                ui.label(RichText::new("Status").weak());
                ui.label(if self.status.is_empty() {
                    "Ready"
                } else {
                    &self.status
                });
            });
        });

        egui::CentralPanel::default().show(ctx, |ui| {
            egui::ScrollArea::vertical().show(ui, |ui| match self.tab {
                Tab::Crash => self.draw_crash_tab(ui),
                Tab::History => self.draw_history_tab(ui),
                Tab::Collect => self.draw_collect_tab(ui),
            });
        });
    }
}
