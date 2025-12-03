use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::time::SystemTime;
use std::fs;
use serde::{Serialize, Deserialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BuildCache {
    files: HashMap<PathBuf, FileInfo>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct FileInfo {
    hash: String,
    timestamp: u64,  // Unix timestamp
    dependencies: Vec<PathBuf>,
}

impl BuildCache {
    pub fn new() -> Self {
        BuildCache {
            files: HashMap::new(),
        }
    }
    
    pub fn load(cache_path: &Path) -> Result<Self, String> {
        if !cache_path.exists() {
            return Ok(Self::new());
        }
        
        let content = fs::read_to_string(cache_path)
            .map_err(|e| format!("Failed to read cache: {}", e))?;
        
        serde_json::from_str(&content)
            .map_err(|e| format!("Failed to parse cache: {}", e))
    }
    
    pub fn save(&self, cache_path: &Path) -> Result<(), String> {
        let content = serde_json::to_string_pretty(self)
            .map_err(|e| format!("Failed to serialize cache: {}", e))?;
        
        if let Some(parent) = cache_path.parent() {
            fs::create_dir_all(parent)
                .map_err(|e| format!("Failed to create cache directory: {}", e))?;
        }
        
        fs::write(cache_path, content)
            .map_err(|e| format!("Failed to write cache: {}", e))
    }
    
    pub fn needs_rebuild(&self, path: &Path, current_hash: &str) -> bool {
        match self.files.get(path) {
            None => true,  // File not in cache, needs build
            Some(info) => {
                // Check if hash changed
                if info.hash != current_hash {
                    return true;
                }
                
                // Check if any dependencies changed
                for dep in &info.dependencies {
                    if let Ok(metadata) = fs::metadata(dep) {
                        if let Ok(modified) = metadata.modified() {
                            let dep_time = modified.duration_since(SystemTime::UNIX_EPOCH)
                                .unwrap().as_secs();
                            if dep_time > info.timestamp {
                                return true;
                            }
                        }
                    }
                }
                
                false
            }
        }
    }
    
    pub fn update(&mut self, path: PathBuf, hash: String, dependencies: Vec<PathBuf>) {
        let timestamp = SystemTime::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .unwrap()
            .as_secs();
        
        self.files.insert(path, FileInfo {
            hash,
            timestamp,
            dependencies,
        });
    }
    
    pub fn clear(&mut self) {
        self.files.clear();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;
    
    #[test]
    fn test_cache_save_load() {
        let temp = TempDir::new().unwrap();
        let cache_path = temp.path().join("cache.json");
        
        let mut cache = BuildCache::new();
        cache.update(
            PathBuf::from("src/main.cpp"),
            "abc123".to_string(),
            vec![],
        );
        
        cache.save(&cache_path).unwrap();
        
        let loaded = BuildCache::load(&cache_path).unwrap();
        assert_eq!(loaded.files.len(), 1);
    }
    
    #[test]
    fn test_needs_rebuild() {
        let mut cache = BuildCache::new();
        let path = PathBuf::from("src/main.cpp");
        
        // Not in cache, needs rebuild
        assert!(cache.needs_rebuild(&path, "abc"));
        
        // Add to cache
        cache.update(path.clone(), "abc".to_string(), vec![]);
        
        // Same hash, no rebuild
        assert!(!cache.needs_rebuild(&path, "abc"));
        
        // Different hash, needs rebuild
        assert!(cache.needs_rebuild(&path, "def"));
    }
}
