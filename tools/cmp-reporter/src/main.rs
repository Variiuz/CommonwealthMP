#![windows_subsystem = "windows"]

mod app;
mod bundle;
mod crash_parse;
mod diagnostics;
mod paths;

use app::ReporterApp;
use clap::Parser;
use std::path::PathBuf;

#[derive(Parser, Debug)]
#[command(name = "cmp-reporter", version, about = "CommonwealthMP debug / crash reporter")]
struct Cli {
    /// Path to CommonwealthMP.crash.txt
    #[arg(long)]
    crash_txt: Option<PathBuf>,

    /// Path to CommonwealthMP.dmp
    #[arg(long)]
    crash_dmp: Option<PathBuf>,

    /// Crash origin tag from the plugin (veh, unhandled, …)
    #[arg(long)]
    origin: Option<String>,

    /// Open an existing CMPReports\<stamp> folder
    #[arg(long)]
    report_dir: Option<PathBuf>,

    /// Start on the Collect tab
    #[arg(long)]
    collect: bool,
}

fn main() -> eframe::Result<()> {
    let cli = Cli::parse();

    let mut initial = None;
    let mut boot_status = String::new();

    if let Some(dir) = &cli.report_dir {
        match bundle::load_report_dir(dir) {
            Ok(r) => initial = Some(r),
            Err(e) => boot_status = e,
        }
    } else if let Some(txt) = &cli.crash_txt {
        match bundle::archive_crash(txt, cli.crash_dmp.as_deref(), cli.origin.as_deref()) {
            Ok(r) => initial = Some(r),
            Err(e) => boot_status = e,
        }
    }

    let options = eframe::NativeOptions {
        viewport: eframe::egui::ViewportBuilder::default()
            .with_inner_size([720.0, 640.0])
            .with_min_inner_size([560.0, 420.0])
            .with_title("CMP Reporter"),
        ..Default::default()
    };

    let start_collect = cli.collect;
    eframe::run_native(
        "CMP Reporter",
        options,
        Box::new(move |cc| {
            Ok(Box::new(ReporterApp::new(
                cc,
                initial,
                start_collect,
                boot_status,
            )))
        }),
    )
}
