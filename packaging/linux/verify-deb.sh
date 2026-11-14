#!/usr/bin/env bash
#
# verify-deb.sh - content, metadata, dependency and install gate for the native
# qtranslate .deb.
#
# The package is verified against the host that produced it: the expected
# Architecture comes from dpkg --print-architecture, the expected Version from
# debian/changelog and CMakeLists.txt, the expected Depends from dpkg-shlibdeps
# run on the packaged binary, and the libsecret rule from readelf -d. This is
# what makes the same script usable on Astra Linux and in debian:trixie.
#
# The script never writes into the source tree. It extracts the package once
# into a temporary directory, removes that directory on exit, and removes an
# installed test package if this run installed it.

set -u

# Keep dpkg, readelf, apt and desktop-file-validate output stable and English.
# Prefer a UTF-8 locale so Qt does not print its "non-UTF-8 locale" warning.
if command -v locale >/dev/null 2>&1 && locale -a 2>/dev/null | grep -qiE '^c\.utf-?8$'; then
    LC_ALL=C.UTF-8
else
    LC_ALL=C
fi
export LC_ALL

# --- result reporting --------------------------------------------------------

passes=0
failures=0
skips=0
skipped_reasons=()
installed_flag=0
installed_package=""
tmpdir=""
priv=()
REPLY_LIST=()
SHLIBDEPS_DEPENDS=""

pass() {
    passes=$((passes + 1))
    printf 'PASS %s\n' "$1"
}

fail() {
    failures=$((failures + 1))
    printf 'FAIL %s: %s\n' "$1" "$2"
}

skip() {
    skips=$((skips + 1))
    skipped_reasons+=("$1: $2")
    printf 'SKIP %s: %s\n' "$1" "$2"
}

note() {
    printf 'NOTE %s: %s\n' "$1" "$2"
}

warn() {
    printf 'WARN %s: %s\n' "$1" "$2"
}

die() {
    printf 'error: %s\n' "$1" >&2
    exit 2
}

have() {
    command -v "$1" >/dev/null 2>&1
}

# Run a command as root: directly when uid 0, through "sudo -n" otherwise.
as_root() {
    if [ "${#priv[@]}" -eq 0 ]; then
        "$@"
    else
        "${priv[@]}" "$@"
    fi
}

# --- small text helpers ------------------------------------------------------

