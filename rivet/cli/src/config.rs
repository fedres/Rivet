use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Debug, Deserialize, Serialize)]
pub struct RivetConfig {
    pub package: PackageConfig,
    pub dependencies: Option<HashMap<String, String>>,
    pub toolchain: Option<ToolchainConfig>,
    pub workspace: Option<WorkspaceConfig>,
    pub lib: Option<LibraryConfig>,
    pub bin: Option<Vec<BinaryConfig>>,
    pub example: Option<Vec<ExampleConfig>>,
    pub bench: Option<Vec<BenchConfig>>,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct PackageConfig {
    pub name: String,
    pub version: String,
    pub edition: Option<String>,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct ToolchainConfig {
    pub compiler: String,
    pub version: Option<String>,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct WorkspaceConfig {
    pub members: Vec<String>,
    pub resolver: Option<String>,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct LibraryConfig {
    pub name: Option<String>,
    pub path: Option<String>,
    #[serde(rename = "crate-type")]
    pub crate_type: Option<String>,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct BinaryConfig {
    pub name: String,
    pub path: String,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct ExampleConfig {
    pub name: String,
    pub path: String,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct BenchConfig {
    pub name: String,
    pub path: String,
}

impl RivetConfig {
    pub fn default_config(name: String) -> Self {
        RivetConfig {
            package: PackageConfig {
                name,
                version: "0.1.0".to_string(),
                edition: Some("2023".to_string()),
            },
            dependencies: None,
            toolchain: Some(ToolchainConfig {
                compiler: "clang++".to_string(),
                version: Some("auto".to_string()),
            }),
            workspace: None,
            lib: None,
            bin: None,
            example: None,
            bench: None,
        }
    }
}
