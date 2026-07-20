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

helper_protocol_test = executable(
  'test-helper-protocol',
  files('unit/test-helper-protocol.c'),
  include_directories: include_directories('../src'),
  dependencies: [core_dep],
)
test('helper-protocol', helper_protocol_test)

write_operation_test = executable(
  'test-write-operation',
  files('unit/test-write-operation.c'),
  include_directories: include_directories('../src'),
  dependencies: [write_operation_dep],
)
test('write-operation', write_operation_test)

test('helper-rejects-invalid-invocation', helper_exe, should_fail: true)

cli_help_test = find_program('integration/test-cli-help.sh')
test('cli-help', cli_help_test, args: app_exe.full_path())
