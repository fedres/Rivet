use crate::format;

pub fn execute(check: bool) {
    if let Err(e) = format::format_files(check) {
        eprintln!("Error: {}", e);
        std::process::exit(1);
    }
}
