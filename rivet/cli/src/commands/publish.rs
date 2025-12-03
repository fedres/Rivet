use crate::config::RivetConfig;
use crate::registry::client::RegistryClient;
use crate::registry::types::PackageMetadata;
use colored::Colorize;
use std::fs;

pub fn execute() {
    // Read rivet.toml
    let config_content = fs::read_to_string("rivet.toml")
        .expect("Could not read rivet.toml");
    let config: RivetConfig = toml::from_str(&config_content)
        .expect("Could not parse rivet.toml");
    
    // Create package metadata
    let metadata = PackageMetadata {
        name: config.package.name.clone(),
        version: config.package.version.clone(),
        description: None,
        authors: vec![],
        license: None,
        dependencies: config.dependencies.unwrap_or_default(),
        repository: None,
    };
    
    // Publish to registry
    let client = RegistryClient::new();
    match client.publish(&metadata) {
        Ok(_) => {
            println!("{}", "✓ Publish complete".green());
        }
        Err(e) => {
            eprintln!("{} {}", "Error:".red(), e);
            std::process::exit(1);
        }
    }
}
