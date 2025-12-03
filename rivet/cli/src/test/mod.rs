use std::fs;
use std::path::Path;
use std::process::Command;

pub struct TestRunner;

impl TestRunner {
    pub fn run() -> Result<(), String> {
        println!("Running tests...");
        
        // 1. Detect test framework
        let framework = Self::detect_framework()?;
        println!("Detected test framework: {:?}", framework);
        
        // 2. Find test files
        let test_files = Self::find_test_files()?;
        if test_files.is_empty() {
            return Err("No test files found".to_string());
        }
        
        // 3. Compile test binary
        let test_binary = Self::compile_tests(&test_files)?;
        
        // 4. Run tests
        Self::execute_tests(&test_binary)?;
        
        Ok(())
    }
    
    fn detect_framework() -> Result<TestFramework, String> {
        // Scan source files for test framework includes
        let paths = vec!["tests", "test", "src"];
        
        for path in paths {
            if !Path::new(path).exists() {
                continue;
            }
            
            if let Ok(entries) = fs::read_dir(path) {
                for entry in entries.flatten() {
                    if let Ok(content) = fs::read_to_string(entry.path()) {
                        if content.contains("#include <catch2") || content.contains("#include \"catch2") {
                            return Ok(TestFramework::Catch2);
                        }
                        if content.contains("#include <gtest") || content.contains("#include \"gtest") {
                            return Ok(TestFramework::GoogleTest);
                        }
                    }
                }
            }
        }
        
        Err("No test framework detected".to_string())
    }
    
    fn find_test_files() -> Result<Vec<String>, String> {
        let mut files = Vec::new();
        let paths = vec!["tests", "test"];
        
        for path in paths {
            if !Path::new(path).exists() {
                continue;
            }
            
            if let Ok(entries) = fs::read_dir(path) {
                for entry in entries.flatten() {
                    let path = entry.path();
                    if path.extension().and_then(|s| s.to_str()) == Some("cpp") {
                        files.push(path.to_string_lossy().to_string());
                    }
                }
            }
        }
        
        Ok(files)
    }
    
    fn compile_tests(files: &[String]) -> Result<String, String> {
        println!("Compiling tests...");
        
        let output = "target/debug/test_runner";
        fs::create_dir_all("target/debug").map_err(|e| e.to_string())?;
        
        let mut cmd = Command::new("clang++");
        cmd.args(files)
           .arg("-o")
           .arg(output)
           .arg("-std=c++17")
           .arg("-g");
        
        let status = cmd.status().map_err(|e| format!("Failed to compile tests: {}", e))?;
        
        if !status.success() {
            return Err("Test compilation failed".to_string());
        }
        
        Ok(output.to_string())
    }
    
    fn execute_tests(binary: &str) -> Result<(), String> {
        println!("Executing tests...");
        
        let status = Command::new(binary)
            .status()
            .map_err(|e| format!("Failed to run tests: {}", e))?;
        
        if !status.success() {
            return Err("Tests failed".to_string());
        }
        
        Ok(())
    }
}

#[derive(Debug)]
enum TestFramework {
    Catch2,
    GoogleTest,
}
