use std::fs;
use std::path::Path;
use std::process::Command;

pub fn lint_files() -> Result<(), String> {
    println!("Running clang-tidy...");
    
    let files = find_source_files()?;
    if files.is_empty() {
        println!("No source files found");
        return Ok(());
    }
    
    let mut has_errors = false;
    
    for file in &files {
        println!("Linting {}...", file);
        let status = Command::new("clang-tidy")
            .arg(file)
            .arg("--")
            .arg("-std=c++17")
            .status()
            .map_err(|e| format!("Failed to run clang-tidy: {}", e))?;
        
        if !status.success() {
            has_errors = true;
        }
    }
    
    if has_errors {
        println!("✗ Lint found issues");
        return Err("Lint failed".to_string());
    }
    
    println!("✅ Lint passed");
    Ok(())
}

fn find_source_files() -> Result<Vec<String>, String> {
    let mut files = Vec::new();
    let paths = vec!["src", "include"];
    
    for path in paths {
        if !Path::new(path).exists() {
            continue;
        }
        
        walk_dir(path, &mut files)?;
    }
    
    Ok(files)
}

fn walk_dir(dir: &str, files: &mut Vec<String>) -> Result<(), String> {
    if let Ok(entries) = fs::read_dir(dir) {
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_dir() {
                walk_dir(path.to_str().unwrap(), files)?;
            } else if let Some(ext) = path.extension() {
                if ext == "cpp" || ext == "cc" {
                    files.push(path.to_string_lossy().to_string());
                }
            }
        }
    }
    Ok(())
}
