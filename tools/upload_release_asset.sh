#!/bin/sh
# Attach a file as a release asset on a Forgejo instance.
#
#   tools/upload_release_asset.sh TAG FILE ASSET_NAME
#
# Environment:
#   CC_TOKEN          API token with repo write access (secrets.RELEASE_TOKEN)
#   CC_FALLBACK_TOKEN used when CC_TOKEN is unset (the job's GITHUB_TOKEN)
#   CC_API_URL        API base, e.g. http://host:3000/api/v1
#                     (falls back to the runner's $GITHUB_API_URL)
#   CC_REPO           owner/repository, e.g. jack/crystal-caves
#
# The release is looked up by tag — the Forgejo release interface creates
# tag + release together, so a UI-triggered run finds it.  A plain
# git-pushed tag gets a draft release created so assets still have
# somewhere to land.  An existing asset with the same name is deleted
# first, so job re-runs stay idempotent.
#
# POSIX sh only (runs on the Linux, Windows and macOS runners alike);
# the JSON field extraction uses tr/sed with full-stream reads so no
# pipe consumer can SIGPIPE under `sh -e`.
set -e

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

api=${CC_API_URL:-$GITHUB_API_URL}
if [ -z "$api" ]; then
    echo "CC_API_URL / GITHUB_API_URL is unset" >&2
    exit 2
fi
token=${CC_TOKEN:-$CC_FALLBACK_TOKEN}
if [ -z "$token" ]; then
    echo "CC_TOKEN is unset (create the RELEASE_TOKEN secret)" >&2
    exit 2
fi
if [ -z "$CC_REPO" ]; then
    echo "CC_REPO is unset" >&2
    exit 2
fi

auth="Authorization: token $token"

# ---- find (or draft-create) the release for the tag ------------------------

rel=$(curl -fsS -H "$auth" "$api/repos/$CC_REPO/releases/tags/$tag" \
      2>/dev/null || true)
id=$(printf '%s' "$rel" | tr ',' '\n' \
     | sed -n 's/.*"id": *\([0-9][0-9]*\).*/\1/p' | sed -n '1p')
if [ -z "$id" ]; then
    echo "no release for tag $tag; creating a draft release"
    rel=$(curl -fsS -X POST -H "$auth" -H 'Content-Type: application/json' \
          -d "{\"tag_name\":\"$tag\",\"name\":\"$tag\",\"draft\":true}" \
          "$api/repos/$CC_REPO/releases")
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
         "$api/repos/$CC_REPO/releases/$id/assets?limit=100" || true)
aid=$(printf '%s' "$assets" | tr '{' '\n' \
      | sed -n "/\"name\":\"$name\"/s/.*\"id\": *\([0-9][0-9]*\).*/\1/p")
for a in $aid; do
    echo "removing existing asset $name (id $a)"
    curl -fsS -X DELETE -H "$auth" \
         "$api/repos/$CC_REPO/releases/$id/assets/$a" || true
done

# ---- upload ----------------------------------------------------------------

resp=$(curl -fsS -X POST -H "$auth" -F "attachment=@$file" \
       "$api/repos/$CC_REPO/releases/$id/assets?name=$name")
url=$(printf '%s' "$resp" | tr ',' '\n' \
      | sed -n 's/.*"browser_download_url": *"\([^"]*\)".*/\1/p' | sed -n '1p')
if [ -n "$url" ]; then
    echo "uploaded: $url"
else
    echo "uploaded $name to release $tag"
fi
