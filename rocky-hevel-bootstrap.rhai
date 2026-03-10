let ctx = #{
    marker: "/usr/local/share/recipe-state/rocky-hevel-bootstrap",
    name: "rocky-hevel-bootstrap",
    packages: "gcc make pkgconf-pkg-config git patch dbus-daemon wayland-devel wayland-protocols-devel libdrm-devel pixman-devel libxkbcommon-devel fontconfig-devel freetype-devel libinput-devel systemd-devel libxcb-devel xcb-util-wm-devel libXt-devel libXaw-devel rust cargo pam-devel policycoreutils checkpolicy policycoreutils-python-utils dbus-devel libei libeis xdg-desktop-portal meson ninja-build python3-jinja2 python3-attrs",
};

fn is_installed(ctx) {
    if !is_file(ctx.marker) { throw "marker missing"; }
    if shell_status("rpm -q --quiet " + ctx.packages) != 0 {
        throw "required RPMs missing";
    }
    ctx
}

fn is_acquired(ctx) {
    ctx
}

fn acquire(ctx) {
    ctx
}

fn is_built(ctx) {
    ctx
}

fn install(ctx) {
    let marker_tmp = join_path(BUILD_DIR, "rocky-hevel-bootstrap.marker");
    shell("sudo dnf -y --setopt=install_weak_deps=False --enablerepo=crb install " + ctx.packages);
    write_file(marker_tmp, "installed\n");
    shell("sudo install -D -m 644 \"" + marker_tmp + "\" \"" + ctx.marker + "\"");
    ctx
}

fn remove(ctx) {
    shell("sudo rm -f \"" + ctx.marker + "\"");
    ctx
}

fn cleanup(ctx, reason) {
    ctx
}
