use serde::{Serialize, Deserialize};
use std::collections::HashMap;

#[derive(Serialize, Deserialize, Debug)]
pub struct RivetConfig {
    pub package: PackageConfig,
    pub toolchain: Option<ToolchainConfig>,
    pub dependencies: Option<HashMap<String, String>>,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct PackageConfig {
    pub name: String,
    pub version: String,
    pub edition: String,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct ToolchainConfig {
    pub compiler: String,
    pub version: String,
}

impl RivetConfig {
    pub fn new(name: &str) -> Self {
        RivetConfig {
            package: PackageConfig {
                name: name.to_string(),
                version: "0.1.0".to_string(),
                edition: "2023".to_string(),
            },
            toolchain: None,
            dependencies: Some(HashMap::new()),
        }
    }
}
