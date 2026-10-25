# Exact source and history

Source `b656d4934fb9cde51ca4af6eee2f936829d38028`, tree `1f8d33c8ee4dc376a76e07ad34b25a1860dcca13`. The update preserves original commit identities
and all candidates since prerequisite 74958b3878279fc916199a7cd330f7f17b036138.
The complete prerequisite base history remains in the public checkpoint.

```sh
git clone --single-branch --branch checkpoint/native-sdk-sci-route-20261024 https://github.com/TPMJB/K-UI-NeXT checkpoint
cd checkpoint
python3 sdk-sci-route/checkout-source.py
```

The script imports the exact incremental bundle and verifies the tree before
checkout. The same bundle and this commit's patch are included in the test ZIP.
The incremental bundle requires the published prerequisite base history.
