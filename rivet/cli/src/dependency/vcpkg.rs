use std::path::PathBuf;
use std::process::{Command, Stdio};
use std::collections::HashMap;
use std::fs;
use serde_json::Value;
use indicatif::{ProgressBar, ProgressStyle};
use colored::Colorize;
use tracing::{info, debug};

pub struct VcpkgManager {
    vcpkg_root: PathBuf,
}

impl VcpkgManager {
    pub fn new() -> Self {
        let vcpkg_root = PathBuf::from("vcpkg");
        VcpkgManager { vcpkg_root }
    }

    pub fn bootstrap(&self) -> Result<(), String> {
        if self.vcpkg_root.exists() {
            debug!("vcpkg already bootstrapped");
            return Ok(());
        }

        println!("{}", "📦 Bootstrapping vcpkg...".cyan().bold());
        
        let pb = ProgressBar::new_spinner();
        pb.set_style(
            ProgressStyle::default_spinner()
                .template("{spinner:.cyan} {msg}")
                .unwrap()
        );
        pb.set_message("Cloning vcpkg repository...");

        // Clone vcpkg
        let status = Command::new("git")
            .args(["clone", "https://github.com/microsoft/vcpkg.git"])
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status()
            .map_err(|e| format!("Failed to clone vcpkg: {}", e))?;

        if !status.success() {
            pb.finish_and_clear();
            return Err("Failed to clone vcpkg repository".to_string());
        }

        pb.set_message("Running bootstrap script...");

        // Run bootstrap
        let bootstrap_script = if cfg!(windows) {
            self.vcpkg_root.join("bootstrap-vcpkg.bat")
        } else {
            self.vcpkg_root.join("bootstrap-vcpkg.sh")
        };

        let status = Command::new(&bootstrap_script)
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status()
            .map_err(|e| format!("Failed to run bootstrap: {}", e))?;

        pb.finish_and_clear();

        if !status.success() {
            return Err("vcpkg bootstrap failed".to_string());
        }

        println!("{}", "✓ vcpkg bootstrapped successfully".green());
        Ok(())
    }

    pub fn install_dependencies(&self, deps: &HashMap<String, String>) -> Result<(), String> {
        println!("{}", format!("📦 Installing {} dependencies...", deps.len()).cyan().bold());

        // Generate vcpkg.json
        self.generate_vcpkg_json(deps)?;

        let vcpkg_exe = if cfg!(windows) {
            self.vcpkg_root.join("vcpkg.exe")
        } else {
            self.vcpkg_root.join("vcpkg")
        };

        let pb = ProgressBar::new(deps.len() as u64);
        pb.set_style(
            ProgressStyle::default_bar()
                .template("{spinner:.green} [{bar:40.cyan/blue}] {pos}/{len} {msg}")
                .unwrap()
                .progress_chars("=>-")
        );

        for (name, _version) in deps {
            pb.set_message(format!("Installing {}", name));
            
            let status = Command::new(&vcpkg_exe)
                .args(["install", "--triplet", &self.detect_triplet()])
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .status()
                .map_err(|e| format!("Failed to run vcpkg: {}", e))?;

            if !status.success() {
                pb.finish_and_clear();
                return Err(format!("Failed to install {}", name));
            }

            pb.inc(1);
        }

        pb.finish_with_message("All dependencies installed");
        println!("{}", "✓ Dependencies installed successfully".green());
        Ok(())
    }

    fn generate_vcpkg_json(&self, deps: &HashMap<String, String>) -> Result<(), String> {
        let mut vcpkg_json = serde_json::json!({
            "dependencies": []
        });

        let dep_list: Vec<String> = deps.keys().cloned().collect();
        vcpkg_json["dependencies"] = Value::Array(
            dep_list.iter().map(|d| Value::String(d.clone())).collect()
        );

        fs::write("vcpkg.json", serde_json::to_string_pretty(&vcpkg_json).unwrap())
            .map_err(|e| e.to_string())?;

        Ok(())
    }

    fn detect_triplet(&self) -> String {
        if cfg!(target_os = "macos") {
            if cfg!(target_arch = "aarch64") { "arm64-osx" } else { "x64-osx" }
        } else if cfg!(target_os = "linux") {
            "x64-linux"
        } else {
            "x64-windows"
        }.to_string()
    }
}