trim() {
    local s=$1
    s=${s#"${s%%[![:space:]]*}"}
    s=${s%"${s##*[![:space:]]}"}
    printf '%s' "$s"
}

join() {
    local out="" item
    for item in "$@"; do
        out="${out:+$out, }$item"
    done
    printf '%s' "$out"
}

# Render a command line for copy-paste, quoting each argument.
quote_cmd() {
    local out="" item
    for item in "$@"; do
        out="${out:+$out }$(printf '%q' "$item")"
    done
    printf '%s' "$out"
}

contains() {
    local needle=$1 item
    shift
    for item in "$@"; do
        if [ "$item" = "$needle" ]; then
            return 0
        fi
    done
    return 1
}

# Last non-empty line of a stream; used to read the --version output when Qt
# prepends warnings to stdout.
last_line() {
    sed '/^[[:space:]]*$/d' | tail -n 1
}

# Split a comma-separated Depends value into REPLY_LIST, trimming spaces.
split_list() {
    local s=$1 part
    REPLY_LIST=()
    while IFS= read -r part; do
        part=$(trim "$part")
        if [ -n "$part" ]; then
            REPLY_LIST+=("$part")
        fi
    done < <(printf '%s\n' "$s" | tr ',' '\n')
}

# --- temporary directory and package cleanup ---------------------------------

# Called from the EXIT/INT/TERM traps, not directly.
# shellcheck disable=SC2317
cleanup() {
    if [ "$installed_flag" = 1 ] && [ -n "$installed_package" ]; then
        if as_root dpkg -r "$installed_package" >/dev/null 2>&1; then
            installed_flag=0
        else
            printf 'WARN install-roundtrip: could not remove the installed qtranslate package\n' >&2
        fi
    fi
    if [ -n "$tmpdir" ] && [ -d "$tmpdir" ]; then
        rm -rf "$tmpdir"
    fi
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# --- command line ------------------------------------------------------------

usage() {
    cat <<'EOF'
Usage: verify-deb.sh [--self-test] [--source-dir DIR] <package.deb>

Verify the content, metadata, dependencies and installability of the native
qtranslate .deb against the host that produced it.

Options:
  --self-test        build four deliberately broken copies of <package.deb> and
                     prove that each one fails the intended check; the install
                     roundtrip is not run and root is not required
  --source-dir DIR   source tree with debian/control, debian/changelog and
                     CMakeLists.txt (default: the tree this script lives in)
  -h, --help         print this message and exit

Checks: package contents and the doc/icon allowlist, control fields and
version, the absence of maintainer scripts, Depends recomputed with
dpkg-shlibdeps, the glib and libsecret rules, desktop-file-validate,
qtranslate --version on the extracted tree, dpkg -i/-L/-r (root or passwordless
sudo -n only), dpkg-checkbuilddeps, and a non-fatal apt-get -s build-dep
simulation.

Exit status: 0 - nothing failed (skips are reported), 1 - at least one check
failed, 2 - usage error or a missing prerequisite.
EOF
}

self_test=0
source_dir_arg=""
positional=()

while [ $# -gt 0 ]; do
    case "$1" in
        --self-test)
            self_test=1
            shift
            ;;
        --source-dir)
            [ $# -ge 2 ] || { usage >&2; exit 2; }
            source_dir_arg=$2
            shift 2
            ;;
        --source-dir=*)
            source_dir_arg=${1#*=}
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            while [ $# -gt 0 ]; do
                positional+=("$1")
                shift
            done
            ;;
        -*)
            usage >&2
            exit 2
            ;;
        *)
            positional+=("$1")
            shift
            ;;
    esac
done

if [ "${#positional[@]}" -ne 1 ]; then
    usage >&2
    exit 2
fi

deb_arg=${positional[0]}

script_path=$(realpath "$0" 2>/dev/null) || die "realpath is required to resolve this script"

# --- resolve the inputs ------------------------------------------------------

if [ -n "$source_dir_arg" ]; then
    [ -d "$source_dir_arg" ] || die "source tree $source_dir_arg does not exist; pass --source-dir DIR"
    source_dir=$(realpath "$source_dir_arg")
else
    script_dir=$(dirname "$script_path")
    source_dir=$(realpath "$script_dir/../..") || die "cannot resolve the default source tree; pass --source-dir DIR"
fi

for required in debian/control debian/changelog CMakeLists.txt; do
    [ -f "$source_dir/$required" ] || die "source tree $source_dir has no $required; pass --source-dir DIR"
done

[ -e "$deb_arg" ] || die "package $deb_arg does not exist"
[ -f "$deb_arg" ] || die "package $deb_arg is not a regular file"
[ -r "$deb_arg" ] || die "package $deb_arg is not readable"
deb=$(realpath "$deb_arg")

missing_tools=()
for tool in dpkg-deb dpkg dpkg-query dpkg-shlibdeps dpkg-parsechangelog readelf awk sed grep tar mktemp realpath; do
    if ! have "$tool"; then
        missing_tools+=("$tool")
    fi
done
if [ "${#missing_tools[@]}" -gt 0 ]; then
    die "missing prerequisites: $(join "${missing_tools[@]}") (install dpkg-dev and binutils)"
fi

if ! dpkg-deb --info "$deb" >/dev/null 2>&1; then
    die "not a Debian package: $deb"
fi

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/verify-deb.XXXXXX") || die "cannot create a temporary directory"

# --- header ------------------------------------------------------------------

sha=$(sha256sum "$deb" 2>/dev/null | awk '{ print $1 }')
[ -n "$sha" ] || sha="(sha256sum is unavailable)"

os_id=unknown
os_version=unknown
if [ -r /etc/os-release ]; then
    # shellcheck disable=SC1091
    os_id=$(. /etc/os-release && printf '%s' "${ID:-unknown}")
    # shellcheck disable=SC1091
    os_version=$(. /etc/os-release && printf '%s' "${VERSION_ID:-unknown}")
fi

arch=$(dpkg --print-architecture)
dpkg_line=$(dpkg --version 2>/dev/null | head -n 1)

priv=()
if [ "$(id -u)" -eq 0 ]; then
    install_mode=root
elif have sudo && sudo -n true >/dev/null 2>&1; then
    priv=(sudo -n)
    install_mode="sudo -n"
else
    install_mode="skipped (not root and sudo -n is unavailable)"
fi

# Exact command to re-run the gate, built from the arguments actually used:
# "sudo" only when the gate itself goes through sudo -n, and --source-dir only
# when the caller passed it.
rerun_parts=()
if [ "${#priv[@]}" -gt 0 ]; then
    rerun_parts+=(sudo)
fi
rerun_parts+=("$script_path")
if [ -n "$source_dir_arg" ]; then
    rerun_parts+=(--source-dir "$source_dir")
fi
rerun_parts+=("$deb")
rerun_cmd=$(quote_cmd "${rerun_parts[@]}")

printf '== verify-deb: %s\n' "$deb"
printf '== sha256: %s\n' "$sha"
printf '== source: %s\n' "$source_dir"
printf '== host: %s %s\n' "$os_id" "$os_version"
printf '== architecture: %s\n' "$arch"
printf '== dpkg: %s\n' "$dpkg_line"
printf '== install mode: %s\n' "$install_mode"

# --- expected values derived from the source tree and the host ---------------

changelog_version=$(dpkg-parsechangelog -l "$source_dir/debian/changelog" -S Version 2>/dev/null)
changelog_maintainer=$(dpkg-parsechangelog -l "$source_dir/debian/changelog" -S Maintainer 2>/dev/null)
source_package=$(dpkg-parsechangelog -l "$source_dir/debian/changelog" -S Source 2>/dev/null)
[ -n "$changelog_version" ] || die "cannot read Version from $source_dir/debian/changelog"
[ -n "$changelog_maintainer" ] || die "cannot read Maintainer from $source_dir/debian/changelog"
[ -n "$source_package" ] || die "cannot read Source from $source_dir/debian/changelog"

cmake_version=$(sed -n 's/^[[:space:]]*project([^)]*VERSION[[:space:]]\+\([0-9][0-9.]*\).*/\1/p' "$source_dir/CMakeLists.txt" | head -n 1)
[ -n "$cmake_version" ] || die "cannot find 'project(qtranslate VERSION X.Y.Z)' in $source_dir/CMakeLists.txt"

# --- extract once ------------------------------------------------------------

if ! dpkg-deb -R "$deb" "$tmpdir/root" >/dev/null 2>&1; then
    die "cannot extract $deb"
fi

# Normalized listing: "<mode> <path>" per entry. Paths have no spaces in this
# package, so the last field is the path.
if ! dpkg-deb --contents "$deb" >"$tmpdir/contents.raw" 2>/dev/null; then
    die "dpkg-deb --contents failed for $deb"
fi
awk '{ print $1 " " $NF }' "$tmpdir/contents.raw" >"$tmpdir/contents.txt"

listing_mode() {
    awk -v p="$1" '$2 == p { print $1; exit }' "$tmpdir/contents.txt"
}

listing_has() {
    awk -v p="$1" '$2 == p { found = 1 } END { exit(found ? 0 : 1) }' "$tmpdir/contents.txt"
}

# Recompute shlibs:Depends with the source tree's Build-Depends in a throwaway
# debian/control. Without the real Build-Depends dpkg-shlibdeps silently drops
# the symbols files' Build-Depends-Package link and lowers the Qt floors.
compute_shlibdeps() {
    local root=$1
    mkdir -p "$tmpdir/shlibdeps/debian"
    cp "$source_dir/debian/control" "$tmpdir/shlibdeps/debian/control"
    SHLIBDEPS_OUTPUT=$(cd "$tmpdir/shlibdeps" && dpkg-shlibdeps -O -e"$root/usr/bin/qtranslate" 2>"$tmpdir/shlibdeps.stderr")
    SHLIBDEPS_STATUS=$?
    SHLIBDEPS_DEPENDS=$(printf '%s\n' "$SHLIBDEPS_OUTPUT" | sed -n 's/^shlibs:Depends=//p' | head -n 1)
}

# --- self-test ---------------------------------------------------------------

SELFTEST_PASSED=0
SELFTEST_FAILED=0
SELFTEST_SKIPPED=0

selftest_run_case() {
    local name=$1 expected_id=$2 extra_pattern=$3 mutant=$4
    local out rc ids ok=1

    out=$(VERIFY_DEB_NO_INSTALL=1 "$script_path" --source-dir "$source_dir" "$mutant" 2>&1)
    rc=$?
    ids=$(printf '%s\n' "$out" | sed -n 's/^FAIL \([^:]*\):.*/\1/p' | paste -sd, -)
    [ -n "$ids" ] || ids="(exit $rc, no failing check)"

    if ! printf '%s\n' "$out" | grep -q "^FAIL ${expected_id}:"; then
        ok=0
    fi
    if [ "$ok" -eq 1 ] && [ -n "$extra_pattern" ]; then
        if ! printf '%s\n' "$out" | grep "^FAIL ${expected_id}:" | grep -qF "$extra_pattern"; then
            ok=0
        fi
    fi

    if [ "$ok" -eq 1 ]; then
        printf 'SELFTEST PASS %s\n' "$name"
        SELFTEST_PASSED=$((SELFTEST_PASSED + 1))
    else
        printf 'SELFTEST FAIL %s: expected %s, got %s\n' "$name" "$expected_id" "$ids"
        printf '%s\n' "$out" | tail -n 20 | sed 's/^/    /'
        SELFTEST_FAILED=$((SELFTEST_FAILED + 1))
    fi
}

selftest_rebuild() {
    local dir=$1 out=$2
    dpkg-deb --build --root-owner-group "$dir" "$out" >/dev/null 2>&1
}

run_self_test() {
    local case_deb mutant multiarch hardcoded_floor can_construct entry

    printf '== self-test mode: four mutated copies, install roundtrip disabled\n'

    # missing-file: remove a required desktop entry, contents-app-files must fail.
    printf -- '-- self-test case missing-file\n'
    mutant="$tmpdir/neg-missing-file"
    case_deb="$tmpdir/missing-file.deb"
    dpkg-deb -R "$deb" "$mutant" >/dev/null 2>&1 || die "self-test: cannot extract $deb"
    rm -f "$mutant/usr/share/applications/qtranslate.desktop"
    selftest_rebuild "$mutant" "$case_deb" || die "self-test: cannot rebuild the missing-file copy"
    selftest_run_case missing-file contents-app-files "" "$case_deb"

    # bundle-file: add a file under usr/lib, contents-bundle must fail.
    printf -- '-- self-test case bundle-file\n'
    mutant="$tmpdir/neg-bundle-file"
    case_deb="$tmpdir/bundle-file.deb"
    multiarch=$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null) || multiarch=""
    [ -n "$multiarch" ] || multiarch=x86_64-linux-gnu
    dpkg-deb -R "$deb" "$mutant" >/dev/null 2>&1 || die "self-test: cannot extract $deb"
    mkdir -p "$mutant/usr/lib/$multiarch"
    : >"$mutant/usr/lib/$multiarch/libbundle-test.so"
    selftest_rebuild "$mutant" "$case_deb" || die "self-test: cannot rebuild the bundle-file copy"
    selftest_run_case bundle-file contents-bundle "" "$case_deb"

    # edited-depends: drop the first Depends entry, depends-shlibs must report
    # it as missing.
    printf -- '-- self-test case edited-depends\n'
    mutant="$tmpdir/neg-edited-depends"
    case_deb="$tmpdir/edited-depends.deb"
    dpkg-deb -R "$deb" "$mutant" >/dev/null 2>&1 || die "self-test: cannot extract $deb"
    sed -i 's/^Depends: [^,]*, /Depends: /' "$mutant/DEBIAN/control"
    selftest_rebuild "$mutant" "$case_deb" || die "self-test: cannot rebuild the edited-depends copy"
    selftest_run_case edited-depends depends-shlibs "missing in Depends" "$case_deb"

    # hardcoded-dep: append a fixed libxtst6 floor. Only impossible when the
    # host's own shlibs already demand exactly that floor.
    printf -- '-- self-test case hardcoded-dep\n'
    compute_shlibdeps "$tmpdir/root"
    hardcoded_floor='libxtst6 (>= 2:1.2.5)'
    can_construct=1
    if [ -n "$SHLIBDEPS_DEPENDS" ]; then
        split_list "$SHLIBDEPS_DEPENDS"
        for entry in "${REPLY_LIST[@]}"; do
            if [ "$entry" = "$hardcoded_floor" ]; then
                can_construct=0
            fi
        done
    fi
    if [ "$can_construct" -eq 0 ]; then
        printf 'SELFTEST SKIP hardcoded-dep: the host shlibs already require %s, so this case cannot be constructed\n' "$hardcoded_floor"
        SELFTEST_SKIPPED=$((SELFTEST_SKIPPED + 1))
    elif [ -z "$SHLIBDEPS_DEPENDS" ]; then
        printf 'SELFTEST SKIP hardcoded-dep: dpkg-shlibdeps produced no Depends on the host, so the case cannot be constructed\n'
        SELFTEST_SKIPPED=$((SELFTEST_SKIPPED + 1))
    else
        mutant="$tmpdir/neg-hardcoded-dep"
        case_deb="$tmpdir/hardcoded-dep.deb"
        dpkg-deb -R "$deb" "$mutant" >/dev/null 2>&1 || die "self-test: cannot extract $deb"
        sed -i "/^Depends: /s/\$/, $hardcoded_floor/" "$mutant/DEBIAN/control"
        selftest_rebuild "$mutant" "$case_deb" || die "self-test: cannot rebuild the hardcoded-dep copy"
        selftest_run_case hardcoded-dep depends-shlibs "unexpected in Depends" "$case_deb"
    fi

    printf '== self-test summary: %d cases, %d passed, %d failed, %d skipped\n' \
        "$((SELFTEST_PASSED + SELFTEST_FAILED + SELFTEST_SKIPPED))" \
        "$SELFTEST_PASSED" "$SELFTEST_FAILED" "$SELFTEST_SKIPPED"
    if [ "${#skipped_reasons[@]}" -gt 0 ]; then
        printf '== skipped checks\n'
        for entry in "${skipped_reasons[@]}"; do
            printf '   %s\n' "$entry"
        done
    fi
    if [ "$SELFTEST_FAILED" -gt 0 ]; then
        return 1
    fi
    return 0
}

