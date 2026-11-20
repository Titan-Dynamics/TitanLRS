#!/bin/bash
# Publish firmware to the titanlrs R2 bucket, where the web flasher reads it at runtime (so new
# builds need no flasher redeploy).
#
#   r2-publish.sh master  <firmware dir>            # latest green master build
#   r2-publish.sh release <version> <firmware dir>  # a release
#
# <firmware dir> is the CI "firmware" artifact's firmware/ folder (or an unpacked release zip); only
# its FCC/ and LBT/ trees are uploaded, which is all the flasher fetches.
#
# Bucket layout:
#   master/latest.json                      {sha, version, date, run} of the published master build
#   master/<sha>/{FCC,LBT}/<target>/*.bin   that build, plus the one it replaced (for flashes in flight)
#   releases/index.json                     {"tags": {"<version>": "<version>", ...}}
#   releases/<version>/{FCC,LBT}/<target>/*.bin
#   releases/<version>/release.json         written last; marks the upload complete
#
# Needs AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY, AWS_ENDPOINT_URL (R2 S3 endpoint), and git history
# for "master". Without credentials it warns and exits 0.
set -euo pipefail

BUCKET=s3://titanlrs

usage() {
  echo "usage: $0 master <firmware dir> | release <version> <firmware dir>" >&2
  exit 2
}

if [[ -z ${AWS_ACCESS_KEY_ID:-} || -z ${AWS_SECRET_ACCESS_KEY:-} || -z ${AWS_ENDPOINT_URL:-} ]]; then
  echo "::warning title=R2 Skip::R2 credentials not defined. Nothing published."
  exit 0
fi

# upload_regions <firmware dir> <s3 dest>
upload_regions() {
  local region found=0
  for region in FCC LBT; do
    if [[ -d $1/$region ]]; then
      aws s3 sync --delete --only-show-errors "$1/$region" "$2/$region"
      found=1
    fi
  done
  if [[ $found == 0 ]]; then
    echo "::error title=R2 Publish::No FCC/ or LBT/ folder in $1"
    exit 1
  fi
}

# put_json <local file> <s3 key>: JSON the flasher polls, so never cached.
put_json() {
  aws s3 cp --only-show-errors "$1" "$2" --content-type application/json --cache-control no-cache
}

publish_master() {
  local dir=$1 sha=${GITHUB_SHA:-$(git rev-parse HEAD)}
  local s3=$BUCKET/master published prefix

  # Never replace a newer published build with an older one (runs can finish out of order).
  published=$(aws s3 cp "$s3/latest.json" - 2>/dev/null | jq -r '.sha // empty' || true)
  if [[ -n $published && $published != "$sha" ]]; then
    git fetch -q origin master || true
    if git merge-base --is-ancestor "$sha" "$published" 2>/dev/null; then
      echo "::notice title=R2 Skip::Newer master build ${published:0:8} is already published."
      return
    fi
  fi

  upload_regions "$dir" "$s3/$sha"

  jq -n \
    --arg sha "$sha" \
    --arg version "$(git describe --tags --always)" \
    --arg date "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
    --arg run "${GITHUB_SERVER_URL:-}/${GITHUB_REPOSITORY:-}/actions/runs/${GITHUB_RUN_ID:-}" \
    '{sha: $sha, version: $version, date: $date, run: $run}' > latest.json
  put_json latest.json "$s3/latest.json"

  for prefix in $(aws s3 ls "$s3/" | awk '$1 == "PRE" {print $2}' | tr -d /); do
    if [[ $prefix != "$sha" && $prefix != "$published" ]]; then
      aws s3 rm --recursive --only-show-errors "$s3/$prefix/"
    fi
  done

  echo "::notice title=R2 Publish::Master build ${sha:0:8} published."
}

publish_release() {
  local version=${1#v} dir=$2
  local s3=$BUCKET/releases
  if [[ ! $version =~ ^[0-9][0-9A-Za-z._-]*$ ]]; then
    echo "::error title=R2 Publish::'$version' is not a release version"
    exit 1
  fi

  upload_regions "$dir" "$s3/$version"
  jq -n --arg version "$version" --arg date "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
    '{version: $version, date: $date}' > release.json
  put_json release.json "$s3/$version/release.json"

  # Rebuild the index from every completed release rather than editing the old one, so two
  # publishes at once can't drop each other's version.
  local versions
  versions=$(aws s3 ls --recursive "$s3/" | awk '{print $4}' |
    sed -n 's#^releases/\([^/]*\)/release\.json$#\1#p')
  # shellcheck disable=SC2086 # one version per word
  jq -n '{tags: ($ARGS.positional | map({key: ., value: .}) | from_entries)}' --args $versions > index.json
  put_json index.json "$s3/index.json"

  echo "::notice title=R2 Publish::Release $version published ($(echo $versions | wc -w | tr -d ' ') releases in index)."
}

case ${1:-} in
  master)  [[ $# == 2 ]] || usage; publish_master "$2" ;;
  release) [[ $# == 3 ]] || usage; publish_release "$2" "$3" ;;
  *) usage ;;
esac
