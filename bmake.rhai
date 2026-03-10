let deps = ["rocky-hevel-bootstrap"];

let ctx = #{
    archive: "",
    bin_path: "/usr/local/bin/bmake",
    name: "bmake",
    sha256: "d4e019e26c64cc8ffcf1cae9bb04fbb13da8fa6f41fb30fd26e221f655d4e84d",
    src_dir: "",
    stage_dir: "",
    url: "https://www.crufty.net/ftp/pub/sjg/bmake-20240909.tar.gz",
    version: "20240909",
};

fn is_installed(ctx) {
    let bin_path = "/usr/local/bin/bmake";
    let sys_path = "/usr/local/share/mk/sys.mk";
    if !is_file(bin_path) { throw "bmake missing"; }
    if !is_file(sys_path) { throw "mk files missing"; }
    ctx.bin_path = bin_path;
    ctx
}

fn is_acquired(ctx) {
    if ctx.archive == "" || !is_file(ctx.archive) { throw "archive missing"; }
    let src_dir = join_path(BUILD_DIR, "bmake");
    if !is_dir(src_dir) { throw "source missing"; }
    ctx.src_dir = src_dir;
    ctx
}

fn acquire(ctx) {
    mkdir(BUILD_DIR);
    let archive = download(ctx.url, join_path(BUILD_DIR, "bmake-20240909.tar.gz"));
    verify_sha256(archive, ctx.sha256);
    shell_in(BUILD_DIR, "rm -rf bmake stage");
    extract(archive, BUILD_DIR);
    ctx.archive = archive;
    ctx.src_dir = join_path(BUILD_DIR, "bmake");
    ctx
}

fn is_built(ctx) {
    let stage_dir = join_path(BUILD_DIR, "stage");
    let stage_bin = join_path(stage_dir, "usr/local/bin/bmake");
    if !is_file(stage_bin) { throw "staged bmake missing"; }
    ctx.stage_dir = stage_dir;
    ctx
}

fn build(ctx) {
    let stage_dir = join_path(BUILD_DIR, "stage");
    shell_in(BUILD_DIR, "rm -rf stage");
    shell_in(ctx.src_dir, "sh ./boot-strap --prefix=" + join_path(stage_dir, "usr/local") + " --with-default-sys-path=/usr/local/share/mk --install");
    ctx.stage_dir = stage_dir;
    ctx
}

fn install(ctx) {
    shell("sudo install -d /usr/local");
    shell("sudo cp -a --no-preserve=ownership \"" + join_path(ctx.stage_dir, "usr/local/.") + "\" /usr/local/");
    ctx.bin_path = "/usr/local/bin/bmake";
    ctx
}

fn remove(ctx) {
    shell("sudo rm -f /usr/local/bin/bmake /usr/local/share/man/man1/bmake.1");
    shell("sudo rm -rf /usr/local/share/mk");
    ctx.bin_path = "";
    ctx
}

fn cleanup(ctx, reason) {
    ctx
}
