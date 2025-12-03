use serde::{Serialize, Deserialize};
use std::collections::HashMap;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PackageMetadata {
    pub name: String,
    pub version: String,
    pub description: Option<String>,
    pub authors: Vec<String>,
    pub license: Option<String>,
    pub dependencies: HashMap<String, String>,
    pub repository: Option<String>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct SearchResult {
    pub name: String,
    pub version: String,
    pub description: Option<String>,
    pub downloads: u64,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct PublishRequest {
    pub metadata: PackageMetadata,
    pub tarball: Vec<u8>,
}
