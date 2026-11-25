# The encoder is Nayuki's, unmodified (vendor/): it is built without our warnings.
idf_component_register(SRCS src/qr.c vendor/qrcodegen.c INCLUDE_DIRS include PRIV_INCLUDE_DIRS vendor REQUIRES cydui)
set_source_files_properties(vendor/qrcodegen.c PROPERTIES COMPILE_OPTIONS "-Wno-error")
