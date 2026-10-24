# Exact source and history

This update retains the original commits from prerequisite
`74958b3878279fc916199a7cd330f7f17b036138` through `f518592088f9e397aa11780514535d0dbd52b42a`.
The full base history is preserved in the public checkpoint alongside it.

```sh
git clone --single-branch --branch checkpoint/native-hybrid-cooperative-20261024 https://github.com/TPMJB/K-UI-NeXT checkpoint
cd checkpoint
python3 cooperative/checkout-source.py
```

The script imports the incremental bundle and verifies exact source tree
`004dbce46dc69187b952ec5bdb2a8cec18a43fed`. The ZIP contains the same update bundle and the new commit's patch.
It is not a standalone history bundle without the prerequisite base history.
