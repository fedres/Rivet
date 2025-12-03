use std::path::PathBuf;
use std::fs;
use crate::toolchain::types::{Toolchain, ToolchainSource};
use which::which;

pub struct ToolchainManager {
    root_dir: PathBuf,
}

impl ToolchainManager {
    pub fn new() -> Self {
        let home = dirs::home_dir().expect("Could not find home directory");
        let root_dir = home.join(".rivet").join("toolchains");
        ToolchainManager { root_dir }
    }

    pub fn list(&self) -> Vec<Toolchain> {
        let mut toolchains = Vec::new();

        // 1. Detect system toolchains
        if let Ok(path) = which("clang++") {
            toolchains.push(Toolchain {
                name: "clang++ (system)".to_string(),
                path,
                version: "unknown".to_string(), // TODO: Parse version
                source: ToolchainSource::System,
            });
        }
        if let Ok(path) = which("g++") {
            toolchains.push(Toolchain {
                name: "g++ (system)".to_string(),
                path,
                version: "unknown".to_string(),
                source: ToolchainSource::System,
            });
        }
        if let Ok(path) = which("cl.exe") {
            toolchains.push(Toolchain {
                name: "msvc (system)".to_string(),
                path,
                version: "unknown".to_string(),
                source: ToolchainSource::System,
            });
        }

        // 2. List installed toolchains
        if self.root_dir.exists() {
            if let Ok(entries) = fs::read_dir(&self.root_dir) {
                for entry in entries.flatten() {
                    if let Ok(file_type) = entry.file_type() {
                        if file_type.is_dir() {
                            let name = entry.file_name().to_string_lossy().to_string();
                            let path = entry.path().join("bin").join("clang++"); // Assumption for now
                            toolchains.push(Toolchain {
                                name,
                                path,
                                version: "managed".to_string(),
                                source: ToolchainSource::Rivet,
                            });
                        }
                    }
                }
            }
        }

        toolchains
    }

    pub fn install(&self, name: &str) -> Result<(), String> {
        // Mock installation for now
        println!("Downloading toolchain {}...", name);
        
        let install_dir = self.root_dir.join(name);
        if install_dir.exists() {
            return Ok(()); // Already installed
        }

        fs::create_dir_all(&install_dir).map_err(|e| e.to_string())?;
        let bin_dir = install_dir.join("bin");
        fs::create_dir_all(&bin_dir).map_err(|e| e.to_string())?;

        // Create a dummy compiler script
        let compiler_path = bin_dir.join("clang++");
        let script = if cfg!(windows) {
            "@echo off\necho Mock clang++ called with args: %*"
        } else {
            "#!/bin/sh\necho \"Mock clang++ called with args: $@\""
        };
        
        fs::write(&compiler_path, script).map_err(|e| e.to_string())?;
        
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let mut perms = fs::metadata(&compiler_path).unwrap().permissions();
            perms.set_mode(0o755);
            fs::set_permissions(&compiler_path, perms).map_err(|e| e.to_string())?;
        }

        println!("Installed toolchain {} to {}", name, install_dir.display());
        Ok(())
    }
}
