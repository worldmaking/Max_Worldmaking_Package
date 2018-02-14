# this modifies libLeap.dylib 
# so that it can be loaded from within a bundle
# (only needs to be run when the libLeap.dylib is updated)

install_name_tool -id @loader_path/../Frameworks/libLeap.dylib libLeap.dylib