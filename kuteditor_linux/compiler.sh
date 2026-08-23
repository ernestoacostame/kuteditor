#!/usr/bin/env bash
# =============================================================================
# compiler.sh
# Builds KutEditor as a .deb for Debian and Debian-based systems
#
# Usage: ./compiler.sh [VERSION]
#        ./compiler.sh          -> builds VERSION_DEFAULT
#        ./compiler.sh 2.48     -> builds that version number
#
# Result: <project directory>/kuteditor_<VERSION>-<REVISION>_amd64.deb
#
# Notes:
#   - Upstream publishes no release tarballs, so the source comes from a
#     shallow git clone and the orig tarball is made from it here.
#   - Only the kuteditor_linux/ subdirectory of the repository is packaged.
#     The macOS tree is not part of this package.
#   - Whisper transcription is disabled: whisper.cpp is not packaged in
#     Debian, and enabling it would mean building it from source on the
#     user's machine.
#   - Audio goes through the JACK API, which PipeWire implements. The
#     package depends on pipewire-jack first, so no standalone JACK server
#     is pulled in on a PipeWire system.
#   - Needs an internet connection for the clone and the build deps.
# =============================================================================

set -e

# -----------------------------------------------------------------------------
# CONFIGURATION
# -----------------------------------------------------------------------------
VERSION_DEFAULT="2.48"
VERSION="${1:-$VERSION_DEFAULT}"
REVISION="1"

UPSTREAM_REPO="https://github.com/ernestoacostame/kuteditor.git"
UPSTREAM_SUBDIR="kuteditor_linux"

# Directory of this script. The debian/ that gets packaged comes from here, so
# what is built is always what is versioned in the project.
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

BUILD_ROOT="${PROJECT_DIR}/build"
SOURCE_DIR="${BUILD_ROOT}/kuteditor-${VERSION}"
CLONE_DIR="${BUILD_ROOT}/upstream"
ORIG_TARBALL="${BUILD_ROOT}/kuteditor_${VERSION}.orig.tar.xz"

# Tools needed to build a Debian source package.
PACKAGING_TOOLS=(build-essential devscripts debhelper dpkg-dev equivs quilt git)

# -----------------------------------------------------------------------------
# HELPERS
# -----------------------------------------------------------------------------
error() {
    echo "ERROR: $*" >&2
    exit 1
}

step() {
    echo ""
    echo "==> $*"
}

# -----------------------------------------------------------------------------
# CHECKS
# -----------------------------------------------------------------------------
[[ -f "${PROJECT_DIR}/debian/control" ]] || error "debian/control not found next to this script"

if [[ "$(dpkg --print-architecture)" != "amd64" ]]; then
    echo "WARNING: this has only been tested on amd64"
fi

# -----------------------------------------------------------------------------
# PACKAGING TOOLS
# -----------------------------------------------------------------------------
step "Installing packaging tools"
sudo apt install -y "${PACKAGING_TOOLS[@]}"

# -----------------------------------------------------------------------------
# SOURCE
# -----------------------------------------------------------------------------
step "Fetching upstream source"
mkdir -p "${BUILD_ROOT}"

if [[ -d "${CLONE_DIR}/.git" ]]; then
    echo "Reusing the existing clone in ${CLONE_DIR}"
    git -C "${CLONE_DIR}" fetch --depth 1 origin
    git -C "${CLONE_DIR}" reset --hard origin/HEAD
else
    rm -rf "${CLONE_DIR}"
    git clone --depth 1 "${UPSTREAM_REPO}" "${CLONE_DIR}"
fi

[[ -d "${CLONE_DIR}/${UPSTREAM_SUBDIR}" ]] || error "${UPSTREAM_SUBDIR}/ not found in the clone"

step "Preparing the source tree"
rm -rf "${SOURCE_DIR}"
mkdir -p "${SOURCE_DIR}"
# Only the Linux tree is packaged, and without its git metadata.
tar -C "${CLONE_DIR}/${UPSTREAM_SUBDIR}" --exclude=.git -cf - . \
    | tar -C "${SOURCE_DIR}" -xf -

step "Creating the orig tarball"
rm -f "${ORIG_TARBALL}"
tar -C "${BUILD_ROOT}" -cJf "${ORIG_TARBALL}" "kuteditor-${VERSION}"

# -----------------------------------------------------------------------------
# DEBIAN DIRECTORY
# -----------------------------------------------------------------------------
step "Adding the debian directory"
cp -a "${PROJECT_DIR}/debian" "${SOURCE_DIR}/debian"

# Keep the changelog version in step with the requested one, so building a
# different version number does not need the changelog edited by hand.
sed -i "1s|^kuteditor (.*)|kuteditor (${VERSION}-${REVISION})|" \
    "${SOURCE_DIR}/debian/changelog"

# -----------------------------------------------------------------------------
# BUILD DEPENDENCIES
# -----------------------------------------------------------------------------
step "Installing build dependencies"
cd "${SOURCE_DIR}"
sudo apt-get remove -y kuteditor-build-deps 2>/dev/null || true
sudo mk-build-deps -i -r debian/control -t "apt-get -y --reinstall"

# -----------------------------------------------------------------------------
# BUILD
# -----------------------------------------------------------------------------
step "Building the package"
dpkg-buildpackage -us -uc -b

# -----------------------------------------------------------------------------
# RESULT
# -----------------------------------------------------------------------------
DEB="${BUILD_ROOT}/kuteditor_${VERSION}-${REVISION}_$(dpkg --print-architecture).deb"
[[ -f "${DEB}" ]] || error "The .deb was not produced where it was expected: ${DEB}"

mv "${DEB}" "${PROJECT_DIR}/"
RESULT="${PROJECT_DIR}/$(basename "${DEB}")"

step "Done"
echo "Package: ${RESULT}"
echo ""
echo "Install it with:"
echo "  sudo apt install ${RESULT}"
echo ""
echo "The build tree is kept in ${BUILD_ROOT} so a failed run can be inspected."

# The packaging steps run under sudo and leave root-owned files behind.
sudo chown -R "$(id -un):$(id -gn)" "${BUILD_ROOT}" "${PROJECT_DIR}" 2>/dev/null || true
