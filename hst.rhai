let deps = ["rocky-hevel-bootstrap", "neuwld"];

let ctx = #{
    bin_path: "/usr/local/bin/st-wl",
    name: "hst",
    repo_url: "https://git.sr.ht/~dlm/hst",
    src_dir: "/tmp/.tmpc1Nnqi/hst-6187ef823d1fabe2139aed54dbb7a7e28c6d8ff4",
    version: "6187ef823d1fabe2139aed54dbb7a7e28c6d8ff4",
};

fn is_installed(ctx) {
    let bin_path = "/usr/local/bin/st-wl";
    if !is_file(bin_path) { throw "st-wl missing"; }
    if !exists("/usr/local/bin/hst") { throw "hst symlink missing"; }
    ctx.bin_path = bin_path;
    ctx
}

fn is_acquired(ctx) {
    let src_dir = join_path(BUILD_DIR, "hst-6187ef823d1fabe2139aed54dbb7a7e28c6d8ff4");
    if !is_dir(src_dir) { throw "source missing"; }
    ctx.src_dir = src_dir;
    ctx
}

fn acquire(ctx) {
    mkdir(BUILD_DIR);
    let src_dir = join_path(BUILD_DIR, "hst-6187ef823d1fabe2139aed54dbb7a7e28c6d8ff4");
    shell_in(BUILD_DIR, "rm -rf \"" + src_dir + "\"");
    shell_in(BUILD_DIR, "git clone \"" + ctx.repo_url + "\" \"" + src_dir + "\"");
    shell_in(src_dir, "git checkout --detach \"" + ctx.version + "\"");
    ctx.src_dir = src_dir;
    ctx
}

fn is_built(ctx) {
    if !is_file(join_path(ctx.src_dir, "st-wl")) { throw "st-wl binary missing"; }
    ctx
}

fn build(ctx) {
    shell_in(ctx.src_dir, "PKG_CONFIG_PATH=/usr/local/lib/pkgconfig make clean");
    shell_in(ctx.src_dir, "PKG_CONFIG_PATH=/usr/local/lib/pkgconfig make");
    ctx
}

fn install(ctx) {
    shell("sudo install -D -m 755 \"" + join_path(ctx.src_dir, "st-wl") + "\" /usr/local/bin/st-wl");
    shell("sudo ln -sf /usr/local/bin/st-wl /usr/local/bin/hst");
    shell_in(ctx.src_dir, "sed \"s/VERSION/0.8.2/g\" < st-wl.1 > " + join_path(BUILD_DIR, "st-wl.1"));
    shell("sudo install -D -m 644 \"" + join_path(BUILD_DIR, "st-wl.1") + "\" /usr/local/share/man/man1/st-wl.1");
    shell_in(ctx.src_dir, "sudo tic -sx st-wl.info");
    ctx.bin_path = "/usr/local/bin/st-wl";
    ctx
}

fn remove(ctx) {
    shell("sudo rm -f /usr/local/bin/st-wl /usr/local/bin/hst /usr/local/share/man/man1/st-wl.1");
    ctx.bin_path = "";
    ctx
}

fn cleanup(ctx, reason) {
    ctx
}
