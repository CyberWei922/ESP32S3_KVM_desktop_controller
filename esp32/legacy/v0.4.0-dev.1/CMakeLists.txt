cmake_minimum_required(VERSION 3.16)

# The optional ignored Wi-Fi header may be created after the first build.
# Make CMake notice its creation, removal, and later edits.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/main/include/kvm_config.local.h")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/main/include/kvm_config.local.h")
    set(KVM_HAS_LOCAL_CONFIG 1)
else()
    set(KVM_HAS_LOCAL_CONFIG 0)
endif()
add_compile_definitions(KVM_HAS_LOCAL_CONFIG=${KVM_HAS_LOCAL_CONFIG})

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(kvm_controller)
