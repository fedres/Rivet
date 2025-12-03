use std::path::{Path, PathBuf};
use std::fs;

use crate::config::RivetConfig;
use crate::package::Package;
use colored::Colorize;

pub struct Workspace {
    pub root: PathBuf,
    pub members: Vec<Package>,
    pub target_dir: PathBuf,
}

impl Workspace {
    /// Discover workspace from current directory
    pub fn discover(start_path: &Path) -> Result<Self, String> {
        // Try to find rivet.toml in current dir or parent dirs
        let mut current = start_path.to_path_buf();
        
        loop {
            let config_path = current.join("rivet.toml");
            if config_path.exists() {
                let content = fs::read_to_string(&config_path)
                    .map_err(|e| format!("Failed to read rivet.toml: {}", e))?;
                
                let config: RivetConfig = toml::from_str(&content)
                    .map_err(|e| format!("Failed to parse rivet.toml: {}", e))?;
                
                // Check if this is a workspace
                if let Some(workspace_config) = &config.workspace {
                    return Self::from_workspace_config(&current, workspace_config);
                } else {
                    // Single package workspace
                    return Self::from_single_package(current, config);
                }
            }
            
            if !current.pop() {
                break;
            }
        }
        
        Err("No rivet.toml found in current directory or parent directories".to_string())
    }
    
    fn from_workspace_config(root: &Path, workspace_config: &crate::config::WorkspaceConfig) -> Result<Self, String> {
        let mut members = Vec::new();
        
        println!("{}", "📦 Discovering workspace members...".cyan());
        
        for member_path in &workspace_config.members {
            let member_dir = root.join(member_path);
            let config_path = member_dir.join("rivet.toml");
            
            if !config_path.exists() {
                return Err(format!("Member '{}' does not have rivet.toml", member_path));
            }
            
            let content = fs::read_to_string(&config_path)
                .map_err(|e| format!("Failed to read {}/rivet.toml: {}", member_path, e))?;
            
            let config: RivetConfig = toml::from_str(&content)
                .map_err(|e| format!("Failed to parse {}/rivet.toml: {}", member_path, e))?;
            
            let package = Package::from_config(member_dir.clone(), &config);
            println!("  {} {}", "→".cyan(), package.name);
            members.push(package);
        }
        
        let target_dir = root.join("target");
        
        Ok(Workspace {
            root: root.to_path_buf(),
            members,
            target_dir,
        })
    }
    
    fn from_single_package(root: PathBuf, config: RivetConfig) -> Result<Self, String> {
        let package = Package::from_config(root.clone(), &config);
        let target_dir = root.join("target");
        
        Ok(Workspace {
            root,
            members: vec![package],
            target_dir,
        })
    }
    
    /// Get build order for packages based on dependencies
    pub fn build_order(&self) -> Vec<&Package> {
        // Simple topological sort based on internal dependencies
        // For now, just return in order (more sophisticated later)
        self.members.iter().collect()
    }
    
    /// Get package by name
    pub fn get_package(&self, name: &str) -> Option<&Package> {
        self.members.iter().find(|p| p.name == name)
    }
    
    /// Check if this is a multi-package workspace
    pub fn is_multi_package(&self) -> bool {
        self.members.len() > 1
    }
}
