let deps = ["rocky-hevel-bootstrap"];

let ctx = #{
    bin_path: "/usr/local/plan9/bin/9",
    name: "plan9port",
    repo_url: "https://github.com/9fans/plan9port.git",
    src_dir: "",
    version: "6654cad3c5c32ebca045a943957777b1b3167425",
};

fn is_installed(ctx) {
    let bin_path = "/usr/local/plan9/bin/9";
    if !is_file(bin_path) { throw "plan9port missing"; }
    if !is_file("/etc/profile.d/plan9port.sh") { throw "profile missing"; }
    ctx.bin_path = bin_path;
    ctx
}

fn is_acquired(ctx) {
    let src_dir = join_path(BUILD_DIR, "plan9port-6654cad3c5c32ebca045a943957777b1b3167425");
    if !is_dir(src_dir) { throw "source missing"; }
    ctx.src_dir = src_dir;
    ctx
}

fn acquire(ctx) {
    mkdir(BUILD_DIR);
    let src_dir = join_path(BUILD_DIR, "plan9port-6654cad3c5c32ebca045a943957777b1b3167425");
    shell_in(BUILD_DIR, "rm -rf \"" + src_dir + "\"");
    shell_in(BUILD_DIR, "git clone \"" + ctx.repo_url + "\" \"" + src_dir + "\"");
    shell_in(src_dir, "git checkout --detach \"" + ctx.version + "\"");
    ctx.src_dir = src_dir;
    ctx
}

fn is_built(ctx) {
    if !is_file(join_path(ctx.src_dir, "bin/9")) { throw "plan9port not built"; }
    if !is_file(join_path(ctx.src_dir, "bin/acme")) { throw "acme missing"; }
    ctx
}

fn build(ctx) {
    shell_in(ctx.src_dir, "./INSTALL -r /usr/local/plan9");
    ctx
}

fn install(ctx) {
    shell("sudo rm -rf /usr/local/plan9.new");
    shell("sudo cp -a --no-preserve=ownership \"" + ctx.src_dir + "\" /usr/local/plan9.new");
    shell("sudo rm -rf /usr/local/plan9");
    shell("sudo mv /usr/local/plan9.new /usr/local/plan9");
    shell("printf '%s\n' 'export PLAN9=/usr/local/plan9' 'export PATH=\"$PATH:$PLAN9/bin\"' | sudo tee /etc/profile.d/plan9port.sh >/dev/null");
    ctx.bin_path = "/usr/local/plan9/bin/9";
    ctx
}

fn remove(ctx) {
    shell("sudo rm -rf /usr/local/plan9 /etc/profile.d/plan9port.sh");
    ctx.bin_path = "";
    ctx
}

fn cleanup(ctx, reason) {
    ctx
}
