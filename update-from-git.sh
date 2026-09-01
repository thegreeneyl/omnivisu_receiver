#!/usr/bin/env bash
# Pull the latest omnivisu_receiver source from origin/main and rebuild.
#
# Usage (from anywhere):
#   /path/to/omnivisu_receiver/update-from-git.sh
#   /path/to/omnivisu_receiver/update-from-git.sh --no-build
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_DIR"

BUILD=1
if [[ "${1:-}" == "--no-build" ]]; then
	BUILD=0
elif [[ "${1:-}" != "" ]]; then
	echo "usage: $0 [--no-build]" >&2
	exit 2
fi

if [[ ! -f src/ofApp.cpp ]] || ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
	echo "error: this script must live in the omnivisu_receiver git repo" >&2
	exit 1
fi

branch="$(git rev-parse --abbrev-ref HEAD)"
if [[ "$branch" != "main" ]]; then
	echo "error: on branch '${branch}', expected main" >&2
	exit 1
fi

echo "==> fetching origin"
git fetch origin

stashed=0
if [[ -n "$(git status --porcelain)" ]]; then
	echo "==> stashing local changes (including machine-specific config)"
	git stash push -u -m "update-from-git.sh $(date +%Y-%m-%dT%H:%M:%S)"
	stashed=1
fi

echo "==> pulling origin/main (fast-forward only)"
git pull --ff-only origin main

if [[ "$stashed" -eq 1 ]]; then
	echo "==> restoring local changes"
	if ! git stash pop; then
		echo "warning: stash pop had conflicts. Fix them, then run: git stash drop" >&2
	fi
fi

echo "==> now at $(git log -1 --oneline)"

if [[ "$BUILD" -eq 1 ]]; then
	echo "==> building Release"
	if jobs="$(sysctl -n hw.ncpu 2>/dev/null)"; then
		:
	elif jobs="$(nproc 2>/dev/null)"; then
		:
	else
		jobs=4
	fi
	make Release -j"${jobs}"
	echo "==> done"
	if [[ -d bin/omnivisu_receiver.app ]]; then
		echo "    open bin/omnivisu_receiver.app"
	fi
else
	echo "==> skipped build (--no-build)"
fi
