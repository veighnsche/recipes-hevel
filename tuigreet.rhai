let deps = ["rocky-hevel-bootstrap"];

let ctx = #{
    bin_path: "/usr/local/bin/tuigreet",
    marker: "/usr/local/share/recipe-state/tuigreet",
    name: "tuigreet",
    repo_url: "https://github.com/apognu/tuigreet.git",
    src_dir: "/tmp/.tmplEQFYv/tuigreet-0.9.1",
    version: "0.9.1",
};

fn is_installed(ctx) {
    if !is_file(ctx.marker) { throw "marker missing"; }
    if !is_file(ctx.bin_path) { throw "tuigreet missing"; }
    if shell_status("strings \"" + ctx.bin_path + "\" | grep -Fq 'tuigreet 0.9.1'") != 0 {
        throw "unexpected tuigreet version";
    }
    ctx
}

fn is_acquired(ctx) {
    let src_dir = join_path(BUILD_DIR, "tuigreet-0.9.1");
    if !is_dir(src_dir) { throw "source missing"; }
    ctx.src_dir = src_dir;
    ctx
}

fn acquire(ctx) {
    mkdir(BUILD_DIR);
    let src_dir = join_path(BUILD_DIR, "tuigreet-0.9.1");
    shell_in(BUILD_DIR, "rm -rf \"" + src_dir + "\"");
    shell_in(BUILD_DIR, "git clone \"" + ctx.repo_url + "\" \"" + src_dir + "\"");
    shell_in(src_dir, "git checkout --detach \"" + ctx.version + "\"");
    ctx.src_dir = src_dir;
    ctx
}

fn is_built(ctx) {
    if !is_file(join_path(ctx.src_dir, "target/release/tuigreet")) {
        throw "built tuigreet missing";
    }
    ctx
}

fn build(ctx) {
    shell_in(ctx.src_dir, "cargo build --locked --release");
    ctx
}

fn install(ctx) {
    let marker_tmp = join_path(BUILD_DIR, "tuigreet.marker");
    shell("sudo install -D -m 755 \"" + join_path(ctx.src_dir, "target/release/tuigreet") + "\" \"" + ctx.bin_path + "\"");
    write_file(marker_tmp, "installed\n");
    shell("sudo install -D -m 644 \"" + marker_tmp + "\" \"" + ctx.marker + "\"");
    ctx
}

fn remove(ctx) {
    shell("sudo rm -f \"" + ctx.bin_path + "\" \"" + ctx.marker + "\"");
    ctx
}

fn cleanup(ctx, reason) {
    ctx
}
