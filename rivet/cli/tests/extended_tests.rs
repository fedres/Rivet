use assert_cmd::Command;
use predicates::prelude::*;
use tempfile::TempDir;
use std::fs;

/// Test building a simple project
#[test]
fn test_build_simple_project() {
    let temp = TempDir::new().unwrap();
    
    // Create project
    Command::cargo_bin("rivet-cli")
        .unwrap()
        .arg("new")
        .arg("test_build")
        .current_dir(&temp)
        .assert()
        .success();
    
    let project_dir = temp.path().join("test_build");
    
    // Build project
    Command::cargo_bin("rivet-cli")
        .unwrap()
        .arg("build")
        .current_dir(&project_dir)
        .assert()
        .success();
    
    // Verify binary was created
    assert!(project_dir.join("target/debug/test_build").exists());
}

/// Test error when building without rivet.toml
#[test]
fn test_build_without_config() {
    let temp = TempDir::new().unwrap();
    
    Command::cargo_bin("rivet-cli")
        .unwrap()
        .arg("build")
        .current_dir(&temp)
        .assert()
        .failure();
}

/// Test removing a dependency
#[test]
fn test_remove_dependency() {
    let temp = TempDir::new().unwrap();
    
    // Initialize and add dependency
    Command::cargo_bin("rivet-cli")
        .unwrap()
        .arg("init")
        .current_dir(&temp)
        .assert()
        .success();
    
    Command::cargo_bin("rivet-cli")
        .unwrap()
        .arg("add")
        .arg("fmt")
        .current_dir(&temp)
        .assert()
        .success();
    
    // Remove dependency
    Command::cargo_bin("rivet-cli")
        .unwrap()
        .arg("remove")
        .arg("fmt")
        .current_dir(&temp)
        .assert()
        .success();
    
    // Verify dependency was removed
    let config = fs::read_to_string(temp.path().join("rivet.toml")).unwrap();
    assert!(!config.contains("fmt"));
}

/// Test toolchain installation
#[test]
fn test_toolchain_install() {
    Command::cargo_bin("rivet-cli")
        .unwrap()
        .arg("toolchain")
        .arg("install")
        .arg("test-toolchain")
        .assert()
        .success();
    
    // Verify toolchain directory was created
    let home = dirs::home_dir().unwrap();
    let toolchain_dir = home.join(".rivet/toolchains/test-toolchain");
    assert!(toolchain_dir.exists());
}

/// Test format command (will fail if clang-format not installed)
#[test]
#[ignore] // Ignore by default since it requires clang-format
fn test_format_command() {
    let temp = TempDir::new().unwrap();
    
    // Create project with badly formatted code
    Command::cargo_bin("rivet-cli")
        .unwrap()
        .arg("new")
        .arg("test_fmt")
        .current_dir(&temp)
        .assert()
        .success();
    
    let project_dir = temp.path().join("test_fmt");
    
    // Run format
    Command::cargo_bin("rivet-cli")
        .unwrap()
        .arg("fmt")
        .current_dir(&project_dir)
        .assert()
        .success();
}
