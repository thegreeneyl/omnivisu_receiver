#!/usr/bin/env bash
# Back up THIS machine's live bin/data/config.json and bin/data/grading.json
# into the tracked machine-config/<hostname>/ folder, commit, and push.
#
# The live files are ignored by git (machine-local, never overwritten by an
# update); this script is the intentional way to get their current values
# into the repo. Run it after tuning something worth keeping.
#
# Usage (from anywhere):
#   /path/to/omnivisu_receiver/backup-config.sh
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_DIR"

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
	echo "error: this script must live in the omnivisu_receiver git repo" >&2
	exit 1
fi

host="$(hostname -s)"
dest="machine-config/${host}"
mkdir -p "$dest"

copied=0
for name in config grading; do
	src="bin/data/${name}.json"
	if [[ -f "$src" ]]; then
		cp "$src" "${dest}/${name}.json"
		copied=1
	else
		echo "warning: ${src} not found - skipped" >&2
	fi
done
if [[ "$copied" -eq 0 ]]; then
	echo "error: nothing to back up" >&2
	exit 1
fi

git add "$dest"
if git diff --cached --quiet; then
	echo "==> no changes since the last backup of ${host}"
	exit 0
fi

git commit -m "Config backup from ${host} ($(date +%Y-%m-%d\ %H:%M))"
echo "==> pushing backup to origin"
git push origin HEAD
echo "==> done: ${dest} is backed up"
