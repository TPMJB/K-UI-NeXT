# Recover the exact source

The complete base source/history is public in TPMJB/K-UI-NeXT. Clone the
hybrid checkpoint branch, then run its verified restore script:

    git clone --single-branch --branch checkpoint/native-hybrid-20261024 https://github.com/TPMJB/K-UI-NeXT checkpoint
    cd checkpoint
    python3 hybrid/restore-hybrid.py

Exact source commit: 1920659698aeec78c1e812bbb76b2b9274f7558d
Exact source tree: 569869f7b50ec1096c1cfb4a19664ad9e5f37973

The ZIP also includes the same incremental source bundle. Its prerequisite
is 74958b3878279fc916199a7cd330f7f17b036138, restored from the base checkpoint.
