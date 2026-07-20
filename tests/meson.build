device_test = executable(
  'test-device',
  files('unit/test-device.c'),
  include_directories: include_directories('../src'),
  dependencies: [core_dep],
)
test('device-policy', device_test)

image_test = executable(
  'test-image-writer',
  files('unit/test-image-writer.c'),
  include_directories: include_directories('../src'),
  dependencies: [core_dep],
)
test('image-writer', image_test)
