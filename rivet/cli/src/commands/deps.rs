use std::fs;
use crate::config::RivetConfig;

pub fn add(name: String) {
    let config_content = fs::read_to_string("rivet.toml").expect("Failed to read rivet.toml");
    let mut config: RivetConfig = toml::from_str(&config_content).expect("Failed to parse rivet.toml");

    if let Some(deps) = &mut config.dependencies {
        deps.insert(name.clone(), "*".to_string()); // Default to latest
    } else {
        let mut deps = std::collections::HashMap::new();
        deps.insert(name.clone(), "*".to_string());
        config.dependencies = Some(deps);
    }

    let new_toml = toml::to_string(&config).unwrap();
    fs::write("rivet.toml", new_toml).expect("Failed to write rivet.toml");
    println!("Added dependency: {}", name);
}

pub fn remove(name: String) {
    let config_content = fs::read_to_string("rivet.toml").expect("Failed to read rivet.toml");
    let mut config: RivetConfig = toml::from_str(&config_content).expect("Failed to parse rivet.toml");

    if let Some(deps) = &mut config.dependencies {
        if deps.remove(&name).is_some() {
            let new_toml = toml::to_string(&config).unwrap();
            fs::write("rivet.toml", new_toml).expect("Failed to write rivet.toml");
            println!("Removed dependency: {}", name);
        } else {
            println!("Dependency '{}' not found.", name);
        }
    }
}
