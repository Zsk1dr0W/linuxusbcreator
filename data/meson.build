application_id = 'io.github.zsk1dr0w.LinuxUsbCreator'

desktop_file = configure_file(
  input: 'io.github.zsk1dr0w.LinuxUsbCreator.desktop.in',
  output: '@BASENAME@',
  copy: true,
)
install_data(
  desktop_file,
  install_dir: get_option('datadir') / 'applications',
)

appstream_file = configure_file(
  input: 'io.github.zsk1dr0w.LinuxUsbCreator.metainfo.xml.in',
  output: '@BASENAME@',
  copy: true,
)
install_data(
  appstream_file,
  install_dir: get_option('datadir') / 'metainfo',
)

foreach size : ['128', '256', '512']
  install_data(
    'icons/hicolor/' + size + 'x' + size + '/apps/' + application_id + '.png',
    install_dir: get_option('datadir') / 'icons/hicolor' / (size + 'x' + size) / 'apps',
  )
endforeach

policy_conf = configuration_data()
policy_conf.set('HELPER_PATH', get_option('prefix') / get_option('libexecdir') / 'linuxusbcreator-helper')
policy_file = configure_file(
  input: 'io.github.zsk1dr0w.LinuxUsbCreator.policy.in',
  output: 'io.github.zsk1dr0w.LinuxUsbCreator.policy',
  configuration: policy_conf,
)
install_data(
  policy_file,
  install_dir: get_option('datadir') / 'polkit-1/actions',
)

if find_program('desktop-file-validate', required: false).found()
  test(
    'desktop-file-validate',
    find_program('desktop-file-validate'),
    args: [desktop_file],
  )
endif

if find_program('appstream-util', required: false).found()
  test(
    'appstream-validate',
    find_program('appstream-util'),
    args: ['validate-relax', '--nonet', appstream_file],
  )
endif

if find_program('xmllint', required: false).found()
  test(
    'polkit-policy-xml',
    find_program('xmllint'),
    args: ['--noout', policy_file],
  )
endif
