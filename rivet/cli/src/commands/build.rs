use crate::build::manager::BuildManager;

pub fn execute() {
    match BuildManager::new() {
        Ok(manager) => {
            if let Err(e) = manager.build() {
                eprintln!("Build failed: {}", e);
                std::process::exit(1);
            }
        }
        Err(e) => {
            eprintln!("Failed to initialize build: {}", e);
            std::process::exit(1);
        }
    }
}
