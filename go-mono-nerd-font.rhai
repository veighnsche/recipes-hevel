let deps = ["rocky-hevel-bootstrap"];

let ctx = #{
    archive: "/tmp/.tmp1YhDGv/Go-Mono.tar.xz",
    font_dir: "/usr/share/fonts/TTF",
    font_path: "/usr/share/fonts/TTF/GoMonoNerdFont-Regular.ttf",
    marker: "/usr/local/share/recipe-state/go-mono-nerd-font",
    name: "go-mono-nerd-font",
    sha256: "81c879e6a86dd6e6cb94d7a73322bf39f06b3294a727effb5a5bb5c7f9274fd6",
    src_dir: "/tmp/.tmp1YhDGv/Go-Mono",
    url: "https://github.com/ryanoasis/nerd-fonts/releases/download/v3.4.0/Go-Mono.tar.xz",
    version: "v3.4.0",
};

fn is_installed(ctx) {
    if !is_file(ctx.marker) { throw "marker missing"; }
    if !is_file(ctx.font_path) { throw "GoMono Nerd Font missing"; }
    ctx
}

fn is_acquired(ctx) {
    if ctx.archive == "" || !is_file(ctx.archive) { throw "archive missing"; }
    if ctx.src_dir == "" || !is_dir(ctx.src_dir) { throw "source missing"; }
    if !is_file(join_path(ctx.src_dir, "GoMonoNerdFont-Regular.ttf")) { throw "font archive not extracted"; }
    ctx
}

fn acquire(ctx) {
    mkdir(BUILD_DIR);
    let archive = download(ctx.url, join_path(BUILD_DIR, "Go-Mono.tar.xz"));
    verify_sha256(archive, ctx.sha256);
    let src_dir = join_path(BUILD_DIR, "Go-Mono");
    shell_in(BUILD_DIR, "rm -rf \"" + src_dir + "\" && mkdir -p \"" + src_dir + "\"");
    shell_in(BUILD_DIR, "tar -xf \"" + archive + "\" -C \"" + src_dir + "\"");
    ctx.archive = archive;
    ctx.src_dir = src_dir;
    ctx
}

fn is_built(ctx) {
    if !is_file(join_path(ctx.src_dir, "GoMonoNerdFont-Regular.ttf")) { throw "font files missing"; }
    ctx
}

fn build(ctx) {
    ctx
}

fn install(ctx) {
    let marker_tmp = join_path(BUILD_DIR, "go-mono-nerd-font.marker");
    shell("sudo install -d \"" + ctx.font_dir + "\"");
    shell("sudo install -m 644 \"" + join_path(ctx.src_dir, "GoMonoNerdFont-Regular.ttf") + "\" \"" + ctx.font_dir + "/GoMonoNerdFont-Regular.ttf\"");
    shell("sudo install -m 644 \"" + join_path(ctx.src_dir, "GoMonoNerdFont-Bold.ttf") + "\" \"" + ctx.font_dir + "/GoMonoNerdFont-Bold.ttf\"");
    shell("sudo install -m 644 \"" + join_path(ctx.src_dir, "GoMonoNerdFont-Italic.ttf") + "\" \"" + ctx.font_dir + "/GoMonoNerdFont-Italic.ttf\"");
    shell("sudo install -m 644 \"" + join_path(ctx.src_dir, "GoMonoNerdFont-BoldItalic.ttf") + "\" \"" + ctx.font_dir + "/GoMonoNerdFont-BoldItalic.ttf\"");
    shell("sudo fc-cache -f \"" + ctx.font_dir + "\"");
    write_file(marker_tmp, "installed\n");
    shell("sudo install -D -m 644 \"" + marker_tmp + "\" \"" + ctx.marker + "\"");
    ctx
}

fn remove(ctx) {
    shell("sudo rm -f \"" + ctx.font_dir + "/GoMonoNerdFont-Regular.ttf\" \"" + ctx.font_dir + "/GoMonoNerdFont-Bold.ttf\" \"" + ctx.font_dir + "/GoMonoNerdFont-Italic.ttf\" \"" + ctx.font_dir + "/GoMonoNerdFont-BoldItalic.ttf\" \"" + ctx.marker + "\"");
    shell("sudo fc-cache -f \"" + ctx.font_dir + "\"");
    ctx
}

fn cleanup(ctx, reason) {
    ctx
}
