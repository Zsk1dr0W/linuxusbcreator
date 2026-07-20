core_sources = files(
  'core/device.c',
  'core/helper-protocol.c',
  'core/image-writer.c',
  'core/operation-log.c',
  'core/windows-image.c',
)
linux_sources = files('linux/device-monitor.c')
iso_mount_sources = files('linux/iso-mount.c')

core_lib = static_library(
  'linuxusbcreator-core',
  core_sources,
  include_directories: include_directories('.'),
  dependencies: [glib_dep, gio_dep],
)

core_dep = declare_dependency(
  link_with: core_lib,
  include_directories: include_directories('.'),
  dependencies: [glib_dep, gio_dep],
)

write_operation_lib = static_library(
  'linuxusbcreator-write-operation',
  files('app/write-operation.c'),
  include_directories: include_directories('.', '..'),
  dependencies: [core_dep, gio_dep],
)

write_operation_dep = declare_dependency(
  link_with: write_operation_lib,
  include_directories: include_directories('.'),
  dependencies: [core_dep, gio_dep],
)

app_exe = executable(
  'linuxusbcreator',
  files('app/main.c', 'app/window.c') + linux_sources + iso_mount_sources,
  include_directories: include_directories('.', '..'),
  dependencies: [core_dep, write_operation_dep, gio_dep, gio_unix_dep, gtk_dep, adw_dep],
  install: true,
)

helper_exe = executable(
  'linuxusbcreator-helper',
  files('helper/main.c') + linux_sources,
  include_directories: include_directories('.', '..'),
  dependencies: [core_dep, gio_dep],
  install: true,
  install_dir: get_option('libexecdir'),
)
