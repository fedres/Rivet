use crate::build_system::compiler::Compiler;
use crate::build_system::graph::BuildGraph;
use colored::Colorize;
use std::path::{Path, PathBuf};

pub fn execute() {
    println!("{}", "🔍 Checking project...".cyan().bold());
    
    let root = Path::new("src");
    if !root.exists() {
        eprintln!("{} src/ directory not found", "Error:".red());
        std::process::exit(1);
    }
    
    // Discover source files
    let sources = BuildGraph::discover_sources(root);
    
    if sources.is_empty() {
        eprintln!("{} No source files found in src/", "Error:".red());
        std::process::exit(1);
    }
    
    println!("Found {} source files", sources.len());
    
    // Get compiler
    let compiler = Compiler::new(PathBuf::from("clang++"));
    
    // Check each source file
    let mut errors = 0;
    for src in &sources {
        match compiler.check_syntax(src, &[PathBuf::from("src")]) {
            Ok(_) => {}
            Err(e) => {
                eprintln!("{} {}", "Error:".red(), e);
                errors += 1;
            }
        }
    }
    
    if errors > 0 {
        eprintln!("\n{} Syntax check failed with {} errors", "✗".red(), errors);
        std::process::exit(1);
    }
    
    println!("\n{} All checks passed", "✓".green());
}
