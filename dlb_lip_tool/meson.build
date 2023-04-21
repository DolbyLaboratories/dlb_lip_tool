inc = [include_directories('include')]
src = files('src/dlb_lip_libcec_bus.c', 'src/dlb_lip_tool.c', 'src/dlb_lip_xml_parser.c')
deps = [libdlb_xml_dep, libdlb_lip_dep]

libcec_include_dir = get_option('libcec-include-dir')
libcec = dependency('libcec', required: false)

if (libcec_include_dir != '')
    inc += [include_directories(libcec_include_dir)]
    c_compiler = meson.get_compiler('c')
    c_compiler.has_header('cec.h', include_directories : inc, required : true)
elif libcec.found()
    deps += [libcec]
else
    error('libcec header not found - please use -Dlibcec-include-dir=PATH to set header location.')
endif

executable('dlb_lip_tool', src, include_directories : inc, dependencies : deps)