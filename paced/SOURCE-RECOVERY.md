# Exact source and history

Source `7121b29837496b1831f23f5a97f3fe9fff139a19`, tree `aaeccdf7919e0f48dd9142ec3cc9699dfa2631d9`. The update preserves original commit identities
and all candidates since prerequisite74958b3878279fc916199a7cd330f7f17b036138.
The complete prerequisite base history remains in the public checkpoint.

```sh
git clone --single-branch --branch checkpoint/native-paced-check-20261024 https://github.com/TPMJB/K-UI-NeXT checkpoint
cd checkpoint
python3 paced/checkout-source.py
```

The script imports the exact incremental bundle and verifies the tree before
checkout. The same bundle and this commit's patch are included in the test ZIP.
The incremental bundle requires the published prerequisite base history.
