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

operation_log_test = executable(
  'test-operation-log',
  files('unit/test-operation-log.c'),
  include_directories: include_directories('../src'),
  dependencies: [core_dep],
)
test('operation-log', operation_log_test)
