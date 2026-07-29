# Shared changed-file discovery for check.sh / fmt.sh. Source, then call.
# Union of: commits ahead of origin/master, staged+unstaged edits, untracked files.
compute_changed_files() {
    local base_ref=""
    if git rev-parse --verify --quiet origin/master > /dev/null; then
        base_ref="origin/master"
    elif git rev-parse --verify --quiet master > /dev/null; then
        base_ref="master"
    fi
    {
        if [ -n "$base_ref" ]; then
            git diff --name-only "$base_ref...HEAD"
        fi
        git diff --name-only HEAD
        git ls-files --others --exclude-standard
    } | sort -u | while IFS= read -r f; do
        [ -f "$f" ] && printf '%s\n' "$f" || true
    done
}
