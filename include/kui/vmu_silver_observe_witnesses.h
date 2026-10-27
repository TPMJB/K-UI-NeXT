/* SPDX-License-Identifier: GPL-3.0-only */
/* Sparse scalar checks only; no retail implementation or payload. Included
 * by the stage after its exact owner-profile selection and witness types. */
static const struct observer_witness witnesses[]={
    {KUI_VMU_OBS_HEAP_SLOT,KUI_VMU_OBS_HEAP_INIT,4,OBS_HEAP_SLOT},
    {0x8c11dbe8u,0x8c0000e0u,4,OBS_NATIVE_EXIT_CELL},
    {0x8c11dbccu,0xd206u,2,OBS_CHECK}, /* exit mode 1 vector-address load */
    {0x8c11dbceu,0x6322u,2,OBS_CHECK}, /* exit mode 1 vector dereference */
    {0x8c11dbd0u,0x432bu,2,OBS_CHECK}, /* exit mode 1 tail jump */
    {0x8c11dbd2u,0xe401u,2,OBS_CHECK}, /* exit mode 1 delay slot */
    {0x8c11dbdcu,0xd302u,2,OBS_CHECK}, /* exit mode 2 vector-address load */
    {0x8c11dbdeu,0xe402u,2,OBS_CHECK}, /* exit mode 2 argument */
    {0x8c11dbe0u,0x6032u,2,OBS_CHECK}, /* exit mode 2 vector dereference */
    {0x8c11dbe2u,0x402bu,2,OBS_CHECK}, /* exit mode 2 tail jump */
    {0x8c11dbe4u,0x4f26u,2,OBS_CHECK}, /* exit mode 2 delay restores PR */
    {0x8c01002cu,0x8c0100a0u,4,OBS_CHECK}, /* native absolute startup target */
    {0x8c1546d0u,0x9028u,2,OBS_CHECK}, /* start stolen first word; R0 dead */
    {0x8c1546d2u,0x63e2u,2,OBS_CHECK}, /* start manager dereference */
    {0x8c1546d4u,0x023eu,2,OBS_CHECK}, /* start Maple base load */
    {0x8c1546d6u,0x12c6u,2,OBS_CHECK}, /* start MDST store */
    {0x8c1546d8u,0x9029u,2,OBS_CHECK}, /* start pending field offset */
    {0x8c1546dau,0x63e2u,2,OBS_CHECK}, /* start manager reload */
    {0x8c154724u,0x10f4u,2,OBS_CHECK}, /* start Maple-base field offset */
    {0x8c15472eu,0x0fd8u,2,OBS_CHECK}, /* start pending field offset */
    {0x8c154610u,0x401bu,2,OBS_CHECK}, /* start TAS lock */
    {0x8c1545ceu,0x60e2u,2,OBS_CHECK}, /* start manager read */
    {0x8c1545d0u,0x001eu,2,OBS_CHECK}, /* start ready read */
    {0x8c1545d2u,0x8801u,2,OBS_CHECK}, /* start requires ready1 */
#if KUI_VMU_SILVER_READONLY_PROOF
    {0x8c1545d4u,0x8d02u,2,OBS_CHECK}, /* ready1 admission branch */
    {0x8c1545d8u,0xa0d3u,2,OBS_CHECK}, /* not-ready exit */
    {0x8c1545dau,0xe0fdu,2,OBS_CHECK}, /* not-ready result -3 */
    {0x8c1547dau,0x0316u,2,OBS_CHECK}, /* ready1 only after consumption */
    {0x8c1547e0u,0x2320u,2,OBS_CHECK}, /* unlock only after consumption */
    {0x8c12b1a2u,0xb01eu,2,OBS_CHECK}, /* sole direct gun-helper call */
#endif
    {0x8c1546e6u,0x22cau,2,OBS_CHECK}, /* toggle TX selector */
    {0x8c1546e8u,0x0326u,2,OBS_CHECK}, /* store TX selector */
    {0x8c15470au,0x22cau,2,OBS_CHECK}, /* toggle RX selector */
    {0x8c15470cu,0x0326u,2,OBS_CHECK}, /* store RX selector */
    {0x8c15477au,0x03d6u,2,OBS_CHECK}, /* mark ready0 */
    {0x8c154780u,0x23d0u,2,OBS_CHECK}, /* release start lock */
    {0x8c12b7a0u,0xd32bu,2,OBS_CHECK}, /* load consumer into R3 */
    {0x8c12b7a2u,0x430bu,2,OBS_CHECK}, /* call consumer through R3 */
    {0x8c12b7a4u,0x0009u,2,OBS_CHECK}, /* consumer call delay slot */
    {0x8c12b850u,0x8c1547e6u,4,OBS_CHECK}, /* consumer pointer literal */
    {0x8c12b84cu,0x8c15478eu,4,OBS_CHECK}, /* completion gate pointer */
    {0x8c12b798u,0x4c11u,2,OBS_CHECK}, /* test gate result nonnegative */
    {0x8c12b79au,0x8901u,2,OBS_CHECK}, /* only success reaches consumer */
    {0x8c154796u,0x024eu,2,OBS_CHECK}, /* completion ready read */
    {0x8c154798u,0x2228u,2,OBS_CHECK}, /* completion ready test */
    {0x8c1547a6u,0x5036u,2,OBS_CHECK}, /* completion MDST read */
    {0x8c1547a8u,0xc801u,2,OBS_CHECK}, /* completion MDST test */
    {0x8c1547b6u,0x401bu,2,OBS_CHECK}, /* completion TAS lock */
    {0x8c1547b8u,0x8906u,2,OBS_CHECK}, /* completion lock-success branch */
    {0x8c1547c8u,0xe000u,2,OBS_CHECK}, /* successful gate sets R0zero */
    {0x8c154818u,0x420bu,2,OBS_CHECK}, /* consumer main-device call */
    {0x8c154880u,0x8c155332u,4,OBS_CHECK}, /* main-device consumer pointer */
    {0x8c15483eu,0x430bu,2,OBS_CHECK}, /* consumer unit call */
    {0x8c154884u,0x8c1553f8u,4,OBS_CHECK}, /* unit consumer pointer */
    {0x8c12b7d8u,0x430bu,2,OBS_CHECK}, /* ready/unlock after consumption */
    {0x8c12b860u,0x8c1547d0u,4,OBS_CHECK}, /* ready/unlock pointer */
    {0x8c12b8b4u,0x410bu,2,OBS_CHECK}, /* registered callback dispatch */
    {0x8c12b18cu,0x400eu,2,OBS_CHECK}, /* IRQ installs IMASK15 */
    {0x8c12b19eu,0xb2e3u,2,OBS_CHECK}, /* IRQ invokes common wrapper */
    {0x8c12b192u,0x2228u,2,OBS_CHECK}, /* IRQ gun flag test */
    {0x8c12b1ccu,0x2432u,2,OBS_CHECK}, /* gun completion clears flag and bypasses common consumer */
    {0x8c12b220u,0x11e6u,2,OBS_CHECK}, /* separate gun MDST start */
    {0x8c12b288u,0x80000200u,4,OBS_CHECK}, /* gun descriptor control */
    {0x8c12b268u,0x8c25972cu,4,OBS_CHECK}, /* gun flag pointer */
    {0x8c12b3dcu,0x8c12b17au,4,OBS_CHECK}, /* IRQ completion callback registration */
    {0x8c12b3e0u,0x8c12b298u,4,OBS_CHECK}, /* IRQ acknowledge callback registration */
    {0x8c154870u,0x8c11cd24u,4,OBS_CHECK}, /* SDK lock pointer cell reference */
    {0x8c154874u,0x8c29143cu,4,OBS_CHECK}, /* SDK manager pointer cell reference */
    {0x8c0dde66u,0x2422u,2,OBS_CHECK}, /* startup installs P2 lock pointer */
    {0x8c0dde86u,0xd226u,2,OBS_CHECK}, /* root initializer pointer load */
    {0x8c0dde92u,0x420bu,2,OBS_CHECK}, /* root initializer call */
    {0x8c0dde94u,0x7420u,2,OBS_CHECK}, /* root initializer delay slot */
    {0x8c0ddf14u,0x8c19deacu,4,OBS_CHECK}, /* native BSS end pointer */
    {0x8c0ddf18u,0x0d000000u,4,OBS_CHECK}, /* startup RAM end */
    {0x8c0ddf1cu,0x80000000u,4,OBS_CHECK}, /* startup cached alias */
    {0x8c19deacu,0x8c2d9b60u,4,OBS_CHECK}, /* native BSS end */
    {0x8c19e2b8u,0x00000105u,4,OBS_CHECK}, /* native cache configuration */
};
static const uint8_t executable_sha[32]={
    0x44,0x60,0xc6,0x05,0x97,0xd6,0xc1,0xf5,0x87,0xc1,0x25,0xc2,0xdd,0x4b,0x2b,0x49,
    0x1d,0x0a,0x56,0x8f,0x31,0xb5,0x2c,0x91,0xbc,0xe7,0xd8,0xc7,0x4f,0x9d,0x55,0x0d};
