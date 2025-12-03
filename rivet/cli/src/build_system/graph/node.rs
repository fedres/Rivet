use std::path::PathBuf;
use serde::{Serialize, Deserialize};

#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum BuildNode {
    SourceFile {
        path: PathBuf,
        hash: String,
    },
    ObjectFile {
        path: PathBuf,
    },
    StaticLib {
        name: String,
        path: PathBuf,
    },
    SharedLib {
        name: String,
        path: PathBuf,
    },
    Binary {
        name: String,
        path: PathBuf,
    },
}

impl BuildNode {
    pub fn path(&self) -> &PathBuf {
        match self {
            BuildNode::SourceFile { path, .. } => path,
            BuildNode::ObjectFile { path } => path,
            BuildNode::StaticLib { path, .. } => path,
            BuildNode::SharedLib { path, .. } => path,
            BuildNode::Binary { path, .. } => path,
        }
    }
    
    pub fn name(&self) -> String {
        match self {
            BuildNode::SourceFile { path, .. } => {
                path.file_stem().unwrap().to_string_lossy().to_string()
            }
            BuildNode::ObjectFile { path } => {
                path.file_stem().unwrap().to_string_lossy().to_string()
            }
            BuildNode::StaticLib { name, .. } => name.clone(),
            BuildNode::SharedLib { name, .. } => name.clone(),
            BuildNode::Binary { name, .. } => name.clone(),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EdgeType {
    Compile,      // Source -> Object
    Link,         // Object -> Binary/Lib
    Dependency,   // Header dependency
}
