use crate::protocol::{self, Request, State};
mod native_ui;
use std::ffi::CString;
use std::os::{
    fd::{AsRawFd, RawFd},
    unix::{
        fs::{MetadataExt, OpenOptionsExt, PermissionsExt},
        net::{UnixListener, UnixStream},
    },
};
use std::{
    env, fs,
    io::{self, Read, Write},
    path::{Path, PathBuf},
    process::{Child, Command, Stdio},
    sync::{
        atomic::{AtomicBool, Ordering},
        mpsc::{self, SyncSender},
        Arc,
    },
    thread,
    time::{Duration, Instant},
};

type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;
const BASE: &str = "/mnt/us/kindle-android";
static STOP: AtomicBool = AtomicBool::new(false);
#[repr(C)]
struct PollFd {
    fd: i32,
    events: i16,
    revents: i16,
}
#[repr(C)]
struct Timespec {
    sec: i64,
    nsec: i64,
}
#[repr(C)]
struct Cred {
    pid: i32,
    uid: u32,
    gid: u32,
}
extern "C" {
    fn poll(p: *mut PollFd, n: usize, timeout: i32) -> i32;
    fn inotify_init1(flags: i32) -> i32;
    fn inotify_add_watch(fd: i32, path: *const std::ffi::c_char, mask: u32) -> i32;
    fn read(fd: i32, data: *mut std::ffi::c_void, size: usize) -> isize;
    fn close(fd: i32) -> i32;
    fn flock(fd: i32, operation: i32) -> i32;
    fn signal(sig: i32, handler: extern "C" fn(i32)) -> usize;
    fn getsockopt(fd: i32, level: i32, name: i32, value: *mut Cred, len: *mut u32) -> i32;
    fn syscall(number: isize, ...) -> isize;
    fn kill(pid: i32, sig: i32) -> i32;
    fn setsid() -> i32;
    #[cfg(not(target_arch = "arm"))]
    fn clock_gettime(id: i32, t: *mut Timespec) -> i32;
}
extern "C" fn stop_signal(_: i32) {
    STOP.store(true, Ordering::Relaxed);
}
fn now() -> u64 {
    let mut t = Timespec { sec: 0, nsec: 0 };
    #[cfg(target_arch = "arm")]
    let rc = unsafe { syscall(403isize, 1i32, &mut t as *mut Timespec) };
    #[cfg(not(target_arch = "arm"))]
    let rc = unsafe { clock_gettime(1, &mut t) };
    if rc != 0 {
        panic!("CLOCK_MONOTONIC failed");
    }
    (t.sec as u64) * 1_000_000_000 + t.nsec as u64
}
fn text(path: impl AsRef<Path>) -> String {
    fs::read_to_string(path).unwrap_or_default()
}
fn marker(path: impl AsRef<Path>) -> Result<()> {
    fs::write(path, b"1\n")?;
    Ok(())
}
fn remove(path: impl AsRef<Path>) {
    let _ = fs::remove_file(path);
}
fn atomic(path: impl AsRef<Path>, value: &str) -> Result<()> {
    let path = path.as_ref();
    let tmp = path.with_extension(format!("controller-{}.new", std::process::id()));
    if fs::symlink_metadata(path).is_ok_and(|m| m.file_type().is_symlink()) {
        return Err("symlink_destination".into());
    }
    let mut f = fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .mode(0o600)
        .custom_flags(0x20000)
        .open(&tmp)?;
    f.write_all(value.as_bytes())?;
    drop(f);
    fs::rename(tmp, path)?;
    Ok(())
}
#[derive(Clone)]
struct Identity {
    pid: u32,
    start: u64,
}
impl Identity {
    fn new(pid: u32) -> Result<Self> {
        if pid <= 1 {
            return Err("invalid_pid".into());
        }
        let start =
            protocol::proc_start(&text(format!("/proc/{pid}/stat"))).ok_or("pid_not_alive")?;
        Ok(Self { pid, start })
    }
    fn alive(&self) -> bool {
        protocol::proc_start(&text(format!("/proc/{}/stat", self.pid))) == Some(self.start)
    }
}
struct Events {
    fd: RawFd,
    watches: usize,
    missing: bool,
}
impl Events {
    fn new() -> Result<Self> {
        let fd = unsafe { inotify_init1(0x800 | 0x80000) };
        if fd < 0 {
            return Err(io::Error::last_os_error().into());
        }
        Ok(Self {
            fd,
            watches: 0,
            missing: false,
        })
    }
    fn watch(&mut self, path: &Path) -> Result<()> {
        let s = CString::new(path.as_os_str().as_encoded_bytes())?;
        let mask = 0x8 | 0x80 | 0x100 | 0x200 | 0x400 | 0x800 | 0x4;
        if unsafe { inotify_add_watch(self.fd, s.as_ptr(), mask) } < 0 {
            self.missing = true;
            eprintln!(
                "CONTROLLER_WATCH_FALLBACK path={} error={}",
                path.display(),
                io::Error::last_os_error()
            );
        } else {
            self.watches += 1;
        }
        Ok(())
    }
    fn wait(&self, timeout: Duration, other: Option<RawFd>) -> Result<bool> {
        let mut fds = [
            PollFd {
                fd: self.fd,
                events: 1,
                revents: 0,
            },
            PollFd {
                fd: other.unwrap_or(-1),
                events: 1,
                revents: 0,
            },
        ];
        let ms = timeout.as_millis().min(i32::MAX as u128) as i32;
        let rc = unsafe {
            poll(
                fds.as_mut_ptr(),
                fds.len(),
                if self.missing { ms.min(200) } else { ms },
            )
        };
        if rc < 0 && io::Error::last_os_error().kind() != io::ErrorKind::Interrupted {
            return Err(io::Error::last_os_error().into());
        }
        let mut bytes = [0u8; 8192];
        while unsafe { read(self.fd, bytes.as_mut_ptr().cast(), bytes.len()) } > 0 {}
        Ok(fds[1].revents & 1 != 0)
    }
}
impl Drop for Events {
    fn drop(&mut self) {
        unsafe {
            close(self.fd);
        }
    }
}
fn output(program: &str, args: &[&str], timeout: Duration) -> Result<String> {
    let mut c = Command::new(program)
        .args(args)
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::null())
        .spawn()?;
    // Small control replies only. Read concurrently so a malformed helper cannot
    // fill its pipe and block the deadline handler.
    let stdout = c.stdout.take().ok_or("stdout_missing")?;
    let reader = thread::spawn(move || {
        let mut s = String::new();
        let _ = stdout.take(65536).read_to_string(&mut s);
        s
    });
    let end = Instant::now() + timeout;
    loop {
        if let Some(status) = c.try_wait()? {
            let s = reader.join().unwrap_or_default();
            if !status.success() {
                return Err(format!("command_failed {program} {args:?}").into());
            }
            return Ok(s);
        }
        if STOP.load(Ordering::Relaxed) || Instant::now() >= end {
            let _ = c.kill();
            let _ = c.wait();
            return Err(format!("command_timeout {program} {args:?}").into());
        }
        thread::sleep(Duration::from_millis(10));
    }
}
fn usb_host_disconnected() -> Result<bool> {
    let status = Command::new(helper("usb-session-guard"))
        .arg("--check")
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()?;
    match status.code() {
        Some(0) => Ok(true),
        Some(2) => Ok(false),
        code => Err(format!("usb_guard_unexpected_exit:{code:?}").into()),
    }
}
fn action(program: &str, args: &[&str], timeout: Duration) -> Result<()> {
    let begin = now();
    println!("CONTROLLER_ACTION_BEGIN program={program} args={args:?} monotonic_ns={begin}");
    let mut c = Command::new(program)
        .args(args)
        .stdin(Stdio::null())
        .spawn()?;
    let end = Instant::now() + timeout;
    loop {
        if let Some(status) = c.try_wait()? {
            println!(
                "CONTROLLER_ACTION_END program={program} elapsed_ms={} rc={:?}",
                (now() - begin) / 1_000_000,
                status.code()
            );
            return if status.success() {
                Ok(())
            } else {
                Err("action_failed".into())
            };
        }
        if STOP.load(Ordering::Relaxed) || Instant::now() >= end {
            let _ = c.kill();
            let _ = c.wait();
            return Err("action_timeout".into());
        }
        thread::sleep(Duration::from_millis(10));
    }
}
fn helper(name: &str) -> String {
    format!("{BASE}/{name}")
}
struct Controller {
    launcher: Identity,
    guard: Identity,
    stage: String,
    tmp: PathBuf,
    state: State,
    events: Events,
    listener: UnixListener,
    socket_dir: PathBuf,
    _owner: fs::File,
    generation: u64,
    transition_ns: u64,
    started_ns: u64,
    last_status: Instant,
    home: Option<(Child, Instant)>,
    next_home: Instant,
    hardware_tx: Option<SyncSender<HardwareWork>>,
    hardware_thread: Option<thread::JoinHandle<()>>,
    hardware_active: Arc<AtomicBool>,
}
impl Controller {
    fn new(launcher: u32, stage: &str) -> Result<Self> {
        if !protocol::stage_valid(stage) || fs::canonicalize(stage)? != Path::new(stage) {
            return Err("invalid_stage".into());
        }
        let owner_path = format!("/dev/shm/kindle-android-owner-{launcher}");
        let owner = fs::OpenOptions::new()
            .create(true)
            .read(true)
            .write(true)
            .mode(0o600)
            .custom_flags(0x20000)
            .open(owner_path)?;
        if unsafe { flock(owner.as_raw_fd(), 2 | 4) } != 0 {
            return Err("another_controller_owns_session".into());
        }
        if text(helper("session.lock/pid")).trim() != launcher.to_string() {
            return Err("launcher_lock_mismatch".into());
        }
        let launcher = Identity::new(launcher)?;
        let started = Instant::now();
        let guard = loop {
            if !launcher.alive() || started.elapsed() > Duration::from_secs(180) {
                return Err("guard_wait_failed".into());
            }
            match output(
                &helper("session-freezer"),
                &["guard", stage],
                Duration::from_secs(3),
            )
            .and_then(|s| Ok(Identity::new(s.trim().parse()?)?))
            {
                Ok(g) => break g,
                Err(_) => thread::sleep(Duration::from_millis(200)),
            }
        };
        let tmp = PathBuf::from(format!("/proc/{}/root/data/local/tmp", guard.pid));
        if fs::read_link(format!("/proc/{}/root", guard.pid))?
            != PathBuf::from(format!("{stage}/rootfs"))
        {
            return Err("guard_root_mismatch".into());
        }
        // Kindle's framework startup recursively grants javausers access to
        // /var/tmp and /var/run. /dev/shm is outside that permission sweep.
        let socket_dir = PathBuf::from(format!("/dev/shm/kindle-android-ctl-{}", launcher.pid));
        fs::create_dir(&socket_dir)?;
        fs::set_permissions(&socket_dir, fs::Permissions::from_mode(0o700))?;
        let listener = UnixListener::bind(socket_dir.join("control.sock"))?;
        listener.set_nonblocking(true)?;
        let mut events = Events::new()?;
        events.watch(Path::new(BASE))?;
        events.watch(&tmp)?;
        let private = PathBuf::from(format!(
            "/proc/{}/root/data/user_de/0/org.kindlelab.apkprobe/files",
            guard.pid
        ));
        if private.is_dir() {
            events.watch(&private)?;
        }
        atomic(
            helper("warm-controller.socket"),
            &format!("{}\n", socket_dir.join("control.sock").display()),
        )?;
        let (tx, rx) = mpsc::sync_channel(8);
        let active = Arc::new(AtomicBool::new(false));
        let worker_active = active.clone();
        let worker_tmp = tmp.clone();
        let worker = thread::spawn(move || {
            while let Ok(work) = rx.recv() {
                if worker_active.load(Ordering::Relaxed) {
                    if let Err(e) = hardware_work(&worker_tmp, &worker_active, work) {
                        eprintln!("HARDWARE_STATUS_ERROR={e}");
                    }
                }
            }
        });
        let mut c = Self {
            launcher,
            guard,
            stage: stage.into(),
            tmp,
            state: State::Starting,
            events,
            listener,
            socket_dir,
            _owner: owner,
            generation: 0,
            transition_ns: now(),
            started_ns: now(),
            last_status: Instant::now() - Duration::from_secs(60),
            home: None,
            next_home: Instant::now(),
            hardware_tx: Some(tx),
            hardware_thread: Some(worker),
            hardware_active: active,
        };
        let startup = (|| -> Result<()> {
            c.set_state(State::Starting)?;
            action(
                &helper("session-freezer"),
                &["prepare", stage],
                Duration::from_secs(6),
            )?;
            let mut warned = Instant::now();
            loop {
                c.check()?;
                let command = c.clients()?;
                if command == Some(Request::Exit)
                    || text(helper("warm.command")).trim() == "exit"
                    || c.tmp.join("kindle-android-exit").exists()
                {
                    remove(helper("warm.command"));
                    c.exit()?;
                    return Err("startup_exit_requested".into());
                }
                let bytes = fs::read(Path::new(stage).join("result.txt")).unwrap_or_default();
                if protocol::contains_marker(&bytes, b"APK_ACTIVITY_PROCESS_LAUNCH_PASS=1") {
                    break;
                }
                if warned.elapsed() >= Duration::from_secs(30) {
                    println!(
                        "CONTROLLER_DESKTOP_STILL_STARTING elapsed_ms={}",
                        (now() - c.started_ns) / 1_000_000
                    );
                    warned = Instant::now();
                }
                c.events
                    .wait(Duration::from_millis(200), Some(c.listener.as_raw_fd()))?;
            }
            remove(c.tmp.join("kindle-android-retain"));
            marker(c.tmp.join("kindle-warm-available"))?;
            c.set_state(State::Active)?;
            Ok(())
        })();
        if let Err(error) = startup {
            STOP.store(false, Ordering::Relaxed);
            if c.state != State::Exiting && c.guard.alive() {
                if let Err(exit_error) = c.exit() {
                    eprintln!("CONTROLLER_STARTUP_CLEANUP_ERROR={exit_error}");
                }
            }
            return Err(error);
        }
        Ok(c)
    }
    fn check(&self) -> Result<()> {
        if STOP.load(Ordering::Relaxed) || !self.launcher.alive() || !self.guard.alive() {
            return Err("session_owner_gone".into());
        }
        if text(helper("session.lock/pid")).trim() != self.launcher.pid.to_string() {
            return Err("session_lock_changed".into());
        }
        Ok(())
    }
    fn set_state(&mut self, next: State) -> Result<()> {
        if !self.state.allows(next) {
            return Err(format!("invalid_transition {:?}->{next:?}", self.state).into());
        }
        self.state = next;
        self.hardware_active
            .store(next == State::Active, Ordering::Relaxed);
        atomic(
            helper("warm-state"),
            &format!(
                "{}\n{}\n{}\n{}\n",
                next.name(),
                self.stage,
                self.launcher.pid,
                self.guard.pid
            ),
        )?;
        println!(
            "WARM_SESSION_STATE={} monotonic_ns={} generation={}",
            next.name(),
            now(),
            self.generation
        );
        Ok(())
    }
    fn begin(&mut self, kind: &str, stamp: Option<u64>) {
        self.generation += 1;
        let detected = now();
        self.transition_ns = stamp
            .filter(|v| *v <= detected && detected - *v < 120_000_000_000)
            .unwrap_or(detected);
        println!("WARM_TRANSITION_BEGIN kind={kind} requested_ns={} detected_ns={detected} generation={}",self.transition_ns,self.generation);
    }
    fn ready(&self, kind: &str) {
        println!("WARM_TRANSITION_SOFTWARE_READY kind={kind} duration_ms={} monotonic_ns={} generation={}",(now()-self.transition_ns)/1_000_000,now(),self.generation);
    }
    fn wait_file(&self, name: &str) -> Result<()> {
        let start = now();
        let end = Instant::now() + Duration::from_secs(5);
        loop {
            self.check()?;
            if self.tmp.join(name).is_file() {
                println!(
                    "CONTROLLER_HANDSHAKE name={name} elapsed_ms={}",
                    (now() - start) / 1_000_000
                );
                return Ok(());
            }
            if Instant::now() > end {
                return Err(format!("handshake_timeout {name}").into());
            }
            self.events.wait(Duration::from_secs(1), None)?;
        }
    }
    fn wait_power(&self, value: &str) -> Result<()> {
        let end = Instant::now() + Duration::from_secs(5);
        loop {
            self.check()?;
            if text(helper("warm-power.state")).trim() == value {
                return Ok(());
            }
            if Instant::now() > end {
                return Err("power_handoff_timeout".into());
            }
            self.events.wait(Duration::from_millis(200), None)?;
        }
    }
    fn stop_native(&self) -> Result<()> {
        marker(helper("framework-stopped"))?;
        if native_ui::enabled() {
            return native_ui::hold(self.launcher.pid);
        }
        for job in ["lab126_gui", "x"] {
            let status = output("/sbin/initctl", &["status", job], Duration::from_secs(3))?;
            if status.contains("start/running") {
                action("/sbin/stop", &[job], Duration::from_secs(20))?;
            }
            let after = output("/sbin/initctl", &["status", job], Duration::from_secs(3))?;
            if !after.contains("stop/waiting") {
                return Err("native_display_not_released".into());
            }
        }
        Ok(())
    }
    fn retain(&mut self) -> Result<()> {
        self.begin(
            "retain",
            text(self.tmp.join("kindle-android-retain"))
                .trim()
                .parse()
                .ok(),
        );
        atomic(
            helper("native-home.trace-id"),
            &format!("{}\n", self.transition_ns),
        )?;
        self.set_state(State::Retaining)?;
        for f in [
            "kindle-display-quiesced",
            "kindle-input-released",
            "kindle-display-resumed",
        ] {
            remove(self.tmp.join(f));
        }
        for f in [
            "kindle-session-locked",
            "kindle-input-release",
            "kindle-display-quiesce",
        ] {
            marker(self.tmp.join(f))?;
        }
        self.wait_file("kindle-display-quiesced")?;
        self.wait_file("kindle-input-released")?;
        marker(helper("warm-native"))?;
        self.wait_power("native")?;
        action(
            &helper("session-freezer"),
            &["freeze", &self.stage],
            Duration::from_secs(6),
        )?;
        action(
            "/bin/sh",
            &[&helper("restore-framework.sh")],
            Duration::from_secs(110),
        )?;
        remove(self.tmp.join("kindle-android-retain"));
        self.set_state(State::NativePending)?;
        self.start_home(false)?;
        Ok(())
    }
    fn start_home(&mut self, check: bool) -> Result<()> {
        let mut cmd = Command::new("/bin/sh");
        cmd.arg(helper("activate-native-home.sh"));
        if check {
            cmd.arg("--check");
        }
        self.home = Some((
            cmd.stdin(Stdio::null()).spawn()?,
            Instant::now() + Duration::from_secs(15),
        ));
        Ok(())
    }
    fn poll_home(&mut self) -> Result<()> {
        let mut result = None;
        if let Some((child, end)) = &mut self.home {
            if let Some(s) = child.try_wait()? {
                result = Some(s.success());
            } else if Instant::now() > *end {
                let _ = child.kill();
                let _ = child.wait();
                result = Some(false);
            }
        }
        if let Some(success) = result {
            self.home = None;
            if success && Path::new(&helper("native-ui.ready")).is_file() {
                self.set_state(State::Native)?;
                match action(
                    "/bin/sh",
                    &[&helper("storage-gate.sh"), "open-frozen"],
                    Duration::from_secs(12),
                ) {
                    Ok(()) => println!("CONTROLLER_FROZEN_MTP_READY=1"),
                    Err(error) => eprintln!("CONTROLLER_FROZEN_MTP_UNAVAILABLE={error}"),
                }
                self.ready("retain");
            } else {
                self.next_home = Instant::now() + Duration::from_secs(1);
            }
        }
        if self.state == State::NativePending
            && self.home.is_none()
            && Instant::now() >= self.next_home
        {
            self.start_home(true)?;
        }
        Ok(())
    }
    fn resume(&mut self) -> Result<()> {
        // Recheck at the point of use. A cable can be connected after the
        // request-loop preflight; stopping FunctionFS MTP during an active
        // host session can block and otherwise kill the warm controller.
        if !usb_host_disconnected()? {
            println!("WARM_RESUME_DEFERRED_USB_CONNECTED=1 phase=pre_mtp_stop");
            return Err("resume_deferred_usb_connected".into());
        }
        if let Some((mut child, _)) = self.home.take() {
            let _ = child.kill();
            let _ = child.wait();
        }
        self.begin(
            "resume",
            text(helper("entry-request.started-ns")).trim().parse().ok(),
        );
        if let Err(error) = action(
            "/bin/sh",
            &[&helper("storage-gate.sh"), "close-frozen"],
            Duration::from_secs(12),
        ) {
            // The guard in storage-gate is a second fence. If the host was
            // connected in the narrow race after our preflight, keep the
            // native screen and frozen Android session instead of treating
            // the expected deferral as a fatal controller error.
            if !usb_host_disconnected()? {
                println!("WARM_RESUME_DEFERRED_USB_CONNECTED=1 phase=mtp_stop_race");
                return Err("resume_deferred_usb_connected".into());
            }
            return Err(error);
        }
        self.set_state(State::Resuming)?;
        remove(helper("native-home.trace-id"));
        remove(helper("native-ui.ready"));
        self.stop_native()?;
        remove(helper("warm-native"));
        remove(self.tmp.join("kindle-input-acquired"));
        action(
            &helper("session-freezer"),
            &["thaw", &self.stage],
            Duration::from_secs(6),
        )?;
        self.wait_power("android")?;
        remove(self.tmp.join("kindle-input-release"));
        self.wait_file("kindle-input-acquired")?;
        atomic(
            self.tmp.join("kindle-input-resume-cutoff"),
            &format!("{}\n", now()),
        )?;
        fs::set_permissions(
            self.tmp.join("kindle-input-resume-cutoff"),
            fs::Permissions::from_mode(0o644),
        )?;
        remove(self.tmp.join("kindle-display-quiesce"));
        remove(self.tmp.join("kindle-display-quiesced"));
        marker(self.tmp.join("kindle-display-full-refresh"))?;
        marker(self.tmp.join("kindle-display-resume"))?;
        remove(self.tmp.join("kindle-session-locked"));
        self.wait_file("kindle-display-resumed")?;
        self.set_state(State::Active)?;
        self.last_status = Instant::now() - Duration::from_secs(60);
        self.ready("resume");
        Ok(())
    }
    fn exit(&mut self) -> Result<()> {
        if matches!(self.state, State::Native | State::NativePending)
            && Path::new(&helper("frozen-mtp.pid")).exists()
        {
            action(
                "/bin/sh",
                &[&helper("storage-gate.sh"), "close-frozen"],
                Duration::from_secs(12),
            )?;
        }
        atomic(helper("full-exit.started-ns"), &format!("{}\n", now()))?;
        self.set_state(State::Exiting)?;
        remove(self.tmp.join("kindle-warm-available"));
        if Path::new(&helper("warm-native")).exists() {
            self.stop_native()?;
        }
        action(
            &helper("session-freezer"),
            &["thaw", &self.stage],
            Duration::from_secs(6),
        )?;
        fs::write(
            self.tmp.join("kindle-android-exit"),
            b"rust-controller-exit\n",
        )?;
        Ok(())
    }
    fn clients(&mut self) -> Result<Option<Request>> {
        let mut command = None;
        for _ in 0..8 {
            let (mut stream, _) = match self.listener.accept() {
                Ok(s) => s,
                Err(e) if e.kind() == io::ErrorKind::WouldBlock => break,
                Err(e) => return Err(e.into()),
            };
            stream.set_read_timeout(Some(Duration::from_millis(100)))?;
            stream.set_write_timeout(Some(Duration::from_millis(100)))?;
            let mut cred = Cred {
                pid: 0,
                uid: u32::MAX,
                gid: 0,
            };
            let mut len = std::mem::size_of::<Cred>() as u32;
            if unsafe { getsockopt(stream.as_raw_fd(), 1, 17, &mut cred, &mut len) } != 0
                || cred.uid != 0
            {
                let _ = stream.write_all(b"DENIED\n");
                continue;
            }
            let mut buffer = [0u8; 66];
            let n = match stream.read(&mut buffer) {
                Ok(n) => n,
                Err(_) => continue,
            };
            match std::str::from_utf8(&buffer[..n])
                .ok()
                .and_then(|s| protocol::request(s).ok())
            {
                Some(Request::Status) => {
                    let _ = stream.write_all(
                        format!("{} generation={}\n", self.state.name(), self.generation)
                            .as_bytes(),
                    );
                }
                Some(request) => {
                    if command.is_some() {
                        let _ = stream.write_all(b"BUSY\n");
                    } else {
                        command = Some(request);
                        let _ = stream.write_all(b"ACCEPTED\n");
                    }
                }
                None => {
                    let _ = stream.write_all(b"INVALID\n");
                }
            }
        }
        Ok(command)
    }
    fn hardware(&mut self) -> Result<()> {
        if self.state != State::Active {
            return Ok(());
        }
        let path = self.tmp.join("kindle-device-request");
        let work = if path.is_file() {
            let s = text(&path);
            remove(path);
            if protocol::hardware_request(&s).is_none() {
                return Err("invalid_hardware_request".into());
            }
            Some(HardwareWork::Set(s))
        } else if self.last_status.elapsed() >= Duration::from_secs(60) {
            Some(HardwareWork::Status)
        } else {
            None
        };
        if let Some(work) = work {
            self.hardware_tx
                .as_ref()
                .ok_or("hardware_worker_missing")?
                .try_send(work)?;
            self.last_status = Instant::now();
        }
        Ok(())
    }
    fn run(&mut self) -> Result<()> {
        loop {
            self.check()?;
            let mut command = self.clients()?;
            if self.tmp.join("kindle-android-exit").exists()
                || PathBuf::from(format!(
                    "/proc/{}/root/data/user_de/0/org.kindlelab.apkprobe/files/kindle-android-exit",
                    self.guard.pid
                ))
                .exists()
            {
                command = Some(Request::Exit);
            }
            let path = PathBuf::from(helper("warm.command"));
            if path.is_file() {
                let s = text(&path);
                remove(path);
                if let Ok(c) = protocol::request(&s) {
                    if command != Some(Request::Exit) {
                        command = Some(c);
                    }
                }
            }
            if command == Some(Request::Exit) {
                if matches!(self.state, State::Native | State::NativePending)
                    && Path::new(&helper("frozen-mtp.pid")).exists()
                    && !usb_host_disconnected()?
                {
                    println!("WARM_EXIT_DEFERRED_USB_CONNECTED=1 phase=frozen_mtp");
                    continue;
                }
                self.exit()?;
                return Ok(());
            }
            if self.state == State::Active
                && (command == Some(Request::Retain)
                    || self.tmp.join("kindle-android-retain").is_file())
            {
                self.retain()?;
            } else if matches!(self.state, State::Native | State::NativePending)
                && command == Some(Request::Resume)
            {
                match usb_host_disconnected() {
                    Ok(true) => match self.resume() {
                        Ok(()) => (),
                        Err(error) if error.to_string().contains("resume_deferred_usb_connected") => (),
                        Err(error) => return Err(error),
                    },
                    Ok(false) => println!("WARM_RESUME_DEFERRED_USB_CONNECTED=1"),
                    Err(error) => eprintln!("WARM_RESUME_USB_CHECK_ERROR={error}"),
                }
            }
            if self.state == State::NativePending {
                self.poll_home()?;
            }
            // A failed optional status query must not terminate a reading session.
            if let Err(e) = self.hardware() {
                eprintln!("HARDWARE_STATUS_ERROR={e}");
                self.last_status = Instant::now();
            }
            self.events.wait(
                if self.state == State::NativePending {
                    Duration::from_millis(200)
                } else {
                    Duration::from_secs(1)
                },
                Some(self.listener.as_raw_fd()),
            )?;
        }
    }
}
impl Drop for Controller {
    fn drop(&mut self) {
        if let Some((mut child, _)) = self.home.take() {
            let _ = child.kill();
            let _ = child.wait();
        }
        self.hardware_active.store(false, Ordering::Relaxed);
        self.hardware_tx.take();
        if let Some(worker) = self.hardware_thread.take() {
            let _ = worker.join();
        }
        remove(self.socket_dir.join("control.sock"));
        let _ = fs::remove_dir(&self.socket_dir);
        if text(helper("warm-controller.socket")).trim()
            == self.socket_dir.join("control.sock").to_string_lossy()
        {
            remove(helper("warm-controller.socket"));
        }
        println!(
            "CONTROLLER_END lifetime_ms={}",
            (now() - self.started_ns) / 1_000_000
        );
    }
}
#[derive(Debug)]
enum HardwareWork {
    Status,
    Set(String),
}
fn hardware_work(tmp: &Path, active: &AtomicBool, work: HardwareWork) -> Result<()> {
    if let HardwareWork::Set(s) = work {
        let (key, value) = protocol::hardware_request(&s).ok_or("invalid_hardware_request")?;
        let (service, property) = match key {
            "brightness" => ("com.lab126.powerd", "flIntensity"),
            "warmth" => ("com.lab126.powerd", "currentAmberLevel"),
            _ => ("com.lab126.wifid", "enable"),
        };
        action(
            "/usr/bin/lipc-set-prop",
            &[service, property, &value.to_string()],
            Duration::from_secs(3),
        )?;
    }
    let mut state = String::new();
    for (key, service, property) in [
        ("brightness", "com.lab126.powerd", "flIntensity"),
        ("warmth", "com.lab126.powerd", "currentAmberLevel"),
        ("wifi", "com.lab126.wifid", "enable"),
        ("connection", "com.lab126.wifid", "cmState"),
    ] {
        if !active.load(Ordering::Relaxed) {
            return Ok(());
        }
        if let Ok(value) = output(
            "/usr/bin/lipc-get-prop",
            &[service, property],
            Duration::from_secs(2),
        ) {
            state.push_str(&format!(
                "{key}={}\n",
                value.trim().replace(['\n', '\r'], "")
            ));
        }
    }
    if let Ok(entries) = fs::read_dir("/sys/class/power_supply") {
        for e in entries.flatten() {
            let value = text(e.path().join("capacity"));
            if value.trim().parse::<u8>().is_ok_and(|v| v <= 100) {
                state.push_str(&format!("battery={}\n", value.trim()));
                break;
            }
        }
    }
    if let Ok(clock) = Command::new("/bin/date")
        .env("TZ", "CST-8")
        .arg("+%H:%M")
        .output()
    {
        state.push_str(&format!(
            "clock={}\n",
            String::from_utf8_lossy(&clock.stdout).trim()
        ));
    }
    if active.load(Ordering::Relaxed) {
        atomic(tmp.join("kindle-device-status"), &state)?;
        fs::set_permissions(
            tmp.join("kindle-device-status"),
            fs::Permissions::from_mode(0o644),
        )?;
    }
    Ok(())
}
fn request_client(request: &str) -> Result<()> {
    protocol::request(request).map_err(io::Error::other)?;
    let path = text(helper("warm-controller.socket"));
    let path = PathBuf::from(path.trim());
    let parent = path.parent().ok_or("invalid_socket_path")?;
    if !parent
        .to_string_lossy()
        .starts_with("/dev/shm/kindle-android-ctl-")
        || path.file_name().is_none_or(|v| v != "control.sock")
    {
        return Err("invalid_socket_path".into());
    }
    let meta = fs::symlink_metadata(parent)?;
    let owner = parent
        .file_name()
        .and_then(|v| v.to_str())
        .and_then(|v| v.strip_prefix("kindle-android-ctl-"))
        .and_then(|v| v.parse::<u32>().ok())
        .ok_or("invalid_socket_owner")?;
    if parent != Path::new(&format!("/dev/shm/kindle-android-ctl-{owner}")) {
        return Err("invalid_socket_directory".into());
    }
    if !meta.is_dir() || meta.uid() != 0 || meta.mode() & 0o077 != 0 {
        return Err("unsafe_socket_directory".into());
    }
    let mut stream = UnixStream::connect(path)?;
    stream.set_read_timeout(Some(Duration::from_secs(2)))?;
    stream.write_all(format!("{request}\n").as_bytes())?;
    let mut b = [0u8; 256];
    let n = stream.read(&mut b)?;
    let response = std::str::from_utf8(&b[..n])?;
    print!("{response}");
    if response.starts_with("DENIED")
        || response.starts_with("BUSY")
        || response.starts_with("INVALID")
    {
        return Err("request_rejected".into());
    }
    Ok(())
}
fn observe(seconds: u64) -> Result<()> {
    let current = text(helper("warm-state"));
    let lines: Vec<_> = current.lines().collect();
    if lines.len() < 4 {
        return Err("no_session".into());
    }
    let guard: u32 = lines[3].parse()?;
    let tmp = PathBuf::from(format!("/proc/{guard}/root/data/local/tmp"));
    let mut events = Events::new()?;
    events.watch(Path::new(BASE))?;
    events.watch(&tmp)?;
    let end = Instant::now() + Duration::from_secs(seconds);
    let begin = now();
    let mut wakeups = 0u64;
    println!(
        "OBSERVE_BEGIN monotonic_ns={begin} seconds={seconds} watches={} fallback={}",
        events.watches, events.missing
    );
    while Instant::now() < end {
        events.wait(end.saturating_duration_since(Instant::now()), None)?;
        wakeups += 1;
    }
    println!(
        "OBSERVE_END elapsed_ms={} wakeups={} current_state={}",
        (now() - begin) / 1_000_000,
        wakeups,
        text(helper("warm-state"))
            .lines()
            .next()
            .unwrap_or("missing")
    );
    Ok(())
}
fn self_test() -> Result<()> {
    let dir = PathBuf::from(format!(
        "/tmp/kindle-controller-selftest-{}",
        std::process::id()
    ));
    fs::create_dir(&dir)?;
    let mut events = Events::new()?;
    events.watch(&dir)?;
    let file = dir.join("notice");
    let writer = file.clone();
    let begin = now();
    let t = thread::spawn(move || {
        thread::sleep(Duration::from_millis(80));
        fs::write(writer, b"test").unwrap();
    });
    events.wait(Duration::from_secs(2), None)?;
    t.join().map_err(|_| "test_thread_failed")?;
    if text(&file) != "test" || (now() - begin) > 1_500_000_000 {
        return Err("inotify_selftest_failed".into());
    }
    let own = Identity::new(std::process::id())?;
    if !own.alive() {
        return Err("identity_selftest_failed".into());
    }
    let stale = Identity {
        pid: own.pid,
        start: own.start + 1,
    };
    if stale.alive() {
        return Err("pid_reuse_check_failed".into());
    }
    let deadline = Instant::now();
    events.wait(Duration::from_millis(40), None)?;
    if deadline.elapsed() > Duration::from_millis(500) {
        return Err("event_deadline_failed".into());
    }
    remove(file);
    fs::remove_dir(dir)?;
    println!("CONTROLLER_SELFTEST_PASS clock=monotonic identity=verified inotify=delivered elapsed_ms={}",(now()-begin)/1_000_000);
    Ok(())
}
pub fn entry() -> Result<()> {
    let args: Vec<String> = env::args().collect();
    match args.get(1).map(String::as_str){
        Some("self-test")=>self_test(),
        Some("observe")=>observe(args.get(2).ok_or("seconds_required")?.parse()?),
        Some("request")=>request_client(args.get(2).ok_or("request_required")?),
        Some("native-probe")=>native_probe(args.get(2).ok_or("seconds_required")?.parse()?),
        Some("native-hold")=>native_ui::hold(args.get(2).ok_or("launcher_required")?.parse()?),
        Some("native-release")=>native_ui::release(),
        Some("native-ui-guard")=>native_ui::guard(args.get(2).ok_or("lease_required")?),
        Some("native-probe-guard")=>native_probe_guard(args.get(2).ok_or("lease_required")?,args.get(3).ok_or("owner_required")?.parse()?,args.get(4).ok_or("identity_required")?.parse()?,args.get(5).ok_or("deadline_required")?.parse()?),
        Some("run")=>{
            unsafe{signal(1,stop_signal);signal(2,stop_signal);signal(15,stop_signal);}
            let launcher=args.get(2).ok_or("launcher_required")?.parse()?;let stage=args.get(3).ok_or("stage_required")?;
            let mut c=Controller::new(launcher,stage)?;let result=c.run();
            if result.is_err()&&c.guard.alive(){STOP.store(false,Ordering::Relaxed);if let Err(e)=c.exit(){eprintln!("CONTROLLER_EMERGENCY_EXIT_ERROR={e}");}}
            result
        }
        _=>Err("usage: session-controller self-test | observe SECONDS | request status|retain|resume|exit | run LAUNCHER STAGE".into())
    }
}

