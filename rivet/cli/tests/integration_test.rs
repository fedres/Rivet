use assert_cmd::Command;
use predicates::prelude::*;
use tempfile::TempDir;
use std::fs;

#[test]
fn test_version_command() {
    Command::cargo_bin("rivet-cli")
        .unwrap()
        .arg("--version")
        .assert()
        .success();
}

#[test]
fn test_help_command() {
    Command::cargo_bin("rivet-cli")
        .unwrap()
        .arg("--help")
        .assert()
        .success()
        .stdout(predicate::str::contains("Rivet"));
}

#[test]
fn test_init_command() {
    let temp = TempDir::new().unwrap();
    
    Command::cargo_bin("rivet-cli")
        .unwrap()
        .arg("init")
        .current_dir(&temp)
        .assert()
        .success();
    
    // Verify rivet.toml was created
    assert!(temp.path().join("rivet.toml").exists());
    
    // Verify content
    let content = fs::read_to_string(temp.path().join("rivet.toml")).unwrap();
    assert!(content.contains("[package]"));
    assert!(content.contains("edition = \"2023\""));
}

#[test]
fn test_new_command() {
    let temp = TempDir::new().unwrap();
    
    Command::cargo_bin("rivet-cli")
        .unwrap()
        .arg("new")
        .arg("test_project")
        .current_dir(&temp)
        .assert()
        .success();
    
    let project_dir = temp.path().join("test_project");
    
    // Verify directory structure
    assert!(project_dir.exists());
    assert!(project_dir.join("rivet.toml").exists());
    assert!(project_dir.join("src").exists());
    assert!(project_dir.join("src/main.cpp").exists());
    
    // Verify rivet.toml content
    let config = fs::read_to_string(project_dir.join("rivet.toml")).unwrap();
    assert!(config.contains("name = \"test_project\""));
    
    // Verify main.cpp content
    let main_cpp = fs::read_to_string(project_dir.join("src/main.cpp")).unwrap();
    assert!(main_cpp.contains("int main()"));
}

#[test]
fn test_toolchain_list() {
    Command::cargo_bin("rivet-cli")
        .unwrap()
        .arg("toolchain")
        .arg("list")
        .assert()
        .success()
        .stdout(predicate::str::contains("Installed Toolchains"));
}

#[test]
fn test_add_dependency() {
    let temp = TempDir::new().unwrap();
    
    // Initialize project
    Command::cargo_bin("rivet-cli")
        .unwrap()
        .arg("init")
        .current_dir(&temp)
        .assert()
        .success();
    
    // Add dependency
    Command::cargo_bin("rivet-cli")
        .unwrap()
        .arg("add")
        .arg("fmt")
        .current_dir(&temp)
        .assert()
        .success();
    
    // Verify dependency was added
    let config = fs::read_to_string(temp.path().join("rivet.toml")).unwrap();
    assert!(config.contains("fmt"));
}

#[test]
fn test_isolate_init() {
    let temp = TempDir::new().unwrap();
    
    // Initialize project
    Command::cargo_bin("rivet-cli")
        .unwrap()
        .arg("init")
        .current_dir(&temp)
        .assert()
        .success();
    
    // Initialize isolation
    Command::cargo_bin("rivet-cli")
        .unwrap()
        .arg("isolate")
        .arg("init")
        .current_dir(&temp)
        .assert()
        .success();
    
    // Verify .rivet directory was created
    assert!(temp.path().join(".rivet").exists());
    assert!(temp.path().join(".rivet/env.sh").exists());
}
