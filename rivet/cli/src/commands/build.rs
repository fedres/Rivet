use std::fs;
use crate::config::RivetConfig;
use crate::build::manager::BuildManager;

pub fn execute() {
    let config_content = fs::read_to_string("rivet.toml").expect("Failed to read rivet.toml");
    let config: RivetConfig = toml::from_str(&config_content).expect("Failed to parse rivet.toml");

    let manager = BuildManager::new(config);
    if let Err(e) = manager.build() {
        eprintln!("Error: {}", e);
        std::process::exit(1);
    }
}
