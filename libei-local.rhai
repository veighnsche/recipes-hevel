let deps = ["rocky-hevel-bootstrap"];

let ctx = #{
    marker: "/usr/local/share/recipe-state/libei-local",
    name: "libei-local",
    repo_url: "https://gitlab.freedesktop.org/libinput/libei.git",
    src_dir: "/tmp/.tmpjKt6l6/libei-1.3.0",
    stage_dir: "/tmp/.tmpjKt6l6/stage",
    version: "1.3.0",
};

fn is_installed(ctx) {
    if !is_file(ctx.marker) { throw "marker missing"; }
    if !is_file("/usr/local/include/libei-1.0/libei.h") { throw "libei headers missing"; }
    if !is_file("/usr/local/include/libei-1.0/libeis.h") { throw "libeis headers missing"; }
    if !is_file("/usr/local/lib64/pkgconfig/libei-1.0.pc") { throw "libei pc file missing"; }
    if !is_file("/usr/local/lib64/pkgconfig/libeis-1.0.pc") { throw "libeis pc file missing"; }
    if shell_status("PKG_CONFIG_PATH=/usr/local/lib64/pkgconfig:/usr/local/lib/pkgconfig:/usr/lib64/pkgconfig:/usr/share/pkgconfig pkg-config --exists libei-1.0 libeis-1.0") != 0 {
        throw "pkg-config cannot resolve libei/libeis";
    }
    ctx
}

fn is_acquired(ctx) {
    if ctx.src_dir == "" || !is_dir(ctx.src_dir) { throw "source missing"; }
    ctx
}

fn acquire(ctx) {
    mkdir(BUILD_DIR);
    let src_dir = join_path(BUILD_DIR, "libei-1.3.0");
    shell_in(BUILD_DIR, "rm -rf \"" + src_dir + "\" stage build");
    shell_in(BUILD_DIR, "git clone \"" + ctx.repo_url + "\" \"" + src_dir + "\"");
    shell_in(src_dir, "git checkout --detach \"" + ctx.version + "\"");
    ctx.src_dir = src_dir;
    ctx.stage_dir = join_path(BUILD_DIR, "stage");
    ctx
}

fn is_built(ctx) {
    if !is_file(join_path(ctx.stage_dir, "usr/local/include/libei-1.0/libei.h")) { throw "staged libei header missing"; }
    if !is_file(join_path(ctx.stage_dir, "usr/local/include/libei-1.0/libeis.h")) { throw "staged libeis header missing"; }
    if !is_file(join_path(ctx.stage_dir, "usr/local/lib64/pkgconfig/libei-1.0.pc")) { throw "staged libei pc file missing"; }
    if !is_file(join_path(ctx.stage_dir, "usr/local/lib64/pkgconfig/libeis-1.0.pc")) { throw "staged libeis pc file missing"; }
    ctx
}

fn build(ctx) {
    let pkg_config_path = "/usr/local/lib64/pkgconfig:/usr/local/lib/pkgconfig:/usr/lib64/pkgconfig:/usr/share/pkgconfig";
    shell_in(BUILD_DIR, "rm -rf build stage");
    shell_in(
        ctx.src_dir,
        "PKG_CONFIG_PATH=" + pkg_config_path + " meson setup build --prefix=/usr/local -Dtests=disabled -Dliboeffis=disabled"
    );
    shell_in(ctx.src_dir, "meson compile -C build");
    shell_in(ctx.src_dir, "meson install -C build --destdir \"" + ctx.stage_dir + "\"");
    write_file(join_path(ctx.stage_dir, "libei-local.marker"), "installed\n");
    ctx
}

fn install(ctx) {
    shell("sudo install -d /usr/local");
    shell("sudo cp -a --no-preserve=ownership \"" + join_path(ctx.stage_dir, "usr/local/.") + "\" /usr/local/");
    shell("sudo install -D -m 644 \"" + join_path(ctx.stage_dir, "libei-local.marker") + "\" \"" + ctx.marker + "\"");
    ctx
}

fn remove(ctx) {
    shell("sudo rm -f /usr/local/lib64/libei.so* /usr/local/lib64/libeis.so* /usr/local/lib64/pkgconfig/libei-1.0.pc /usr/local/lib64/pkgconfig/libeis-1.0.pc \"" + ctx.marker + "\"");
    shell("sudo rm -rf /usr/local/include/libei-1.0");
    ctx
}

fn cleanup(ctx, reason) {
    ctx
}
