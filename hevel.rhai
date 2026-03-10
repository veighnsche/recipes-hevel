let deps = ["rocky-hevel-bootstrap", "neuswc", "libei-local"];

let ctx = #{
    bin_path: "/usr/local/bin/hevel",
    name: "hevel",
    overlay_dir: "/home/vince/.local/share/recipe/recipes/assets/hevel-m0",
    repo_url: "https://git.sr.ht/~dlm/hevel",
    src_dir: "/tmp/.tmpXxKU8I/hevel-f5f689cfe2d0f398ad227af1fba2d960cee000c6",
    version: "f5f689cfe2d0f398ad227af1fba2d960cee000c6",
};

fn apply_overlay(ctx) {
    let src_overlay = join_path(ctx.overlay_dir, "src");

    write_file(join_path(ctx.src_dir, "Makefile"), read_file(join_path(ctx.overlay_dir, "Makefile")));
    for file in ["hevel.c", "hevel.h", "eis.c", "inject.c", "portal.c", "inputcapture.c", "remotedesktop.c"] {
        write_file(join_path(join_path(ctx.src_dir, "src"), file), read_file(join_path(src_overlay, file)));
    }

    ctx
}

fn is_installed(ctx) {
    let bin_path = "/usr/local/bin/hevel";
    if !is_file(bin_path) { throw "hevel missing"; }
    ctx.bin_path = bin_path;
    ctx
}

fn is_acquired(ctx) {
    let src_dir = join_path(BUILD_DIR, "hevel-f5f689cfe2d0f398ad227af1fba2d960cee000c6");
    if !is_dir(src_dir) { throw "source missing"; }
    ctx.src_dir = src_dir;
    ctx
}

fn acquire(ctx) {
    mkdir(BUILD_DIR);
    let src_dir = join_path(BUILD_DIR, "hevel-f5f689cfe2d0f398ad227af1fba2d960cee000c6");
    shell_in(BUILD_DIR, "rm -rf \"" + src_dir + "\" .hevel-built");
    shell_in(BUILD_DIR, "git clone \"" + ctx.repo_url + "\" \"" + src_dir + "\"");
    shell_in(src_dir, "git checkout --detach \"" + ctx.version + "\"");
    ctx.src_dir = src_dir;
    shell_in(ctx.src_dir, "rm -f hevel hevel.o src/*.o");
    ctx
}

fn is_built(ctx) {
    if !is_file(join_path(BUILD_DIR, ".hevel-built")) { throw "hevel build stamp missing"; }
    let built = join_path(ctx.src_dir, "hevel");
    if !is_file(built) { throw "hevel binary missing"; }
    ctx
}

fn build(ctx) {
    let pkg_config_path = "/usr/local/lib64/pkgconfig:/usr/local/lib/pkgconfig:/usr/lib64/pkgconfig:/usr/share/pkgconfig";
    apply_overlay(ctx);
    shell_in(ctx.src_dir, "PKG_CONFIG_PATH=" + pkg_config_path + " make clean");
    shell_in(ctx.src_dir, "PKG_CONFIG_PATH=" + pkg_config_path + " make");
    write_file(join_path(BUILD_DIR, ".hevel-built"), "built\n");
    ctx
}

fn install(ctx) {
    shell("sudo install -D -m 755 \"" + join_path(ctx.src_dir, "hevel") + "\" /usr/local/bin/hevel");
    ctx.bin_path = "/usr/local/bin/hevel";
    ctx
}

fn remove(ctx) {
    shell("sudo rm -f /usr/local/bin/hevel");
    ctx.bin_path = "";
    ctx
}

fn cleanup(ctx, reason) {
    ctx
}
