use std::path::{Path, PathBuf};
use std::process::Command;
use colored::Colorize;

pub struct Compiler {
    pub executable: PathBuf,
    flags: Vec<String>,
}

impl Compiler {
    pub fn new(executable: PathBuf) -> Self {
        Compiler {
            executable,
            flags: vec!["-std=c++17".to_string()],
        }
    }
    
    pub fn with_flags(mut self, flags: Vec<String>) -> Self {
        self.flags.extend(flags);
        self
    }
    
    /// Compile source file to object file
    pub fn compile_object(&self, src: &Path, obj: &Path, include_dirs: &[PathBuf]) -> Result<(), String> {
        println!("   {} {}", "Compiling".green(), src.display());
        
        let mut cmd = Command::new(&self.executable);
        cmd.arg("-c")
           .arg(src)
           .arg("-o")
           .arg(obj);
        
        // Add flags
        for flag in &self.flags {
            cmd.arg(flag);
        }
        
        // Add include directories
        for dir in include_dirs {
            cmd.arg("-I").arg(dir);
        }
        
        let status = cmd.status()
            .map_err(|e| format!("Failed to run compiler: {}", e))?;
        
        if !status.success() {
            return Err(format!("Compilation failed for {}", src.display()));
        }
        
        Ok(())
    }
    
    /// Link object files into binary
    pub fn link_binary(&self, objects: &[PathBuf], output: &Path, libs: &[String]) -> Result<(), String> {
        println!("    {} {}", "Linking".cyan(), output.display());
        
        let mut cmd = Command::new(&self.executable);
        
        for obj in objects {
            cmd.arg(obj);
        }
        
        cmd.arg("-o").arg(output);
        
        // Add libraries
        for lib in libs {
            cmd.arg(format!("-l{}", lib));
        }
        
        let status = cmd.status()
            .map_err(|e| format!("Failed to run linker: {}", e))?;
        
        if !status.success() {
            return Err("Linking failed".to_string());
        }
        
        Ok(())
    }
    
    /// Check syntax without compiling
    pub fn check_syntax(&self, src: &Path, include_dirs: &[PathBuf]) -> Result<(), String> {
        println!("   {} {}", "Checking".yellow(), src.display());
        
        let mut cmd = Command::new(&self.executable);
        cmd.arg("-fsyntax-only")
           .arg(src);
        
        for flag in &self.flags {
            cmd.arg(flag);
        }
        
        for dir in include_dirs {
            cmd.arg("-I").arg(dir);
        }
        
        let status = cmd.status()
            .map_err(|e| format!("Failed to run compiler: {}", e))?;
        
        if !status.success() {
            return Err(format!("Syntax check failed for {}", src.display()));
        }
        
        Ok(())
    }
}
