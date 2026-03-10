let deps = ["rocky-hevel-bootstrap"];

let ctx = #{
    bin_path: "/usr/local/bin/greetd",
    marker: "/usr/local/share/recipe-state/greetd",
    name: "greetd",
    repo_url: "https://github.com/kennylevinsen/greetd.git",
    src_dir: "/tmp/.tmpEO5MCT/greetd-0.10.0",
    version: "0.10.0",
};

fn is_installed(ctx) {
    if !is_file(ctx.marker) { throw "marker missing"; }
    if !is_file(ctx.bin_path) { throw "greetd missing"; }
    if shell_status("getent passwd greeter >/dev/null 2>&1") != 0 {
        throw "greeter user missing";
    }
    ctx
}

fn is_acquired(ctx) {
    let src_dir = join_path(BUILD_DIR, "greetd-0.10.0");
    if !is_dir(src_dir) { throw "source missing"; }
    ctx.src_dir = src_dir;
    ctx
}

fn acquire(ctx) {
    mkdir(BUILD_DIR);
    let src_dir = join_path(BUILD_DIR, "greetd-0.10.0");
    shell_in(BUILD_DIR, "rm -rf \"" + src_dir + "\" stage");
    shell_in(BUILD_DIR, "git clone \"" + ctx.repo_url + "\" \"" + src_dir + "\"");
    shell_in(src_dir, "git checkout --detach \"" + ctx.version + "\"");
    ctx.src_dir = src_dir;
    ctx
}

fn is_built(ctx) {
    if !is_file(join_path(ctx.src_dir, "target/release/greetd")) {
        throw "built greetd missing";
    }
    ctx
}

fn build(ctx) {
    shell_in(ctx.src_dir, "cargo build --locked --release -p greetd");
    ctx
}

fn install(ctx) {
    let marker_tmp = join_path(BUILD_DIR, "greetd.marker");
    shell("sudo getent group greeter >/dev/null 2>&1 || sudo groupadd --system greeter");
    shell("sudo getent passwd greeter >/dev/null 2>&1 || sudo useradd --system --gid greeter --home-dir /var/lib/greetd --create-home --shell /usr/sbin/nologin greeter");
    shell("sudo install -D -m 755 \"" + join_path(ctx.src_dir, "target/release/greetd") + "\" \"" + ctx.bin_path + "\"");
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
