#!/usr/bin/env bash
# Build the Linux release artifacts: the portable archive with a bundled Qt
# runtime, the native Debian package, and the AppImage.
#
# The archive has one top-level directory named like the artifact and contains
# the CMake install tree plus the Qt libraries, plugins, and translations that
# the application loads at runtime. The .deb is not staged from that tree: it is
# the native source-package build, produced by dpkg-buildpackage in a clean copy
# of the repository under <output-dir>/.qtranslate-staging. The AppImage is
# built from the install tree by the pinned linuxdeploy,
# linuxdeploy-plugin-qt, and appimagetool AppImages; those tools are downloaded
# into build/linux-tools/appimage and verified by SHA256. This script owns the
# Linux bundle layout; CMake only installs the binary, the desktop file, the
# icon, and the license text.
set -euo pipefail
umask 022

# The bundle must resolve its libraries through RUNPATH. Ignore whatever the
# caller exported so the checks below see the same behavior as a clean system.
unset LD_LIBRARY_PATH

build_dir=build/linux-release
output_dir=build/artifacts-linux
target=trixie
version=
deb_version=
bundle_name=
tarball_name=
checksums_file=SHA256SUMS-linux.txt
host_label='Debian 13 (trixie)'
stage=
install_root=
bundle_root=
qt_libs=
qt_plugins=
qt_translations=
bundle_real=
library_count=0
translation_count=0
translation_total=0
translation_dependency_count=0
translation_dependency_total=0
wayland_bundled=0
artifact=
artifact_size=
artifact_sha=
repo_root=
deb_artifact=
deb_size=
deb_sha=
deb_depends=
deb_installed_size=
appimage_enabled=1
appimage_artifact=
appimage_size=
appimage_sha=
appimage_tools_dir=build/linux-tools/appimage
# UI languages whose Qt catalogs are bundled in the portable archive and the
# AppImage.
ui_language_codes=(ru es de fr pt_BR it pl uk tr zh_CN ja ko ar)
declare -A system_libs=()

die() {
    printf 'package.sh: error: %s\n' "$*" >&2
    exit 1
}

usage() {
    cat <<'EOF'
Usage: packaging/linux/package.sh [options]

Build the portable Linux archive qtranslate-<version>-linux-x86_64.tar.gz,
the native Debian package qtranslate_<version>_amd64.deb, and the AppImage
qtranslate-<version>-x86_64.AppImage for the trixie target. The astra target
builds Astra Linux 1.8.7 artifacts with a distinct archive name and no
AppImage.

Options:
  --build-dir DIR    CMake Release build directory (default: build/linux-release)
  --output-dir DIR   artifact directory (default: build/artifacts-linux)
  --target TARGET    packaging target: trixie (default) or astra; the astra
                     target requires an Astra Linux 1.8.7 host and skips the
                     AppImage
  --version X.Y.Z    version used in the artifact name (default: project
                     version from CMakeLists.txt); it must match
                     debian/changelog, which supplies the .deb version
  --skip-appimage    do not build the AppImage; use this only for an explicit
                     local offline run, CI must build the AppImage
  --help             print this help and exit

The trixie target must run on Debian 13 (trixie), or inside the
qtranslate-trixie image built from packaging/linux/trixie.Dockerfile. Other
hosts are refused with the docker command to use and, on Astra, the --target
astra hint. The astra target must run on Astra Linux 1.8.7 itself.

The .deb is built natively: the script copies the source tree into
<output-dir>/.qtranslate-staging/deb-src and runs dpkg-buildpackage there with
DEB_BUILD_OPTIONS="parallel=<nproc> nocheck". The .deb is therefore a Release
build without the test targets; the test gate is the Release CTest run of the
build directory. The clean copy is removed again after the build.
qtranslate-dbgsym, the .changes and the .buildinfo that dpkg-buildpackage
writes stay in <output-dir>/.qtranslate-staging and are not release artifacts.

Required tools: cmake, qtpaths6, patchelf, tar, sha256sum,
desktop-file-validate, readelf, ldd, dpkg-deb, dpkg-buildpackage,
dpkg-parsechangelog, and the usual coreutils/findutils tools. curl is required
unless --skip-appimage is used; the astra target never builds the AppImage.

Artifacts (trixie):
  <output-dir>/qtranslate-<version>-linux-x86_64.tar.gz
  <output-dir>/qtranslate_<version>_amd64.deb
  <output-dir>/qtranslate-<version>-x86_64.AppImage
  <output-dir>/SHA256SUMS-linux.txt

Artifacts (astra):
  <output-dir>/qtranslate-<version>-linux-x86_64-astra.tar.gz
  <output-dir>/qtranslate_<version>_amd64.deb
  <output-dir>/SHA256SUMS-linux-astra.txt

The .deb file name is the same for both targets, so build the trixie and the
astra sets into different output directories; SHA256SUMS-linux-astra.txt still
separates the Astra set. The AppImage tools (linuxdeploy,
linuxdeploy-plugin-qt, appimagetool, and the type2 runtime) are downloaded into
build/linux-tools/appimage and verified against pinned SHA256 values. The
script owns the staging directory <output-dir>/.qtranslate-staging.
EOF
}

usage_error() {
    printf 'package.sh: error: %s\n\n' "$1" >&2
    usage >&2
    exit 2
}

