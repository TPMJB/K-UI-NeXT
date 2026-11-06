#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise effective recipe/production-header service geometry and refusals."""
import importlib.util
from pathlib import Path
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("native_service_inventory", ROOT / "tools/native_service_inventory.py")
inventory = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inventory)


class NativeServiceInventoryTests(unittest.TestCase):
    def test_actual_production_recipes_and_routes(self):
        result = inventory.inventory()
        profiles = result["profiles"]
        for name, worker, lease, cache in (
            ("standard_vmu_only_72", 72 * 1024, 72 * 1024, 0),
            ("background_compact_72", 72 * 1024, 284 * 1024, 192 * 1024),
            ("background_session_112", 112 * 1024, 324 * 1024, 192 * 1024),
            ("new_standalone", 0, 0, 0),
        ):
            with self.subTest(profile=name):
                self.assertEqual((profiles[name]["worker_bytes"], profiles[name]["retained_lease_bytes"],
                                  profiles[name]["cache_bytes"]), (worker, lease, cache))
        routes = result["production_package_selection"]
        self.assertNotEqual(routes["standard_vmu_off"], routes["standard_vmu_on"])
        self.assertEqual(routes["new_a2"], routes["legacy_a2"])
        self.assertNotEqual(routes["new_a1"], routes["new_a2"])
        self.assertIn("Makefile.vmu-native-vmu-lite-paced", result["source_sha256"])
        # These are transitive includes of route/geometry contracts, not a
        # manually enumerated top-level list.
        self.assertIn("fatfs/source/ff.h", result["source_sha256"])
        self.assertIn("include/kui/native_standalone_capability.h", result["source_sha256"])
        self.assertEqual(len(result["inventory_tool_sha256"]), 64)

    def test_changed_effective_recipe_cannot_hide_worker_span_mismatch(self):
        # Use real headers and recipes, changing only the final Make assignment.
        # This catches a recipe/header divergence instead of trusting a manifest.
        with tempfile.TemporaryDirectory(prefix="kui-service-mutation-") as temporary:
            root = Path(temporary)
            for path in ROOT.glob("Makefile*"):
                (root / path.name).symlink_to(path)
            for name in ("include", "config", "fatfs"):
                shutil.copytree(ROOT / name, root / name)
            (root / "src").symlink_to(ROOT / "src", target_is_directory=True)
            recipe = root / inventory.PROFILES["background_compact_72"]
            recipe.unlink()
            shutil.copy2(ROOT / recipe.name, recipe)
            with recipe.open("a") as output:
                output.write("\noverride NATIVE_SPAN := 0x1c000\n")
            with self.assertRaisesRegex(ValueError, "worker span mismatch"):
                inventory.inventory(root)


if __name__ == "__main__":
    unittest.main()
