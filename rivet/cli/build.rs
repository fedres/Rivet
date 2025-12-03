fn main() {
    cxx_build::bridge("src/ffi.rs")
        .file("../engine/src/engine.cpp")
        .include("../engine/include")
        .flag_if_supported("-std=c++17")
        .compile("rivet-engine");

    println!("cargo:rerun-if-changed=src/ffi.rs");
    println!("cargo:rerun-if-changed=../engine/include/rivet/engine.hpp");
    println!("cargo:rerun-if-changed=../engine/src/engine.cpp");
}
