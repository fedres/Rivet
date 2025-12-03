use std::fs;
use std::path::Path;
use crate::config::RivetConfig;

pub fn execute() {
    let config_path = "rivet.toml";

    if std::path::Path::new(config_path).exists() {
        eprintln!("Error: rivet.toml already exists");
        std::process::exit(1);
    }

    let current_dir = std::env::current_dir().expect("Could not get current directory");
    let project_name = current_dir
        .file_name()
        .expect("Could not get directory name")
        .to_string_lossy()
        .to_string();

    let config = RivetConfig::default_config(project_name);
    let toml_str = toml::to_string(&config).expect("Could not serialize config");

    fs::write(config_path, toml_str).expect("Could not write rivet.toml");
    println!("Initialized Rivet project");
}
