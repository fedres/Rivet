use colored::Colorize;
use std::fs;
use std::path::Path;

pub fn execute() {
    println!("{}", "🧹 Cleaning build artifacts...".cyan().bold());
    
    let target_dir = Path::new("target");
    
    if !target_dir.exists() {
        println!("Nothing to clean (target/ doesn't exist)");
        return;
    }
    
    match fs::remove_dir_all(target_dir) {
        Ok(_) => {
            println!("{} Removed target/", "✓".green());
        }
        Err(e) => {
            eprintln!("{} Failed to remove target/: {}", "Error:".red(), e);
            std::process::exit(1);
        }
    }
    
    println!("\n{} Clean complete", "✓".green());
}
