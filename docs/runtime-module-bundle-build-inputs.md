# GUI embedded-module build inputs

Changing `RUNTIME_MODULE_BUNDLE` to an existing directory could retain the
previous directory's compiled bundle object when both newly selected inputs
were older than that object. Updating `bundle.bin` or `bundle.S` while restoring
an older timestamp had the same problem. The GUI could then link a module
cohort different from the one requested by the build command.

This was reproduced with the unchanged production Makefile in an isolated
working directory: selecting the older946 bundle retained an object whose
extracted bundle section still contained91fa. The current91fa delivery is not
affected: its driver created fresh inputs and checked that all four modules
embedded in the actual GUI equal the four staged physical modules.

The build now records whether the provider is enabled, the selected absolute
directory and the full SHA256 of both inputs. The identity file changes only
when those values change. The bundle object depends on that identity, so path
and content changes rebuild it even when input timestamps predate the object.
The GUI also depends on the provider configuration. No game reader, helper,
worker, VMU reservation, runtime-provider precedence or updater behavior changes.

`tests/test_runtime_module_bundle_make.py` executes the production Makefile
graph, retaining its `force-build-id` dependencies. It covers older selected
paths, changed binary and assembly inputs with older timestamps, reuse of an
unchanged object, and On → Off → On. The old Makefile fails the path/content
checks. On/Off already worked through the existing forced GUI relink; that
sequence is a preservation check, not a second claimed defect.

The test's compiler/linker and unrelated source-receipt writer are authored
boundaries. It proves build dependencies and selected input bytes, not SH code
execution or console compatibility. Delivery must still compare the actual
linked GUI bundle against its declared staged modules.
