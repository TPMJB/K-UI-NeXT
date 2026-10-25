# Exact source and history

Source `1eba1784a83e7eb5d0cf6dc61cee5ccf46d22c68`, tree `ef2378a69a20d7b10011902b15c0cc52c5cf9fe2`. The update preserves original commit identities
and all candidates since prerequisite 74958b3878279fc916199a7cd330f7f17b036138.
The complete prerequisite base history remains in the public checkpoint.

```sh
git clone --single-branch --branch checkpoint/native-sdk-sci-bound-20261024 https://github.com/TPMJB/K-UI-NeXT checkpoint
cd checkpoint
python3 sdk-sci-bound/checkout-source.py
```

The script imports the exact incremental bundle and verifies the tree before
checkout. The same bundle and this commit's patch are included in the test ZIP.
The incremental bundle requires the published prerequisite base history.
