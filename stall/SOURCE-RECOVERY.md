# Exact source/history

The checkpoint preserves the previous source history and candidate artifacts.
This source update retains the exact original commits from 74958b387827 through
2df1c301b26fe21620e4a48b7a7c67e11958b0fd, including the rejected hybrid, A and B.

```sh
git clone --single-branch --branch checkpoint/native-b-stall-20261024 https://github.com/TPMJB/K-UI-NeXT checkpoint
cd checkpoint
python3 stall/checkout-source.py
```

The ZIP contains the same incremental bundle. Its prerequisite is
74958b3878279fc916199a7cd330f7f17b036138, present in the checkpoint base history.
The script reconstructs the source checkout and checks the exact tree hash.
