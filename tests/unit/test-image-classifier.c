/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <glib.h>

#include "core/image-classifier.h"

static void
test_fedora_x64(void)
{
    g_autoptr(LucImageClassification) result =
        luc_image_classify_listing(
            "Path = LiveOS/squashfs.img\nPath = EFI/BOOT/BOOTX64.EFI\n");

    g_assert_cmpint(result->kind, ==, LUC_IMAGE_KIND_LINUX_ISO);
    g_assert_cmpstr(result->distribution, ==, "Fedora Linux");
    g_assert_cmpstr(result->architecture, ==, "x64");
}

static void
test_windows_arm64(void)
{
    g_autoptr(LucImageClassification) result =
        luc_image_classify_listing(
            "Path = sources/boot.wim\nPath = EFI/BOOT/BOOTAA64.EFI\n");

    g_assert_cmpint(result->kind, ==, LUC_IMAGE_KIND_WINDOWS_ISO);
    g_assert_cmpstr(result->architecture, ==, "ARM64");
}

static void
test_unknown(void)
{
    g_autoptr(LucImageClassification) result =
        luc_image_classify_listing("Path = data.bin\n");

    g_assert_cmpint(result->kind, ==, LUC_IMAGE_KIND_RAW_OR_UNKNOWN);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/image-classifier/fedora-x64", test_fedora_x64);
    g_test_add_func("/image-classifier/windows-arm64", test_windows_arm64);
    g_test_add_func("/image-classifier/unknown", test_unknown);
    return g_test_run();
}
