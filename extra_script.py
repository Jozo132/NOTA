# extra_script.py — PlatformIO build integration for NOTA OTA library
# Registers custom upload targets:
#   pio run -t nota        Upload firmware via CLI (Node.js)
#   pio run -t nota-gui    Open OTA upload GUI (Python/Tkinter)
#
# Configuration in platformio.ini:
#   custom_nota_ip    = 192.168.1.100       ; Target device IP (required for CLI)
#   custom_nota_port  = 8266                ; Target device port (default: 8266)
#   custom_nota_auth  = your_password       ; OTA password (optional)
#   custom_nota_name  = my-device           ; Expected device name (optional)
#   custom_nota_board = XTP14A6E            ; Expected board ID (optional)
#   custom_nota_force = false               ; Skip safety checks (optional)

Import("env")

import os
import sys
import subprocess

# ── Library paths ─────────────────────────────────────────────────────────────
lib_dir = Dir('.').abspath
tools_dir = os.path.join(lib_dir, "tools")
nota_js = os.path.join(tools_dir, "nota.js")
nota_gui_py = os.path.join(tools_dir, "nota-gui.py")

# ── Project environment (needed for upload targets) ──────────────────────────
project_env = DefaultEnvironment()

# Prevent double-registration when multiple library envs process this script
if getattr(project_env, "_nota_targets_registered", False):
    Return()
project_env._nota_targets_registered = True


# ── Configuration helpers ─────────────────────────────────────────────────────

def _opt(key, default=""):
    """Read custom_nota_<key> from platformio.ini"""
    try:
        return project_env.GetProjectOption("custom_nota_%s" % key, default).strip()
    except Exception:
        return default


def _firmware_bin():
    """Resolve path to compiled firmware .bin"""
    return project_env.subst("$BUILD_DIR/${PROGNAME}.bin")


def _project_name():
    """Get device/project name for NOTA identification"""
    name = _opt("name")
    if not name:
        name = os.path.basename(project_env.subst("$PROJECT_DIR"))
    return name


def _board_id():
    """Get board identifier"""
    board = _opt("board")
    if not board:
        board = project_env.get("BOARD", "")
    return board


def _check_node():
    """Verify Node.js is available"""
    try:
        subprocess.check_output(["node", "--version"], stderr=subprocess.STDOUT)
        return True
    except Exception:
        return False


# ── Upload via nota.js (CLI) ─────────────────────────────────────────────────

def _nota_upload(target, source, env):
    """Upload firmware using nota.js via Node.js"""
    firmware = _firmware_bin()
    ip = _opt("ip")
    port = _opt("port", "8266")
    auth = _opt("auth")
    force = _opt("force", "false").lower() in ("true", "1", "yes")
    name = _project_name()
    board = _board_id()

    if not ip:
        sys.stderr.write(
            "\n"
            "  +----------------------------------------------------------+\n"
            "  |  NOTA: Missing upload configuration in platformio.ini    |\n"
            "  +----------------------------------------------------------+\n"
            "  |                                                          |\n"
            "  |  Add these options to your [env:...] section:            |\n"
            "  |                                                          |\n"
            "  |    custom_nota_ip   = 192.168.1.100                      |\n"
            "  |    custom_nota_port = 8266          ; default: 8266      |\n"
            "  |    custom_nota_auth = your_password ; optional           |\n"
            "  |                                                          |\n"
            "  +----------------------------------------------------------+\n"
            "\n"
        )
        env.Exit(1)

    if not os.path.isfile(firmware):
        sys.stderr.write("NOTA Error: Firmware binary not found: %s\n" % firmware)
        sys.stderr.write("  Build first with 'pio run' or use the dependency in AddCustomTarget.\n")
        env.Exit(1)

    if not _check_node():
        sys.stderr.write(
            "\n"
            "  NOTA Error: Node.js is required but not found in PATH.\n"
            "  Install from https://nodejs.org/ and restart your terminal.\n"
            "\n"
        )
        env.Exit(1)

    cmd = ["node", nota_js, "-f", firmware, "-i", ip, "-p", port]
    if name:  cmd += ["-n", name]
    if board: cmd += ["-b", board]
    if auth:  cmd += ["-a", auth]
    if force: cmd.append("--force")

    print("NOTA >> Uploading to %s:%s ..." % (ip, port))
    ret = subprocess.call(cmd)
    if ret != 0:
        sys.stderr.write("NOTA Error: Upload failed (exit code %d)\n" % ret)
        env.Exit(1)


