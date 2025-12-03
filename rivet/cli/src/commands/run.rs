use crate::commands::build;
use crate::config::RivetConfig;
use colored::Colorize;
use std::process::Command;
use std::fs;

pub fn execute(bin_name: Option<String>) {
    // Read config to get project name
    let config_content = fs::read_to_string("rivet.toml")
        .expect("Could not read rivet.toml");
    let config: RivetConfig = toml::from_str(&config_content)
        .expect("Could not parse rivet.toml");
    
    // Determine binary name
    let binary = bin_name.unwrap_or(config.package.name.clone());
    let binary_path = format!("target/debug/{}", binary);
    
    // Build first
    println!("{}", "🔨 Building...".cyan().bold());
    build::execute();
    
    // Run
    println!("\n{}", "🚀 Running...".cyan().bold());
    println!("{}\n", "─".repeat(50).dimmed());
    
    let status = Command::new(&binary_path)
        .status()
        .expect("Failed to run binary");
    
    if !status.success() {
        eprintln!("\n{} Process exited with code {:?}", "✗".red(), status.code());
        std::process::exit(status.code().unwrap_or(1));
    }
}
