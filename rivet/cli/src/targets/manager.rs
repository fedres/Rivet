use std::collections::HashMap;
use std::fs;
use std::path::PathBuf;
use crate::target::types::Target;
use colored::Colorize;

pub struct TargetManager {
    config_dir: PathBuf,
}

impl TargetManager {
    pub fn new() -> Self {
        let home = dirs::home_dir().expect("Could not find home directory");
        let config_dir = home.join(".rivet").join("targets");
        fs::create_dir_all(&config_dir).ok();
        TargetManager { config_dir }
    }
    
    pub fn add_target(&self, triple: String) -> Result<(), String> {
        let target = Target::new(triple.clone());
        let target_file = self.config_dir.join(format!("{}.json", triple));
        
        let json = serde_json::to_string_pretty(&target).map_err(|e| e.to_string())?;
        fs::write(&target_file, json).map_err(|e| e.to_string())?;
        
        println!("{} {}", "✓ Added target:".green(), triple);
        println!("  vcpkg triplet: {}", target.to_vcpkg_triplet());
        Ok(())
    }
    
    pub fn list_targets(&self) -> Result<Vec<Target>, String> {
        let mut targets = Vec::new();
        
        if !self.config_dir.exists() {
            return Ok(targets);
        }
        
        let entries = fs::read_dir(&self.config_dir).map_err(|e| e.to_string())?;
        
        for entry in entries.flatten() {
            if let Some(ext) = entry.path().extension() {
                if ext == "json" {
                    if let Ok(content) = fs::read_to_string(entry.path()) {
                        if let Ok(target) = serde_json::from_str::<Target>(&content) {
                            targets.push(target);
                        }
                    }
                }
            }
        }
        
        Ok(targets)
    }
    
    pub fn remove_target(&self, triple: &str) -> Result<(), String> {
        let target_file = self.config_dir.join(format!("{}.json", triple));
        
        if !target_file.exists() {
            return Err(format!("Target '{}' not found", triple));
        }
        
        fs::remove_file(&target_file).map_err(|e| e.to_string())?;
        println!("{} {}", "✓ Removed target:".green(), triple);
        Ok(())
    }
    
    pub fn get_target(&self, triple: &str) -> Result<Target, String> {
        let target_file = self.config_dir.join(format!("{}.json", triple));
        
        if !target_file.exists() {
            return Err(format!("Target '{}' not found", triple));
        }
        
        let content = fs::read_to_string(&target_file).map_err(|e| e.to_string())?;
        serde_json::from_str(&content).map_err(|e| e.to_string())
    }
}
