# type: ignore
import os
import shutil
import subprocess
import json
import pathlib


# Function to convert Hex code to UTF-8 string EX: "F00D" -> "\xEF\x80\x8D"
def hex_to_utf8(hex_value):
    # Convert hex string to integer
    decimal_value = int(hex_value, 16)

    # Determine the number of bytes needed based on the Unicode code point
    if decimal_value <= 0x7F:
        utf8_bytes = bytes([decimal_value])
    elif decimal_value <= 0x7FF:
        utf8_bytes = bytes([0xC0 | (decimal_value >> 6), 0x80 | (decimal_value & 0x3F)])
    elif decimal_value <= 0xFFFF:
        utf8_bytes = bytes(
            [
                0xE0 | (decimal_value >> 12),
                0x80 | ((decimal_value >> 6) & 0x3F),
                0x80 | (decimal_value & 0x3F),
            ]
        )
    elif decimal_value <= 0x10FFFF:
        utf8_bytes = bytes(
            [
                0xF0 | (decimal_value >> 18),
                0x80 | ((decimal_value >> 12) & 0x3F),
                0x80 | ((decimal_value >> 6) & 0x3F),
                0x80 | (decimal_value & 0x3F),
            ]
        )
    else:
        raise ValueError("Invalid Unicode code point")

    return '"' + str(utf8_bytes)[2:-1].upper().replace("X", "x") + '"'


# End of function


if os.name == "nt":
    windows = ["cmd.exe", "/c"]
else:
    windows = []


# Check if npm is installed
print("Checking if npm is installed")
try:
    subprocess.check_call(windows + ["npm", "-v"])
except:
    print(
        "npm is not installed, please install it at https://nodejs.org/en/download/package-manager"
    )
    exit(1)


try:
    subprocess.check_call(windows + ["npm", "list", "-g", "grep", "lv_font_conv"])
except:
    print(
        "Please install lv_font_conv at https://github.com/lvgl/lv_font_conv?tab=readme-ov-file#install-the-script"
    )
    exit(1)

# Get symbols from symbols.json
symbols = json.load(
    open(
        os.path.realpath(
            pathlib.Path(__file__).parent.resolve().joinpath("./symbols.json")
        )
    )
)

symboldefines = []
fontdefines = []
sizes = []
for x in range(1000):  # Large array for font sizes
    sizes.append([])


# get the longest name
longest = 0
for symbol in symbols["symbols"]:
    if len(symbol["name"]) > longest:
        longest = len(symbol["name"])


# Create symboldefines for fonts.hpp
for symbol in symbols["symbols"]:
    symboldefines.append(
        f"#define FA_{(symbol['name'].replace('-', '_').replace(' ', '_').upper().ljust(longest, ' '))} {hex_to_utf8(symbol['hex'])} // U+{symbol['hex'].upper()}, Sizes: {symbol['sizes']}\n"
    )
    for size in symbol["sizes"]:
        if symbol.get("brand") is None:
            sizes[size].append(symbol["hex"])
        else:
            sizes[size].append(symbol["hex"] + "\0")


# Clear generated folder
folder = os.path.realpath(
    pathlib.Path(__file__).parent.resolve().joinpath("./generated")
)
for filename in os.listdir(folder):
    file_path = os.path.join(folder, filename)
    try:
        if os.path.isfile(file_path) or os.path.islink(file_path):
            os.unlink(file_path)
        elif os.path.isdir(file_path):
            shutil.rmtree(file_path)
    except Exception as e:
        print("Failed to delete %s. Reason: %s" % (file_path, e))


# Run lv_font_conv to create files
fontfiles = []

