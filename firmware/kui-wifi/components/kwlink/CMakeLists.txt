# SPDX-License-Identifier: MIT
# The link protocol shared with K-UI. kwhost.c is the Dreamcast's side and
# is not built into the firmware.
idf_component_register(SRCS "kwlink.c" INCLUDE_DIRS "include")
