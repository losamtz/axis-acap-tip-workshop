"""Compile the actual callback/configuration code without camera-only libraries.

Requires a host C compiler, pkg-config, and GLib development files.
Override CC and CFLAGS if your host needs an explicit compiler or SDK path.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

app = Path(__file__).resolve().parents[1] / 'app/parameter_loading_bay.c'
source = app.read_text()
# Select the unmodified configuration and callback definitions; the remaining
# source requires AXParameter, FastCGI, and a running camera.
source = source[source.index('#define APP_NAME'):source.index('/* Occupancy is an input')]
harness = r'''
#include <glib.h>
#include <string.h>
#include <syslog.h>
'''
tests = r'''
int main(void) {
    const char* groups[] = {"root.Parameter_loading_bay.",
                            "root.parameter_loading_bay.", ""};
    for (guint i = 0; i < G_N_ELEMENTS(groups); ++i) {
        Monitor monitor = {0};
        gchar* enabled = g_strconcat(groups[i], "Enabled", NULL);
        gchar* limit = g_strconcat(groups[i], "MaxOccupancySeconds", NULL);
        gchar* cooldown = g_strconcat(groups[i], "AlertCooldownSeconds", NULL);
        parameter_changed(enabled, "yes", &monitor);
        parameter_changed(limit, "10", &monitor);
        parameter_changed(cooldown, "5", &monitor);
        g_assert_true(monitor.enabled);
        g_assert_cmpuint(monitor.max_occupancy_seconds, ==, 10);
        g_assert_cmpuint(monitor.alert_cooldown_seconds, ==, 5);
        parameter_changed(enabled, "no", &monitor);
        g_assert_false(monitor.enabled);
        parameter_changed(limit, "0", &monitor);
        parameter_changed(cooldown, "invalid", &monitor);
        g_assert_cmpuint(monitor.max_occupancy_seconds, ==, 10);
        g_assert_cmpuint(monitor.alert_cooldown_seconds, ==, 5);
        g_free(enabled); g_free(limit); g_free(cooldown);
    }
    g_print("PASS: capitalized, lowercase, and local callback names; invalid values retained\n");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='bay-callbacks-') as directory:
    directory = Path(directory)
    test_source = directory / 'callbacks.c'
    binary = directory / 'callbacks'
    test_source.write_text(harness + source + tests)
    flags = shlex.split(subprocess.check_output(
        ['pkg-config', '--cflags', '--libs', 'glib-2.0'], text=True))
    subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + ['-std=c11'] +
                   shlex.split(os.environ.get('CFLAGS', '')) +
                   [str(test_source), '-o', str(binary)] + flags, check=True)
    subprocess.run([str(binary)], check=True)