# ── Upload via nota-gui.py (GUI) ─────────────────────────────────────────────

def _nota_gui_upload(target, source, env):
    """Launch NOTA GUI for firmware upload with retained settings"""
    firmware = _firmware_bin()
    name = _project_name()
    board = _board_id()

    if not os.path.isfile(firmware):
        sys.stderr.write("NOTA Error: Firmware binary not found: %s\n" % firmware)
        sys.stderr.write("  Build first with 'pio run' or use the dependency in AddCustomTarget.\n")
        env.Exit(1)

    # Find a Python interpreter with tkinter support
    python_exe = _find_gui_python()

    cmd = [python_exe, nota_gui_py, "-f", firmware]
    if name:  cmd += ["-n", name]
    if board: cmd += ["-b", board]

    print("NOTA >> Launching OTA GUI...")
    ret = subprocess.call(cmd)
    if ret != 0:
        sys.stderr.write("NOTA Error: GUI exited with error (exit code %d)\n" % ret)
        env.Exit(1)


def _find_gui_python():
    """Find a Python interpreter with tkinter available"""
    # Try PlatformIO's Python first
    if _has_tkinter(sys.executable):
        return sys.executable
    # Try common system Python names
    for name in ["python3", "python", "py"]:
        try:
            ret = subprocess.call(
                [name, "-c", "import tkinter"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )
            if ret == 0:
                return name
        except Exception:
            pass
    # Fallback — will fail with a clear import error if tkinter is missing
    return sys.executable


def _has_tkinter(python_path):
    """Check if a Python interpreter has tkinter"""
    try:
        ret = subprocess.call(
            [python_path, "-c", "import tkinter"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        return ret == 0
    except Exception:
        return False


# ── Register custom PlatformIO targets ────────────────────────────────────────
#
# Usage from CLI:
#   pio run -t nota        Build firmware, then upload via OTA (CLI)
#   pio run -t nota-gui    Build firmware, then open upload GUI
#
# Usage from PlatformIO IDE (VS Code):
#   These appear as tasks in the Project Tasks sidebar under "Custom"
# ──────────────────────────────────────────────────────────────────────────────

firmware_dep = project_env.subst("$BUILD_DIR/${PROGNAME}.bin")

project_env.AddCustomTarget(
    name="nota",
    dependencies=[firmware_dep],
    actions=[_nota_upload],
    title="NOTA Upload",
    description="Build and upload firmware via NOTA OTA"
)

project_env.AddCustomTarget(
    name="nota-gui",
    dependencies=[firmware_dep],
    actions=[_nota_gui_upload],
    title="NOTA GUI",
    description="Build firmware and open NOTA OTA upload GUI"
)


# ── Optional: Override default 'upload' target when protocol is set ───────────
# If the user sets `upload_protocol = custom` and has `custom_nota_ip` configured,
# the standard Upload button / `pio run -t upload` will use NOTA automatically.

upload_protocol = project_env.GetProjectOption("upload_protocol", "")

if upload_protocol == "custom" and _opt("ip"):
    # Build the nota.js command as a string for PlatformIO's custom upload protocol
    ip = _opt("ip")
    port = _opt("port", "8266")
    auth = _opt("auth")
    force = _opt("force", "false").lower() in ("true", "1", "yes")
    name = _project_name()
    board = _board_id()

    cmd_parts = ['"node"', '"%s"' % nota_js, '-f', '"$SOURCE"', '-i', ip, '-p', port]
    if name:  cmd_parts += ['-n', '"%s"' % name]
    if board: cmd_parts += ['-b', '"%s"' % board]
    if auth:  cmd_parts += ['-a', '"%s"' % auth]
    if force: cmd_parts.append('--force')

    project_env.Replace(UPLOADCMD=" ".join(cmd_parts))
