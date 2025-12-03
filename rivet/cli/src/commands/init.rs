use std::fs;
use std::path::Path;
use crate::config::RivetConfig;

pub fn execute() {
    let path = Path::new("rivet.toml");
    if path.exists() {
        println!("Error: rivet.toml already exists in this directory.");
        return;
    }

    let current_dir = std::env::current_dir().unwrap();
    let name = current_dir.file_name().unwrap().to_str().unwrap();

    let config = RivetConfig::new(name);
    let toml_string = toml::to_string(&config).unwrap();

    fs::write(path, toml_string).expect("Unable to write rivet.toml");
    println!("Initialized Rivet project in {}", current_dir.display());
}