if [ "$self_test" -eq 1 ]; then
    run_self_test
    exit $?
fi

# --- content checks ----------------------------------------------------------

problems=()

while IFS= read -r spec; do
    mode=${spec%% *}
    path=${spec#* }
    got=$(listing_mode "$path")
    if [ -z "$got" ]; then
        problems+=("missing $path")
    elif [ "$got" != "$mode" ]; then
        problems+=("$path has mode $got, expected $mode")
    fi
done <<'EOF'
-rwxr-xr-x ./usr/bin/qtranslate
-rw-r--r-- ./usr/share/applications/qtranslate.desktop
-rw-r--r-- ./usr/share/icons/hicolor/256x256/apps/qtranslate.png
EOF
if [ "${#problems[@]}" -eq 0 ]; then
    pass contents-app-files
else
    fail contents-app-files "expected usr/bin/qtranslate, the desktop entry and the 256x256 icon with their modes; got: $(join "${problems[@]}")"
fi

problems=()
# dh_compress gzips regular files over 4 KiB under usr/share/doc, so the
# LICENSE that CMake installs arrives here as LICENSE.gz (copyright is exempt).
for path in \
    ./usr/share/doc/qtranslate/copyright \
    ./usr/share/doc/qtranslate/LICENSE.gz \
    ./usr/share/doc/qtranslate/changelog.gz; do
    if ! listing_has "$path"; then
        problems+=("missing $path")
    fi
done
if listing_has ./usr/share/doc/qtranslate/changelog.Debian.gz; then
    problems+=("native package must not ship ./usr/share/doc/qtranslate/changelog.Debian.gz")
fi
if [ "${#problems[@]}" -eq 0 ]; then
    pass contents-docs
else
    fail contents-docs "expected copyright, LICENSE.gz and changelog.gz only (README.Debian optional); got: $(join "${problems[@]}")"
fi

bundle_hits=$(awk '
    $2 == "./usr/bin/qt.conf" { print $2; next }
    $2 ~ /^\.\/usr\/(share\/qt6|lib|plugins|translations)(\/|$)/ { print $2 }
' "$tmpdir/contents.txt")
if [ -z "$bundle_hits" ]; then
    pass contents-bundle
else
    fail contents-bundle "expected no bundled Qt/runtime paths; got: $(printf '%s' "$bundle_hits" | paste -sd, -)"
fi

{
    printf '%s\n' \
        ./usr/bin/qtranslate \
        ./usr/share/applications/qtranslate.desktop \
        ./usr/share/icons/hicolor/256x256/apps/qtranslate.png \
        ./usr/share/doc/qtranslate/copyright \
        ./usr/share/doc/qtranslate/LICENSE.gz \
        ./usr/share/doc/qtranslate/changelog.gz \
        ./usr/share/doc/qtranslate/README.Debian
} >"$tmpdir/allowlist.txt"
extras=$(awk 'NR == FNR { allowed[$0] = 1; next } $1 ~ /^-/ && !($2 in allowed) { print $2 }' \
    "$tmpdir/allowlist.txt" "$tmpdir/contents.txt")
if [ -z "$extras" ]; then
    pass contents-unexpected
else
    fail contents-unexpected "expected no regular files outside the allowlist; got: $(printf '%s' "$extras" | paste -sd, -)"
fi

# --- metadata checks ---------------------------------------------------------

package_field=$(dpkg-deb --field "$deb" Package 2>/dev/null)
version_field=$(dpkg-deb --field "$deb" Version 2>/dev/null)
arch_field=$(dpkg-deb --field "$deb" Architecture 2>/dev/null)
maintainer_field=$(dpkg-deb --field "$deb" Maintainer 2>/dev/null)
description_field=$(dpkg-deb --field "$deb" Description 2>/dev/null)
depends_field=$(dpkg-deb --field "$deb" Depends 2>/dev/null)

problems=()
if [ "$package_field" != "$source_package" ]; then
    problems+=("Package is '$package_field', expected '$source_package'")
fi
if [ "$arch_field" != "$arch" ]; then
    problems+=("Architecture is '$arch_field', expected '$arch'")
fi
if [ -z "$maintainer_field" ]; then
    problems+=("Maintainer is empty")
elif [ "$maintainer_field" != "$changelog_maintainer" ]; then
    problems+=("Maintainer is '$maintainer_field', expected '$changelog_maintainer'")
fi
if [ -z "$description_field" ]; then
    problems+=("Description is empty")
fi
if [ "${#problems[@]}" -eq 0 ]; then
    pass control-fields
else
    fail control-fields "expected Package=$source_package, Architecture=$arch, the changelog Maintainer and a non-empty Description; got: $(join "${problems[@]}")"
fi

problems=()
if [ "$version_field" != "$changelog_version" ]; then
    problems+=("Version is '$version_field', expected changelog version '$changelog_version'")
fi
upstream_version=${version_field%%+*}
if [ "$upstream_version" != "$cmake_version" ]; then
    problems+=("upstream version '$upstream_version' does not equal CMake project version '$cmake_version'")
fi
if [ "${#problems[@]}" -eq 0 ]; then
    pass control-version
else
    fail control-version "expected Version=$changelog_version with upstream $cmake_version; got: $(join "${problems[@]}")"
fi

members_raw=$(dpkg-deb --ctrl-tarfile "$deb" 2>/dev/null | tar -t 2>/dev/null)
members=()
while IFS= read -r member; do
    member=${member#./}
    [ -n "$member" ] || continue
    members+=("$member")
done <<<"$members_raw"
problems=()
for member in "${members[@]}"; do
    case "$member" in
        control|md5sums) ;;
        preinst|postinst|prerm|postrm|config|triggers|conffiles|shlibs|symbols)
            problems+=("maintainer script or debhelper metadata '$member' is not allowed")
            ;;
        *)
            problems+=("unexpected control archive member '$member'")
            ;;
    esac
