use std::path::{Path, PathBuf};
use std::process::Command;
use std::fs;

use crate::dependency::vcpkg::VcpkgManager;
use crate::workspace::Workspace;
use crate::package::{Target, LibraryKind};
use crate::build_system::compiler::Compiler;
use colored::Colorize;
use tracing::info;

pub struct BuildManager {
    workspace: Workspace,
}

impl BuildManager {
    pub fn new() -> Result<Self, String> {
        let workspace = Workspace::discover(Path::new("."))?;
        Ok(BuildManager { workspace })
    }
    
    pub fn build(&self) -> Result<(), String> {
        if self.workspace.is_multi_package() {
            println!("{} {} packages", 
                "🔨 Building workspace with".cyan().bold(), 
                self.workspace.members.len()
            );
        } else {
            println!("{} {}", 
                "🔨 Building".cyan().bold(), 
                self.workspace.members[0].name
            );
        }
        
        // Install dependencies for all packages
        self.install_dependencies()?;
        
        // Build packages in order
        let build_order = self.workspace.build_order();
        for package in build_order {
            self.build_package(package)?;
        }
        
        println!("\n{} Build complete", "✓".green());
        Ok(())
    }
    
    fn install_dependencies(&self) -> Result<(), String> {
        // Collect all unique dependencies
        let mut all_deps = std::collections::HashMap::new();
        for package in &self.workspace.members {
            for (name, version) in &package.dependencies {
                all_deps.insert(name.clone(), version.clone());
            }
        }
        
        if !all_deps.is_empty() {
            info!("Installing {} dependencies", all_deps.len());
            let vcpkg = VcpkgManager::new();
            vcpkg.bootstrap()?;
            vcpkg.install_dependencies(&all_deps)?;
        }
        
        Ok(())
    }
    
    fn build_package(&self, package: &crate::package::Package) -> Result<(), String> {
        println!("\n{} {}", "Building".green(), package.name.bold());
        
        for target in &package.targets {
            self.build_target(package, target)?;
        }
        
        Ok(())
    }
    
    fn build_target(&self, package: &crate::package::Package, target: &Target) -> Result<(), String> {
        match target {
            Target::Library { name, path, kind } => {
                self.build_library(package, name, path, kind)
            }
            Target::Binary { name, path } => {
                self.build_binary(package, name, path)
            }
            Target::Example { name, path } => {
                self.build_binary(package, name, path)
            }
            Target::Bench { name, path } => {
                self.build_binary(package, name, path)
            }
        }
    }
    
    fn build_library(&self, package: &crate::package::Package, name: &str, path: &Path, kind: &LibraryKind) -> Result<(), String> {
        println!("  {} library {}", "Compiling".cyan(), name);
        
        if !path.exists() {
            return Err(format!("Library source not found: {}", path.display()));
        }
        
        let compiler = self.get_compiler()?;
        let obj_dir = self.workspace.target_dir.join("debug").join(&package.name);
        fs::create_dir_all(&obj_dir).map_err(|e| e.to_string())?;
        
        let obj_path = obj_dir.join(format!("{}.o", name));
        let include_dirs = vec![package.root.join("include")];
        
        // Compile to object
        compiler.compile_object(path, &obj_path, &include_dirs)?;
        
        // Link into library
        match kind {
            LibraryKind::Static => {
                let lib_path = self.workspace.target_dir.join("debug").join(format!("lib{}.a", name));
                self.link_static_lib(&[obj_path], &lib_path)?;
            }
            LibraryKind::Shared => {
                let lib_path = self.workspace.target_dir.join("debug").join(format!("lib{}.so", name));
                compiler.link_binary(&[obj_path], &lib_path, &[])?;
            }
            LibraryKind::Both => {
                let static_path = self.workspace.target_dir.join("debug").join(format!("lib{}.a", name));
                let shared_path = self.workspace.target_dir.join("debug").join(format!("lib{}.so", name));
                self.link_static_lib(&[obj_path.clone()], &static_path)?;
                compiler.link_binary(&[obj_path], &shared_path, &[])?;
            }
        }
        
        Ok(())
    }
    
    fn build_binary(&self, package: &crate::package::Package, name: &str, path: &Path) -> Result<(), String> {
        println!("  {} binary {}", "Compiling".cyan(), name);
        
        if !path.exists() {
            return Err(format!("Binary source not found: {}", path.display()));
        }
        
        let compiler = self.get_compiler()?;
        let output = self.workspace.target_dir.join("debug").join(name);
        fs::create_dir_all(output.parent().unwrap()).map_err(|e| e.to_string())?;
        
        // Simple single-file compilation for now
        let mut cmd = Command::new(compiler.executable_path());
        cmd.arg(path)
           .arg("-o")
           .arg(&output)
           .arg("-std=c++17")
           .arg("-g");
        
        // Add include paths
        cmd.arg("-I").arg(package.root.join("include"));
        
        // Add vcpkg paths if we have dependencies
        if !package.dependencies.is_empty() {
            let triplet = self.detect_triplet();
            let vcpkg_root = PathBuf::from("vcpkg_installed").join(&triplet);
            
            if vcpkg_root.join("include").exists() {
                cmd.arg("-I").arg(vcpkg_root.join("include"));
            }
            if vcpkg_root.join("lib").exists() {
                cmd.arg("-L").arg(vcpkg_root.join("lib"));
            }
        }
        
        let status = cmd.status().map_err(|e| format!("Failed to run compiler: {}", e))?;
        
        if !status.success() {
            return Err("Build failed".to_string());
        }
        
        println!("    {} {}", "Finished".green(), output.display());
        Ok(())
    }
    
    fn link_static_lib(&self, objects: &[PathBuf], output: &Path) -> Result<(), String> {
        let mut cmd = Command::new("ar");
        cmd.arg("rcs")
           .arg(output);
        
        for obj in objects {
            cmd.arg(obj);
        }
        
        let status = cmd.status().map_err(|e| format!("Failed to run ar: {}", e))?;
        
        if !status.success() {
            return Err("Failed to create static library".to_string());
        }
        
        Ok(())
    }
    
    fn get_compiler(&self) -> Result<Compiler, String> {
        // For now, use default clang++
        Ok(Compiler::new(PathBuf::from("clang++")))
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

impl Compiler {
    pub fn executable_path(&self) -> &Path {
        &self.executable
    }
}
