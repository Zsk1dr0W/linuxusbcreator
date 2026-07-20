device_test = executable(
  'test-device',
  files('unit/test-device.c'),
  include_directories: include_directories('../src'),
  dependencies: [core_dep],
)
test('device-policy', device_test)