done
for member in control md5sums; do
    if ! contains "$member" "${members[@]}"; then
        problems+=("missing control archive member '$member'")
    fi
done
if [ "${#problems[@]}" -eq 0 ]; then
    pass control-maintainer-scripts
else
    fail control-maintainer-scripts "expected only ./control and ./md5sums in the control archive; got: $(join "${problems[@]}")"
fi

# --- dependency checks -------------------------------------------------------

split_list "$depends_field"
depends_actual=("${REPLY_LIST[@]}")

compute_shlibdeps "$tmpdir/root"
if [ "$SHLIBDEPS_STATUS" -ne 0 ]; then
    fail depends-shlibs "dpkg-shlibdeps failed (exit $SHLIBDEPS_STATUS): $(tail -n 3 "$tmpdir/shlibdeps.stderr" 2>/dev/null | paste -sd' ' -)"
elif [ -z "$SHLIBDEPS_DEPENDS" ]; then
    fail depends-shlibs "dpkg-shlibdeps produced no shlibs:Depends (exit 0), so there is nothing to compare; stderr: $(tail -n 3 "$tmpdir/shlibdeps.stderr" 2>/dev/null | paste -sd' ' -)"
else
    split_list "$SHLIBDEPS_DEPENDS"
    depends_expected=("${REPLY_LIST[@]}")
    missing=()
    unexpected=()
    for entry in "${depends_expected[@]}"; do
        if ! contains "$entry" "${depends_actual[@]}"; then
            missing+=("$entry")
        fi
    done
    for entry in "${depends_actual[@]}"; do
        if ! contains "$entry" "${depends_expected[@]}"; then
            unexpected+=("$entry")
        fi
    done
    if [ "${#missing[@]}" -eq 0 ] && [ "${#unexpected[@]}" -eq 0 ]; then
        pass depends-shlibs
    else
        parts=()
        if [ "${#missing[@]}" -gt 0 ]; then
            parts+=("missing in Depends: $(join "${missing[@]}")")
        fi
        if [ "${#unexpected[@]}" -gt 0 ]; then
            parts+=("unexpected in Depends: $(join "${unexpected[@]}")")
        fi
        message="expected [$SHLIBDEPS_DEPENDS]; got [$depends_field]; $(join "${parts[@]}")"
        missing_glib_old=0
        missing_glib_t64=0
        unexpected_glib_old=0
        unexpected_glib_t64=0
        for entry in "${missing[@]}"; do
            case "${entry%% *}" in
                libglib2.0-0) missing_glib_old=1 ;;
                libglib2.0-0t64) missing_glib_t64=1 ;;
            esac
        done
        for entry in "${unexpected[@]}"; do
            case "${entry%% *}" in
                libglib2.0-0) unexpected_glib_old=1 ;;
                libglib2.0-0t64) unexpected_glib_t64=1 ;;
            esac
        done
        glib_hint=0
        if [ "$missing_glib_old" -eq 1 ] && [ "$unexpected_glib_t64" -eq 1 ]; then
            glib_hint=1
        fi
        if [ "$missing_glib_t64" -eq 1 ] && [ "$unexpected_glib_old" -eq 1 ]; then
            glib_hint=1
        fi
        if [ "$glib_hint" -eq 1 ]; then
            message="$message; the glib package name differs (libglib2.0-0 vs libglib2.0-0t64), so the .deb was built on a different distribution and must be verified on the host that produced it"
        fi
        fail depends-shlibs "$message"
    fi
