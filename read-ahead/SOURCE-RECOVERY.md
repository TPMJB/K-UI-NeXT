# Exact source and history

Source `afcf7e1524a9a00cd8f4c2cae6a59541eaf3ddef`, tree `e8fad55a72404a3e991cb8fe1ee428905044ac1d`. The update preserves original commit identities
and all candidates since prerequisite 74958b3878279fc916199a7cd330f7f17b036138.
The complete prerequisite base history remains in the public checkpoint.

```sh
git clone --single-branch --branch checkpoint/native-sdk-readahead-20261024 https://github.com/TPMJB/K-UI-NeXT checkpoint
cd checkpoint
python3 read-ahead/checkout-source.py
```

The script imports the exact incremental bundle and verifies the tree before
checkout. The same bundle and this commit's patch are included in the test ZIP.
The incremental bundle requires the published prerequisite base history.
