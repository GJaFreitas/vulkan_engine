project('vulkan_engine', ['c', 'cpp'],
  version: '0.1.0',
  default_options: [
    'warning_level=2',
    'buildtype=debug'
  ]
)

cc = meson.get_compiler('c')

ignore_override_warnings = cc.get_supported_arguments([
    '-Wno-override-init',         # GCC
    '-Wno-initializer-overrides'  # Clang
])

add_project_arguments(ignore_override_warnings, language: 'c')

# -----------------------------------------------------------------------------
# Include Directories
# -----------------------------------------------------------------------------
vcpkg_inc = include_directories('vcpkg_installed/x64-linux/include')
inc_dirs = include_directories('include', 'deps')

# -----------------------------------------------------------------------------
# External Dependencies & Linker Flags
# -----------------------------------------------------------------------------
vcpkg_lib_dir = meson.current_source_dir() / 'vcpkg_installed/x64-linux/lib'

# Find system / vcpkg libraries
freetype_dep = dependency('freetype2', required: true)
harfbuzz_dep = dependency('harfbuzz', required: true)
sdl3_dep     = dependency('sdl3', required: true)
vulkan_dep   = dependency('vulkan', required: true)

# Manual/Vcpkg library links
m_dep        = cc.find_library('m', required: true)
stdcxx_dep   = cc.find_library('stdc++', required: true)
volk_dep     = cc.find_library('volk', dirs: vcpkg_lib_dir)
shaderc_dep  = cc.find_library('shaderc_shared', dirs: vcpkg_lib_dir)
dl_dep       = cc.find_library('dl', required: true)

engine_deps = [
  vulkan_dep, sdl3_dep, freetype_dep, harfbuzz_dep,
  m_dep, stdcxx_dep, volk_dep, shaderc_dep, dl_dep
]

# -----------------------------------------------------------------------------
# Slang Shader Compiler Generator
# -----------------------------------------------------------------------------
slangc = find_program('slangc', required: true)

shader_gen = generator(slangc,
  # Outputs go to build/shaders/compiled/<name>.spv
  output: 'compiled_@BASENAME@.spv',
  depfile: '@BASENAME@.spv.d',
  arguments: [
    '@INPUT@',
    '-I' + meson.project_source_root() / 'shaders',
    '-I' + meson.project_source_root() / 'shaders/include',
    '-I' + meson.project_source_root() / 'include',
    '-target', 'spirv',
    '-fvk-use-scalar-layout',
    '-emit-spirv-directly',
    '-fvk-use-entrypoint-name',
    '-entry', 'vertMain',
    '-entry', 'fragMain',
    '-depfile', '@DEPFILE@', # Slang writes dependency tree here
    '-o', '@OUTPUT@'
  ]
)

compiled_shaders = shader_gen.process(
  'shaders/grid.slang',
  'shaders/pbr.slang',
  'shaders/shadows.slang',
  'shaders/text.slang',
)

# -----------------------------------------------------------------------------
# Font Baking Tool Target
# -----------------------------------------------------------------------------
msdf_baker_src = files(
  'tools/msdf_baker.c',
  'src/base_layer.c',
  'src/vector.c',
  'src/str.c',
  'src/allocators.c',
  'src/logging.c'
)

msdf_baker_bin = executable('msdf_baker',
  msdf_baker_src,
  include_directories: [inc_dirs],
  dependencies: [freetype_dep],
  native: true
)

font_path = '/usr/share/fonts/TTF/JetBrainsMonoNerdFont-Bold.ttf'
font_bake_file = custom_target('font_file_bake',
  output: 'font_file.bin',
  command: [
    'env',
    '-C', meson.project_source_root(), # Sets the working directory to your project root
    msdf_baker_bin.full_path(),
    font_path,
    meson.current_build_dir() / '@OUTPUT@'
  ],
  depends: [msdf_baker_bin],
  build_by_default: true
)

# -----------------------------------------------------------------------------
# Source Files
# -----------------------------------------------------------------------------

core_sources = files(
  'src/vector.c',
  'src/logging.c',
  'src/allocators.c',
  'src/base_layer.c',
  'src/str.c',
)

cpp_sources = files(
  'cpp_layer/vma.cpp',
)

# -----------------------------------------------------------------------------
# Hot-Reloadable Module (libgame_module.so)
# -----------------------------------------------------------------------------
module_sources = files(
	'src/modules/editor.c',
	'src/modules/ui.c',
)

# Compile the module as a shared library
game_module = shared_library('game_module',
  [module_sources, core_sources],
  include_directories: [inc_dirs, vcpkg_inc],
  dependencies: engine_deps,
  # Allow symbol resolution at load time if needed
  override_options: ['b_lundef=false']
)

# -----------------------------------------------------------------------------
# Host Executable Application
# -----------------------------------------------------------------------------
host_sources = files(
  'src/console.c',
  'src/entities.c',
  'src/fonts.c',
  'src/gltf_loading.c',
  'src/hotload_callbacks.c',
  'src/input.c',
  'src/main.c',
  'src/pipelines.c',
  'src/variables.c',
  'src/vulkan.c',

  'deps/tiny_gltf_v3.c',
)

executable('engine.out',
  [host_sources, core_sources, cpp_sources, compiled_shaders],
  include_directories: [inc_dirs, vcpkg_inc],
  dependencies: [dl_dep, freetype_dep, harfbuzz_dep, sdl3_dep, vulkan_dep, m_dep, volk_dep, shaderc_dep],
  export_dynamic: true # Exposes g_frame_arena
)
