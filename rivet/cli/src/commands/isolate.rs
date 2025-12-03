use crate::isolation::manager::IsolationManager;
use clap::Subcommand;

#[derive(Subcommand)]
pub enum IsolateCommands {
    Init,
    Run {
        #[arg(last = true)]
        command: Vec<String>,
    },
}

pub fn execute(command: IsolateCommands) {
    let manager = IsolationManager::new();
    match command {
        IsolateCommands::Init => {
            if let Err(e) = manager.init() {
                eprintln!("Error initializing isolation: {}", e);
            }
        }
        IsolateCommands::Run { command } => {
            if command.is_empty() {
                eprintln!("Error: No command specified");
                return;
            }
            let cmd = &command[0];
            let args = &command[1..];
            if let Err(e) = manager.run(cmd, args) {
                eprintln!("Error running command: {}", e);
                std::process::exit(1);
            }
        }
    }
}
