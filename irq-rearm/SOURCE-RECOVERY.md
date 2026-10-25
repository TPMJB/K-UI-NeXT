# Exact source and history

Source `a12f32a632214bcb63359746a9a292ea99ce3037`, tree `a039d7cede5b666e8ee9e77878f0abcb38f2f224`. The update preserves original commit identities
and all candidates since prerequisite74958b3878279fc916199a7cd330f7f17b036138.
The complete prerequisite base history remains in the public checkpoint.

```sh
git clone --single-branch --branch checkpoint/native-irq-rearm-20261024 https://github.com/TPMJB/K-UI-NeXT checkpoint
cd checkpoint
python3 irq-rearm/checkout-source.py
```

The script imports the exact incremental bundle and verifies the tree before
checkout. The same bundle and this commit's patch are included in the test ZIP.
The incremental bundle requires the published prerequisite base history.
