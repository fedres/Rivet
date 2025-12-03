use std::path::PathBuf;
use std::fs;
use std::os::unix::fs::symlink;
use crate::config::RivetConfig;
use std::process::Command;

pub struct IsolationManager {
    project_root: PathBuf,
    rivet_dir: PathBuf,
}

impl IsolationManager {
    pub fn new() -> Self {
        let project_root = std::env::current_dir().expect("Failed to get current directory");
        let rivet_dir = project_root.join(".rivet");
        IsolationManager { project_root, rivet_dir }
    }

    pub fn init(&self) -> Result<(), String> {
        if !self.rivet_dir.exists() {
            fs::create_dir_all(&self.rivet_dir).map_err(|e| e.to_string())?;
        }

        // 1. Symlink toolchain if configured
        let config_path = self.project_root.join("rivet.toml");
        if config_path.exists() {
            let config_content = fs::read_to_string(&config_path).map_err(|e| e.to_string())?;
            let config: RivetConfig = toml::from_str(&config_content).map_err(|e| e.to_string())?;

            if let Some(tc) = config.toolchain {
                let home = dirs::home_dir().expect("Could not find home directory");
                let toolchain_src = home.join(".rivet").join("toolchains").join(&tc.compiler);
                let toolchain_dest = self.rivet_dir.join("toolchain");

                if toolchain_src.exists() {
                    if toolchain_dest.exists() {
                        fs::remove_file(&toolchain_dest).ok(); // Remove existing symlink
                    }
                    symlink(&toolchain_src, &toolchain_dest).map_err(|e| e.to_string())?;
                    println!("Linked toolchain {} to .rivet/toolchain", tc.compiler);
                }
            }
        }

        // 2. Generate activation script (env.sh)
        let env_sh_path = self.rivet_dir.join("env.sh");
        let env_sh_content = format!(
            r#"#!/bin/bash
export RIVET_PROJECT_ROOT="{}"
export PATH="$RIVET_PROJECT_ROOT/.rivet/toolchain/bin:$PATH"
export CC="$RIVET_PROJECT_ROOT/.rivet/toolchain/bin/clang"
export CXX="$RIVET_PROJECT_ROOT/.rivet/toolchain/bin/clang++"
echo "Rivet environment activated"
"#,
            self.project_root.display()
        );
        fs::write(&env_sh_path, env_sh_content).map_err(|e| e.to_string())?;

        println!("Created isolated environment in .rivet/");
        println!("Run 'source .rivet/env.sh' to activate");

        Ok(())
    }

    pub fn run(&self, cmd: &str, args: &[String]) -> Result<(), String> {
        // Construct environment variables
        let toolchain_bin = self.rivet_dir.join("toolchain").join("bin");
        
        let mut command = Command::new(cmd);
        command.args(args);

        if toolchain_bin.exists() {
            let path = std::env::var("PATH").unwrap_or_default();
            let new_path = format!("{}:{}", toolchain_bin.display(), path);
            command.env("PATH", new_path);
            command.env("CC", toolchain_bin.join("clang"));
            command.env("CXX", toolchain_bin.join("clang++"));
        }

        let status = command.status().map_err(|e| format!("Failed to run command: {}", e))?;

        if !status.success() {
            return Err("Command failed".to_string());
        }

        Ok(())
    }
}
