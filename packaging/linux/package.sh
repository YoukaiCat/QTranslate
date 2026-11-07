#!/usr/bin/env bash
# Build the portable Linux archive with a bundled Qt runtime.
#
# The archive has one top-level directory named like the artifact and contains
# the CMake install tree plus the Qt libraries, plugins, and translations that
# the application loads at runtime. This script owns the Linux bundle layout;
# CMake only installs the binary, the desktop file, and the icon.
set -euo pipefail
umask 022

# The bundle must resolve its libraries through RUNPATH. Ignore whatever the
# caller exported so the checks below see the same behavior as a clean system.
unset LD_LIBRARY_PATH

build_dir=build/linux-release
output_dir=build/artifacts-linux
version=
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
declare -A system_libs=()

die() {
    printf 'package.sh: error: %s\n' "$*" >&2
    exit 1
}

usage() {
    cat <<'EOF'
Usage: packaging/linux/package.sh [options]

Build the portable Linux archive qtranslate-<version>-linux-x86_64.tar.gz.

Options:
  --build-dir DIR    CMake Release build directory (default: build/linux-release)
  --output-dir DIR   artifact directory (default: build/artifacts-linux)
  --version X.Y.Z    version used in the artifact name (default: project
                     version from CMakeLists.txt)
  --help             print this help and exit

The script must run on Debian 13 (trixie), or inside the qtranslate-trixie
image built from packaging/linux/trixie.Dockerfile. Other hosts are refused
with the docker command to use.

Required tools: cmake, qtpaths6, patchelf, tar, sha256sum,
desktop-file-validate, readelf, ldd, and the usual coreutils/findutils tools.

Artifacts:
  <output-dir>/qtranslate-<version>-linux-x86_64.tar.gz
  <output-dir>/SHA256SUMS-linux.txt

The script owns the staging directory <output-dir>/.qtranslate-staging.
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
            --version)
                [[ $# -ge 2 && -n ${2-} ]] \
                    || usage_error "option --version requires a non-empty value"
                [[ $2 =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] \
                    || usage_error "invalid version '$2': expected X.Y.Z"
                version=$2
                shift 2
                ;;
            *)
                usage_error "unknown argument '$1'"
                ;;
        esac
    done
}

check_repo_root() {
    [[ -f CMakeLists.txt && -d src ]] \
        || die "run this script from the repository root (CMakeLists.txt and src/ are required)"
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

    if [[ $os_id == debian ]]; then
        if [[ $codename == trixie || $version_id == 13 ]]; then
            supported=1
        fi
    fi
    if (( supported == 0 )); then
        printf 'package.sh: error: unsupported host: ID=%s VERSION_ID=%s VERSION_CODENAME=%s\n' \
            "${os_id:-unknown}" "${version_id:-unknown}" "${codename:-unknown}" >&2
        printf 'This script must run on Debian 13 (trixie) or in the qtranslate-trixie image:\n' >&2
        local version_arg=
        if [[ -n $version ]]; then
            printf -v version_arg ' --version %q' "$version"
        fi
        printf '  docker run --rm -v "$PWD:/src" -w /src qtranslate-trixie ./packaging/linux/package.sh --build-dir %q --output-dir %q%s\n' \
            "$build_dir" "$output_dir" "$version_arg" >&2
        exit 1
    fi
}

check_tools() {
    # Keep this as a literal list: one missing-tool error names every command
    # and the trixie package that provides it.
    local spec tool package missing=() message=
    for spec in \
        cmake:cmake \
        qtpaths6:qt6-base-dev-tools \
        patchelf:patchelf \
        tar:tar \
        sha256sum:coreutils \
        desktop-file-validate:desktop-file-utils \
        readelf:binutils \
        ldd:libc-bin \
        sed:sed \
        grep:grep \
        tr:coreutils \
        find:findutils \
        mkdir:coreutils \
        rm:coreutils \
        cp:coreutils \
        ln:coreutils \
        readlink:coreutils \
        realpath:coreutils \
        dirname:coreutils \
        basename:coreutils \
        uname:coreutils \
        cat:coreutils \
        tail:coreutils \
        sort:coreutils \
        stat:coreutils
    do
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

prepare_stage() {
    [[ -n $output_dir && $output_dir != / ]] \
        || die "refusing to use '$output_dir' as the output directory"
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

    # The install tree stays bundle-free for the .deb stage; the portable root
    # is a separate copy that receives the Qt runtime below.
    bundle_root="$stage/qtranslate-$version-linux-x86_64"
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
    local soname file ldd_output
    mkdir -p "$bundle_root/lib"

    for soname in \
        libQt6Core.so.6 \
        libQt6Gui.so.6 \
        libQt6Widgets.so.6 \
        libQt6Network.so.6 \
        libQt6DBus.so.6 \
        libQt6XcbQpa.so.6 \
        libicui18n.so.76 \
        libicuuc.so.76 \
        libicudata.so.76 \
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
    local plugin family
    mkdir -p "$platforms"

    for plugin in libqxcb.so libqoffscreen.so libqminimal.so; do
        [[ -f "$qt_plugins/platforms/$plugin" ]] \
            || die "missing Qt platform plugin $plugin in $qt_plugins/platforms"
        cp -a "$qt_plugins/platforms/$plugin" "$platforms/"
    done

    for family in tls imageformats; do
        [[ -d "$qt_plugins/$family" ]] \
            || die "missing Qt plugin directory $qt_plugins/$family"
        mkdir -p "$bundle_root/plugins/$family"
        cp -a "$qt_plugins/$family/." "$bundle_root/plugins/$family/"
    done

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
    local codes=(ru es de fr pt_BR it pl uk tr zh_CN ja ko ar)
    local code dependency found=()
    mkdir -p "$bundle_root/translations"

    for code in "${codes[@]}"; do
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
    translation_total=${#codes[@]}
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
    artifact="$output_dir/qtranslate-$version-linux-x86_64.tar.gz"
    tar -C "$stage" --owner=0 --group=0 --numeric-owner -czf "$artifact" \
        "qtranslate-$version-linux-x86_64"
    artifact_size=$(stat -c %s "$artifact")
}

write_checksums() {
    ( cd "$output_dir" && sha256sum "qtranslate-$version-linux-x86_64.tar.gz" > SHA256SUMS-linux.txt )
    artifact_sha=$(sha256sum "$artifact")
    artifact_sha=${artifact_sha%% *}
}

print_summary() {
    local plugins=() wayland_text=no
    mapfile -t plugins < <(find "$bundle_root/plugins" -type f -name '*.so' | sort)
    if (( wayland_bundled )); then
        wayland_text=yes
    fi

    cat <<EOF
Package summary:
  artifact: $artifact
  size: $artifact_size bytes
  sha256: $artifact_sha
  libraries bundled: $library_count
  plugins bundled: ${#plugins[@]}
  Qt translations: $translation_count/$translation_total
  Wayland bundle: $wayland_text
  host check: Debian 13 (trixie)
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
    prepare_stage
    install_tree
    write_qt_conf
    bundle_libraries
    bundle_plugins
    bundle_translations
    set_rpaths
    pack_tarball
    write_checksums
    print_summary
}

main "$@"