parse_args() {
    while (( $# > 0 )); do
        case $1 in
            --help)
                usage
                exit 0
                ;;
            --build-dir)
                [[ $# -ge 2 && -n ${2-} ]] \
                    || usage_error "option --build-dir requires a non-empty value"
                build_dir=$2
                shift 2
                ;;
            --output-dir)
                [[ $# -ge 2 && -n ${2-} ]] \
                    || usage_error "option --output-dir requires a non-empty value"
                output_dir=$2
                shift 2
                ;;
            --target)
                [[ $# -ge 2 && -n ${2-} ]] \
                    || usage_error "option --target requires a non-empty value"
                case $2 in
                    trixie|astra) target=$2 ;;
                    *) usage_error "invalid target '$2': expected trixie or astra" ;;
                esac
                shift 2
                ;;
            --version)
                [[ $# -ge 2 && -n ${2-} ]] \
                    || usage_error "option --version requires a non-empty value"
                [[ $2 =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] \
                    || usage_error "invalid version '$2': expected X.Y.Z"
                version=$2
                shift 2
                ;;
            --skip-appimage)
                appimage_enabled=0
                shift
                ;;
            *)
                usage_error "unknown argument '$1'"
                ;;
        esac
    done

    # The Astra target never builds the AppImage. Applying this after the loop
    # keeps --target and --skip-appimage order-independent; --skip-appimage is
    # still accepted in Astra mode as a redundant option.
    if [[ $target == astra ]]; then
        appimage_enabled=0
    fi
}

check_repo_root() {
    [[ -f CMakeLists.txt && -d src ]] \
        || die "run this script from the repository root (CMakeLists.txt and src/ are required)"
    # The absolute root feeds the native .deb copy and its output-dir exclusion.
    repo_root=$(pwd -P)
}

check_host() {
    local os_release=/etc/os-release
    [[ -r $os_release ]] || die "cannot read $os_release"

    # The host gate needs sed and tr. On a host with a reduced PATH let the
    # tool check report the missing commands instead of failing here with a
    # misleading host detection error.
    if ! command -v sed >/dev/null 2>&1 || ! command -v tr >/dev/null 2>&1; then
        return 0
    fi

    local os_id version_id codename supported=0
    os_id=$(sed -n 's/^ID=//p' "$os_release" | tr -d '"')
    version_id=$(sed -n 's/^VERSION_ID=//p' "$os_release" | tr -d '"')
    codename=$(sed -n 's/^VERSION_CODENAME=//p' "$os_release" | tr -d '"')

    if [[ $target == astra ]]; then
        if [[ $os_id == astra ]]; then
            # Astra reports VERSION_ID=1.8_x86-64, not 1.8.7, so the target
            # keys on the distribution ID only.
            host_label='Astra Linux (astra target)'
            return 0
        fi
        printf 'package.sh: error: the astra target must be built on Astra Linux, not on ID=%s VERSION_ID=%s VERSION_CODENAME=%s\n' \
            "${os_id:-unknown}" "${version_id:-unknown}" "${codename:-unknown}" >&2
        printf 'The native Astra artifacts need Astra Linux 1.8.7 (Qt 6.10, ICU 72, libglib2.0-0); they cannot be built in the debian:trixie image.\n' >&2
        exit 1
    fi

    if [[ $os_id == debian ]]; then
        if [[ $codename == trixie || $version_id == 13 ]]; then
            supported=1
        fi
    fi
    if (( supported == 0 )); then
        printf 'package.sh: error: unsupported host: ID=%s VERSION_ID=%s VERSION_CODENAME=%s\n' \
            "${os_id:-unknown}" "${version_id:-unknown}" "${codename:-unknown}" >&2
        printf 'This script must run on Debian 13 (trixie) or in the qtranslate-trixie image:\n' >&2
        local version_arg='' skip_arg=''
        if [[ -n $version ]]; then
            printf -v version_arg ' --version %q' "$version"
        fi
        if (( appimage_enabled == 0 )); then
            skip_arg=' --skip-appimage'
        fi
        printf '  docker run --rm -v "$PWD:/src" -w /src qtranslate-trixie ./packaging/linux/package.sh --build-dir %q --output-dir %q%s%s\n' \
            "$build_dir" "$output_dir" "$version_arg" "$skip_arg" >&2
        if [[ $os_id == astra ]]; then
            printf 'For the native Astra artifacts run this script on Astra Linux 1.8.7 with --target astra --build-dir %q --output-dir %q%s\n' \
                "$build_dir" "$output_dir" "$version_arg" >&2
        fi
        exit 1
    fi
    host_label='Debian 13 (trixie)'
}

check_tools() {
    # Keep this as a literal list: one missing-tool error names every command
    # and the trixie package that provides it.
    local spec tool package missing=() message=
    local -a specs=(
        cmake:cmake
        qtpaths6:qt6-base-dev-tools
        patchelf:patchelf
        tar:tar
        sha256sum:coreutils
        desktop-file-validate:desktop-file-utils
        readelf:binutils
        ldd:libc-bin
        dpkg-deb:dpkg
        dpkg-buildpackage:dpkg-dev
        dpkg-parsechangelog:dpkg-dev
        nproc:coreutils
        chmod:coreutils
        sed:sed
        grep:grep
        tr:coreutils
        find:findutils
        mkdir:coreutils
        rm:coreutils
        cp:coreutils
        ln:coreutils
        readlink:coreutils
        realpath:coreutils
        dirname:coreutils
        basename:coreutils
        uname:coreutils
        cat:coreutils
        tail:coreutils
        sort:coreutils
        stat:coreutils
    )
    # curl downloads the pinned AppImage tools; --skip-appimage is the explicit
    # offline path and must not require it.
    if (( appimage_enabled )); then
        specs+=(curl:curl)
    fi
    for spec in "${specs[@]}"; do
        tool=${spec%%:*}
        package=${spec#*:}
        if ! command -v "$tool" >/dev/null 2>&1; then
            missing+=("$tool ($package)")
        fi
    done
    if (( ${#missing[@]} > 0 )); then
        for tool in "${missing[@]}"; do
            message+="${message:+, }$tool"
        done
        die "missing required tools: $message"
    fi
}

resolve_version() {
    if [[ -z $version ]]; then
        version=$(sed -nE 's/^project\(qtranslate VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt) \
            || die "cannot read the project version from CMakeLists.txt"
        [[ -n $version ]] \
            || die "cannot read the project version from CMakeLists.txt; pass --version X.Y.Z"
    fi

    # The .deb version comes from the source package; --version only names the
    # archive and the AppImage and must agree with debian/changelog. The
    # application version and AppInfo::version() stay X.Y.Z as well.
    local changelog_version
    changelog_version=$(dpkg-parsechangelog -SVersion) \
        || die "cannot read the package version from debian/changelog with dpkg-parsechangelog"
    [[ $changelog_version == "$version" ]] \
        || die "debian/changelog is $changelog_version but the artifact version is $version; update debian/changelog or pass --version $changelog_version"
    deb_version=$version

    if [[ $target == astra ]]; then
        bundle_name="qtranslate-$version-linux-x86_64-astra"
        checksums_file=SHA256SUMS-linux-astra.txt
    else
        bundle_name="qtranslate-$version-linux-x86_64"
        checksums_file=SHA256SUMS-linux.txt
    fi
    tarball_name="$bundle_name.tar.gz"
}

check_arch() {
    local arch
    arch=$(uname -m)
    [[ $arch == x86_64 ]] \
        || die "unsupported architecture '$arch': the artifact name is linux-x86_64"
}

check_build_dir() {
    local cache="$build_dir/CMakeCache.txt"
    local hint="cmake -S . -B $build_dir -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr"
    [[ -f $cache ]] \
        || die "no CMakeCache.txt in '$build_dir'; configure a Release build first: $hint"
    grep -qx 'CMAKE_BUILD_TYPE:STRING=Release' "$cache" \
        || die "build directory '$build_dir' is not a Release build; configure it with: $hint"
}

query_qt() {
    local key=$1 value
    value=$(qtpaths6 --query "$key" 2>/dev/null | tail -n1) \
        || die "qtpaths6 failed to report $key"
    printf '%s\n' "$value"
}

check_qt() {
    local cache="$build_dir/CMakeCache.txt"
    local qt6core_dir cache_qt_libs cache_core reported_core
    local qt_version qt_major qt_minor required required_major required_minor

    qt6core_dir=$(sed -n 's/^Qt6Core_DIR:[A-Z]*=//p' "$cache" | tail -n1)
    [[ -n $qt6core_dir ]] \
        || die "cannot read Qt6Core_DIR from '$cache'; configure the build against the Qt to bundle"
    cache_qt_libs=$(dirname "$(dirname "$qt6core_dir")")

    qt_libs=$(query_qt QT_INSTALL_LIBS)
    qt_plugins=$(query_qt QT_INSTALL_PLUGINS)
    qt_translations=$(query_qt QT_INSTALL_TRANSLATIONS)
    qt_version=$(query_qt QT_VERSION)

    cache_core=$(realpath "$cache_qt_libs/libQt6Core.so.6" 2>/dev/null || true)
    reported_core=$(realpath "$qt_libs/libQt6Core.so.6" 2>/dev/null || true)
    if [[ -z $cache_core || -z $reported_core || $cache_core != "$reported_core" ]]; then
        die "the build uses Qt from '$cache_qt_libs' but qtpaths6 reports '$qt_libs'; run package.sh with the Qt installation that configured the build"
    fi

    # The path comparison above cannot tell two Qt installations apart when
    # both live at /usr/lib/x86_64-linux-gnu (Astra Qt 6.10 and trixie Qt 6.8
    # do). The linked binary records its required Qt symbol versions (for
    # example Qt_6.10), so compare those with the Qt qtpaths6 reports.
    [[ -x $build_dir/qtranslate ]] \
        || die "no built binary in '$build_dir'; build it with: cmake --build $build_dir"
    required=$(LC_ALL=C readelf --version-info "$build_dir/qtranslate" \
        | grep -oE 'Qt_[0-9]+(\.[0-9]+)?' | sed 's/^Qt_//' | sort -V -u | tail -n1 || true)
    if [[ -n $required ]]; then
        required_major=${required%%.*}
        required_minor=${required#*.}
        if [[ $required_minor == "$required" ]]; then
            required_minor=0
        fi
        qt_major=${qt_version%%.*}
        qt_minor=${qt_version#*.}
        qt_minor=${qt_minor%%.*}
        if (( required_major > qt_major \
            || (required_major == qt_major && required_minor > qt_minor) )); then
            die "the build in '$build_dir' requires Qt $required, but qtpaths6 reports Qt $qt_version at '$qt_libs'; run package.sh with the Qt installation that configured the build"
        fi
    fi

    [[ -d $qt_libs ]] || die "Qt library directory '$qt_libs' does not exist"
    [[ -d $qt_plugins ]] || die "Qt plugin directory '$qt_plugins' does not exist"
    [[ -d $qt_translations ]] \
        || die "Qt translations directory '$qt_translations' does not exist; install qt6-translations-l10n"
}

download_tool() {
    local name=$1 url=$2 sha256=$3 mode=$4
    local path="$appimage_tools_dir/$name"
    local part="$path.part"
    local actual

    mkdir -p "$appimage_tools_dir"

    if [[ -f $path ]]; then
        actual=$(sha256sum "$path")
        actual=${actual%% *}
        if [[ $actual != "$sha256" ]]; then
            # A cache hit is trusted only after the checksum matches; a stale
            # pin must be removed explicitly, never re-downloaded silently.
            die "cached AppImage tool '$path' has SHA256 $actual, expected $sha256; remove the cached file and rerun"
        fi
        chmod "$mode" "$path" || die "cannot set mode $mode on cached AppImage tool '$path'"
        printf 'AppImage tool %s: cached (SHA256 %s)\n' "$name" "$sha256"
        return 0
    fi

    rm -f "$part"
    if ! curl --fail --location --retry 3 --connect-timeout 20 --silent --show-error \
        --output "$part" "$url"; then
        rm -f "$part"
        die "cannot download AppImage tool '$name' from $url (expected SHA256 $sha256); put the pinned release into '$appimage_tools_dir' or pass --skip-appimage for an offline run"
    fi
    actual=$(sha256sum "$part")
    actual=${actual%% *}
    if [[ $actual != "$sha256" ]]; then
        rm -f "$part"
        die "downloaded AppImage tool '$name' from $url has SHA256 $actual, expected $sha256; the release asset changed, refusing to use it"
    fi
    chmod "$mode" "$part" || die "cannot set mode $mode on '$part'"
    mv "$part" "$path" || die "cannot move '$part' to '$path'"
    printf 'AppImage tool %s: downloaded (SHA256 %s)\n' "$name" "$sha256"
}

download_appimage_tools() {
    printf 'Pinned AppImage tools: linuxdeploy 1-alpha-20251107-1, linuxdeploy-plugin-qt 1-alpha-20250213-1, appimagetool 1.9.1, type2-runtime 20251108\n'

    download_tool linuxdeploy-x86_64.AppImage \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage" \
        c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d \
        0755
    download_tool linuxdeploy-plugin-qt-x86_64.AppImage \
        "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage" \
        15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724 \
        0755
    download_tool appimagetool-x86_64.AppImage \
        "https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage" \
        ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0 \
        0755
    download_tool runtime-x86_64 \
        "https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64" \
        2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d \
        0644
}

prepare_stage() {
    local output_real repo_real

    [[ -n $output_dir && $output_dir != / ]] \
        || die "refusing to use '$output_dir' as the output directory"
    # Checked before anything is created or removed: at the repository root the
    # staging tree and the tarball would land in the working tree before
    # build_deb refused the output directory.
    output_real=$(realpath -m "$output_dir")
    repo_real=$(realpath -m "$repo_root")
    if [[ $output_real == "$repo_real" ]]; then
        die "refusing to build with --output-dir at the repository root; the native copy would recurse into its own staging directory"
    fi

    stage="$output_dir/.qtranslate-staging"
    mkdir -p "$output_dir"
    # The staging directory is the only entry of the output directory this
    # script removes; everything else there belongs to the user.
    rm -rf "$stage"
    mkdir -p "$stage"
}

install_tree() {
    install_root="$stage/install"
    cmake --install "$build_dir" --prefix "$install_root"

    [[ -x $install_root/bin/qtranslate ]] \
        || die "the install tree is missing an executable bin/qtranslate"
    [[ -f $install_root/share/applications/qtranslate.desktop ]] \
        || die "the install tree is missing share/applications/qtranslate.desktop"
    [[ -f $install_root/share/icons/hicolor/256x256/apps/qtranslate.png ]] \
        || die "the install tree is missing share/icons/hicolor/256x256/apps/qtranslate.png"
    if [[ -e $install_root/bin/qt.conf ]]; then
        die "the install tree contains bin/qt.conf; the Linux deploy block must not write one"
    fi
    if [[ -e $install_root/share/qt6 ]]; then
        die "the install tree contains share/qt6; the Linux deploy block must not deploy Qt"
    fi

    desktop-file-validate "$install_root/share/applications/qtranslate.desktop"

    # The install tree stays bundle-free for the AppImage; the portable root is
    # a separate copy that receives the Qt runtime below.
    bundle_root="$stage/$bundle_name"
    cp -a "$install_root/." "$bundle_root/"
}

write_qt_conf() {
    cat > "$bundle_root/bin/qt.conf" <<'EOF'
[Paths]
Prefix = ..
Plugins = plugins
Translations = translations
EOF
    if grep -q 'x86_64-linux-gnu' "$bundle_root/bin/qt.conf" \
        || grep -qE '=[[:space:]]*/' "$bundle_root/bin/qt.conf"; then
        die "bin/qt.conf contains a host path"
    fi
}

copy_lib() {
    local soname=$1 real name
    real=$(readlink -f "$qt_libs/$soname") \
        || die "library $soname not found in $qt_libs"
    [[ -f $real ]] || die "library $soname does not resolve to a file in $qt_libs"
    name=$(basename "$real")
    cp -a "$real" "$bundle_root/lib/"
    if [[ $name != "$soname" ]]; then
        # Copying the SONAME symlink alone would leave it dangling; create the
        # SONAME link next to the versioned file that was actually copied.
        ln -sfn "$name" "$bundle_root/lib/$soname"
    fi
    library_count=$((library_count + 1))
}

bundle_libraries() {
    local soname file ldd_output icu_major
    mkdir -p "$bundle_root/lib"

    # The ICU major version follows the Qt build: trixie bundles ICU 76 and
    # Astra ships ICU 72. Derive it from libQt6Core's NEEDED entry instead of
    # hardcoding one target's soname.
    icu_major=$(LC_ALL=C readelf -d "$qt_libs/libQt6Core.so.6" 2>/dev/null \
        | sed -n 's/.*libicui18n\.so\.\([0-9][0-9]*\).*/\1/p' | head -n1 || true)
    [[ -n $icu_major ]] \
        || die "cannot read the ICU major version from $qt_libs/libQt6Core.so.6"

    for soname in \
        libQt6Core.so.6 \
        libQt6Gui.so.6 \
        libQt6Widgets.so.6 \
        libQt6Network.so.6 \
        libQt6DBus.so.6 \
        libQt6XcbQpa.so.6 \
        "libicui18n.so.$icu_major" \
        "libicuuc.so.$icu_major" \
        "libicudata.so.$icu_major" \
        libdouble-conversion.so.3 \
        libb2.so.1 \
        libpcre2-16.so.0 \
        libpcre2-8.so.0 \
        libzstd.so.1
    do
        copy_lib "$soname"
    done

    if [[ -f "$qt_plugins/platforms/libqwayland-generic.so" ]]; then
        wayland_bundled=1
        for soname in \
            libQt6WaylandClient.so.6 \
            libQt6WaylandEglClientHwIntegration.so.6 \
            libQt6WlShellIntegration.so.6 \
            libQt6OpenGL.so.6 \
            libwayland-client.so.0 \
            libwayland-cursor.so.0 \
            libwayland-egl.so.1
        do
            copy_lib "$soname"
        done
    fi

    # Sanity check before patching: every copied library must resolve on the
    # build host. The strict in-bundle RUNPATH check runs in set_rpaths.
    while IFS= read -r -d '' file; do
        ldd_output=$(ldd "$file" 2>&1 || true)
        if grep -q 'not found' <<<"$ldd_output"; then
            die "library $(basename "$file") does not resolve: $(grep -m1 'not found' <<<"$ldd_output")"
        fi
    done < <(find "$bundle_root/lib" -maxdepth 1 -type f -print0)
}

bundle_plugins() {
    local platforms="$bundle_root/plugins/platforms"
    local imageformats="$bundle_root/plugins/imageformats"
    local plugin family
    mkdir -p "$platforms" "$imageformats"

    for plugin in libqxcb.so libqoffscreen.so libqminimal.so; do
        [[ -f "$qt_plugins/platforms/$plugin" ]] \
            || die "missing Qt platform plugin $plugin in $qt_plugins/platforms"
        cp -a "$qt_plugins/platforms/$plugin" "$platforms/"
    done

    # The application only loads JPEG, GIF, and ICO images. Copy that measured
    # set explicitly: Qt 6.10 ships more image format plugins whose libraries
    # (libQt6Pdf, libQt6Svg) are not bundled, and check_ldd rejects them.
    for plugin in libqgif.so libqico.so libqjpeg.so; do
        [[ -f "$qt_plugins/imageformats/$plugin" ]] \
            || die "missing Qt image format plugin $plugin in $qt_plugins/imageformats"
        cp -a "$qt_plugins/imageformats/$plugin" "$imageformats/"
    done

    [[ -d "$qt_plugins/tls" ]] \
        || die "missing Qt plugin directory $qt_plugins/tls"
    mkdir -p "$bundle_root/plugins/tls"
    cp -a "$qt_plugins/tls/." "$bundle_root/plugins/tls/"

    if (( wayland_bundled )); then
        for plugin in libqwayland-generic.so libqwayland-egl.so; do
            [[ -f "$qt_plugins/platforms/$plugin" ]] \
                || die "missing Qt Wayland platform plugin $plugin in $qt_plugins/platforms"
            cp -a "$qt_plugins/platforms/$plugin" "$platforms/"
        done

        for family in wayland-shell-integration wayland-graphics-integration-client; do
            [[ -d "$qt_plugins/$family" ]] \
                || die "missing Qt plugin directory $qt_plugins/$family"
        done
        mkdir -p "$bundle_root/plugins/wayland-shell-integration"
        cp -a "$qt_plugins/wayland-shell-integration/." \
            "$bundle_root/plugins/wayland-shell-integration/"
        mkdir -p "$bundle_root/plugins/wayland-graphics-integration-client"
        for plugin in libqt-plugin-wayland-egl.so libshm-emulation-server.so; do
            [[ -f "$qt_plugins/wayland-graphics-integration-client/$plugin" ]] \
                || die "missing Qt Wayland graphics plugin $plugin"
            cp -a "$qt_plugins/wayland-graphics-integration-client/$plugin" \
                "$bundle_root/plugins/wayland-graphics-integration-client/"
        done
    else
        printf 'warning: Qt Wayland platform plugin not found in %s/platforms; the archive will need XWayland for Wayland sessions\n' \
            "$qt_plugins" >&2
    fi
}

bundle_translations() {
    local code dependency found=()
    mkdir -p "$bundle_root/translations"

    for code in "${ui_language_codes[@]}"; do
        if [[ -f "$qt_translations/qt_$code.qm" ]]; then
            cp -a "$qt_translations/qt_$code.qm" "$bundle_root/translations/"
            found+=("qt_$code.qm")

            # Qt 6 ships qt_<lang>.qm as a small meta-catalog that references
            # the module catalogs. QTranslator::load fails when a referenced
            # catalog is missing, so the two measured dependencies travel with
            # every qt_<lang>.qm.
            for dependency in "qtbase_$code" "qtmultimedia_$code"; do
                translation_dependency_total=$((translation_dependency_total + 1))
                if [[ -f "$qt_translations/$dependency.qm" ]]; then
                    cp -a "$qt_translations/$dependency.qm" "$bundle_root/translations/"
                    translation_dependency_count=$((translation_dependency_count + 1))
                else
                    printf 'warning: missing Qt translation dependency %s.qm\n' \
                        "$dependency" >&2
                fi
            done
        else
            printf 'warning: missing Qt translation qt_%s.qm\n' "$code" >&2
        fi
    done

    translation_count=${#found[@]}
    translation_total=${#ui_language_codes[@]}
    printf 'Qt translations: %d/%d\n' "$translation_count" "$translation_total"
    if (( translation_count > 0 )); then
        printf '%s\n' "${found[@]}" | sort | sed 's/^/  /'
    fi
    printf 'Qt translation dependency catalogs: %d/%d\n' \
        "$translation_dependency_count" "$translation_dependency_total"
}

check_ldd() {
    local file=$1 ldd_output line resolved base real_resolved
    ldd_output=$(ldd "$file" 2>&1 || true)
    if grep -q 'not found' <<<"$ldd_output"; then
        die "unresolved dependency in $(basename "$file"): $(grep -m1 'not found' <<<"$ldd_output")"
    fi
    while IFS= read -r line; do
        [[ $line == *'=> /'* ]] || continue
        resolved=${line#*=> }
        resolved=${resolved%% (*}
        base=${resolved##*/}
        # The bundle owns Qt and ICU. Other NEEDED entries (glib, dbus, X11,
        # OpenSSL, the driver's EGL, and their own dependencies such as
        # libpcre2-8) stay the target's desktop stack and are recorded.
        case $base in
            libQt6*|libicu*)
                real_resolved=$(realpath "$resolved" 2>/dev/null || true)
                if [[ -z $real_resolved || $real_resolved != "$bundle_real"/* ]]; then
                    die "bundled library $base resolves outside the bundle: $resolved (from $(basename "$file"))"
                fi
                ;;
            *)
                real_resolved=$(realpath "$resolved" 2>/dev/null || true)
                if [[ -z $real_resolved || $real_resolved != "$bundle_real"/* ]]; then
                    system_libs[$base]=1
                fi
                ;;
        esac
    done <<<"$ldd_output"
}

set_rpaths() {
    local file rpath exe_dynamic base
    bundle_real=$(realpath "$bundle_root")

    while IFS= read -r -d '' file; do
        patchelf --set-rpath '$ORIGIN' "$file"
        rpath=$(patchelf --print-rpath "$file")
        [[ $rpath == '$ORIGIN' ]] \
            || die "patchelf set '$rpath' on $(basename "$file") instead of \$ORIGIN"
    done < <(find "$bundle_root/lib" -maxdepth 1 -type f -print0)

    while IFS= read -r -d '' file; do
        # A plugin's $ORIGIN is its own directory, so it needs the two-level
        # hop to lib/. Measured: platforms/libqxcb.so needs libQt6XcbQpa.so.6,
        # which the executable never loads.
        patchelf --set-rpath '$ORIGIN/../../lib' "$file"
        rpath=$(patchelf --print-rpath "$file")
        [[ $rpath == '$ORIGIN/../../lib' ]] \
            || die "patchelf set '$rpath' on $(basename "$file") instead of \$ORIGIN/../../lib"
    done < <(find "$bundle_root/plugins" -type f -name '*.so' -print0)

    exe_dynamic=$(LC_ALL=C readelf -d "$bundle_root/bin/qtranslate")
    grep -qF 'Library runpath: [$ORIGIN:$ORIGIN/../lib]' <<<"$exe_dynamic" \
        || die "bin/qtranslate RUNPATH is not \$ORIGIN:\$ORIGIN/../lib"

    check_ldd "$bundle_root/bin/qtranslate"
    while IFS= read -r -d '' file; do
        check_ldd "$file"
    done < <(find "$bundle_root/lib" -maxdepth 1 -type f -print0)
    while IFS= read -r -d '' file; do
        check_ldd "$file"
    done < <(find "$bundle_root/plugins" -type f -name '*.so' -print0)

    printf 'System libraries resolved from the host:\n'
    for base in "${!system_libs[@]}"; do
        printf '%s\n' "$base"
    done | sort | sed 's/^/  /'
}

pack_tarball() {
    local listing top_level
    artifact="$output_dir/$tarball_name"
    tar -C "$stage" --owner=0 --group=0 --numeric-owner -czf "$artifact" \
        "$bundle_name"

    # The archive is packed before build_deb: the native build only then copies
    # the tree into a subdirectory of $stage, so the top level still holds
    # nothing but the bundle root.
    listing=$(tar -tzf "$artifact")
    top_level=$(sed 's|/.*||' <<<"$listing" | sort -u)
    [[ $top_level == "$bundle_name" ]] \
        || die "the archive top-level entries are '$top_level' instead of only $bundle_name"
    if grep -qE '^(DEBIAN|deb|dpkg)(/|$)' <<<"$listing"; then
        die "the archive contains DEBIAN/, deb/, or dpkg/ staging entries"
    fi

    artifact_size=$(stat -c %s "$artifact")
}

build_deb() {
    local deb_src="$stage/deb-src"
    local output_real repo_real output_rel
    local native_deb
    local -a exclude_output=()

    # The .deb is the native source-package build, not a staging of the CMake
    # install tree. dpkg-buildpackage runs in a clean copy so debian/rules
    # writes (build/debian, debian/qtranslate) stay out of the working tree, and
    # it writes its output to $stage, the parent of the copy: the .deb, the
    # qtranslate-dbgsym package, the .changes and the .buildinfo.
    printf 'Building the Debian package with dpkg-buildpackage in %s\n' "$deb_src"
    mkdir -p "$deb_src"

    output_real=$(realpath -m "$output_dir")
    repo_real=$(realpath -m "$repo_root")
    if [[ $output_real == "$repo_real"/* ]]; then
        # --output-dir may point anywhere inside the tree, so exclude it as
        # well; otherwise the copy reads the staging directory it is written
        # into. The other names mirror debian/source/options.
        output_rel=${output_real#"$repo_real"/}
        exclude_output=(--exclude="./$output_rel")
    fi

    tar -C "$repo_root" -cf - \
        --exclude=./.git \
        --exclude=./build \
        --exclude='./build-*' \
        --exclude=./out \
        --exclude=./.claude \
        "${exclude_output[@]}" \
        . | tar -C "$deb_src" -xf - \
        || die "cannot copy the source tree into $deb_src"

    # -b skips the source .tar.xz/.dsc and -us -uc skip signing. No -r is
    # needed: debian/control has Rules-Requires-Root: no, so the build needs
    # neither root nor fakeroot. nocheck matches debian/rules: the .deb is a
    # Release build without the test targets, the test gate is the CTest run of
    # the build directory.
    ( cd "$deb_src" \
        && DEB_BUILD_OPTIONS="parallel=$(nproc) nocheck ${DEB_BUILD_OPTIONS-}" \
            dpkg-buildpackage -b -us -uc ) \
        || die "dpkg-buildpackage failed; the copy in '$deb_src' is left in place for inspection"

    native_deb="$stage/qtranslate_${deb_version}_amd64.deb"
    [[ -f $native_deb ]] \
        || die "dpkg-buildpackage did not produce $native_deb"

    deb_artifact="$output_dir/qtranslate_${deb_version}_amd64.deb"
    cp "$native_deb" "$deb_artifact" \
        || die "cannot copy $native_deb to $deb_artifact"
    deb_size=$(stat -c %s "$deb_artifact")
    deb_depends=$(dpkg-deb -f "$deb_artifact" Depends)
    deb_installed_size=$(dpkg-deb -f "$deb_artifact" Installed-Size)

    # Short self-check only: verify-deb.sh owns the content, the Depends
    # comparison and the control field checks.
    [[ $(dpkg-deb -f "$deb_artifact" Package) == qtranslate ]] \
        || die "the .deb Package field is not qtranslate"
    [[ $(dpkg-deb -f "$deb_artifact" Version) == "$deb_version" ]] \
        || die "the .deb Version field is not $deb_version"
    [[ $(dpkg-deb -f "$deb_artifact" Architecture) == amd64 ]] \
        || die "the .deb Architecture field is not amd64"

    # The dbgsym package and the build metadata are build outputs, never
    # artifacts: they stay in $stage, which the next prepare_stage removes.
    [[ ! -e $output_dir/qtranslate-dbgsym_${deb_version}_amd64.deb ]] \
        || die "the output directory contains qtranslate-dbgsym_${deb_version}_amd64.deb; the dbgsym package is not a release artifact"
    [[ ! -e $output_dir/qtranslate_${deb_version}_amd64.changes ]] \
        || die "the output directory contains qtranslate_${deb_version}_amd64.changes; it is not a release artifact"
    [[ ! -e $output_dir/qtranslate_${deb_version}_amd64.buildinfo ]] \
        || die "the output directory contains qtranslate_${deb_version}_amd64.buildinfo; it is not a release artifact"

    rm -rf "$deb_src"
    printf 'Native build outputs kept in %s (not artifacts):\n' "$stage"
    find "$stage" -maxdepth 1 -type f -printf '  %f\n' | sort
}

build_appimage() {
    local appdir="$stage/AppDir"
    local platform_plugins='libqoffscreen.so;libqminimal.so'
    local plugin header code translation family plugin_file real lib ldd_output
    local -a checkout=()

    [[ -x /usr/bin/qmake6 ]] \
        || die "the AppImage build needs /usr/bin/qmake6; install qt6-base-dev-tools"
    [[ -x $install_root/bin/qtranslate ]] \
        || die "the AppImage build is missing an executable bin/qtranslate"
    [[ -f $install_root/share/applications/qtranslate.desktop ]] \
        || die "the AppImage build is missing share/applications/qtranslate.desktop"
    [[ -f $install_root/share/icons/hicolor/256x256/apps/qtranslate.png ]] \
        || die "the AppImage build is missing share/icons/hicolor/256x256/apps/qtranslate.png"
    if [[ -e $install_root/bin/qt.conf || -e $install_root/share/qt6 \
        || -e $install_root/lib || -e $install_root/plugins \
        || -e $install_root/translations ]]; then
        die "the install tree is not bundle-free; the AppDir must start from the CMake install tree"
    fi

    # linuxdeploy expects the AppDir's FHS layout under usr/, like the .deb
    # staging, so the install tree goes to usr/ rather than the AppDir root.
    rm -rf "$appdir"
    mkdir -p "$appdir/usr"
    cp -a "$install_root/." "$appdir/usr/"

    [[ -x $appdir/usr/bin/qtranslate ]] \
        || die "the AppDir is missing an executable usr/bin/qtranslate"
    [[ -f $appdir/usr/share/applications/qtranslate.desktop ]] \
        || die "the AppDir is missing usr/share/applications/qtranslate.desktop"
    [[ -f $appdir/usr/share/icons/hicolor/256x256/apps/qtranslate.png ]] \
        || die "the AppDir is missing usr/share/icons/hicolor/256x256/apps/qtranslate.png"

    if [[ -f "$qt_plugins/platforms/libqwayland-generic.so" ]]; then
        platform_plugins+=';libqwayland-egl.so;libqwayland-generic.so'
    fi

    # linuxdeploy runs linuxdeploy-plugin-qt as a child AppImage, so the
    # extract-and-run variable must be exported, not just passed as an option.
    export ARCH=x86_64
    export APPIMAGE_EXTRACT_AND_RUN=1
    export QMAKE=/usr/bin/qmake6
    export EXTRA_PLATFORM_PLUGINS="$platform_plugins"
    export PATH="$appimage_tools_dir:$PATH"

    "$appimage_tools_dir/linuxdeploy-x86_64.AppImage" --appimage-extract-and-run \
        --appdir "$appdir" \
        --desktop-file "$install_root/share/applications/qtranslate.desktop" \
        --icon-file "$install_root/share/icons/hicolor/256x256/apps/qtranslate.png" \
        --plugin qt \
        || die "linuxdeploy failed to build the AppDir"

    [[ -x $appdir/usr/bin/qtranslate ]] \
        || die "linuxdeploy did not leave an executable usr/bin/qtranslate in the AppDir"
    [[ -e $appdir/AppRun ]] || die "linuxdeploy did not create AppRun"
    [[ -e $appdir/qtranslate.desktop ]] \
        || die "the AppDir root is missing qtranslate.desktop"
    [[ -e $appdir/qtranslate.png ]] || die "the AppDir root is missing qtranslate.png"
    [[ -f $appdir/usr/bin/qt.conf ]] \
        || die "linuxdeploy-plugin-qt did not write usr/bin/qt.conf"
    grep -qF '# generated by linuxdeploy-plugin-qt' "$appdir/usr/bin/qt.conf" \
        || die "the AppDir usr/bin/qt.conf was not generated by linuxdeploy-plugin-qt"
    for plugin in libqxcb.so libqoffscreen.so libqminimal.so; do
        [[ -f "$appdir/usr/plugins/platforms/$plugin" ]] \
            || die "the AppDir is missing the platform plugin $plugin"
    done
    if [[ $platform_plugins == *libqwayland-generic.so* ]]; then
        for plugin in libqwayland-egl.so libqwayland-generic.so; do
            [[ -f "$appdir/usr/plugins/platforms/$plugin" ]] \
                || die "linuxdeploy-plugin-qt did not deploy the Wayland platform plugin $plugin"
        done

        # linuxdeploy-plugin-qt 1-alpha-20250213-1 deploys the Wayland
        # platform plugins but not the shell-integration and decoration-client
        # families. Without xdg-shell the platform plugin cannot create a
        # surface on a compositor, so deploy both families explicitly with the
        # same RUNPATH as the plugin's own plugins. libQt6WlShellIntegration
        # and libQt6Svg are the two libraries those plugins need beyond what
        # the Qt plugin already deployed.
        for family in wayland-shell-integration wayland-decoration-client; do
            [[ -d $qt_plugins/$family ]] \
                || die "missing Qt plugin directory $qt_plugins/$family"
            mkdir -p "$appdir/usr/plugins/$family"
            cp -a "$qt_plugins/$family/." "$appdir/usr/plugins/$family/"
            while IFS= read -r -d '' plugin_file; do
                patchelf --set-rpath '$ORIGIN/../../lib:$ORIGIN' "$plugin_file"
            done < <(find "$appdir/usr/plugins/$family" -type f -name '*.so' -print0)
        done
        for lib in libQt6WlShellIntegration.so.6 libQt6Svg.so.6; do
            real=$(readlink -f "$qt_libs/$lib") \
                || die "library $lib not found in $qt_libs"
            cp -a "$real" "$appdir/usr/lib/$lib"
            patchelf --set-rpath '$ORIGIN' "$appdir/usr/lib/$lib"
        done

        checkout=(
            "$appdir/usr/lib/libQt6WlShellIntegration.so.6"
            "$appdir/usr/lib/libQt6Svg.so.6"
        )
        while IFS= read -r -d '' plugin_file; do
            checkout+=("$plugin_file")
        done < <(find "$appdir/usr/plugins/wayland-shell-integration" \
            "$appdir/usr/plugins/wayland-decoration-client" \
            -type f -name '*.so' -print0)
        for plugin_file in "${checkout[@]}"; do
            ldd_output=$(ldd "$plugin_file" 2>&1 || true)
            if grep -q 'not found' <<<"$ldd_output"; then
                die "unresolved dependency in AppDir file $(basename "$plugin_file"): $(grep -m1 'not found' <<<"$ldd_output")"
            fi
        done
        for plugin_file in \
            "$appdir/usr/plugins/wayland-shell-integration/libxdg-shell.so" \
            "$appdir/usr/plugins/wayland-decoration-client/libbradient.so"; do
            [[ -f $plugin_file ]] \
                || die "the AppDir is missing the Wayland plugin $(basename "$plugin_file")"
        done
    fi

    # linuxdeploy-plugin-qt deploys only the module catalogs (qtbase_*.qm).
    # The application asks QTranslator for Debian's qt_<lang>.qm meta-catalog,
    # which in turn references qtbase_<lang>.qm and qtmultimedia_<lang>.qm; the
    # portable archive bundles the same trio. Mirror it here after the plugin
    # ran, because the plugin does not deploy the meta-catalog.
    mkdir -p "$appdir/usr/translations"
    for code in "${ui_language_codes[@]}"; do
        for translation in "qt_$code.qm" "qtmultimedia_$code.qm"; do
            [[ -f $qt_translations/$translation ]] \
                || die "missing Qt translation $translation in $qt_translations"
            cp -a "$qt_translations/$translation" "$appdir/usr/translations/"
        done
    done
    [[ -f $appdir/usr/translations/qt_de.qm ]] \
        || die "the AppDir is missing usr/translations/qt_de.qm"
    [[ -f $appdir/usr/translations/qtmultimedia_de.qm ]] \
        || die "the AppDir is missing usr/translations/qtmultimedia_de.qm"

    appimage_artifact="$output_dir/qtranslate-$version-x86_64.AppImage"
    rm -f "$appimage_artifact"
    ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 \
        "$appimage_tools_dir/appimagetool-x86_64.AppImage" --appimage-extract-and-run \
        --runtime-file "$appimage_tools_dir/runtime-x86_64" \
        --no-appstream "$appdir" "$appimage_artifact" \
        || die "appimagetool failed to create $appimage_artifact"

    [[ -x $appimage_artifact ]] \
        || die "appimagetool did not create an executable $appimage_artifact"
    appimage_size=$(stat -c %s "$appimage_artifact")
    appimage_sha=$(sha256sum "$appimage_artifact")
    appimage_sha=${appimage_sha%% *}

    # readelf stands in for file(1) in this gate: the AppImage is an x86-64
    # PIE, and a truncated or wrong download must not pass for an artifact.
    header=$(LC_ALL=C readelf -h "$appimage_artifact") \
        || die "readelf cannot read the header of $appimage_artifact"
    grep -qE 'Class:[[:space:]]+ELF64' <<<"$header" \
        || die "the AppImage is not an ELF64 file"
    grep -qE 'Machine:[[:space:]]+Advanced Micro Devices X86-64' <<<"$header" \
        || die "the AppImage is not an x86-64 ELF file"
    grep -qE 'Type:[[:space:]]+DYN' <<<"$header" \
        || die "the AppImage is not a position-independent executable"

    printf 'AppImage: %s\n' "$appimage_artifact"
    printf 'AppImage size: %s bytes\n' "$appimage_size"
    printf 'AppImage sha256: %s\n' "$appimage_sha"
    printf 'AppImage platform plugins: %s\n' "$platform_plugins"
}

write_checksums() {
    local files=(
        "$tarball_name"
        "qtranslate_${deb_version}_amd64.deb"
    )
    if (( appimage_enabled )); then
        files+=("qtranslate-$version-x86_64.AppImage")
    elif [[ $target == trixie && -e $output_dir/qtranslate-$version-x86_64.AppImage ]]; then
        # The output directory belongs to the user: keep a foreign AppImage,
        # but do not let it look like part of this run. Astra mode does not
        # build an AppImage at all, so SHA256SUMS-linux-astra.txt never lists
        # one.
        printf 'warning: %s exists but was not built in this run (--skip-appimage); it is not listed in %s\n' \
            "$output_dir/qtranslate-$version-x86_64.AppImage" "$checksums_file" >&2
    fi
    ( cd "$output_dir" && sha256sum "${files[@]}" > "$checksums_file" )
    artifact_sha=$(sha256sum "$artifact")
    artifact_sha=${artifact_sha%% *}
    deb_sha=$(sha256sum "$deb_artifact")
    deb_sha=${deb_sha%% *}
    if (( appimage_enabled )); then
        appimage_sha=$(sha256sum "$appimage_artifact")
        appimage_sha=${appimage_sha%% *}
    fi
}

print_summary() {
    local plugins=() wayland_text=no appimage_summary

    # Both values come from the built .deb; an empty one means the package is
    # unusable even though dpkg-buildpackage succeeded.
    [[ -n $deb_depends ]] \
        || die "the .deb has an empty Depends field; dpkg-buildpackage should have expanded shlibs:Depends"
    [[ -n $deb_installed_size ]] \
        || die "the .deb has an empty Installed-Size field"

    mapfile -t plugins < <(find "$bundle_root/plugins" -type f -name '*.so' | sort)
    if (( wayland_bundled )); then
        wayland_text=yes
    fi

    if (( appimage_enabled )); then
        appimage_summary=$(printf '  appimage: %s\n  appimage size: %s bytes\n  appimage sha256: %s\n  appimage tools: linuxdeploy 1-alpha-20251107-1, linuxdeploy-plugin-qt 1-alpha-20250213-1, appimagetool 1.9.1, type2-runtime 20251108' \
            "$appimage_artifact" "$appimage_size" "$appimage_sha")
    elif [[ $target == astra ]]; then
        appimage_summary='  appimage: skipped (Astra target)'
    else
        appimage_summary='  appimage: skipped (--skip-appimage)'
    fi

    cat <<EOF
Package summary:
  artifact: $artifact
  size: $artifact_size bytes
  sha256: $artifact_sha
  deb: $deb_artifact
  deb size: $deb_size bytes
  deb sha256: $deb_sha
  depends: $deb_depends
  installed size: $deb_installed_size KiB
$appimage_summary
  libraries bundled: $library_count
  plugins bundled: ${#plugins[@]}
  Qt translations: $translation_count/$translation_total
  Wayland bundle: $wayland_text
  host check: $host_label
  tool check: passed
  Qt check: $qt_libs (matches the build cache)
  build type: Release
EOF
}

main() {
    parse_args "$@"
    check_repo_root
    # The host gate comes before the tool gate so an unsupported host prints
    # the docker command even when packaging tools such as patchelf are absent
    # (the Astra host has no patchelf).
    check_host
    check_tools
    resolve_version
    check_arch
    check_build_dir
    check_qt
    # Fail before the build work when a pinned tool cannot be fetched.
    if (( appimage_enabled )); then
        download_appimage_tools
    fi
    prepare_stage
    install_tree
    write_qt_conf
    bundle_libraries
    bundle_plugins
    bundle_translations
    set_rpaths
    pack_tarball
    build_deb
    if (( appimage_enabled )); then
        build_appimage
    fi
    write_checksums
    print_summary
}

main "$@"
