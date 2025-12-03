use crate::registry::client::RegistryClient;
use colored::Colorize;

pub fn execute(query: String) {
    let client = RegistryClient::new();
    
    match client.search(&query) {
        Ok(results) => {
            if results.is_empty() {
                println!("No packages found for '{}'", query);
                return;
            }
            
            println!("\n{}", "Search Results:".cyan().bold());
            for result in results {
                println!("\n  {} {} {}", 
                    "→".cyan(),
                    result.name.bold(),
                    format!("v{}", result.version).dimmed()
                );
                if let Some(desc) = result.description {
                    println!("    {}", desc);
                }
                println!("    {} downloads", result.downloads);
            }
            
            println!("\n{}", format!("Add with: rivet add <package>").dimmed());
        }
        Err(e) => {
            eprintln!("{} {}", "Error:".red(), e);
            std::process::exit(1);
        }
    }
}
