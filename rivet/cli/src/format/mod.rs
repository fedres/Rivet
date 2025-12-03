use std::fs;
use std::path::Path;
use std::process::Command;

pub fn format_files(check: bool) -> Result<(), String> {
    println!("Formatting source files...");
    
    let files = find_source_files()?;
    if files.is_empty() {
        println!("No source files found");
        return Ok(());
    }
    
    for file in &files {
        if check {
            // Check mode: verify formatting without modifying
            let status = Command::new("clang-format")
                .arg("--dry-run")
                .arg("--Werror")
                .arg(file)
                .status()
                .map_err(|e| format!("Failed to run clang-format: {}", e))?;
            
            if !status.success() {
                println!("✗ {} is not formatted", file);
            }
        } else {
            // Format mode: modify files in place
            let status = Command::new("clang-format")
                .arg("-i")
                .arg(file)
                .status()
                .map_err(|e| format!("Failed to run clang-format: {}", e))?;
            
            if status.success() {
                println!("✓ Formatted {}", file);
            }
        }
    }
    
    println!("✅ Formatting complete");
    Ok(())
}

fn find_source_files() -> Result<Vec<String>, String> {
    let mut files = Vec::new();
    let paths = vec!["src", "include", "tests", "test"];
    
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
                if ext == "cpp" || ext == "hpp" || ext == "h" || ext == "cc" {
                    files.push(path.to_string_lossy().to_string());
                }
            }
        }
    }
    Ok(())
}
