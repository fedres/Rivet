use clap::{Parser, Subcommand};

mod commands;
mod config;
mod ffi;
mod dependency;
mod build;
mod toolchain;
mod isolation;
mod test;
mod format;
mod lint;
mod error;
mod target;
mod registry;

#[derive(Parser)]
#[command(name = "rivet")]
#[command(about = "Rivet: The C++ Project Manager", long_about = None)]
#[command(version)]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Initialize a new Rivet project in the current directory
    Init,
    /// Create a new Rivet project in a new directory
    New {
        /// Name of the project
        name: String,
        /// Create a library project instead of a binary
        #[arg(long)]
        lib: bool,
    },
    /// Show system info and test FFI
    Info,
    /// Build the project
    Build,
    /// Add a dependency
    Add {
        /// Name of the dependency
        name: String,
    },
    /// Remove a dependency
    Remove {
        /// Name of the dependency
        name: String,
    },
    /// Manage toolchains
    Toolchain {
        #[command(subcommand)]
        command: ToolchainCommands,
    },
    /// Manage isolated environments
    Isolate {
        #[command(subcommand)]
        command: commands::isolate::IsolateCommands,
    },
    /// Run tests
    Test,
    /// Format source files
    Fmt {
        /// Check formatting without modifying files
        #[arg(long)]
        check: bool,
    },
    /// Lint source files
    Lint,
    /// Manage build targets
    Target {
        #[command(subcommand)]
        command: TargetCommands,
    },
    /// Search for packages in the registry
    Search {
        /// Search query
        query: String,
    },
    /// Publish package to registry
    Publish,
}

#[derive(Subcommand)]
enum TargetCommands {
    Add { triple: String },
    List,
    Remove { triple: String },
}

#[derive(Subcommand)]
enum ToolchainCommands {
    List,
    Install { name: String },
    Use { name: String },
}

fn main() {
    // Initialize tracing/logging
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("warn"))
        )
        .with_target(false)
        .init();

    let cli = Cli::parse();

    match cli.command {
        Commands::Init => {
            commands::init::execute();
        }
        Commands::New { name, lib } => {
            commands::new::execute(name, lib);
        }
        Commands::Info => {
            ffi::call_cpp();
        }
        Commands::Build => {
            commands::build::execute();
        }
        Commands::Add { name } => {
            commands::deps::add(name);
        }
        Commands::Remove { name } => {
            commands::deps::remove(name);
        }
        Commands::Toolchain { command } => {
            match command {
                ToolchainCommands::List => commands::toolchain::list(),
                ToolchainCommands::Install { name } => commands::toolchain::install(name),
                ToolchainCommands::Use { name } => commands::toolchain::use_toolchain(name),
            }
        }
        Commands::Isolate { command } => {
            commands::isolate::execute(command);
        }
        Commands::Test => {
            commands::test::execute();
        }
        Commands::Fmt { check } => {
            commands::format::execute(check);
        }
        Commands::Lint => {
            commands::lint::execute();
        }
        Commands::Target { command } => {
            match command {
                TargetCommands::Add { triple } => commands::target::add(triple),
                TargetCommands::List => commands::target::list(),
                TargetCommands::Remove { triple } => commands::target::remove(triple),
            }
        }
        Commands::Search { query } => {
            commands::search::execute(query);
        }
        Commands::Publish => {
            commands::publish::execute();
        }
    }
}
