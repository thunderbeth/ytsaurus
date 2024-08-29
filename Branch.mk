##@ Git:

# map current branch upstream <remote>/<ANY>/<VER> to <remote>/stable/<VER> or <remote>/main
BRANCH_BASE = $(shell git rev-parse --symbolic-full-name @{u} | sed -E 's!(refs/remotes/[^/]+)/(.*)/([^/]+)!\1/stable/\3!' | sed -E 's!/stable/main$$!/main!')

# map current branch upstream <remote>/<ANY>/<VER> to <remote>/<ANY>/patches/<VER>
BRANCH_FRAGMENT_PREFIXES = $(shell git rev-parse --symbolic-full-name @{u} | sed -E 's!(refs/remotes/[^/]+)/(.*)/([^/]+)!\1/\2/patches/\3!')

# map current branch upstream <remote>/<ANY>/<VER> to all branches with prefix <remote>/<ANY>/patches/<VER>
BRANCH_FRAGMENTS = $(shell git for-each-ref --sort="-authordate" "--format=%(refname)" ${BRANCH_FRAGMENT_PREFIXES})

define newline


endef

cherry-pick-branch-fragments: ## Reset current branch/version to stable/version or main and cherry-pick commits from fragments branch/patches/version
	git diff --quiet
	git diff --quiet --cached
	git reset --hard ${BRANCH_BASE}
	$(foreach fragment,${BRANCH_FRAGMENTS},git cherry-pick -x --keep-redundant-commits ${fragment} --not HEAD $(newline))
