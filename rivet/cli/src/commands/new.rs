use std::fs;
use std::path::Path;
use crate::config::RivetConfig;

pub fn execute(name: String, _lib: bool) {
    let path = Path::new(&name);
    if path.exists() {
        println!("Error: Directory '{}' already exists.", name);
        return;
    }

    fs::create_dir(path).expect("Unable to create project directory");
    
    let config = RivetConfig::new(&name);
    let toml_string = toml::to_string(&config).unwrap();

    fs::write(path.join("rivet.toml"), toml_string).expect("Unable to write rivet.toml");
    
    // Create src directory and main.cpp
    let src_dir = path.join("src");
    fs::create_dir(&src_dir).expect("Unable to create src directory");
    
    let main_cpp = r#"#include <iostream>

int main() {
    std::cout << "Hello, Rivet!" << std::endl;
    return 0;
}
"#;
    fs::write(src_dir.join("main.cpp"), main_cpp).expect("Unable to write main.cpp");

    println!("Created new Rivet project '{}'", name);
}
