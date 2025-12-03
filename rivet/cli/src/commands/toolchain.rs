use crate::toolchain::manager::ToolchainManager;
use crate::config::RivetConfig;
use std::fs;

pub fn list() {
    let manager = ToolchainManager::new();
    let toolchains = manager.list();

    println!("Installed Toolchains:");
    for tc in toolchains {
        println!("- {} ({:?}) -> {}", tc.name, tc.source, tc.path.display());
    }
}

pub fn install(name: String) {
    let manager = ToolchainManager::new();
    if let Err(e) = manager.install(&name) {
        eprintln!("Error installing toolchain: {}", e);
    }
}

pub fn use_toolchain(name: String) {
    // Update rivet.toml
    let config_content = fs::read_to_string("rivet.toml").expect("Failed to read rivet.toml");
    let mut config: RivetConfig = toml::from_str(&config_content).expect("Failed to parse rivet.toml");

    // For now, we just update the toolchain config
    // We need to make ToolchainConfig fields public or add a method, but for now let's just create it
    // Wait, ToolchainConfig fields are public in config.rs
    
    let updated_config = RivetConfig {
        package: config.package,
        dependencies: config.dependencies,
        toolchain: Some(crate::config::ToolchainConfig {
            compiler: name.clone(),
            version: Some("auto".to_string()),
        }),
        workspace: config.workspace,
        lib: config.lib,
        bin: config.bin,
        example: config.example,
        bench: config.bench,
    };

    let new_toml = toml::to_string(&updated_config).unwrap();
    fs::write("rivet.toml", new_toml).expect("Failed to write rivet.toml");
    println!("Set active toolchain to: {}", name);
}
