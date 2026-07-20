project(
  'linuxusbcreator',
  'c',
  version: '0.2.0',
  license: 'GPL-3.0-or-later',
  meson_version: '>= 0.61.0',
  # GTK headers currently trigger a pedantic warning with GCC 16. Keep
  # warnings-as-errors for project code while avoiding -Wpedantic globally.
  default_options: ['c_std=c17', 'warning_level=2', 'werror=true'],
)

# GCC 16 diagnoses a trailing semicolon in a GTK declaration under
# -Wpedantic. Suppress that third-party-header warning while keeping the
# project's own warnings fatal.
add_project_arguments('-Wno-pedantic', language: 'c')

gnome = import('gnome')
i18n = import('i18n')

glib_dep = dependency('glib-2.0', version: '>= 2.72')
gio_dep = dependency('gio-2.0', version: '>= 2.72')
gtk_dep = dependency('gtk4', version: '>= 4.8')
adw_dep = dependency('libadwaita-1', version: '>= 1.2')

conf = configuration_data()
conf.set_quoted('PROJECT_VERSION', meson.project_version())
conf.set_quoted('LUC_HELPER_PATH', get_option('prefix') / get_option('libexecdir') / 'linuxusbcreator-helper')
configure_file(output: 'config.h', configuration: conf)

subdir('src')
subdir('data')
subdir('tests')

summary(
  {
    'Version': meson.project_version(),
    'Prefix': get_option('prefix'),
    'Build tests': true,
  },
  section: 'Linux USB Creator',
)
