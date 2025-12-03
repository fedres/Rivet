use serde::{Serialize, Deserialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Target {
    pub triple: String,
    pub toolchain: Option<String>,
    pub sysroot: Option<String>,
}

impl Target {
    pub fn new(triple: String) -> Self {
        Target {
            triple,
            toolchain: None,
            sysroot: None,
        }
    }
    
    pub fn to_vcpkg_triplet(&self) -> String {
        // Map Rust-style target triples to vcpkg triplets
        match self.triple.as_str() {
            "x86_64-unknown-linux-gnu" => "x64-linux",
            "aarch64-unknown-linux-gnu" => "arm64-linux",
            "x86_64-apple-darwin" => "x64-osx",
            "aarch64-apple-darwin" => "arm64-osx",
            "x86_64-pc-windows-msvc" => "x64-windows",
            "i686-pc-windows-msvc" => "x86-windows",
            "aarch64-pc-windows-msvc" => "arm64-windows",
            _ => {
                // Fallback: try to parse the triple
                if self.triple.contains("x86_64") && self.triple.contains("linux") {
                    "x64-linux"
                } else if self.triple.contains("aarch64") && self.triple.contains("darwin") {
                    "arm64-osx"
                } else if self.triple.contains("x86_64") && self.triple.contains("windows") {
                    "x64-windows"
                } else {
                    "x64-linux" // Default fallback
                }
            }
        }.to_string()
    }
}