# Create Symbol Files
for size in sizes:
    if len(size) > 0:
        fontfiles.append("FontAwesome_" + str(sizes.index(size)) + ".c")

        srange = ""
        brange = ""
        for hex in size:
            if hex.endswith("\0"):
                brange += "0x" + hex[:-1] + ", "
            else:
                srange += "0x" + hex + ", "

        srange = srange[:-2]
        brange = brange[:-2]

        print(
            "Generating font: "
            + "FontAwesome_"
            + str(sizes.index(size))
            + " with srange: "
            + srange
            + " and brange: "
            + brange
        )

        args = windows + [
            "lv_font_conv",
            "--size",
            str(sizes.index(size)),
            "--bpp",
            str(symbols["symbol-bpp"]),
            "--format",
            "lvgl",
            "--output",
            os.path.realpath(
                pathlib.Path(__file__).parent.resolve().joinpath("./generated")
            )
            + "/FontAwesome_"
            + str(sizes.index(size))
            + ".c",
            "--no-compress",
        ]

        if srange:
            args.append("--font")
            args.append(
                os.path.realpath(
                    pathlib.Path(__file__)
                    .parent.resolve()
                    .joinpath("./FontAwesome6Pro.woff")
                )
            )
            args.append("--range")
            args.append(srange)

        if brange:
            args.append("--font")
            args.append(
                os.path.realpath(
                    pathlib.Path(__file__)
                    .parent.resolve()
                    .joinpath("./FontAwesome5.woff")
                )
            )
            args.append("--range")
            args.append(brange)

        # subprocess.run(["lv_font_conv", "--size", str(sizes.index(size)), "--bpp", str(symbols['symbol-bpp']), "--format", "lvgl", "--font", os.path.abspath("FontAwesome5.woff"), "--range", range, "--output", os.path.abspath("generated") + "/FontAwesome_" + str(sizes.index(size)) + ".c",  "--no-compress"])
        subprocess.run(args)

# Create Font Files
for font in symbols["fonts"]:

    for size in font["size"]:
        dest = (
            font["font"]
            .replace(".ttf", "")
            .replace(".woff", "")
            .replace("-", "")
            .replace("_", "")
            .replace(" ", "")
            + "_"
            + str(size)
            + ".c"
        )

        fontdefines.append(dest[:-2])

        args = windows + [
            "lv_font_conv",
            "--size",
            str(size),
            "--bpp",
            str(font["bpp"]),
            "--format",
            "lvgl",
            "--font",
            os.path.realpath(
                pathlib.Path(__file__)
                .parent.resolve()
                .joinpath("./files/" + font["font"])
            ),
            "--output",
            os.path.realpath(
                pathlib.Path(__file__).parent.resolve().joinpath("./generated")
            )
            + "/"
            + dest,
            "--no-compress",
        ]

        if font["range"]:
            args.append("--range")
            args.append(font["range"].replace("default", "0x20-0x7F"))
        if font["symbols"]:
            args.append("--symbols")
            args.append(font["symbols"])

        print("Generating font: " + dest)

        subprocess.run(args)

fontfiles.sort()


# Create fonts.hpp
# hfile = open("fonts.h", "r+")
hfile = open(
    os.path.realpath(pathlib.Path(__file__).parent.resolve().joinpath("./fonts.h")),
    "r+",
)
hfile.truncate(0)  # Clear the file

hfile.write('#include "lvgl.h"\n\n')

hfile.write("\n".join(f"LV_FONT_DECLARE({define});" for define in fontdefines))

hfile.write("\n\n")

for fontfile in fontfiles:
    hfile.write(f"LV_FONT_DECLARE(" + fontfile[:-2] + ");\n")

hfile.write("\n")

for fontfile in fontfiles:
    hfile.write(
        f"#define SET_SYMBOL_"
        + str(fontfile[12:-2])
        + "(obj, sym) lv_obj_set_style_text_font(obj, &"
        + fontfile[:-2]
        + ", LV_PART_MAIN); lv_label_set_text(obj, sym);\n"
    )

hfile.write("\n")

for define in symboldefines:
    hfile.write(define)

# # --- Generate CMake file with all generated .c files as sources ---
# cmake_file_path = os.path.realpath(
#     pathlib.Path(__file__).parent.resolve().joinpath("./CMakeLists.txt")
# )
# with open(cmake_file_path, "w") as cmake_file:
#     cmake_file.write(
#         "# Auto-generated by fonts.py. Contains all generated font sources.\n"
#     )
#     cmake_file.write("idf_component_register(\n    SRCS\n")
#     # Add all .c files in the generated directory
#     for fname in sorted(os.listdir(folder)):
#         if fname.endswith(".c"):
#             cmake_file.write(f"        generated/{fname}\n")
#     cmake_file.write("    INCLUDE_DIRS\n        .\n)\n")
