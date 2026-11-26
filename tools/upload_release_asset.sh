#!/bin/sh
# Attach a file as a release asset on a Forgejo instance.
#
#   tools/upload_release_asset.sh TAG FILE ASSET_NAME
#
# Environment:
#   GITHUB_TOKEN                       job token (secrets.GITHUB_TOKEN);
#                                     same auth candid_app's release
#                                     workflow uses — no extra secret
#   FORGEJO_SERVER_URL / GITHUB_SERVER_URL   instance base URL
#                                     (fallback: the runner's GITHUB_API_URL)
#   GITHUB_REPOSITORY                 owner/repository (provided by the runner)
#
# The release is looked up by tag — the Forgejo release interface creates
# tag + release together, so a UI-triggered run finds it.  A plain
# git-pushed tag gets a draft release created so assets still have
# somewhere to land.  An existing asset with the same name is deleted
# first, so job re-runs stay idempotent, and the upload retries a few
# times on transient network errors (like candid_app's
# scripts/publish_release_asset.py).
#
# POSIX sh only (runs on the Linux, Windows and macOS runners alike);
# the JSON field extraction uses tr/sed with full-stream reads so no
# pipe consumer can SIGPIPE under `sh -e`.
set -e

UPLOAD_ATTEMPTS=3
RETRY_DELAY_SECONDS=5

tag=$1
file=$2
name=$3
if [ -z "$tag" ] || [ -z "$file" ] || [ -z "$name" ]; then
    echo "usage: $0 TAG FILE ASSET_NAME" >&2
    exit 2
fi
if [ ! -f "$file" ]; then
    echo "no such file: $file" >&2
    exit 2
fi

server=${FORGEJO_SERVER_URL:-$GITHUB_SERVER_URL}
if [ -n "$server" ]; then
    api=$(printf '%s' "$server" | sed 's:/*$::')/api/v1
else
    api=$GITHUB_API_URL
fi
if [ -z "$api" ]; then
    echo "FORGEJO_SERVER_URL / GITHUB_API_URL is unset" >&2
    exit 2
fi
token=$GITHUB_TOKEN
if [ -z "$token" ]; then
    echo "GITHUB_TOKEN is unset" >&2
    exit 2
fi
repo=$GITHUB_REPOSITORY
if [ -z "$repo" ]; then
    echo "GITHUB_REPOSITORY is unset" >&2
    exit 2
fi

auth="Authorization: token $token"

# ---- find (or draft-create) the release for the tag ------------------------

rel=$(curl -fsS -H "$auth" "$api/repos/$repo/releases/tags/$tag" \
      2>/dev/null || true)
id=$(printf '%s' "$rel" | tr ',' '\n' \
     | sed -n 's/.*"id": *\([0-9][0-9]*\).*/\1/p' | sed -n '1p')
if [ -z "$id" ]; then
    echo "no release for tag $tag; creating a draft release"
    rel=$(curl -fsS -X POST -H "$auth" -H 'Content-Type: application/json' \
          -d "{\"tag_name\":\"$tag\",\"name\":\"$tag\",\"draft\":true}" \
          "$api/repos/$repo/releases")
    id=$(printf '%s' "$rel" | tr ',' '\n' \
         | sed -n 's/.*"id": *\([0-9][0-9]*\).*/\1/p' | sed -n '1p')
fi
if [ -z "$id" ]; then
    echo "cannot resolve a release id for tag $tag" >&2
    exit 1
fi
echo "release $tag -> id $id"

# ---- drop an existing asset of the same name (re-run idempotency) ---------

assets=$(curl -fsS -H "$auth" \
         "$api/repos/$repo/releases/$id/assets?limit=100" || true)
aid=$(printf '%s' "$assets" | tr '{' '\n' \
      | sed -n "/\"name\":\"$name\"/s/.*\"id\": *\([0-9][0-9]*\).*/\1/p")
for a in $aid; do
    echo "removing existing asset $name (id $a)"
    curl -fsS -X DELETE -H "$auth" \
         "$api/repos/$repo/releases/$id/assets/$a" || true
done

# ---- upload (retrying transient network errors) ---------------------------

n=1
while :; do
    if resp=$(curl -fsS -X POST -H "$auth" -F "attachment=@$file" \
              "$api/repos/$repo/releases/$id/assets?name=$name"); then
        break
    fi
    if [ "$n" -ge "$UPLOAD_ATTEMPTS" ]; then
        echo "upload of $name failed after $n attempts" >&2
        exit 1
    fi
    echo "upload of $name failed (attempt $n/$UPLOAD_ATTEMPTS); \
retrying in ${RETRY_DELAY_SECONDS}s" >&2
    sleep "$RETRY_DELAY_SECONDS"
    n=$((n + 1))
done
url=$(printf '%s' "$resp" | tr ',' '\n' \
      | sed -n 's/.*"browser_download_url": *"\([^"]*\)".*/\1/p' | sed -n '1p')
if [ -n "$url" ]; then
    echo "uploaded: $url"
else
    echo "uploaded $name to release $tag"
fi
