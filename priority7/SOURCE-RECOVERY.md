# Exact source and history

Source `8390fed1414dc10722684d85f7dadd1a92bb65fd`, tree `bd707e24d51dcc4aaad6565ac6f72f15d8ecad37`. The update preserves original commit identities
and all candidates since prerequisite 74958b3878279fc916199a7cd330f7f17b036138.
The complete prerequisite base history remains in the public checkpoint.

```sh
git clone --single-branch --branch checkpoint/native-sci-priority7-20261024 https://github.com/TPMJB/K-UI-NeXT checkpoint
cd checkpoint
python3 priority7/checkout-source.py
```

The script imports the exact incremental bundle and verifies the tree before
checkout. The same bundle and this commit's patch are included in the test ZIP.
The incremental bundle requires the published prerequisite base history.
