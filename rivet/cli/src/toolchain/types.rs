use serde::{Serialize, Deserialize};
use std::path::PathBuf;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum ToolchainSource {
    System,
    Rivet,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Toolchain {
    pub name: String,
    pub path: PathBuf,
    pub version: String,
    pub source: ToolchainSource,
}
