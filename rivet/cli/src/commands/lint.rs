use crate::lint;

pub fn execute() {
    if let Err(e) = lint::lint_files() {
        eprintln!("Error: {}", e);
        std::process::exit(1);
    }
}