fi

readelf_err="$tmpdir/readelf.err"
needed_dynamic=$(readelf -d "$tmpdir/root/usr/bin/qtranslate" 2>"$readelf_err")
readelf_status=$?

if [ "$readelf_status" -ne 0 ]; then
    readelf_error=$(tail -n 2 "$readelf_err" | paste -sd' ' -)
    fail depends-glib-family "readelf -d failed (exit $readelf_status): ${readelf_error:-no output}"
elif printf '%s\n' "$needed_dynamic" | grep -qF 'libglib-2.0.so.0'; then
    if dpkg-query -W -f='${Status}' libglib2.0-0t64 2>/dev/null | grep -q 'install ok installed'; then
        glib_family=libglib2.0-0t64
        glib_other=libglib2.0-0
    else
        glib_family=libglib2.0-0
        glib_other=libglib2.0-0t64
    fi
    family_present=0
    other_present=0
    for entry in "${depends_actual[@]}"; do
        if [ "${entry%% *}" = "$glib_family" ]; then
            family_present=1
        fi
        if [ "${entry%% *}" = "$glib_other" ]; then
            other_present=1
        fi
    done
    if [ "$family_present" -eq 1 ] && [ "$other_present" -eq 0 ]; then
        pass depends-glib-family
    else
        fail depends-glib-family "expected $glib_family in Depends and $glib_other absent; got [$depends_field]"
    fi
