use std::path::PathBuf;
use thiserror::Error;

#[derive(Error, Debug)]
pub enum RivetError {
    #[error("Could not find rivet.toml at {path}\nRun 'rivet init' to create a new project")]
    ConfigNotFound { path: PathBuf },
    
    #[error("Failed to parse rivet.toml: {reason}")]
    ConfigParseFailed { reason: String },
    
    #[error("Dependency '{name}' not found in vcpkg registry")]
    DependencyNotFound { name: String },
    
    #[error("Build failed: {reason}")]
    BuildFailed { reason: String },
    
    #[error("Toolchain '{name}' not found\nRun 'rivet toolchain install {name}' to install it")]
    ToolchainNotFound { name: String },
    
    #[error("vcpkg bootstrap failed: {reason}")]
    VcpkgBootstrapFailed { reason: String },
    
    #[error("vcpkg install failed: {reason}")]
    VcpkgInstallFailed { reason: String },
    
    #[error("No source files found in {path}")]
    NoSourceFiles { path: String },
    
    #[error("Test framework not detected\nSupported frameworks: Catch2, GoogleTest")]
    TestFrameworkNotDetected,
    
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
    
    #[error("TOML parse error: {0}")]
    TomlParse(#[from] toml::de::Error),
    
    #[error("TOML serialize error: {0}")]
    TomlSerialize(#[from] toml::ser::Error),
}

pub type Result<T> = std::result::Result<T, RivetError>;
