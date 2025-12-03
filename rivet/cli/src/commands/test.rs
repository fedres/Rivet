use crate::test::TestRunner;

pub fn execute() {
    if let Err(e) = TestRunner::run() {
        eprintln!("Error: {}", e);
        std::process::exit(1);
    }
}