else
    pass depends-glib-family
    note depends-glib-family "the binary does not link libglib-2.0.so.0, so no glib dependency is expected"
fi

if [ "$readelf_status" -ne 0 ]; then
    fail depends-libsecret "readelf -d failed (exit $readelf_status): ${readelf_error:-no output}"
elif printf '%s\n' "$needed_dynamic" | grep -qF 'libsecret-1.so.0'; then
    secret_present=0
    for entry in "${depends_actual[@]}"; do
        if [ "${entry%% *}" = "libsecret-1-0" ]; then
            secret_present=1
        fi
    done
    if [ "$secret_present" -eq 1 ]; then
        pass depends-libsecret
    else
        fail depends-libsecret "the binary links libsecret-1.so.0, so libsecret-1-0 must be in Depends; got [$depends_field]"
    fi
else
    problems=()
    for entry in "${depends_actual[@]}"; do
        case "${entry%% *}" in
            libsecret-1-0*) problems+=("$entry") ;;
        esac
    done
    if [ "${#problems[@]}" -eq 0 ]; then
        pass depends-libsecret
    else
        fail depends-libsecret "the binary does not link libsecret-1.so.0, so no libsecret-1-0 entry is allowed; got: $(join "${problems[@]}")"
    fi
fi

# --- runtime checks on the extracted tree ------------------------------------

