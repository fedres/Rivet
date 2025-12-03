use std::path::PathBuf;
use std::collections::HashMap;
use serde::{Serialize, Deserialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Package {
    pub name: String,
    pub version: String,
    pub root: PathBuf,
    pub targets: Vec<Target>,
    pub dependencies: HashMap<String, String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum Target {
    Binary {
        name: String,
        path: PathBuf,
    },
    Library {
        name: String,
        path: PathBuf,
        kind: LibraryKind,
    },
    Example {
        name: String,
        path: PathBuf,
    },
    Bench {
        name: String,
        path: PathBuf,
    },
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum LibraryKind {
    Static,
    Shared,
    Both,
}

impl Target {
    pub fn name(&self) -> &str {
        match self {
            Target::Binary { name, .. } => name,
            Target::Library { name, .. } => name,
            Target::Example { name, .. } => name,
            Target::Bench { name, .. } => name,
        }
    }
    
    pub fn path(&self) -> &PathBuf {
        match self {
            Target::Binary { path, .. } => path,
            Target::Library { path, .. } => path,
            Target::Example { path, .. } => path,
            Target::Bench { path, .. } => path,
        }
    }
    
    pub fn is_library(&self) -> bool {
        matches!(self, Target::Library { .. })
    }
    
    pub fn is_binary(&self) -> bool {
        matches!(self, Target::Binary { .. })
    }
}

impl Package {
    pub fn from_config(root: PathBuf, config: &crate::config::RivetConfig) -> Self {
        let mut targets = Vec::new();
        
        // Check for library
        if let Some(lib_config) = &config.lib {
            let kind = match lib_config.crate_type.as_deref() {
                Some("staticlib") => LibraryKind::Static,
                Some("dylib") => LibraryKind::Shared,
                _ => LibraryKind::Both,
            };
            
            targets.push(Target::Library {
                name: lib_config.name.clone().unwrap_or_else(|| config.package.name.clone()),
                path: root.join(lib_config.path.as_deref().unwrap_or("src/lib.cpp")),
                kind,
            });
        }
        
        // Check for binaries
        if let Some(bins) = &config.bin {
            for bin_config in bins {
                targets.push(Target::Binary {
                    name: bin_config.name.clone(),
                    path: root.join(&bin_config.path),
                });
            }
        } else if !targets.iter().any(|t| t.is_library()) {
            // Default binary if no lib and no explicit bins
            targets.push(Target::Binary {
                name: config.package.name.clone(),
                path: root.join("src/main.cpp"),
            });
        }
        
        // Examples
        if let Some(examples) = &config.example {
            for ex_config in examples {
                targets.push(Target::Example {
                    name: ex_config.name.clone(),
                    path: root.join(&ex_config.path),
                });
            }
        }
        
        // Benchmarks
        if let Some(benches) = &config.bench {
            for bench_config in benches {
                targets.push(Target::Bench {
                    name: bench_config.name.clone(),
                    path: root.join(&bench_config.path),
                });
            }
        }
        
        Package {
            name: config.package.name.clone(),
            version: config.package.version.clone(),
            root,
            targets,
            dependencies: config.dependencies.clone().unwrap_or_default(),
        }
    }
}
