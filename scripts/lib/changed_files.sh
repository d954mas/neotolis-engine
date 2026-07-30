# Shared changed-file discovery for check.sh / fmt.sh. Source, then call.
# Union of: commits ahead of origin/master, staged+unstaged edits, untracked files.
compute_base_ref() {
    if git rev-parse --verify --quiet origin/master > /dev/null; then
        echo "origin/master"
    elif git rev-parse --verify --quiet master > /dev/null; then
        echo "master"
    fi
}

# Raw name union WITHOUT the exists-filter: deletions stay visible (test
# selection must fire on deleted builder/atlas files too).
compute_changed_names_all() {
    local base_ref
    base_ref="$(compute_base_ref)"
    {
        if [ -n "$base_ref" ]; then
            git diff --name-only "$base_ref...HEAD"
        fi
        git diff --name-only HEAD
        git ls-files --others --exclude-standard
    } | sort -u
}

compute_changed_files() {
    compute_changed_names_all | while IFS= read -r f; do
        [ -f "$f" ] && printf '%s\n' "$f" || true
    done
}
