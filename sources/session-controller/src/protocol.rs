#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum State {
    Starting,
    Active,
    Retaining,
    NativePending,
    Native,
    Resuming,
    Exiting,
}
impl State {
    pub fn name(self) -> &'static str {
        match self {
            Self::Starting => "starting",
            Self::Active => "active",
            Self::Retaining => "retaining",
            Self::NativePending => "native_pending",
            Self::Native => "native",
            Self::Resuming => "resuming",
            Self::Exiting => "exiting",
        }
    }
    pub fn allows(self, next: Self) -> bool {
        self == next
            || next == Self::Exiting
            || matches!(
                (self, next),
                (Self::Starting, Self::Active)
                    | (Self::Active, Self::Retaining)
                    | (Self::Retaining, Self::NativePending)
                    | (Self::NativePending, Self::Native)
                    | (Self::NativePending, Self::Resuming)
                    | (Self::Native, Self::Resuming)
                    | (Self::Resuming, Self::Active)
            )
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Request {
    Retain,
    Resume,
    Exit,
    Status,
}
pub fn request(s: &str) -> Result<Request, &'static str> {
    if s.len() > 64 {
        return Err("request_too_long");
    }
    match s.trim_end_matches(['\r', '\n']) {
        "retain" => Ok(Request::Retain),
        "resume" => Ok(Request::Resume),
        "exit" => Ok(Request::Exit),
        "status" => Ok(Request::Status),
        _ => Err("invalid_request"),
    }
}
pub fn stage_valid(s: &str) -> bool {
    s.strip_prefix("/mnt/us/kindle-a7-art.")
        .is_some_and(|id| id.len() == 6 && id.bytes().all(|v| v.is_ascii_alphanumeric()))
}
// /proc/PID/stat comm can contain spaces and parentheses. Field 22 follows
// the final ')', rather than a split on whitespace over the entire record.
pub fn proc_start(s: &str) -> Option<u64> {
    s.rsplit_once(')')?
        .1
        .split_whitespace()
        .nth(19)?
        .parse()
        .ok()
}
pub fn contains_marker(log: &[u8], marker: &[u8]) -> bool {
    !marker.is_empty() && log.windows(marker.len()).any(|window| window == marker)
}
pub fn hardware_request(s: &str) -> Option<(&str, u8)> {
    let mut f = s.split_whitespace();
    let key = f.next()?;
    let value: u8 = f.next()?.parse().ok()?;
    if f.next().is_some() {
        return None;
    }
    match key {
        "brightness" | "warmth" if value <= 24 => Some((key, value)),
        "wifi" if value <= 1 => Some((key, value)),
        _ => None,
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn transition_rejects_stale_and_interleaved_actions() {
        let path = [
            State::Starting,
            State::Active,
            State::Retaining,
            State::NativePending,
            State::Native,
            State::Resuming,
            State::Active,
            State::Exiting,
        ];
        for pair in path.windows(2) {
            assert!(pair[0].allows(pair[1]));
        }
        assert!(!State::Exiting.allows(State::Active));
        assert!(!State::Retaining.allows(State::Active));
        assert!(!State::Native.allows(State::Retaining));
        assert!(!State::Active.allows(State::Resuming));
    }
    #[test]
    fn strict_requests_and_owned_paths() {
        assert_eq!(State::NativePending.name(), "native_pending");
        assert_eq!(request("exit\n"), Ok(Request::Exit));
        assert!(request("exit; reboot").is_err());
        assert!(request("resume\nexit").is_err());
        assert!(request(&"x".repeat(65)).is_err());
        assert!(stage_valid("/mnt/us/kindle-a7-art.Ab01xy"));
        for p in [
            "/mnt/us/kindle-a7-art.../abc",
            "/mnt/us/kindle-a7-art.ABCDEF/",
            "/mnt/us/kindle-a7-art.",
        ] {
            assert!(!stage_valid(p));
        }
    }
    #[test]
    fn startup_marker_survives_binary_android_logs() {
        let bytes = b"prefix\xff\xfe\x80\nAPK_ACTIVITY_PROCESS_LAUNCH_PASS=1\nsuffix";
        assert!(std::str::from_utf8(bytes).is_err());
        assert!(contains_marker(
            bytes,
            b"APK_ACTIVITY_PROCESS_LAUNCH_PASS=1"
        ));
        assert!(!contains_marker(
            bytes,
            b"APK_ACTIVITY_PROCESS_LAUNCH_PASS=0"
        ));
        assert!(!contains_marker(b"", b""));
    }
    #[test]
    fn pid_identity_handles_unusual_comm() {
        let s = "12 (name (with spaces)) S 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 123456 0";
        assert_eq!(proc_start(s), Some(123456));
        assert_eq!(proc_start("broken"), None);
    }
    #[test]
    fn hardware_values_cannot_inject_shell() {
        assert_eq!(
            hardware_request("brightness 24\n"),
            Some(("brightness", 24))
        );
        for s in [
            "wifi 2",
            "warmth 25",
            "brightness -1",
            "wifi 1 reboot",
            "wifi 1;reboot",
        ] {
            assert_eq!(hardware_request(s), None);
        }
    }
}
