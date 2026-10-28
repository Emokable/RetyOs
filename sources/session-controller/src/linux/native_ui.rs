//! Optional retained native UI. Never release native writers over live Android.
use super::*;
use std::os::unix::process::CommandExt;

pub(super) fn enabled() -> bool {
    text(helper("native-framework.mode")).trim() == "preserve"
}
fn android_guards() -> Vec<Identity> {
    fs::read_dir("/proc")
        .into_iter()
        .flatten()
        .flatten()
        .filter_map(|e| {
            let pid = e.file_name().to_string_lossy().parse().ok()?;
            if text(e.path().join("comm")).trim() != "art-guard" {
                return None;
            }
            Identity::new(pid).ok()
        })
        .collect()
}
fn quiet() -> Result<()> {
    // nuku: native UI handoff boundary.
    let guards = android_guards();
    if guards.is_empty() {
        return Ok(());
    }
    if guards.len() != 1 {
        return Err("multiple_android_guards".into());
    }
    let stage = text(helper("warm-freezer.stage"));
    let stage = stage.trim();
    if !protocol::stage_valid(stage) {
        return Err("invalid_frozen_stage".into());
    }
    let frozen = output(
        &helper("session-freezer"),
        &["status", stage],
        Duration::from_secs(2),
    )?;
    let tmp = PathBuf::from(format!("/proc/{}/root/data/local/tmp", guards[0].pid));
    if frozen.trim() != "FROZEN"
        || !tmp.join("kindle-display-quiesced").is_file()
        || !tmp.join("kindle-input-released").is_file()
    {
        return Err("native_ui_refused_android_not_quiescent".into());
    }
    Ok(())
}
fn lease_dir() -> Result<PathBuf> {
    let value = text(helper("native-ui.lease"));
    let dir = PathBuf::from(value.trim());
    validate(&dir)?;
    Ok(dir)
}
fn validate(dir: &Path) -> Result<()> {
    let pid: u32 = dir
        .file_name()
        .and_then(|v| v.to_str())
        .and_then(|v| v.strip_prefix("kindle-native-ui-"))
        .ok_or("invalid_native_lease_path")?
        .parse()?;
    if dir != Path::new(&format!("/dev/shm/kindle-native-ui-{pid}")) {
        return Err("invalid_native_lease_path".into());
    }
    let m = fs::symlink_metadata(dir)?;
    if !m.is_dir() || m.uid() != 0 || m.mode() & 0o077 != 0 {
        return Err("unsafe_native_lease_directory".into());
    }
    let owner = text(dir.join("owner"));
    if owner.lines().nth(2) != Some(text("/proc/sys/kernel/random/boot_id").trim()) {
        return Err("native_lease_boot_mismatch".into());
    }
    Ok(())
}
fn ledger_valid(dir: &Path) -> bool {
    let data = text(dir.join("lease"));
    !data.is_empty()
        && data.lines().skip(1).all(|line| {
            let v: Vec<_> = line.split_whitespace().collect();
            if v.len() != 3 {
                return false;
            }
            match (v[0].parse(), v[1].parse()) {
                (Ok(pid), Ok(start)) => {
                    Identity { pid, start }.alive() && native_name(pid).as_deref() == Some(v[2])
                }
                _ => false,
            }
        })
}
pub(super) fn hold(pid: u32) -> Result<()> {
    quiet()?;
    if Path::new(&helper("native-ui.lease")).exists() {
        let dir = lease_dir()?;
        if ledger_valid(&dir) {
            return Ok(());
        }
        return Err("stale_native_ui_lease".into());
    }
    let owner = Identity::new(pid)?;
    if text(helper("session.lock/pid")).trim() != pid.to_string() {
        return Err("native_hold_launcher_lock_mismatch".into());
    }
    for job in ["lab126_gui", "x"] {
        if !output("/sbin/initctl", &["status", job], Duration::from_secs(3))?
            .contains("start/running")
        {
            return Err("native_hold_requires_running_ui".into());
        }
    }
    let mut targets = Vec::new();
    for entry in fs::read_dir("/proc")?.flatten() {
        if let Ok(pid) = entry.file_name().to_string_lossy().parse::<u32>() {
            if let Some(name) = native_name(pid) {
                let stat = text(entry.path().join("stat"));
                if matches!(
                    stat.rsplit_once(')')
                        .and_then(|(_, s)| s.trim().chars().next()),
                    Some('T' | 't')
                ) {
                    return Err("native_process_already_stopped".into());
                }
                targets.push((Identity::new(pid)?, name));
            }
        }
    }
    for required in ["Xorg", "awesome", "KPPMainApp"] {
        if !targets.iter().any(|(_, name)| name == required) {
            return Err(format!("missing_native_ui {required}").into());
        }
    }
    let dir = PathBuf::from(format!("/dev/shm/kindle-native-ui-{pid}"));
    fs::create_dir(&dir)?;
    fs::set_permissions(&dir, fs::Permissions::from_mode(0o700))?;
    atomic(
        dir.join("owner"),
        &format!(
            "{}\n{}\n{}\n{}\n",
            pid,
            owner.start,
            text("/proc/sys/kernel/random/boot_id").trim(),
            now()
        ),
    )?;
    let mut ledger = String::from("NATIVE_UI_LEASE_V1\n");
    for (id, name) in &targets {
        ledger.push_str(&format!("{} {} {name}\n", id.pid, id.start));
    }
    atomic(dir.join("lease"), &ledger)?;
    atomic(helper("native-ui.lease"), &format!("{}\n", dir.display()))?;
    let log = fs::File::create(dir.join("guard.log"))?;
    let mut cmd = Command::new(env::current_exe()?);
    cmd.args(["native-ui-guard", dir.to_str().ok_or("lease_encoding")?])
        .stdin(Stdio::null())
        .stdout(Stdio::from(log.try_clone()?))
        .stderr(Stdio::from(log));
    unsafe {
        cmd.pre_exec(|| {
            if setsid() < 0 {
                return Err(io::Error::last_os_error());
            }
            Ok(())
        });
    }
    let result = (|| -> Result<()> {
        cmd.spawn()?;
        for (id, _) in &targets {
            if !id.alive() || unsafe { kill(id.pid as i32, 19) } != 0 {
                return Err("native_pause_failed".into());
            }
        }
        // Wait for each SIGSTOP to take effect before taking the framebuffer.
        let end = Instant::now() + Duration::from_secs(2);
        while targets.iter().any(|(id, _)| {
            !text(format!("/proc/{}/stat", id.pid))
                .rsplit_once(')')
                .is_some_and(|(_, s)| s.trim().starts_with('T'))
        }) {
            if Instant::now() > end {
                return Err("native_pause_timeout".into());
            }
            thread::sleep(Duration::from_millis(10));
        }
        action(
            &helper("native-frame-state"),
            &["save", dir.join("frame").to_str().ok_or("frame_path")?],
            Duration::from_secs(5),
        )?;
        marker(dir.join("held"))?;
        println!(
            "NATIVE_UI_HELD targets={} monotonic_ns={}",
            targets.len(),
            now()
        );
        Ok(())
    })();
    if result.is_err() {
        let _ = release();
    }
    result
}
pub(super) fn release() -> Result<()> {
    let lock = fs::OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .mode(0o600)
        .custom_flags(0x20000)
        .open("/dev/shm/kindle-native-release.lock")?;
    if unsafe { flock(lock.as_raw_fd(), 2) } != 0 {
        return Err(io::Error::last_os_error().into());
    }
    if !Path::new(&helper("native-ui.lease")).exists() {
        return Ok(());
    }
    quiet()?;
    let dir = lease_dir()?;
    let complete = dir.join("held").exists();
    if complete {
        action(
            &helper("native-frame-state"),
            &["restore", dir.join("frame").to_str().ok_or("frame_path")?],
            Duration::from_secs(5),
        )?;
    }
    let count = resume_lease(&dir.join("lease"))?;
    if complete {
        // Restore X's view of the framebuffer as well as its pixels. Without
        // Expose events, unchanged window margins can retain Android text.
        action(
            "/usr/bin/xrefresh",
            &["-display", ":0"],
            Duration::from_secs(3),
        )?;
    }
    remove(helper("native-ui.lease"));
    // Preserve evidence outside the lease without retaining its large frame.
    let _ = fs::copy(dir.join("guard.log"), helper("native-ui-last-guard.log"));
    for name in ["held", "frame", "owner", "lease", "guard.log"] {
        remove(dir.join(name));
    }
    let _ = fs::remove_dir(&dir);
    println!(
        "NATIVE_UI_RELEASED targets={count} snapshot_restored={complete} monotonic_ns={}",
        now()
    );
    Ok(())
}
pub(super) fn guard(value: &str) -> Result<()> {
    let dir = PathBuf::from(value);
    validate(&dir)?;
    let data = text(dir.join("owner"));
    let v: Vec<_> = data.lines().collect();
    let owner = Identity {
        pid: v[0].parse()?,
        start: v[1].parse()?,
    };
    let mut controller: Option<Identity> = None;
    while dir.join("lease").exists() {
        // The same launcher can create several leases. A previous guardian
        // must never attach to a newly created generation at the same path.
        if text(dir.join("owner")) != data {
            break;
        }
        if controller.is_none() {
            controller = text(helper("warm-controller.pid"))
                .trim()
                .parse()
                .ok()
                .and_then(|p| Identity::new(p).ok());
        }
        if !owner.alive()
            || controller.as_ref().is_some_and(|id| !id.alive())
            || !ledger_valid(&dir)
        {
            // Release may have removed the ledger after the loop condition.
            // That is a normal handoff, never an Android failure.
            if !dir.join("lease").exists() || text(dir.join("owner")) != data {
                break;
            }
            println!("NATIVE_UI_GUARD_OWNER_FAILED monotonic_ns={}", now());
            // Keep native writers paused while asking Android to shut down.
            // Never thaw a failed session over a running native compositor.
            for id in android_guards() {
                let tmp = PathBuf::from(format!("/proc/{}/root/data/local/tmp", id.pid));
                for name in [
                    "kindle-session-locked",
                    "kindle-input-release",
                    "kindle-display-quiesce",
                    "kindle-android-exit",
                ] {
                    let _ = marker(tmp.join(name));
                }
            }
            let stage = text(helper("warm-freezer.stage"));
            if protocol::stage_valid(stage.trim()) {
                let _ = action(
                    &helper("session-freezer"),
                    &["thaw", stage.trim()],
                    Duration::from_secs(5),
                );
            }
            // art-guard exiting is not the last display writer: the outer
            // launcher still restores its recovery framebuffer afterwards.
            // Let the live launcher finish that work and call native-release.
            // After launcher death, its existing recovery watchdog must retire
            // the outer framebuffer snapshot before we can resume native X.
            while dir.join("lease").exists()
                && (owner.alive()
                    || !android_guards().is_empty()
                    || Path::new("/mnt/us/.kindle-a7-hwtcon-restore.bin").exists())
            {
                thread::sleep(Duration::from_millis(200));
            }
            if text(helper("native-ui.lease")).trim() == dir.to_string_lossy()
                && text(dir.join("owner")) == data
            {
                release()?;
            }
            // Existing launcher recovery owns home activation and MTP release.
            break;
        }
        thread::sleep(Duration::from_millis(200));
    }
    Ok(())
}
