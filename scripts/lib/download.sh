# Shared by install-deb.sh and run-appimage.sh: finds and downloads a package.
#
#   download_package <deb|appimage> <source> <empty directory>
#
# <source> is "latest" (newest GitHub release), a release version ("0.1.0"
# or "v0.1.0"), or "build" (only for deb: the package CI built for the latest
# green merge to main; needs the GitHub CLI, signed in). Prints the
# downloaded file.

GDS_REPO="${GDRIVE_SYNC_REPO:-anjankow/gdrive-sync}"
GDS_API="${GDRIVE_SYNC_API:-https://api.github.com}"

die() {
    echo "error: $*" >&2
    exit 1
}

have_gh() {
    command -v gh >/dev/null && gh auth status >/dev/null 2>&1
}

download_package() {
    local kind="$1" source="$2" dir="$3" pattern artifact
    case "$kind" in
        deb) pattern='*.deb' artifact='gdrive-sync-deb' ;;
        appimage) pattern='*.AppImage' artifact='' ;;
        *) die "unknown package kind: $kind" ;;
    esac
    mkdir -p "$dir"

    if [ "$source" = "build" ]; then
        [ -n "$artifact" ] || die "AppImages are only built for releases; choose latest or a version"
        have_gh || die "the latest build needs the GitHub CLI, signed in: sudo apt install gh && gh auth login"
        local run
        run="$(gh run list --repo "$GDS_REPO" --workflow ci.yml --branch main --event push \
            --status success --limit 1 --json databaseId --jq '.[0].databaseId // empty')"
        [ -n "$run" ] || die "no successful CI run on main found"
        echo "Downloading the build of CI run $run ..." >&2
        gh run download "$run" --repo "$GDS_REPO" --name "$artifact" --dir "$dir" >&2 \
            || die "CI run $run has no artifact $artifact (artifacts expire after 90 days)"
    else
        local tag=""
        if [ "$source" != "latest" ]; then
            tag="v${source#v}"
        fi
        echo "Downloading the ${tag:-latest} release of $GDS_REPO ..." >&2
        if have_gh; then
            # without a tag, gh takes the latest release
            gh release download ${tag:+"$tag"} --repo "$GDS_REPO" --pattern "$pattern" --dir "$dir" --clobber >&2 \
                || die "no ${tag:-latest} release with a $kind package found"
        else
            command -v curl >/dev/null || die "curl is required (or the GitHub CLI)"
            local api="$GDS_API/repos/$GDS_REPO/releases/${tag:+tags/$tag}"
            [ -n "$tag" ] || api="${api}latest"
            local url
            url="$(curl -fsSL "$api" | grep -o '"browser_download_url": *"[^"]*"' | cut -d'"' -f4 \
                | grep -E "${pattern/\*/.*}\$" | head -n 1)" \
                || die "no ${tag:-latest} release found (a private repository needs the GitHub CLI)"
            [ -n "$url" ] || die "the ${tag:-latest} release has no $kind package"
            curl -fL --progress-bar -o "$dir/$(basename "$url")" "$url" || die "download failed: $url"
        fi
    fi

    local file
    file="$(find "$dir" -maxdepth 1 -name "$pattern" | head -n 1)"
    [ -n "$file" ] || die "the download contains no $kind package"
    echo "$file"
}
