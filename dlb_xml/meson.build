libdlb_xml_inc = include_directories('include')
libdlb_xml_src = files('src/dlb_xml.c')

libdlb_xml = static_library('dlb_xml',
  libdlb_xml_src,
  include_directories : libdlb_xml_inc,
  install : true)

libdlb_xml_dep = declare_dependency(include_directories : libdlb_xml_inc,
  link_with : libdlb_xml)