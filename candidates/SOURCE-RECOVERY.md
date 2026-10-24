# Exact source/history

The public checkpoint contains the complete base history plus the exact
incremental source for both candidates. No original commit identities are changed.

    git clone --single-branch --branch checkpoint/native-dma-candidates-20261024 https://github.com/TPMJB/K-UI-NeXT checkpoint
    cd checkpoint
    python3 candidates/restore-source.py --candidate A
    python3 candidates/restore-source.py --candidate B

The ZIP also contains the same source update bundle, whose prerequisite is
74958b3878279fc916199a7cd330f7f17b036138. Exact commits/trees are in CANDIDATES.json.
