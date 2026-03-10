let deps = ["rocky-hevel-bootstrap", "bmake"];

let ctx = #{
    lib_path: "",
    name: "neuwld",
    repo_url: "https://git.sr.ht/~shrub900/neuwld",
    src_dir: "",
    stage_dir: "",
    version: "235b7b62be7d7c9eefa011eac4a5b78ba7390f1c",
};

fn is_installed(ctx) {
    let lib_path = "/usr/local/lib/libwld.a";
    if !is_file(lib_path) { throw "libwld.a missing"; }
    if !is_file("/usr/local/include/wld/wld.h") { throw "wld headers missing"; }
    if !is_file("/usr/local/lib/pkgconfig/wld.pc") { throw "wld pc file missing"; }
    ctx.lib_path = lib_path;
    ctx
}

fn is_acquired(ctx) {
    let src_dir = join_path(BUILD_DIR, "neuwld-235b7b62be7d7c9eefa011eac4a5b78ba7390f1c");
    if !is_dir(src_dir) { throw "source missing"; }
    ctx.src_dir = src_dir;
    ctx
}

fn acquire(ctx) {
    mkdir(BUILD_DIR);
    let src_dir = join_path(BUILD_DIR, "neuwld-235b7b62be7d7c9eefa011eac4a5b78ba7390f1c");
    shell_in(BUILD_DIR, "rm -rf \"" + src_dir + "\" stage");
    shell_in(BUILD_DIR, "git clone \"" + ctx.repo_url + "\" \"" + src_dir + "\"");
    shell_in(src_dir, "git checkout --detach \"" + ctx.version + "\"");
    ctx.src_dir = src_dir;
    ctx
}

fn is_built(ctx) {
    let stage_dir = join_path(BUILD_DIR, "stage");
    if !is_file(join_path(stage_dir, "usr/local/lib/libwld.a")) { throw "staged libwld.a missing"; }
    ctx.stage_dir = stage_dir;
    ctx
}

fn build(ctx) {
    let stage_dir = join_path(BUILD_DIR, "stage");
    shell_in(BUILD_DIR, "rm -rf stage");
    shell_in(
        ctx.src_dir,
        "PKG_CONFIG_PATH=/usr/local/lib/pkgconfig /usr/local/bin/bmake PREFIX=/usr/local LIBDIR=/usr/local/lib INCLUDEDIR=/usr/local/include PKGCONFIGDIR=/usr/local/lib/pkgconfig ENABLE_DEBUG=0 ENABLE_STATIC=1 ENABLE_SHARED=0 ENABLE_PIXMAN=1 ENABLE_DRM=1 DRM_DRIVERS= ENABLE_WAYLAND=1 WAYLAND_INTERFACES=\"shm drm\" CPPFLAGS=\"$(pkg-config --cflags fontconfig pixman-1 freetype2 libdrm wayland-client) -D_POSIX_C_SOURCE=200809L -DNDEBUG -DWITH_WAYLAND_SHM=1 -DWITH_WAYLAND_DRM=1\" CFLAGS=\"-pipe -fvisibility=hidden -std=c11 -Wvla -Wall -Werror=implicit-function-declaration -Werror=implicit-int -Werror=pointer-sign -Werror=pointer-arith\" WLD_PKG_LIBS=\"$(pkg-config --libs fontconfig pixman-1 freetype2 libdrm wayland-client)\" DESTDIR=" + stage_dir + " install"
    );
    ctx.stage_dir = stage_dir;
    ctx
}

fn install(ctx) {
    shell("sudo install -d /usr/local");
    shell("sudo cp -a --no-preserve=ownership \"" + join_path(ctx.stage_dir, "usr/local/.") + "\" /usr/local/");
    ctx.lib_path = "/usr/local/lib/libwld.a";
    ctx
}

fn remove(ctx) {
    shell("sudo rm -f /usr/local/lib/libwld.a /usr/local/lib/pkgconfig/wld.pc");
    shell("sudo rm -rf /usr/local/include/wld");
    ctx.lib_path = "";
    ctx
}

fn cleanup(ctx, reason) {
    ctx
}
