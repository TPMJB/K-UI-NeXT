# Exact source and history

Source `7568957f5e686deaeab196249667a4f8f7b5fc69`, tree `74b7878b91a0f5218f79ca42b4eee5a06a734da9`. The update preserves original commit identities
and all candidates since prerequisite 74958b3878279fc916199a7cd330f7f17b036138.
The complete prerequisite base history remains in the public checkpoint.

```sh
git clone --single-branch --branch checkpoint/native-readahead-repair-limit-20261025 https://github.com/TPMJB/K-UI-NeXT checkpoint
cd checkpoint
python3 read-ahead-repair-limit/checkout-source.py
```

The script imports the exact incremental bundle and verifies the tree before
checkout. The same bundle and this commit's patch are included in the test ZIP.
The incremental bundle requires the published prerequisite base history.
