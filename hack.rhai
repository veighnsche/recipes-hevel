let deps = ["rocky-hevel-bootstrap", "plan9port", "neuwld", "go-mono-nerd-font"];

let ctx = #{
    bin_path: "/usr/local/bin/hack",
    name: "hack",
    repo_url: "https://git.sr.ht/~shrub900/hack",
    src_dir: "/tmp/.tmpc90e28/hack-346ada49eaa2fb47d81822c567fc5f0876163f80",
    version: "346ada49eaa2fb47d81822c567fc5f0876163f80",
};

fn is_installed(ctx) {
    let bin_path = "/usr/local/bin/hack";
    if !is_file(bin_path) { throw "hack wrapper missing"; }
    if !is_file("/usr/local/libexec/hack.bin") { throw "hack binary missing"; }
    ctx.bin_path = bin_path;
    ctx
}

fn is_acquired(ctx) {
    let src_dir = join_path(BUILD_DIR, "hack-346ada49eaa2fb47d81822c567fc5f0876163f80");
    if !is_dir(src_dir) { throw "source missing"; }
    ctx.src_dir = src_dir;
    ctx
}

fn acquire(ctx) {
    mkdir(BUILD_DIR);
    let src_dir = join_path(BUILD_DIR, "hack-346ada49eaa2fb47d81822c567fc5f0876163f80");
    shell_in(BUILD_DIR, "rm -rf \"" + src_dir + "\" .hack-built");
    shell_in(BUILD_DIR, "git clone \"" + ctx.repo_url + "\" \"" + src_dir + "\"");
    shell_in(src_dir, "git checkout --detach \"" + ctx.version + "\"");
    ctx.src_dir = src_dir;
    shell_in(ctx.src_dir, "rm -f hack acme/mail/Mail test-shim");
    shell_in(ctx.src_dir, "find . -name '*.o' -delete");
    ctx
}

fn is_built(ctx) {
    if !is_file(join_path(BUILD_DIR, ".hack-built")) { throw "hack build stamp missing"; }
    if !is_file(join_path(ctx.src_dir, "hack")) { throw "hack binary missing"; }
    ctx
}

fn build(ctx) {
    shell_in(ctx.src_dir, "PATH=/usr/local/plan9/bin:$PATH PLAN9=/usr/local/plan9 PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/lib64/pkgconfig:/usr/share/pkgconfig make CC=cc clean");
    shell_in(ctx.src_dir, "PATH=/usr/local/plan9/bin:$PATH PLAN9=/usr/local/plan9 PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/lib64/pkgconfig:/usr/share/pkgconfig make CC=cc");
    write_file(join_path(BUILD_DIR, ".hack-built"), "built\n");
    ctx
}

fn install(ctx) {
    shell("sudo install -d /usr/local/libexec /usr/local/bin");
    shell("sudo install -m 755 \"" + join_path(ctx.src_dir, "hack") + "\" /usr/local/libexec/hack.bin");
    write_file(join_path(BUILD_DIR, "hack-wrapper"), "#!/bin/sh\nexport PLAN9=/usr/local/plan9\nexport PATH=\"$PATH:$PLAN9/bin\"\nexec /usr/local/libexec/hack.bin \"$@\"\n");
    shell("sudo install -m 755 \"" + join_path(BUILD_DIR, "hack-wrapper") + "\" /usr/local/bin/hack");
    ctx.bin_path = "/usr/local/bin/hack";
    ctx
}

fn remove(ctx) {
    shell("sudo rm -f /usr/local/bin/hack /usr/local/libexec/hack.bin");
    ctx.bin_path = "";
    ctx
}

fn cleanup(ctx, reason) {
    ctx
}