fn native_name(pid: u32) -> Option<String> {
    let exe = fs::read_link(format!("/proc/{pid}/exe")).ok()?;
    let name = match exe.to_str()? {
        "/usr/bin/Xorg" => "Xorg",
        "/usr/bin/awesome" => "awesome",
        "/usr/sbin/blanket" => "blanket",
        "/app/bin/KPPMainApp" | "/app/bin/KPPMainAppV2" => "KPPMainApp",
        "/app/bin/JunoStatusBarDriver" => "JunoStatusBarDriver",
        "/app/KPPPillow-2.0/bin/pillowd" => "pillow",
        _ => return None,
    };
    Some(name.to_string())
}
fn resume_lease(path: &Path) -> Result<usize> {
    let data = text(path);
    let mut resumed = 0;
    for line in data.lines().skip(1) {
        let fields: Vec<_> = line.split_whitespace().collect();
        if fields.len() != 3 {
            return Err("invalid_native_lease".into());
        }
        let id = Identity {
            pid: fields[0].parse()?,
            start: fields[1].parse()?,
        };
        if id.alive() && native_name(id.pid).as_deref() == Some(fields[2]) {
            if unsafe { kill(id.pid as i32, 18) } != 0 {
                return Err(io::Error::last_os_error().into());
            }
            resumed += 1;
        }
    }
    Ok(resumed)
}
fn native_probe_guard(lease: &str, pid: u32, start: u64, deadline: u64) -> Result<()> {
    let expected = PathBuf::from(format!("/tmp/kindle-native-probe-{pid}/lease"));
    if Path::new(lease) != expected || fs::symlink_metadata(&expected)?.uid() != 0 {
        return Err("invalid_guard_lease_path".into());
    }
    let owner = Identity { pid, start };
    let expected_marker = text(helper("release-maintenance.lock"));
    while owner.alive() && now() < deadline && expected.exists() {
        thread::sleep(Duration::from_millis(50));
    }
    if expected.exists() {
        let count = resume_lease(&expected)?;
        println!(
            "NATIVE_PROBE_GUARD_RESTORED count={count} monotonic_ns={}",
            now()
        );
    }
    if !expected_marker.is_empty()
        && expected_marker.starts_with(&format!("NATIVE_UI_PROBE_V1\n{pid}\n"))
        && text(helper("release-maintenance.lock")) == expected_marker
    {
        remove(helper("release-maintenance.lock"));
    }
    Ok(())
}
fn native_probe(seconds: u64) -> Result<()> {
    use std::os::unix::process::CommandExt;
    if !(1..=60).contains(&seconds) {
        return Err("probe_must_be_1_to_60_seconds".into());
    }
    if Path::new(&helper("session.lock")).exists() {
        return Err("native_probe_refused_android_running".into());
    }
    if fs::read_dir("/proc")?
        .flatten()
        .any(|e| text(e.path().join("comm")).trim() == "art-guard")
    {
        return Err("native_probe_refused_android_process".into());
    }
    action(
        &helper("usb-session-guard"),
        &["--check"],
        Duration::from_secs(3),
    )?;
    for job in ["lab126_gui", "x"] {
        if !output("/sbin/initctl", &["status", job], Duration::from_secs(3))?
            .contains("start/running")
        {
            return Err("native_framework_not_running".into());
        }
    }
    let app = output(
        "/usr/bin/lipc-get-prop",
        &["-s", "com.lab126.appmgrd", "activeApp"],
        Duration::from_secs(2),
    )?;
    if app.trim() != "com.lab126.KPPMainApp" {
        return Err("native_home_not_active".into());
    }
    let owner = Identity::new(std::process::id())?;
    let dir = PathBuf::from(format!("/tmp/kindle-native-probe-{}", owner.pid));
    fs::create_dir(&dir)?;
    fs::set_permissions(&dir, fs::Permissions::from_mode(0o700))?;
    let marker_path = helper("release-maintenance.lock");
    let marker_content = format!(
        "NATIVE_UI_PROBE_V1\n{}\n{}\n",
        owner.pid,
        text("/proc/sys/kernel/random/boot_id").trim()
    );
    let mut marker = fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .mode(0o600)
        .custom_flags(0x20000)
        .open(&marker_path)?;
    marker.write_all(marker_content.as_bytes())?;
    drop(marker);
    let result = (|| -> Result<()> {
        let mut targets = Vec::new();
        for entry in fs::read_dir("/proc")?.flatten() {
            if let Ok(pid) = entry.file_name().to_string_lossy().parse::<u32>() {
                if let Some(name) = native_name(pid) {
                    let stat = text(entry.path().join("stat"));
                    let state = stat
                        .rsplit_once(')')
                        .and_then(|(_, s)| s.trim().chars().next());
                    if state != Some('T') && state != Some('t') {
                        if let Ok(id) = Identity::new(pid) {
                            targets.push((id, name));
                        }
                    }
                }
            }
        }
        for required in ["Xorg", "awesome", "KPPMainApp"] {
            if !targets.iter().any(|(_, name)| name == required) {
                return Err(format!("native_probe_missing_target {required}").into());
            }
        }
        let lease = dir.join("lease");
        let mut contents = String::from("NATIVE_UI_PROBE_V1\n");
        for (id, name) in &targets {
            contents.push_str(&format!("{} {} {name}\n", id.pid, id.start));
        }
        atomic(&lease, &contents)?;
        let log = fs::File::create(dir.join("guard.log"))?;
        let mut command = Command::new(env::current_exe()?);
        command
            .args([
                "native-probe-guard",
                lease.to_str().ok_or("lease_encoding")?,
                &owner.pid.to_string(),
                &owner.start.to_string(),
                &(now() + (seconds + 3) * 1_000_000_000).to_string(),
            ])
            .stdin(Stdio::null())
            .stdout(Stdio::from(log.try_clone()?))
            .stderr(Stdio::from(log));
        unsafe {
            command.pre_exec(|| {
                if setsid() < 0 {
                    return Err(io::Error::last_os_error());
                }
                Ok(())
            });
        }
        let mut guard = command.spawn()?;
        // The lease is fully written and the independent guardian is running
        // before the first UI process is stopped. No Android runs during this probe.
        let started = now();
        let mut failure = None;
        for (id, name) in &targets {
            if !id.alive() || unsafe { kill(id.pid as i32, 19) } != 0 {
                failure = Some(format!("native_pause_failed pid={} name={name}", id.pid));
                break;
            }
        }
        println!(
            "NATIVE_PROBE_HELD targets={} seconds={seconds} monotonic_ns={started}",
            targets.len()
        );
        while failure.is_none()
            && now() - started < seconds * 1_000_000_000
            && !STOP.load(Ordering::Relaxed)
        {
            if targets.iter().any(|(id, _)| !id.alive()) {
                failure = Some("native_process_restarted_while_paused".to_string());
                break;
            }
            thread::sleep(Duration::from_millis(100));
        }
        let resumed = resume_lease(&lease)?;
        remove(&lease);
        let _ = guard.wait();
        for (id, name) in &targets {
            if !id.alive() {
                failure = Some(format!("native_pid_changed pid={} name={name}", id.pid));
            }
        }
        println!(
            "NATIVE_PROBE_RELEASED count={resumed} elapsed_ms={} monotonic_ns={}",
            (now() - started) / 1_000_000,
            now()
        );
        if let Some(error) = failure {
            return Err(error.into());
        }
        // Probe checks process continuity. A separate frame and real touch check
        // are still required before enabling a persistent UI-preserving backend.
        println!("NATIVE_PROBE_PASS process_continuity=1 android_overlap=none");
        Ok(())
    })();
    if text(&marker_path) == marker_content {
        remove(&marker_path);
    }
    result
}
