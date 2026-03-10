let deps = ["rocky-hevel-bootstrap", "bmake", "neuwld"];
let patch_file = join_path(RECIPE_DIR, "assets/neuswc-injection/injection.patch");

let ctx = #{
    bin_path: "/usr/local/bin/swc-launch",
    name: "neuswc",
    patch_file: patch_file,
    repo_url: "https://git.sr.ht/~shrub900/neuswc",
    src_dir: "/tmp/.tmpGjaxJt/neuswc-d7a9eda640d4b4d96f6158266099d3c3fe8e5673",
    stage_dir: "/tmp/.tmpGjaxJt/stage",
    version: "d7a9eda640d4b4d96f6158266099d3c3fe8e5673",
};

fn apply_patch_file(ctx) {
    let header = join_path(ctx.src_dir, "libswc/swc.h");
    if shell_status("grep -Fq 'swc_keyboard_inject_key' \"" + header + "\"") == 0 {
        return ctx;
    }
    shell_in(ctx.src_dir, "patch --forward -p1 < \"" + ctx.patch_file + "\"");
    ctx
}

fn is_installed(ctx) {
    let bin_path = "/usr/local/bin/swc-launch";
    if !is_file(bin_path) { throw "swc-launch missing"; }
    if shell_status("test -u /usr/local/bin/swc-launch") != 0 { throw "swc-launch is not setuid"; }
    if !is_file("/usr/local/lib/libswc.a") { throw "libswc.a missing"; }
    if !is_file("/usr/local/include/swc.h") { throw "swc.h missing"; }
    if !is_file("/usr/local/lib/pkgconfig/swc.pc") { throw "swc.pc missing"; }
    ctx.bin_path = bin_path;
    ctx
}

fn is_acquired(ctx) {
    let src_dir = join_path(BUILD_DIR, "neuswc-d7a9eda640d4b4d96f6158266099d3c3fe8e5673");
    if !is_dir(src_dir) { throw "source missing"; }
    ctx.src_dir = src_dir;
    ctx
}

fn acquire(ctx) {
    mkdir(BUILD_DIR);
    let src_dir = join_path(BUILD_DIR, "neuswc-d7a9eda640d4b4d96f6158266099d3c3fe8e5673");
    shell_in(BUILD_DIR, "rm -rf \"" + src_dir + "\" stage");
    shell_in(BUILD_DIR, "git clone \"" + ctx.repo_url + "\" \"" + src_dir + "\"");
    shell_in(src_dir, "git checkout --detach \"" + ctx.version + "\"");
    ctx.src_dir = src_dir;
    ctx
}

fn is_built(ctx) {
    let stage_dir = join_path(BUILD_DIR, "stage");
    if !is_file(join_path(stage_dir, "usr/local/bin/swc-launch")) { throw "staged swc-launch missing"; }
    ctx.stage_dir = stage_dir;
    ctx
}

fn build(ctx) {
    let stage_dir = join_path(BUILD_DIR, "stage");
    shell_in(BUILD_DIR, "rm -rf stage");
    apply_patch_file(ctx);
    shell_in(
        ctx.src_dir,
        "PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/lib64/pkgconfig:/usr/share/pkgconfig /usr/local/bin/bmake PREFIX=/usr/local BINDIR=/usr/local/bin LIBDIR=/usr/local/lib INCLUDEDIR=/usr/local/include DATADIR=/usr/local/share PKGCONFIGDIR=/usr/local/lib/pkgconfig ENABLE_DEBUG=0 ENABLE_STATIC=1 ENABLE_SHARED=0 ENABLE_LIBUDEV=1 ENABLE_XWAYLAND=1 CPPFLAGS=\"-D_GNU_SOURCE -DENABLE_LIBUDEV -DENABLE_XWAYLAND -DNDEBUG\" CFLAGS=\"-fvisibility=hidden -std=c11 $(PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/lib64/pkgconfig:/usr/share/pkgconfig pkg-config --cflags libdrm pixman-1 wayland-server wayland-protocols wld xkbcommon libinput libudev xcb xcb-composite xcb-ewmh xcb-icccm) -Werror=implicit-function-declaration -Werror=implicit-int -Werror=pointer-sign -Werror=pointer-arith -Wall -Wno-missing-braces\" PKG_LIBS=\"$(PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/lib64/pkgconfig:/usr/share/pkgconfig pkg-config --libs libdrm pixman-1 wayland-server wayland-protocols wld xkbcommon libinput libudev xcb xcb-composite xcb-ewmh xcb-icccm)\" WAYLAND_CLIENT_LIBS=\"$(PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/lib64/pkgconfig:/usr/share/pkgconfig pkg-config --libs wayland-client)\" WAYLAND_PROTOCOLS_DATADIR=\"$(PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/lib64/pkgconfig:/usr/share/pkgconfig pkg-config --variable=pkgdatadir wayland-protocols)\" DESTDIR=" + stage_dir + " install"
    );
    ctx.stage_dir = stage_dir;
    ctx
}

fn install(ctx) {
    shell("sudo install -d /usr/local");
    shell("sudo cp -a --no-preserve=ownership \"" + join_path(ctx.stage_dir, "usr/local/.") + "\" /usr/local/");
    ctx.bin_path = "/usr/local/bin/swc-launch";
    ctx
}

fn remove(ctx) {
    shell("sudo rm -f /usr/local/bin/swc-launch /usr/local/bin/swcsnap /usr/local/lib/libswc.a /usr/local/lib/pkgconfig/swc.pc");
    shell("sudo rm -f /usr/local/include/swc.h");
    shell("sudo rm -f /usr/local/include/linux-dmabuf-unstable-v1-server-protocol.h /usr/local/include/linux-dmabuf-unstable-v1-client-protocol.h /usr/local/include/server-decoration-server-protocol.h /usr/local/include/server-decoration-client-protocol.h /usr/local/include/swc-server-protocol.h /usr/local/include/swc-client-protocol.h /usr/local/include/swc_snap-server-protocol.h /usr/local/include/swc_snap-client-protocol.h /usr/local/include/swc_select-server-protocol.h /usr/local/include/swc_select-client-protocol.h /usr/local/include/swc_wallpaper-server-protocol.h /usr/local/include/swc_wallpaper-client-protocol.h /usr/local/include/wayland-drm-server-protocol.h /usr/local/include/wayland-drm-client-protocol.h /usr/local/include/xdg-decoration-unstable-v1-server-protocol.h /usr/local/include/xdg-decoration-unstable-v1-client-protocol.h /usr/local/include/xdg-shell-server-protocol.h /usr/local/include/xdg-shell-client-protocol.h");
    shell("sudo rm -f /usr/local/share/swc/swc.xml /usr/local/share/swc/swc_snap.xml /usr/local/share/swc/swc_wallpaper.xml");
    ctx.bin_path = "";
    ctx
}

fn cleanup(ctx, reason) {
    ctx
}