desktop_entry="$tmpdir/root/usr/share/applications/qtranslate.desktop"
if ! have desktop-file-validate; then
    die "desktop-file-validate is missing (install desktop-file-utils)"
fi
if validate_out=$(desktop-file-validate "$desktop_entry" 2>&1); then
    pass desktop-file-validate
else
    fail desktop-file-validate "desktop-file-validate reported: ${validate_out:-no output}"
fi

expected_version_line="QTranslate $cmake_version"
extracted_bin="$tmpdir/root/usr/bin/qtranslate"
if have xvfb-run; then
    if version_out=$(xvfb-run -a "$extracted_bin" --version 2>"$tmpdir/run-version.err"); then
        version_line=$(printf '%s\n' "$version_out" | last_line)
        if [ "$version_line" = "$expected_version_line" ]; then
            pass run-version
        else
            fail run-version "expected '$expected_version_line'; got '$(printf '%s' "$version_out" | paste -sd' ' -)'"
        fi
    else
        fail run-version "qtranslate --version failed: $(tail -n 3 "$tmpdir/run-version.err" | paste -sd' ' -)"
    fi
else
    note run-version "xvfb-run is not available, running the binary directly"
    if version_out=$("$extracted_bin" --version 2>"$tmpdir/run-version.err"); then
        version_line=$(printf '%s\n' "$version_out" | last_line)
        if [ "$version_line" = "$expected_version_line" ]; then
            pass run-version
            note run-version "ran without xvfb-run, so a working DISPLAY was used"
        else
            fail run-version "expected '$expected_version_line'; got '$(printf '%s' "$version_out" | paste -sd' ' -)'"
        fi
    else
        skip run-version "xvfb-run is unavailable and the direct run failed: $(tail -n 3 "$tmpdir/run-version.err" | paste -sd' ' -)"
    fi
fi

# --- install roundtrip -------------------------------------------------------

if [ "${VERIFY_DEB_NO_INSTALL:-0}" = 1 ]; then
    skip install-roundtrip "a --self-test child run never installs the mutated package"
elif [ -z "$package_field" ]; then
    skip install-roundtrip "the package has no Package field, so the gate cannot tell what it would install or remove"
elif [ "$package_field" != "$source_package" ]; then
    skip install-roundtrip "package '$package_field' is not '$source_package'; the gate refuses to install or remove a foreign package"
elif [ "${#priv[@]}" -eq 0 ] && [ "$(id -u)" -ne 0 ]; then
    skip install-roundtrip "not root and sudo -n is unavailable"
    warn install-roundtrip "re-run as root or with passwordless sudo to exercise dpkg -i/dpkg -r: $rerun_cmd"
elif dpkg -s "$package_field" 2>/dev/null | grep -Eq '^Status: [^ ]+ ok installed$'; then
    skip install-roundtrip "$package_field is already installed, so the check would remove the user's installation"
    warn install-roundtrip "remove the existing $package_field installation (and any hold) first and re-run: $rerun_cmd"
