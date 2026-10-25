# SDK route console progress

Tested build remains `1eba1784a83e`; its ZIP and audited source are in
`sdk-sci-bound/`. This checkpoint adds authored evidence from capture63346; it
changes no runtime code and ships no new build. The source/history bundle is
exact commit `54ed49b5e276a322c947714235cb5d83fb9d909a`, tree `e98c71be6970fcabba74809e8a368582fb274bec`.

To recover the evidence source, first run the existing
`python3 sdk-sci-bound/checkout-source.py`, then:

```sh
git -C restored-source fetch ../sdk-sci-observed/native-source-update.bundle HEAD
git -C restored-source checkout --detach 54ed49b5e276a322c947714235cb5d83fb9d909a
git -C restored-source rev-parse HEAD^{tree}
```

The last command must print `e98c71be6970fcabba74809e8a368582fb274bec`. All prior candidates and complete
prerequisite history remain in this checkpoint.
