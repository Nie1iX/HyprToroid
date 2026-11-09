project('hyprtoroid', 'cpp',
  version: '0.1.0',
  default_options: ['buildtype=debug', 'cpp_std=c++23'],
  meson_version: '>= 1.1'
)

add_project_arguments(
  [
    '-Wno-narrowing', '-shared', '-fPIC', '--no-gnu-unique'
  ],
  language: 'cpp')

globber = run_command('find', 'src', '-name', '*.cpp', '-print', check: true)
src = globber.stdout().strip().split('\n')

incdir = []
dependencies = [
  dependency('pixman-1'),
  dependency('libdrm'),
  dependency('pangocairo'),
  dependency('libinput'),
  dependency('libudev'),
  dependency('wayland-server'),
  dependency('xkbcommon'),
]

if get_option('local_include')
  incdir += include_directories('include')
  incdir += include_directories('include/hyprland/protocols')
else
  hyprland_dep = dependency('hyprland')
  dependencies += hyprland_dep
endif

shared_module(meson.project_name(), src,
  dependencies: dependencies,
  install: true,
  include_directories: incdir
)

projection_test = executable('grid-projection-test', 'tests/grid_projection.cpp')
test('toroidal grid projection', projection_test)

consumed_keys_test = executable('consumed-keys-test', 'tests/consumed_keys.cpp')
test('consumed key ownership', consumed_keys_test)
