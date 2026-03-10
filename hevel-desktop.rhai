let steps = [
    "rocky-hevel-bootstrap",
    "bmake",
    "neuwld",
    "neuswc",
    "libei-local",
    "hevel",
    "hevel-portal-scaffold",
    "hst",
    "plan9port",
    "go-mono-nerd-font",
    "hack",
    "greetd",
    "tuigreet",
    "hevel-login",
];

let remove_steps = [
    "hevel-login",
    "tuigreet",
    "greetd",
    "hack",
    "go-mono-nerd-font",
    "plan9port",
    "hst",
    "hevel-portal-scaffold",
    "hevel",
    "libei-local",
    "neuswc",
    "neuwld",
    "bmake",
    "rocky-hevel-bootstrap",
];

let ctx = #{
    marker: "/usr/local/share/recipe-state/hevel-desktop",
    name: "hevel-desktop",
};

fn is_installed(ctx) {
    if !is_file(ctx.marker) { throw "marker missing"; }
    for step in steps {
        shell("recipe -r \"" + RECIPE_DIR + "\" isinstalled " + step);
    }
    ctx
}

fn is_acquired(ctx) {
    ctx
}

fn acquire(ctx) {
    ctx
}

fn is_built(ctx) {
    ctx
}

fn install(ctx) {
    let marker_tmp = join_path(BUILD_DIR, "hevel-desktop.marker");
    for step in steps {
        shell("recipe -r \"" + RECIPE_DIR + "\" install " + step);
    }
    write_file(marker_tmp, "installed\n");
    shell("sudo install -D -m 644 \"" + marker_tmp + "\" \"" + ctx.marker + "\"");
    ctx
}

fn remove(ctx) {
    for step in remove_steps {
        shell("recipe -r \"" + RECIPE_DIR + "\" remove " + step);
    }
    shell("sudo rm -f \"" + ctx.marker + "\"");
    ctx
}

fn cleanup(ctx, reason) {
    ctx
}
