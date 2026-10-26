"""DAODroid's PC tool: your own disc image in, a game package (.daopkg) for the launcher out.

    python scripts/daodroid_tool.py --iso "<the disc image>" --out "<folder for the package>"
        [--accept-msvc-license] [--jobs N] [--work <folder>]

What it does (the parts that come from DAORecomp's installer are its own code, recomp/installer):
  1. checks the disc image (title ID 454108C0, media ID 186A1115, the supported default.xex);
  2. extracts the game files;
  3. sets up the tools (sources, LLVM, on Windows the MSVC headers, XenonRecomp) and translates
     the game's code (recomp/ppc);
  4. gets the Android NDK r27d (the installed one from the Android SDK, or a download checked
     against Google's published checksum) and builds the game library for ARM64 (libdaodroid.so);
  5. packs the game files and the library into the package (docs/PACKAGE_FORMAT.md).

No Android Studio is needed. Copy the package to the phone and add it in the DAODroid launcher.
The package is your own copy of the game: don't share it.
"""
import argparse
import hashlib
import os
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RECOMP = ROOT / "recomp"
sys.path.insert(0, str(RECOMP))
sys.path.insert(0, str(ROOT / "scripts"))

from installer import pipeline, toolchain, util  # noqa: E402  (DAORecomp's installer steps)
from installer.util import InstallError, Reporter, check_host  # noqa: E402

import make_package  # noqa: E402  (scripts/make_package.py)

# The NDK the app is built with (android/app/build.gradle ndkVersion), and Google's published
# downloads (github.com/android/ndk/wiki/Unsupported-Downloads).
NDK_VERSION = "27.3.13750724"
NDK_DOWNLOADS = {
    "win32": ("https://dl.google.com/android/repository/android-ndk-r27d-windows.zip", 781506724,
              "56607cbccd3642d4a1991f6bb3114a00f884f426"),
    "linux": ("https://dl.google.com/android/repository/android-ndk-r27d-linux.zip", 663956036,
              "22105e410cf29afcf163760cc95522b9fb981121"),
}
STEPS = 5

# What the Android build takes from xenia-canary beyond DAORecomp's sparse checkout: the Vulkan
# backend's sources and shaders, and the submodules it compiles with (docs/PHASE4_runbook.md).
CANARY_PATHS = ("src/xenia/gpu", "src/xenia/ui", "src/xenia/base", "cmake", "third_party/CMakeLists.txt")
CANARY_SUBMODULES = ("third_party/Vulkan-Headers", "third_party/VulkanMemoryAllocator", "third_party/glslang",
                     "third_party/SPIRV-Tools", "third_party/FidelityFX-CAS", "third_party/FidelityFX-FSR")


def step(reporter, n, text):
    reporter.status(f"[{n}/{STEPS}] {text}")


def find_ndk(work):
    """An installed NDK r27d: ANDROID_NDK_HOME, the Android SDK's, or one this tool downloaded."""
    candidates = []
    if os.environ.get("ANDROID_NDK_HOME"):
        candidates.append(Path(os.environ["ANDROID_NDK_HOME"]))
    for sdk in (os.environ.get("ANDROID_HOME"), os.environ.get("ANDROID_SDK_ROOT"),
                os.path.join(os.environ.get("LOCALAPPDATA", ""), "Android", "Sdk"),
                os.path.expanduser("~/Android/Sdk")):
        if sdk:
            candidates.append(Path(sdk) / "ndk" / NDK_VERSION)
    candidates.append(work / "ndk" / "android-ndk-r27d")
    for c in candidates:
        props = c / "source.properties"
        if props.is_file() and NDK_VERSION in props.read_text(encoding="utf-8", errors="replace"):
            return c
    return None


