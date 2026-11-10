# Build a UF2 file without copying it to any connected device.
Import("env")
from pathlib import Path
import subprocess
import sys
import re


def after_build(source, target, env):
    project = Path(env.subst("$PROJECT_DIR"))
    binary = Path(env.subst("$BUILD_DIR")) / "firmware.bin"
    version = re.search(r'constexpr char VERSION\[\] = "([0-9.]+)"', (project / "src/main.cpp").read_text()).group(1)
    output = project / ("Kraftor_Sinovox_" + version + ".uf2")
    subprocess.run([
        sys.executable, str(project / "uf2conv.py"), str(binary),
        "-b", "0x2000", "-c", "-o", str(output)
    ], check=True)
    print("UF2 ready: " + str(output))


env.AddPostAction("buildprog", after_build)
