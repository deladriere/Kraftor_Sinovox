# works 2025-09-13 / usage :  pio run  -t clean && pio run
Import("env")
import os
import sys
import shutil
import subprocess

# Optional preferred UF2 volume name
preferred_volume_name = "KraftorBOOT"

print("Post build scripts")

def _read_board_id(info_file_path):
    try:
        with open(info_file_path, mode="r") as f:
            for line in f:
                if line.startswith("Board-ID:"):
                    return line.strip().split(":", 1)[1].strip()
    except Exception:
        return None
    return None

def find_uf2_drives():
    rootpath = "/Volumes" if sys.platform == "darwin" else "/media"
    found = []
    try:
        for entry in os.listdir(rootpath):
            mount_path = os.path.join(rootpath, entry)
            info_path = os.path.join(mount_path, "INFO_UF2.TXT")
            if os.path.isfile(info_path):
                found.append({
                    "mount_path": mount_path,
                    "volume_name": entry,
                    "board_id": _read_board_id(info_path)
                })
    except Exception:
        pass
    return found

def pick_destination_drive(drives):
    for d in drives:
        if d.get("volume_name") == preferred_volume_name:
            return d
    return drives[0] if drives else None

def after_build(source, target, env):
    print("----------")

    project_dir = env.subst("$PROJECT_DIR")
    build_dir = env.subst("$BUILD_DIR")

    uf2conv_path = os.path.join(project_dir, "uf2conv.py")
    bin_path = os.path.join(build_dir, "firmware.bin")
    uf2_out_path = os.path.join(project_dir, "firmware.uf2")

    cmd = [sys.executable, uf2conv_path, bin_path, "-b", "0x2000", "-c", "-o", uf2_out_path]

    try:
        result = subprocess.run(cmd, check=False, capture_output=True, text=True)
        if result.stdout:
            print(result.stdout.strip())
        if result.stderr:
            print(result.stderr.strip())

        if result.returncode == 0:
            print("UF2 conversion successful!")
            if os.path.exists(uf2_out_path):
                drives = find_uf2_drives()
                if drives:
                    picked = pick_destination_drive(drives)
                    dest_dir = picked.get("mount_path")
                    try:
                        shutil.copy(uf2_out_path, dest_dir)
                        vol = picked.get("volume_name") or os.path.basename(dest_dir)
                        bid = picked.get("board_id") or "unknown"
                        print(f"Firmware copied to {dest_dir} (volume '{vol}', board {bid})")
                    except Exception as e:
                        print(f"Found UF2 drive at {dest_dir} but copy failed: {e}")
                        print(f"You can manually copy: {uf2_out_path}")
                else:
                    print("No UF2 drive detected. Skipping auto-copy.")
                    print(f"UF2 ready at: {uf2_out_path}")
            else:
                print("The UF2 file does not exist. Conversion might have failed.")
        else:
            print("UF2 conversion failed.")
    except Exception as e:
        print(f"UF2 conversion error: {e}")

env.AddPostAction("buildprog", after_build)