else
    problems=()
    installed_package=$package_field
    installed_flag=1
    if ! install_out=$(as_root dpkg -i "$deb" 2>&1); then
        fail install-roundtrip "dpkg -i failed: $(printf '%s\n' "$install_out" | tail -n 3 | paste -sd' ' -)"
        installed_flag=1
    else
        installed_listing=$(dpkg -L "$installed_package" 2>/dev/null)
        for installed_file in \
            /usr/bin/qtranslate \
            /usr/share/applications/qtranslate.desktop \
            /usr/share/icons/hicolor/256x256/apps/qtranslate.png \
            /usr/share/doc/qtranslate/copyright \
            /usr/share/doc/qtranslate/changelog.gz; do
            if ! printf '%s\n' "$installed_listing" | grep -qxF "$installed_file"; then
                problems+=("dpkg -L does not list $installed_file")
            fi
        done

        if ! installed_validate=$(desktop-file-validate /usr/share/applications/qtranslate.desktop 2>&1); then
            problems+=("installed desktop-file-validate failed: ${installed_validate:-no output}")
        fi

        installed_version_ok=1
        installed_version=""
        if have xvfb-run; then
            if ! installed_version=$(xvfb-run -a qtranslate --version 2>"$tmpdir/installed-version.err"); then
                installed_version_ok=0
                problems+=("installed qtranslate --version failed: $(tail -n 3 "$tmpdir/installed-version.err" | paste -sd' ' -)")
                installed_version=""
            fi
        else
            if ! installed_version=$(qtranslate --version 2>"$tmpdir/installed-version.err"); then
                installed_version_ok=0
                problems+=("installed qtranslate --version failed: $(tail -n 3 "$tmpdir/installed-version.err" | paste -sd' ' -)")
                installed_version=""
            fi
        fi
        if [ "$installed_version_ok" -eq 1 ]; then
            installed_version=$(printf '%s\n' "$installed_version" | last_line)
            if [ -z "$installed_version" ]; then
                problems+=("installed qtranslate --version printed no version line, expected '$expected_version_line'")
            elif [ "$installed_version" != "$expected_version_line" ]; then
                problems+=("installed qtranslate --version printed '$installed_version', expected '$expected_version_line'")
            fi
        fi

        if as_root dpkg -r "$installed_package" >/dev/null 2>&1; then
            installed_flag=0
        else
            problems+=("dpkg -r $installed_package failed")
        fi

        for installed_file in \
            /usr/bin/qtranslate \
            /usr/share/applications/qtranslate.desktop \
            /usr/share/icons/hicolor/256x256/apps/qtranslate.png \
            /usr/share/doc/qtranslate; do
            if [ -e "$installed_file" ]; then
                problems+=("leftover after dpkg -r: $installed_file")
            fi
        done

        if [ "${#problems[@]}" -eq 0 ]; then
            pass install-roundtrip
        else
            fail install-roundtrip "expected dpkg -i, dpkg -L, desktop-file-validate, qtranslate --version and dpkg -r to succeed and leave no files; got: $(join "${problems[@]}")"
        fi
    fi
fi

# --- build dependencies ------------------------------------------------------

if ! have dpkg-checkbuilddeps; then
    fail build-deps "dpkg-checkbuilddeps is missing (install dpkg-dev)"
else
    if checkbuilddeps_out=$(cd "$source_dir" && dpkg-checkbuilddeps 2>&1); then
        if [ -z "$checkbuilddeps_out" ]; then
            pass build-deps
        else
            fail build-deps "dpkg-checkbuilddeps exited 0 but printed output: $(printf '%s\n' "$checkbuilddeps_out" | paste -sd' ' -)"
        fi
    else
        fail build-deps "dpkg-checkbuilddeps failed: $(printf '%s\n' "$checkbuilddeps_out" | paste -sd' ' -)"
    fi
fi

apt_sim_note="dry run against the local debian/control and the installed packages, not a fresh-host resolution; the Astra main repositories have no active deb-src entries (only commented ones and Steam's own), and the CI image removes /var/lib/apt/lists"
if ! have apt-get; then
    skip apt-build-dep "apt-get is not available in this environment"
else
    if apt_out=$(cd "$source_dir" && apt-get -s build-dep . 2>&1); then
        pass apt-build-dep
        note apt-build-dep "$apt_sim_note"
        would_install=$(printf '%s\n' "$apt_out" | grep -E '^([0-9]+ upgraded|The following NEW packages will be installed)' | tail -n 1)
        if [ -n "$would_install" ]; then
            note apt-build-dep "apt says: $would_install"
        fi
    else
        apt_rc=$?
        warn apt-build-dep "the simulation failed (exit $apt_rc) and is not a gate failure: $apt_sim_note"
        printf '%s\n' "$apt_out" | tail -n 20 | sed 's/^/    /'
    fi
fi

# --- summary ----------------------------------------------------------------

total_checks=$((passes + failures + skips))
printf '== summary: %d checks, %d passed, %d failed, %d skipped\n' \
    "$total_checks" "$passes" "$failures" "$skips"
if [ "${#skipped_reasons[@]}" -gt 0 ]; then
    printf '== skipped checks\n'
    for reason in "${skipped_reasons[@]}"; do
        printf '   %s\n' "$reason"
    done
fi

if [ "$failures" -gt 0 ]; then
    exit 1
fi
exit 0
