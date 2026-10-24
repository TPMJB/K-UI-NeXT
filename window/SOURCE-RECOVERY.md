# Exact source and history

Source `1baa80301a62764acb49ca44b2f380a39f869d12`, tree `69a9bf800a742598ae64d5df38d2b1c5790ae4e4`. The update preserves original commit identities
and all candidates since prerequisite74958b3878279fc916199a7cd330f7f17b036138.
The complete prerequisite base history remains in the public checkpoint.

```sh
git clone --single-branch --branch checkpoint/native-reader-window-20261024 https://github.com/TPMJB/K-UI-NeXT checkpoint
cd checkpoint
python3 window/checkout-source.py
```

The script imports the exact incremental bundle and verifies the tree before
checkout. The same bundle and this commit's patch are included in the test ZIP.
The incremental bundle requires the published prerequisite base history.
