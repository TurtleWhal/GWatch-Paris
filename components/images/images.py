# type: ignore

import subprocess
import os
import shutil

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

def copy_and_rename(src_path, dest_path, new_name):
    shutil.copy(src_path, f"{dest_path}/{new_name}")

# Clear generated folder
folder = os.path.join(SCRIPT_DIR, "generated")
for filename in os.listdir(folder):
    file_path = os.path.join(folder, filename)
    try:
        if os.path.isfile(file_path) or os.path.islink(file_path):
            os.unlink(file_path)
        elif os.path.isdir(file_path):
            shutil.rmtree(file_path)
    except Exception as e:
        print('Failed to delete %s. Reason: %s' % (file_path, e))


if os.name == "nt":
    windows = ["cmd.exe", "/c"]
else:
    windows = []


source_dir = os.path.join(SCRIPT_DIR, "source")
generated_dir = os.path.join(SCRIPT_DIR, "generated")

hfile = open(os.path.join(SCRIPT_DIR, "images.hpp"), "w")
hfile.truncate(0)
hfile.write("#include \"lvgl.h\"\n\n")
hfile.write("#define SET_IMG(obj, img) lv_image_set_src(obj, &img);\n\n")

cmakefiles = []

for file in os.listdir(source_dir):
    print("Converting Image: " + file)

    base = "IMG_" + file.split(".")[0].upper()
    ext = file.split(".")[1]

    copy_and_rename(os.path.join(source_dir, file), generated_dir, base + "." + ext)

    hfile.write("LV_IMAGE_DECLARE(" + base + ");\n")
    subprocess.call(windows + ["python3", os.path.join(SCRIPT_DIR, "lv_img_conv.py"), "--ofmt", "C", "--cf", "RGB565A8", "-o", generated_dir + "/", "--compress", "NONE", os.path.join(generated_dir, base + "." + ext)])

    os.remove(os.path.join(generated_dir, base + "." + ext))

    cmakefiles.append(base + ".c")

cmakefiles.sort()

with open(os.path.join(SCRIPT_DIR, "generated_sources.cmake"), "w") as cmake_file:
    cmake_file.write("set(GENERATED_SOURCES\n")
    for name in cmakefiles:
        cmake_file.write(f"    ${{CMAKE_CURRENT_SOURCE_DIR}}/generated/{name}\n")
    cmake_file.write(")\n")