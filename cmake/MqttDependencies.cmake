include(FetchContent)
find_package(OpenSSL REQUIRED)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UTILS OFF CACHE BOOL "" FORCE)
set(ENABLE_CUSTOM_COMPILER_FLAGS OFF CACHE BOOL "" FORCE)
set(CJSON_OVERRIDE_BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(CJSON_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(mqtt_cjson
    URL https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.19.tar.gz
    URL_HASH SHA256=7fa616e3046edfa7a28a32d5f9eacfd23f92900fe1f8ccd988c1662f30454562
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(mqtt_cjson)
set(LOGANALYQT_MQTT_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/mqtt-include")
file(MAKE_DIRECTORY "${LOGANALYQT_MQTT_INCLUDE_DIR}/cjson")
configure_file("${mqtt_cjson_SOURCE_DIR}/cJSON.h" "${LOGANALYQT_MQTT_INCLUDE_DIR}/cjson/cJSON.h" COPYONLY)
target_include_directories(cjson PUBLIC "$<BUILD_INTERFACE:${LOGANALYQT_MQTT_INCLUDE_DIR}>")
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/mqtt")

# Each client is exclusively owned by one Qt worker; no pthreads DLL is needed.
foreach(option WITH_THREADING WITH_SRV WITH_SOCKS WITH_WEBSOCKETS WITH_UNIX_SOCKETS
        WITH_TLS_PSK WITH_TESTS WITH_CLIENTS WITH_APPS WITH_PLUGINS WITH_DOCS
        WITH_LIB_CPP WITH_HTTP_API WITH_CTRL_SHELL WITH_LTO WITH_CONTROL
        WITH_PERSISTENCE INC_BRIDGE_SUPPORT INC_MEMTRACK)
    set(${option} OFF CACHE BOOL "" FORCE)
endforeach()
set(WITH_STATIC_LIBRARIES ON CACHE BOOL "" FORCE)
set(WITH_TLS ON CACHE BOOL "" FORCE)
set(WITH_BROKER ON CACHE BOOL "" FORCE)
set(WITH_BUNDLED_DEPS ON CACHE BOOL "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_argon2 TRUE)
FetchContent_Declare(mqtt_mosquitto
    URL https://mosquitto.org/files/source/mosquitto-2.1.2.tar.gz
    URL_HASH SHA256=fd905380691ac65ea5a93779e8214941829e3d6e038d5edff9eac5fd74cbed02
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    PATCH_COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=<SOURCE_DIR>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/mqtt/patch_mosquitto.cmake)
FetchContent_MakeAvailable(mqtt_mosquitto)
target_compile_definitions(libmosquitto_common PUBLIC LIBMOSQUITTO_STATIC)
target_include_directories(libmosquitto_static PUBLIC "${mqtt_mosquitto_SOURCE_DIR}/include" "${LOGANALYQT_MQTT_INCLUDE_DIR}")
target_include_directories(libmosquitto_static PRIVATE "${mqtt_mosquitto_SOURCE_DIR}/deps"
    "${mqtt_mosquitto_SOURCE_DIR}/common" "${mqtt_mosquitto_SOURCE_DIR}/libcommon")
target_sources(libmosquitto_static PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/core/mqtt/MqttConnect.c")
set_target_properties(libmosquitto PROPERTIES EXCLUDE_FROM_ALL TRUE)
set_target_properties(mosquitto PROPERTIES OUTPUT_NAME LogAnalyQtBroker)
set_source_files_properties("${mqtt_mosquitto_SOURCE_DIR}/src/mosquitto.c"
    DIRECTORY "${mqtt_mosquitto_SOURCE_DIR}/src"
    PROPERTIES COMPILE_DEFINITIONS "main=loganalyqt_mosquitto_main")
target_sources(mosquitto PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/core/mqtt/BrokerMain.cpp")
find_package(Threads REQUIRED)
target_link_libraries(mosquitto PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(mosquitto PRIVATE ws2_32 bcrypt crypt32 gdi32 advapi32 user32)
    target_link_libraries(libmosquitto_static PRIVATE ws2_32 bcrypt crypt32 gdi32 advapi32 user32)
endif()

if(WIN32)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/mqtt_broker_resource.cpp"
        COMMAND ${CMAKE_COMMAND} -DINPUT_FILE=$<TARGET_FILE:mosquitto>
            -DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/mqtt_broker_resource.cpp
            -DSYMBOL_PREFIX=mqtt_broker
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/bin2cpp.cmake
        DEPENDS mosquitto VERBATIM)
    set(LOGANALYQT_MQTT_BROKER_RESOURCE "${CMAKE_CURRENT_BINARY_DIR}/mqtt_broker_resource.cpp")
endif()
