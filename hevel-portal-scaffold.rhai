let deps = ["rocky-hevel-bootstrap", "hevel"];

let ctx = #{
    config_path: "/usr/share/xdg-desktop-portal/hevel-portals.conf",
    legacy_config_path: "/usr/local/share/xdg-desktop-portal/hevel-portals.conf",
    legacy_portal_path: "/usr/local/share/xdg-desktop-portal/portals/hevel.portal",
    legacy_service_path: "/usr/local/share/dbus-1/services/org.freedesktop.impl.portal.desktop.hevel.service",
    legacy_stage_root: "/usr/local/share/hevel-portal-staging",
    marker: "/usr/local/share/recipe-state/hevel-portal-scaffold",
    name: "hevel-portal-scaffold",
    portal_path: "/usr/share/xdg-desktop-portal/portals/hevel.portal",
    service_path: "/usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.hevel.service",
};

fn is_installed(ctx) {
    let assets = join_path(RECIPE_DIR, "assets/hevel-portal");
    if !is_file(ctx.marker) { throw "marker missing"; }
    if shell_status("cmp -s \"" + join_path(assets, "hevel.portal") + "\" \"" + ctx.portal_path + "\"") != 0 {
        throw "portal file drift";
    }
    if shell_status("cmp -s \"" + join_path(assets, "org.freedesktop.impl.portal.desktop.hevel.service") + "\" \"" + ctx.service_path + "\"") != 0 {
        throw "service file drift";
    }
    if shell_status("cmp -s \"" + join_path(assets, "hevel-portals.conf") + "\" \"" + ctx.config_path + "\"") != 0 {
        throw "portals.conf drift";
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
    let assets = join_path(RECIPE_DIR, "assets/hevel-portal");
    let stage_dir = join_path(BUILD_DIR, "hevel-portal-scaffold");

    mkdir(BUILD_DIR);
    mkdir(stage_dir);
    write_file(join_path(stage_dir, "hevel.portal"), read_file(join_path(assets, "hevel.portal")));
    write_file(
        join_path(stage_dir, "org.freedesktop.impl.portal.desktop.hevel.service"),
        read_file(join_path(assets, "org.freedesktop.impl.portal.desktop.hevel.service"))
    );
    write_file(join_path(stage_dir, "hevel-portals.conf"), read_file(join_path(assets, "hevel-portals.conf")));
    write_file(join_path(stage_dir, "hevel-portal-scaffold.marker"), "installed\n");

    shell("sudo rm -rf \"" + ctx.legacy_stage_root + "\"");
    shell("sudo rm -f \"" + ctx.legacy_portal_path + "\" \"" + ctx.legacy_service_path + "\" \"" + ctx.legacy_config_path + "\"");
    shell("sudo install -D -m 644 \"" + join_path(stage_dir, "hevel.portal") + "\" \"" + ctx.portal_path + "\"");
    shell("sudo install -D -m 644 \"" + join_path(stage_dir, "org.freedesktop.impl.portal.desktop.hevel.service") + "\" \"" + ctx.service_path + "\"");
    shell("sudo install -D -m 644 \"" + join_path(stage_dir, "hevel-portals.conf") + "\" \"" + ctx.config_path + "\"");
    shell("sudo install -D -m 644 \"" + join_path(stage_dir, "hevel-portal-scaffold.marker") + "\" \"" + ctx.marker + "\"");
    ctx
}

fn remove(ctx) {
    shell("sudo rm -rf \"" + ctx.legacy_stage_root + "\"");
    shell("sudo rm -f \"" + ctx.portal_path + "\" \"" + ctx.service_path + "\" \"" + ctx.config_path + "\" \"" + ctx.legacy_portal_path + "\" \"" + ctx.legacy_service_path + "\" \"" + ctx.legacy_config_path + "\" \"" + ctx.marker + "\"");
    ctx
}

fn cleanup(ctx, reason) {
    ctx
}
