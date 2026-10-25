# Exact source and history

Source `302f73152e4b9058183ebb286439108eb3d5ba2f`, tree `c1d953338b6de0bd9b487316465a9097c30b822c`. The update preserves original commit identities
and all candidates since prerequisite74958b3878279fc916199a7cd330f7f17b036138.
The complete prerequisite base history remains in the public checkpoint.

```sh
git clone --single-branch --branch checkpoint/native-loader-timeline-20261024 https://github.com/TPMJB/K-UI-NeXT checkpoint
cd checkpoint
python3 timeline/checkout-source.py
```

The script imports the exact incremental bundle and verifies the tree before
checkout. The same bundle and this commit's patch are included in the test ZIP.
The incremental bundle requires the published prerequisite base history.
