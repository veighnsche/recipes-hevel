let deps = ["greetd", "tuigreet", "hevel"];

let ctx = #{
    marker: "/usr/local/share/recipe-state/hevel-login",
    name: "hevel-login",
};

fn is_installed(ctx) {
    let assets = join_path(RECIPE_DIR, "assets/hevel-login");
    if !is_file(ctx.marker) { throw "marker missing"; }
    if !is_file("/usr/local/bin/greetd") { throw "greetd missing"; }
    if !is_file("/usr/local/bin/tuigreet") { throw "tuigreet missing"; }
    if !is_file("/usr/local/bin/hevel") { throw "hevel missing"; }
    if shell_status("cmp -s \"" + join_path(assets, "config.toml") + "\" /etc/greetd/config.toml") != 0 {
        throw "greetd config drift";
    }
    if shell_status("cmp -s \"" + join_path(assets, "greetd.pam") + "\" /etc/pam.d/greetd") != 0 {
        throw "greetd PAM drift";
    }
    if shell_status("cmp -s \"" + join_path(assets, "greetd.service") + "\" /etc/systemd/system/greetd.service") != 0 {
        throw "greetd service drift";
    }
    if shell_status("cmp -s \"" + join_path(assets, "hevel-session") + "\" /usr/local/bin/hevel-session") != 0 {
        throw "hevel-session drift";
    }
    if trim(shell_output("systemctl is-enabled greetd 2>/dev/null || true")) != "enabled" {
        throw "greetd not enabled";
    }
    if trim(shell_output("systemctl get-default")) != "graphical.target" {
        throw "default target drift";
    }
    if shell_status("sudo semodule -l | grep -Fxq local-greetd") != 0 {
        throw "SELinux module missing";
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
    let assets = join_path(RECIPE_DIR, "assets/hevel-login");
    let stage_dir = join_path(BUILD_DIR, "hevel-login");
    if !is_file("/usr/local/bin/greetd") { throw "install greetd recipe first"; }
    if !is_file("/usr/local/bin/tuigreet") { throw "install tuigreet recipe first"; }
    if !is_file("/usr/local/bin/hevel") { throw "install hevel recipe first"; }

    mkdir(stage_dir);
    write_file(join_path(stage_dir, "config.toml"), read_file(join_path(assets, "config.toml")));
    write_file(join_path(stage_dir, "greetd.pam"), read_file(join_path(assets, "greetd.pam")));
    write_file(join_path(stage_dir, "greetd.service"), read_file(join_path(assets, "greetd.service")));
    write_file(join_path(stage_dir, "hevel-session"), read_file(join_path(assets, "hevel-session")));
    write_file(join_path(stage_dir, "local-greetd.pp.b64"), trim(read_file(join_path(assets, "local-greetd.pp.b64"))));
    shell("base64 -d < \"" + join_path(stage_dir, "local-greetd.pp.b64") + "\" > \"" + join_path(stage_dir, "local-greetd.pp") + "\"");

    shell("sudo install -D -m 644 \"" + join_path(stage_dir, "config.toml") + "\" /etc/greetd/config.toml");
    shell("sudo install -D -m 644 \"" + join_path(stage_dir, "greetd.pam") + "\" /etc/pam.d/greetd");
    shell("sudo install -D -m 644 \"" + join_path(stage_dir, "greetd.service") + "\" /etc/systemd/system/greetd.service");
    shell("sudo install -D -m 755 \"" + join_path(stage_dir, "hevel-session") + "\" /usr/local/bin/hevel-session");
    shell("sudo semodule -i \"" + join_path(stage_dir, "local-greetd.pp") + "\"");

    write_file(join_path(stage_dir, "hevel-login.marker"), "installed\n");
    shell("sudo install -D -m 644 \"" + join_path(stage_dir, "hevel-login.marker") + "\" \"" + ctx.marker + "\"");
    shell("sudo systemctl daemon-reload");
    shell("sudo systemctl enable greetd.service");
    shell("sudo systemctl set-default graphical.target");
    ctx
}

fn remove(ctx) {
    shell("sudo systemctl disable greetd.service || true");
    shell("sudo systemctl set-default multi-user.target || true");
    shell("sudo semodule -r local-greetd || true");
    shell("sudo rm -f /etc/greetd/config.toml /etc/pam.d/greetd /etc/systemd/system/greetd.service /usr/local/bin/hevel-session \"" + ctx.marker + "\"");
    shell("sudo systemctl daemon-reload");
    ctx
}

fn cleanup(ctx, reason) {
    ctx
}
