core_sources = files('core/device.c', 'core/image-writer.c', 'core/operation-log.c')
linux_sources = files('linux/device-monitor.c')

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

helper_exe = executable(
  'linuxusbcreator',
  files('app/main.c', 'app/window.c') + linux_sources,
  include_directories: include_directories('.', '..'),
  dependencies: [core_dep, gio_dep, gtk_dep, adw_dep],
  install: true,
)

executable(
  'linuxusbcreator-helper',
  files('helper/main.c') + linux_sources,
  include_directories: include_directories('.', '..'),
  dependencies: [core_dep, gio_dep],
  install: true,
  install_dir: get_option('libexecdir'),
)
