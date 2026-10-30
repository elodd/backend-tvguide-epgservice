#!/usr/bin/env bash
# Downloads epgshare01.online guides into data/ for use with `tvguide --file`.
#
#   scripts/download-epg.sh              # UK1
#   scripts/download-epg.sh UK1 HU1      # several guides
#   scripts/download-epg.sh --keep-gz UK1
#
# Each code is saved as data/epg_<code>.xml (and data/epg_<code>.xml.gz with --keep-gz).
set -euo pipefail

usage() {
    echo "usage: $0 [--keep-gz] [code ...]   (default code: UK1)" >&2
    exit 2
}

keep_gz=0
codes=()
for arg in "$@"; do
    case "$arg" in
        --keep-gz) keep_gz=1 ;;
        -h|--help) usage ;;
        -*) usage ;;
        *)
            [[ "$arg" =~ ^[A-Za-z0-9_]+$ ]] || { echo "invalid code '$arg'" >&2; exit 2; }
            codes+=("$arg")
            ;;
    esac
done
[[ ${#codes[@]} -gt 0 ]] || codes=(UK1)

data_dir="$(cd "$(dirname "$0")/.." && pwd)/data"
mkdir -p "$data_dir"

for code in "${codes[@]}"; do
    url="https://epgshare01.online/epgshare01/epg_ripper_${code}.xml.gz"
    gz="$data_dir/epg_${code}.xml.gz"
    xml="$data_dir/epg_${code}.xml"

    echo "downloading $url"
    # Write to a temp file so a failed download never replaces a good one.
    tmp="$(mktemp "$data_dir/.epg_${code}.XXXXXX")"
    trap 'rm -f "$tmp"' EXIT
    curl --fail --silent --show-error --location --max-time 300 -o "$tmp" "$url"
    gunzip -t "$tmp"
    chmod 644 "$tmp"  # mktemp creates 0600
    mv "$tmp" "$gz"
    trap - EXIT

    if [[ $keep_gz -eq 1 ]]; then
        gunzip -kf "$gz"
    else
        gunzip -f "$gz"
    fi
    echo "wrote $xml ($(grep -c '<programme ' "$xml") programmes)"
done
