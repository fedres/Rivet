#[cxx::bridge]
mod ffi {
    unsafe extern "C++" {
        include!("rivet/engine.hpp");

        fn hello_from_cpp() -> String;
    }
}

pub fn call_cpp() {
    let msg = ffi::hello_from_cpp();
    println!("C++ says: {}", msg);
}
