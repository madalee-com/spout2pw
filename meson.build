project('spout2pw', 'c', 'cpp',
    meson_version: '>=1.1'
)

if not meson.is_cross_build()
    error('must be invoked with "meson setup --cross-file=./x86_64-w64-mingw32.txt"')
endif

message('Checking wine PE library path...')
r = run_command(join_paths(meson.project_source_root(), 'tools/get_wine_path.sh'),
    'x86_64-w64-mingw32', '^/.*/libwinecrt0.a$', check: true)
out = r.stdout().strip().split('\n')[0]
if out.endswith('libwinecrt0.a')
    wine_lib_paths = [out.substring(0, -13)]
    message('Wine PE library paths: ' + ' '.join(wine_lib_paths))
else
    error('Wine library path not found')
endif

message('Checking wine UNIX library path...')
r = run_command(join_paths(meson.project_source_root(), 'tools/get_wine_path.sh'),
    '', '^/.*/libwinecrt0.a$', check: true)
out = r.stdout().strip().split('\n')[0]
if out.endswith('libwinecrt0.a')
    unix_lib_paths = [out.substring(0, -13)]
    message('Wine UNIX library paths: ' + ' '.join(unix_lib_paths))
else
    error('Wine library path not found')
endif

message('Checking wine PE include paths...')
r = run_command(join_paths(meson.project_source_root(), 'tools/get_wine_path.sh'),
    'x86_64-w64-mingw32', '^-i(system|dirafter)/', check: true)
wine_include_args = r.stdout().strip().split('\n')
if wine_include_args.length() > 0
    message('Wine PE include args: ' + ' '.join(wine_include_args))
else
    error('Wine include args not found')
endif

message('Checking wine UNIX include paths...')
r = run_command(join_paths(meson.project_source_root(), 'tools/get_wine_path.sh'),
    '', '^-i(system|dirafter)/', check: true)
unix_include_args = r.stdout().strip().split('\n')
if unix_include_args.length() > 0
    message('Wine UNIX include args: ' + ' '.join(unix_include_args))
else
    error('Wine include args not found')
endif

c = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

# Spout2 subproject (managed via wrap file)
spout2_proj = subproject('spout2')
spout2_dep = spout2_proj.get_variable('spout2_dep')
spout2_include_dir = spout2_proj.get_variable('spout2_include_dir')

# libfunnel subproject
libfunnel_proj = subproject(
  'libfunnel',
  default_options: [
    'native=true',
    'test_apps=false',
    'egl=false',
    'default_library=static',
    'static_pipewire=true',
  ]
)

funnel = libfunnel_proj.get_variable('funnel')
funnel_vk = libfunnel_proj.get_variable('funnel_vk')

# Fixups include dir
fixups_include_dir = include_directories('src/fixups')

# Bundle the mingw-targeted Vulkan headers so the build does not depend on
# system packages that may be absent on minimal CI runners.
vulkan_include_dir = [
  include_directories('tools/vulkan-mingw-headers'),
]

# Build spout2dx.cpp as part of the main project
spout2dx_src = [
  'src/spout2dx.cpp',
]

spout2dx_cpp_args = [
  '-msse4',
  '-gdwarf-4',
  '-w',  # Ignore all warnings from Spout SDK
]

spout2dx_lib = static_library(
  'spout2dx',
  spout2dx_src,
  include_directories: [fixups_include_dir] + spout2_include_dir + vulkan_include_dir,
  cpp_args: spout2dx_cpp_args,
)

spout2dx_dep = declare_dependency(
  link_with: spout2dx_lib,
  include_directories: [fixups_include_dir] + spout2_include_dir + vulkan_include_dir,
)

windows_src = [
  'src/spout2pw.c',
  'src/debug_log.c',
]

unix_src = [
  'src/spout2pw_unix.c',
]

windows_compiler_args = [
    '-Wno-error', '-std=c99', '-mcmodel=small',
    '-fno-omit-frame-pointer', '-O2', '-march=nocona',
    '-mtune=core-avx2', '-mfpmath=sse', '-D__WINE_PE_BUILD',
    '-mcx16', '-fno-strict-aliasing', '-ffunction-sections'
] + wine_include_args

windows_link_args = [
  '-static-libgcc',
  '-static-libstdc++',
  '-static',
]
unix_compiler_args = [ '-DWINE_UNIX_LIB' ] + unix_include_args
unix_link_args = [
  '-lm',
  '-l:ntdll.so',
  '-Wl,--no-as-needed',
  '-lwinecrt0',
  '-Wl,--as-needed',
]
pipewire_static_lib = get_option('libpipewire_static_lib')
if pipewire_static_lib != ''
  unix_link_args += [
    '-Wl,--whole-archive',
    '-Wl,' + pipewire_static_lib,
    '-Wl,--no-whole-archive',
  ]
endif

windows_deps = [
  c.find_library('dxgi', dirs: wine_lib_paths),
  c.find_library('d3d11', dirs: wine_lib_paths),
  c.find_library('ntdll', dirs: wine_lib_paths),
  c.find_library('winecrt0', dirs: wine_lib_paths),
  c.find_library('vulkan-1', dirs: wine_lib_paths),
  spout2_dep,
  spout2dx_dep,
]

# Add static ucrtbase via link_args
windows_link_args += ['/usr/x86_64-w64-mingw32/lib/libucrtbase.a']

vulkan = dependency('vulkan', native: true)

unix_deps = [
  funnel,
  funnel_vk,
  vulkan,
]

foreach p : unix_lib_paths
  unix_link_args += ['-L' + p]
endforeach

spout2pw_bin = executable(
  'spout2pw',
  windows_src,
  name_prefix: '',
  include_directories: vulkan_include_dir,
  c_args: windows_compiler_args,
  link_args: windows_link_args,
  dependencies: [windows_deps],
  win_subsystem: 'console',
)

# Minimal DLL with DllMain that initializes the Wine unixlib handle.
# This DLL is loaded as LO_BUILTIN (via the placeholder marker) so that
# __wine_init_unix_call() succeeds from its DllMain, setting the global
# __wine_unixlib_handle used by spout2pw.exe's ServiceMain.
spout2pw_dll_src = [
  'src/spout2pw_dll_main.c',
  'src/debug_log.c',
]

spout2pw_dll = shared_library(
  'spout2pw_dll',
  spout2pw_dll_src,
  name_prefix: '',
  c_args: windows_compiler_args,
  link_args: windows_link_args,
  dependencies: [
    c.find_library('ntdll', dirs: wine_lib_paths),
    c.find_library('winecrt0', dirs: wine_lib_paths),
  ],
  install: false,
)

unixlib = shared_library(
  'spout2pw_unixlib',
  unix_src,
  name_prefix: '',
  c_args: unix_compiler_args,
  link_args: unix_link_args,
  dependencies: unix_deps,
  native: true,
)

fs = import('fs')

meson.add_install_script('tools/package.sh')
