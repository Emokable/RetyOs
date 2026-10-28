#[cfg(target_os = "linux")]
mod linux;
mod protocol;

fn main() {
    #[cfg(target_os = "linux")]
    if let Err(error) = linux::entry() {
        eprintln!("SESSION_CONTROLLER_ERROR={error}");
        std::process::exit(1);
    }
    #[cfg(not(target_os = "linux"))]
    {
        eprintln!("Runtime requires Linux; protocol tests are portable.");
        std::process::exit(2);
    }
}
