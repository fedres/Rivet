use std::fs;
use std::path::Path;
use crate::config::RivetConfig;

pub fn execute(name: String, lib: bool) {
    let project_dir = std::path::Path::new(&name);

    if project_dir.exists() {
        eprintln!("Error: Directory '{}' already exists", name);
        std::process::exit(1);
    }

    fs::create_dir_all(project_dir).expect("Could not create project directory");
    fs::create_dir_all(project_dir.join("src")).expect("Could not create src directory");

    let config = RivetConfig::default_config(name.clone());
    let toml_str = toml::to_string(&config).expect("Could not serialize config");
    fs::write(project_dir.join("rivet.toml"), toml_str).expect("Could not write rivet.toml");

    // Create main.cpp
    let main_cpp = r#"#include <iostream>

int main() {
    std::cout << "Hello from Rivet!" << std::endl;
    return 0;
}
"#;
    fs::write(project_dir.join("src/main.cpp"), main_cpp).expect("Could not write main.cpp");

    println!("Created Rivet project '{}'", name);
}
