project('spout2', ['c', 'cpp'], default_options: ['cpp_std=c++11'])

cpp = meson.get_compiler('cpp')

spout2_include_dir = [
  include_directories('SPOUTSDK/SpoutGL'),
  include_directories('SPOUTSDK/SpoutDirectX/SpoutDX'),
  include_directories('/usr/x86_64-w64-mingw32/include/vulkan/'),
]

spout2_src = [
  'SPOUTSDK/SpoutGL/SpoutCopy.cpp',
  'SPOUTSDK/SpoutGL/SpoutDirectX.cpp',
  'SPOUTSDK/SpoutGL/SpoutFrameCount.cpp',
  'SPOUTSDK/SpoutGL/SpoutSenderNames.cpp',
  'SPOUTSDK/SpoutGL/SpoutSharedMemory.cpp',
  'SPOUTSDK/SpoutGL/SpoutUtils.cpp',
]

spout2_deps = [
  cpp.find_library('dxgi'),
  cpp.find_library('d3d11'),
  cpp.find_library('version'),
  cpp.find_library('winmm'),
]

spout2_lib = static_library(
  'spout2',
  spout2_src,
  include_directories: spout2_include_dir,
  cpp_args: ['-msse4', '-gdwarf-4', '-w'],
  link_args: [
    '-static',
    '-static-libgcc',
    '-static-libstdc++',
    '-Wl,--file-alignment=4096',
  ],
)

spout2_dep = declare_dependency(
  link_with: spout2_lib,
  include_directories: spout2_include_dir,
  dependencies: spout2_deps,
)
