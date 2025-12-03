use crate::target::manager::TargetManager;
use colored::Colorize;

pub fn add(triple: String) {
    let manager = TargetManager::new();
    if let Err(e) = manager.add_target(triple) {
        eprintln!("{} {}", "Error:".red(), e);
        std::process::exit(1);
    }
}

pub fn list() {
    let manager = TargetManager::new();
    match manager.list_targets() {
        Ok(targets) => {
            if targets.is_empty() {
                println!("No targets configured");
                println!("\nAdd a target with: {}", "rivet target add <triple>".cyan());
                return;
            }
            
            println!("{}", "Configured Targets:".cyan().bold());
            for target in targets {
                println!("\n  {} {}", "→".cyan(), target.triple.bold());
                println!("    vcpkg triplet: {}", target.to_vcpkg_triplet());
                if let Some(tc) = target.toolchain {
                    println!("    toolchain: {}", tc);
                }
                if let Some(sr) = target.sysroot {
                    println!("    sysroot: {}", sr);
                }
            }
        }
        Err(e) => {
            eprintln!("{} {}", "Error:".red(), e);
            std::process::exit(1);
        }
    }
}

pub fn remove(triple: String) {
    let manager = TargetManager::new();
    if let Err(e) = manager.remove_target(&triple) {
        eprintln!("{} {}", "Error:".red(), e);
        std::process::exit(1);
    }
}