def download_ndk(reporter, work):
    platform = "win32" if sys.platform == "win32" else "linux"
    if platform not in NDK_DOWNLOADS:
        raise InstallError("No NDK download for this system: install NDK " + NDK_VERSION + " and set ANDROID_NDK_HOME.")
    url, size, sha1 = NDK_DOWNLOADS[platform]
    dest = work / "ndk"
    dest.mkdir(parents=True, exist_ok=True)
    archive = dest / url.rsplit("/", 1)[1]
    if not (archive.is_file() and archive.stat().st_size == size):
        reporter.log(f"Downloading {url} ({size / 2**20:.0f} MB)")
        partial = archive.with_suffix(".zip.part")
        have = partial.stat().st_size if partial.exists() else 0
        request = urllib.request.Request(url, headers={"User-Agent": "daodroid-tool"})
        if have:
            request.add_header("Range", f"bytes={have}-")
        with urllib.request.urlopen(request, timeout=60) as response:
            if have and response.status != 206:
                have = 0
            with open(partial, "ab" if have else "wb") as out:
                done, last = have, -1
                while chunk := response.read(1 << 20):
                    reporter.check_cancel()
                    out.write(chunk)
                    done += len(chunk)
                    reporter.progress(done / size)
                    if done * 10 // size != last:
                        last = done * 10 // size
                        reporter.log(f"  {done * 100 // size}%")
        partial.replace(archive)
    h = hashlib.sha1()
    with open(archive, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    if h.hexdigest() != sha1:
        archive.unlink()
        raise InstallError("The NDK download is damaged (checksum mismatch): run the tool again.")
    reporter.log("Extracting the NDK")
    with zipfile.ZipFile(archive) as z:
        z.extractall(dest)
    if sys.platform != "win32":
        for p in (dest / "android-ndk-r27d").rglob("*"):
            if p.is_file() and ("/bin/" in p.as_posix() or p.suffix == ""):
                p.chmod(p.stat().st_mode | 0o111)
    return dest / "android-ndk-r27d"


def ensure_vulkan_sources(reporter):
    """The xenia-canary parts only the Android build needs, at the commits xenia-canary records
    (util.GIT is the downloaded MinGit when the PC has no git)."""
    canary = RECOMP / "tools" / "xenia-canary"
    util.run(reporter, [*util.GIT, "-C", canary, "sparse-checkout", "add", *CANARY_PATHS], quiet=True)
    util.run(reporter, [*util.GIT, "-C", canary, "submodule", "update", "--init", "--depth", "1",
                        *CANARY_SUBMODULES], quiet=True)


def build_library(reporter, tc, ndk, work, jobs):
    """libdaodroid.so with the NDK, CMake and Ninja (the app's native build, without Gradle)."""
    build = work / "android"
    configure = [str(tc.cmake), "-G", "Ninja", "-S", str(ROOT / "android" / "app" / "src" / "main" / "cpp"),
                 "-B", str(build),
                 f"-DCMAKE_MAKE_PROGRAM={tc.ninja}",
                 f"-DCMAKE_TOOLCHAIN_FILE={ndk / 'build' / 'cmake' / 'android.toolchain.cmake'}",
                 "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-29", "-DANDROID_STL=c++_static",
                 "-DCMAKE_BUILD_TYPE=Release", f"-DPython3_EXECUTABLE={sys.executable}"]
    run(reporter, configure, work / "android-configure.log")
    run(reporter, [str(tc.cmake), "--build", str(build), "--target", "daodroid", "--", f"-j{jobs}"],
        work / "android-build.log")
    library = build / "libdaodroid.so"
    if not library.is_file():
        found = list(build.rglob("libdaodroid.so"))
        if not found:
            raise InstallError(f"The game library wasn't built (see {work / 'android-build.log'}).")
        library = found[0]
    return library


def run(reporter, args, log_file):
    reporter.log("$ " + " ".join(args))
    with open(log_file, "w", encoding="utf-8", errors="replace") as log:
        p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                             encoding="utf-8", errors="replace")
        for line in p.stdout:
            log.write(line)
            line = line.rstrip()
            # Ninja's progress lines ("[123/456] ...") as progress, the rest to the log.
            if line.startswith("[") and "/" in line.split("]", 1)[0]:
                done, total = line[1:].split("]", 1)[0].split("/", 1)
                if done.isdigit() and total.isdigit() and int(total):
                    reporter.progress(int(done) / int(total))
                    if int(done) % 200 == 0 or done == total:
                        reporter.log(f"  {done}/{total}")
                continue
            if "error" in line.lower() or "warning: " in line.lower():
                reporter.log("  " + line)
        if p.wait() != 0:
            raise InstallError(f"{args[0]} failed (see {log_file}).")


def main(argv=None, reporter=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--iso", required=True, help="your Dragon Age: Origins Xbox 360 disc image")
    parser.add_argument("--out", required=True, help="where to put the game package")
    parser.add_argument("--work", help="the tool's working folder (default: build/tool in this repository)")
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    parser.add_argument("--accept-msvc-license", dest="accept_license", action="store_true",
                        help="accept Microsoft's license for the Windows SDK headers (Windows only)")
    parser.add_argument("--llvm", default="download", help="DAORecomp's --llvm choice (download, system, auto)")
    args = parser.parse_args(argv)
    reporter = reporter or Reporter()
    work = Path(args.work).resolve() if args.work else ROOT / "build" / "tool"
    work.mkdir(parents=True, exist_ok=True)
    iso = Path(args.iso).expanduser().resolve()

    check_host()
    step(reporter, 1, "Checking the disc image")
    pipeline.check_iso(reporter, iso)

    step(reporter, 2, "Extracting the game files")
    game = work / "game"
    pipeline.extract_game(reporter, iso, game)

    step(reporter, 3, "Translating the game's code (the first time takes a while: the tools are set up too)")
    if sys.platform == "win32" and not args.accept_license and not toolchain.winsysroot_ready():
        raise InstallError("Pass --accept-msvc-license (Microsoft's license for the Windows SDK headers DAORecomp's "
                           "recompiler is built with), or tick it in the window.")
    tc, recompiler = pipeline._prepare(reporter, args, 3)
    pipeline.recompile(reporter, recompiler, game)

    step(reporter, 4, "Building the game library for the phone")
    ensure_vulkan_sources(reporter)
    ndk = find_ndk(work) or download_ndk(reporter, work)
    reporter.log(f"NDK: {ndk}")
    library = build_library(reporter, tc, ndk, work, args.jobs)

    step(reporter, 5, "Packing the game package")
    package = make_package.build(argparse.Namespace(game=str(game), library=str(library),
                                                    title_update=None, out=args.out))
    reporter.status(f"Done: {package}\nCopy it to your phone and add it in DAODroid (+). "
                    "It's your own copy of the game: don't share it.")
    return package


if __name__ == "__main__":
    try:
        main()
    except InstallError as e:
        sys.exit(f"Error: {e}")
