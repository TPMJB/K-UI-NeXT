# Exact source and history

Source `6675bd6fdcbe3c5237ff7ae913ee94e3ea3f9b85`, tree `c2ad8e7c0d7aad3258e509e64f9ea3e7501f3959`. The update preserves original commit identities
and all candidates since prerequisite 74958b3878279fc916199a7cd330f7f17b036138.
The complete prerequisite base history remains in the public checkpoint.

```sh
git clone --single-branch --branch checkpoint/native-readahead-diagnostics-20261024 https://github.com/TPMJB/K-UI-NeXT checkpoint
cd checkpoint
python3 read-ahead-diagnostics/checkout-source.py
```

The script imports the exact incremental bundle and verifies the tree before
checkout. The same bundle and this commit's patch are included in the test ZIP.
The incremental bundle requires the published prerequisite base history.
