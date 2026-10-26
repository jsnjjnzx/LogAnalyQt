cmake_minimum_required(VERSION 3.24)

# 1. 寮哄埗闈欐€佺紪璇戞ā寮?(蹇呴』鍦?project 涔嬪墠)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build bundled dependencies statically" FORCE)

# 2. 绮惧噯鎸囧悜闈欐€?Qt 搴撹矾寰勫強宸ュ叿閾?
if(WIN32)
    set(QT_STATIC_DIR "D:/DevTool/QtStaticLib" CACHE PATH "Static Qt installation")
    if(EXISTS ${QT_STATIC_DIR})
        set(CMAKE_PREFIX_PATH "${QT_STATIC_DIR}" ${CMAKE_PREFIX_PATH})
        # 寮哄埗瑕嗙洊澶栭儴锛堝 VS Code Kit锛夎缃殑宸ュ叿閾炬枃浠讹紝纭繚浣跨敤闈欐€佺増閫昏緫
        set(CMAKE_TOOLCHAIN_FILE "${QT_STATIC_DIR}/lib/cmake/Qt6/qt.toolchain.cmake" CACHE FILEPATH "Static Toolchain" FORCE)
        # 寮哄埗鎸囧畾 Qt6 鐨勬悳绱㈡牴鐩綍
        set(Qt6_DIR "${QT_STATIC_DIR}/lib/cmake/Qt6")
        message(STATUS "Force searching Static Qt from: ${QT_STATIC_DIR}")
    endif()
endif()

# 3. 绂佺敤涓嶉渶瑕佺殑 Qt 缁勪欢鏌ユ壘锛堥伩鍏嶉潤鎬佺紪璇戜笅澶氬獟浣撳簱渚濊禆鎶ラ敊锛?
# 闈欐€?Qt 缂栬瘧鏃跺彲鑳界己灏?WMF 鏀寔锛屽鑷?Multimedia 鐩稿叧缁勪欢涓嶅彲鐢?

# 绂佺敤 WMF 鍜岀浉鍏冲寘鐨勬煡鎵撅紙娑堥櫎璀﹀憡锛?
set(CMAKE_DISABLE_FIND_PACKAGE_WMF ON CACHE BOOL "Disable WMF" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_Qt6Multimedia ON CACHE BOOL "Disable Qt6Multimedia" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_Qt6SpatialAudio ON CACHE BOOL "Disable Qt6SpatialAudio" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_Qt6Quick3DSpatialAudioPrivate ON CACHE BOOL "Disable Qt6Quick3DSpatialAudioPrivate" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_Qt6MultimediaQuickPrivate ON CACHE BOOL "Disable Qt6MultimediaQuickPrivate" FORCE)

# 鍏抽敭锛氬湪 find_package(Qt6) 涔嬪墠鍒涘缓鍋囩殑 IMPORTED 鐩爣
# 杩欐牱褰?Qt6QmlPlugins.cmake 鍔犺浇 quickmultimedia 鏃讹紝渚濊禆妫€鏌ヨ兘閫氳繃
# 杩欎簺鐩爣鍙槸鍗犱綅绗︼紝瀹為檯涓嶄細閾炬帴浠讳綍浠ｇ爜锛堥€氳繃 qt_import_plugins 鎺掗櫎锛?
# 蹇呴』鍚屾椂鍒涘缓涓荤洰鏍囥€佽祫婧愮洰鏍囧拰鍒濆鍖栫洰鏍囷紝鍚﹀垯 Qt6*Targets.cmake 浼氭姤閿?
if(WIN32)
foreach(_pkg
    Qt6::Multimedia
    Qt6::MultimediaPrivate
    Qt6::MultimediaWidgets
    Qt6::MultimediaWidgetsPrivate
    Qt6::MultimediaQuickPrivate
    Qt6::SpatialAudio
    Qt6::SpatialAudioPrivate
    Qt6::Quick3DSpatialAudioPrivate
    Qt6::quickmultimedia
    Qt6::quick3dspatialaudio
    # 璧勬簮鐩爣锛圦t 闈欐€佸簱闇€瑕侊級
    Qt6::Multimedia_resources_1
    Qt6::Multimedia_resources_2
    Qt6::Multimedia_resources_3
    Qt6::Multimedia_resources_4
    Qt6::MultimediaQuickPrivate_resources_1
    Qt6::MultimediaQuickPrivate_resources_2
    Qt6::MultimediaQuickPrivate_qmlcache
    # 鍒濆鍖栫洰鏍?
    Qt6::quickmultimedia_init
    Qt6::quick3dspatialaudio_init
    Qt6::Multimedia_init
    Qt6::SpatialAudio_init
)
    if(NOT TARGET ${_pkg})
        add_library(${_pkg} INTERFACE IMPORTED GLOBAL)
    endif()
endforeach()
endif()

# MQTT helpers must remain relocatable and the Windows download remains one EXE.
set(OPENSSL_USE_STATIC_LIBS TRUE)
foreach(openssl_library OPENSSL_SSL_LIBRARY OPENSSL_CRYPTO_LIBRARY)
    if("${${openssl_library}}" MATCHES "\\.(dylib|so)$")
        unset(${openssl_library} CACHE)
    endif()
endforeach()

# === libssh2 闈欐€佺紪璇?(浣跨敤 WinCNG 鍚庣锛屾棤闇€ OpenSSL) ===
if(WIN32)
    set(CRYPTO_BACKEND "WinCNG" CACHE STRING "libssh2 crypto backend" FORCE)
else()
    set(CRYPTO_BACKEND "OpenSSL" CACHE STRING "libssh2 crypto backend" FORCE)
endif()
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ENABLE_ZLIB_COMPRESSION OFF CACHE BOOL "" FORCE) # 閬垮厤瀵绘壘 zlib
add_subdirectory(third_party/libssh2)

project(LogAnalyQt VERSION 5.0.0 LANGUAGES CXX)
if(NOT WIN32)
    find_package(Iconv REQUIRED)
endif()
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 4. 寮哄埗 MinGW 瀹屽叏闈欐€佽繍琛岀幆澧?(纭繚鎺掗櫎瀵?libstdc++-6.dll 绛夌殑渚濊禆)
if(WIN32 AND MINGW)
    add_link_options("-static" "-static-libgcc" "-static-libstdc++")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")

    # 鏇挎崲 LTO 涓烘洿瀹夊叏鐨勬浠ｇ爜鍓旈櫎 (Dead Code Elimination)
    # 杩欒兘瀹夊叏鍦扮Щ闄ゆ湭浣跨敤鐨勫嚱鏁板拰鏁版嵁锛屽噺灏忎綋绉笖涓嶄細鎶ラ敊
    add_compile_options("-ffunction-sections" "-fdata-sections")
    add_link_options("-Wl,--gc-sections")

    # 閽堝鏌愪簺娣卞眰渚濊禆锛岄€氳繃璇ユ柟寮忓己鍒跺憡鐭ョ紪璇戝櫒
    set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")
    set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")
endif()

find_package(Qt6 6.11 REQUIRED COMPONENTS Quick Xml Network QuickControls2 Svg Concurrent)
include(cmake/MqttDependencies.cmake)

option(LOGANALYQT_BUILD_PROTOCOL_TESTS
       "Build LogAnalyQt protocol tests"
       OFF)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_library(loganalyqt_credentials STATIC src/core/SecureCredentialStore.h src/core/SecureCredentialStore.cpp)
target_link_libraries(loganalyqt_credentials PUBLIC Qt6::Core)
if(APPLE)
    target_link_libraries(loganalyqt_credentials PUBLIC "-framework Security" "-framework CoreFoundation")
elseif(WIN32)
    target_link_libraries(loganalyqt_credentials PUBLIC advapi32)
endif()

qt_add_library(loganalyqt_mqtt STATIC
    src/core/mqtt/MqttTypes.h src/core/mqtt/MqttTypes.cpp
    src/core/mqtt/MqttMessageModel.h src/core/mqtt/MqttMessageModel.cpp
    src/core/mqtt/MqttWorker.h src/core/mqtt/MqttWorker.cpp
    src/core/mqtt/MqttSession.h src/core/mqtt/MqttSession.cpp
    src/core/mqtt/MqttBroker.h src/core/mqtt/MqttBroker.cpp
    src/core/mqtt/MqttController.h src/core/mqtt/MqttController.cpp
    ${LOGANALYQT_MQTT_BROKER_RESOURCE})
target_include_directories(loganalyqt_mqtt PUBLIC src)
target_link_libraries(loganalyqt_mqtt PUBLIC Qt6::Core Qt6::Gui Qt6::Network Qt6::Concurrent libmosquitto_static loganalyqt_credentials)
target_link_libraries(loganalyqt_mqtt PRIVATE OpenSSL::SSL)
set_target_properties(loganalyqt_mqtt PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

# 灏嗙増鏈彿浼犻€掔粰 C++ 浠ｇ爜
if(DEFINED APP_VERSION_STR)
    add_compile_definitions(APP_VERSION=\"${APP_VERSION_STR}\")
else()
    add_compile_definitions(APP_VERSION=\"${PROJECT_VERSION}\")
endif()

qt_add_executable(appLogAnalyQt
    src/core/main.cpp
    src/core/config/ConfigManager.h src/core/config/ConfigManager.cpp
    src/core/config/ConfigMigration.h src/core/config/ConfigMigration.cpp
    src/core/LogBackend.h
    src/core/LogBackend.cpp
    src/core/ProtocolDetector.h
    src/core/ProtocolDetector.cpp
    src/common/DataParser.h
    src/common/DataParser.cpp
    src/protocols/app_msg/AppMsgDef.h
    src/protocols/app_msg/AppMsgLoader.h
    src/protocols/app_msg/AppMsgLoader.cpp
    src/protocols/app_msg/AppMsgParser.h
    src/protocols/app_msg/AppMsgParser.cpp
    src/protocols/app_msg/AppMsgServiceSchema.h
    src/protocols/app_msg/AppMsgServiceSchema.cpp
    src/protocols/app_msg/AppMsgDataEncoder.h
    src/protocols/app_msg/AppMsgDataEncoder.cpp
    src/protocols/app_msg/AppMsgBuilder.h
    src/protocols/app_msg/AppMsgBuilder.cpp
    src/protocols/module_msg/ModuleMsgDef.h
    src/protocols/module_msg/ModuleMsgLoader.h
    src/protocols/module_msg/ModuleMsgLoader.cpp
    src/protocols/module_msg/ModuleMsgParser.h
    src/protocols/module_msg/ModuleMsgParser.cpp
    src/protocols/698_prot/Prot698Def.h
    src/protocols/698_prot/Prot698Parser.h
    src/protocols/698_prot/Prot698Parser.cpp
    src/protocols/698_prot/Prot698Parser_proxy.cpp
    src/protocols/698_prot/Prot698Parser_get.cpp
    src/protocols/698_prot/Prot698Parser_set.cpp
    src/protocols/698_prot/Prot698Parser_action.cpp
    src/protocols/698_prot/Prot698Parser_report.cpp
    src/protocols/698_prot/Prot698Parser_security.cpp
    src/protocols/698_prot/Prot698OadManager.h
    src/protocols/698_prot/Prot698OadManager.cpp
    src/protocols/698_prot/Prot698DataEncoder.h
    src/protocols/698_prot/Prot698DataEncoder.cpp
    src/protocols/698_prot/Prot698ServiceBuilder.h
    src/protocols/698_prot/Prot698ServiceBuilder.cpp
    src/protocols/698_prot/Prot698FrameBuilder.h
    src/protocols/698_prot/Prot698FrameBuilder.cpp
    src/protocols/698_prot/Prot698ServiceSchema.h
    src/protocols/698_prot/Prot698ServiceSchema.cpp
    src/protocols/iec_104/Prot104Def.h
    src/protocols/iec_104/Prot104Parser.h
    src/protocols/iec_104/Prot104Parser.cpp
    src/protocols/iec_101/Prot101Def.h
    src/protocols/iec_101/Prot101Parser.h
    src/protocols/iec_101/Prot101Parser.cpp
    src/protocols/3762_prot/Prot3762Def.h
    src/protocols/3762_prot/Prot3762ConfigManager.h
    src/protocols/3762_prot/Prot3762ConfigManager.cpp
    src/protocols/3762_prot/Prot3762Parser.h
    src/protocols/3762_prot/Prot3762Parser.cpp
    src/protocols/645_prot/Prot645Def.h
    src/protocols/645_prot/Prot645ConfigManager.h
    src/protocols/645_prot/Prot645ConfigManager.cpp
    src/protocols/645_prot/Prot645Parser.h
    src/protocols/645_prot/Prot645Parser.cpp
    src/protocols/csg_upstream/CsgUpstreamDef.h
    src/protocols/csg_upstream/CsgUpstreamConfigManager.h
    src/protocols/csg_upstream/CsgUpstreamConfigManager.cpp
    src/protocols/csg_upstream/CsgUpstreamValueDecoder.h
    src/protocols/csg_upstream/CsgUpstreamValueDecoder.cpp
    src/protocols/csg_upstream/CsgUpstreamFieldParser.h
    src/protocols/csg_upstream/CsgUpstreamFieldParser.cpp
    src/protocols/csg_upstream/CsgUpstreamServiceParser.h
    src/protocols/csg_upstream/CsgUpstreamServiceParser.cpp
    src/protocols/csg_upstream/CsgUpstreamParser.h
    src/protocols/csg_upstream/CsgUpstreamParser.cpp
    src/core/PresetManager.h
    src/core/PresetManager.cpp
    src/core/WindowHelper.h
    src/core/WindowHelper.cpp
    src/core/UpdateChecker.h
    src/core/UpdateChecker.cpp
    src/core/ProtocolSyncManager.h
    src/core/ProtocolSyncManager.cpp
    src/core/SecurityStateStore.h
    src/core/SecurityStateStore.cpp
    src/core/LogMerger.h
    src/core/LogMerger.cpp
    src/core/SshTerminal.h
    src/core/SshTerminal.cpp
    src/core/SshLogStream.h
    src/core/SshLogStream.cpp
    src/core/SshHostKeyStore.h
    src/core/SmbShare.h
    src/core/SmbShare.cpp
)

set(AI_FRAMING_BASE_SOURCES
    src/core/SecureCredentialStore.h
    src/core/ai/DeepSeekClient.h src/core/ai/DeepSeekClient.cpp
    src/core/ai/AiCredentialStore.h src/core/ai/AiCredentialStore.cpp
    src/core/ai/AiCredentialWorker.h src/core/ai/AiCredentialWorker.cpp
    src/core/ai/AiModelSettings.h src/core/ai/AiModelSettings.cpp
)
target_sources(appLogAnalyQt PRIVATE ${AI_FRAMING_BASE_SOURCES}
    src/core/ai/AiFramingController.h src/core/ai/AiFramingController.cpp
    src/protocols/698_prot/Ai698Tools.h src/protocols/698_prot/Ai698Tools.cpp)
if(APPLE)
    target_sources(appLogAnalyQt PRIVATE src/core/MacWindowBackdrop.h src/core/MacWindowBackdrop.mm)
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
    target_link_libraries(appLogAnalyQt PRIVATE ${APPKIT_FRAMEWORK})
    find_library(AI_SECURITY_FRAMEWORK Security REQUIRED)
    find_library(AI_CORE_FOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    target_link_libraries(appLogAnalyQt PRIVATE ${AI_SECURITY_FRAMEWORK} ${AI_CORE_FOUNDATION_FRAMEWORK})
endif()

if(LOGANALYQT_BUILD_PROTOCOL_TESTS)
    enable_testing()
    target_compile_definitions(loganalyqt_mqtt PUBLIC LOGANALYQT_UI_TESTING)
    target_compile_definitions(loganalyqt_credentials PRIVATE LOGANALYQT_UI_TESTING)
    add_executable(mqtt_tests tests/mqtt_tests.cpp)
    target_link_libraries(mqtt_tests PRIVATE loganalyqt_mqtt OpenSSL::Crypto)
    set_target_properties(mqtt_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_dependencies(mqtt_tests mosquitto)
    add_test(NAME mqtt_tests COMMAND mqtt_tests $<TARGET_FILE:mosquitto>)
    set_tests_properties(mqtt_tests PROPERTIES TIMEOUT 90)
    target_compile_definitions(appLogAnalyQt PRIVATE LOGANALYQT_UI_TESTING)
    target_sources(appLogAnalyQt PRIVATE tests/ssh-performance/ssh_performance.cpp)
    target_compile_definitions(appLogAnalyQt PRIVATE LOGANALYQT_FULL_APP_BENCHMARK)
    if(WIN32)
        target_link_libraries(appLogAnalyQt PRIVATE psapi)
    endif()
    if(APPLE)
        find_package(Qt6 REQUIRED COMPONENTS Test)
        add_executable(mac_backdrop_tests tests/mac_backdrop_tests.mm src/core/MacWindowBackdrop.mm src/core/MacWindowBackdrop.h)
        target_include_directories(mac_backdrop_tests PRIVATE src)
        target_link_libraries(mac_backdrop_tests PRIVATE Qt6::Quick Qt6::Test ${APPKIT_FRAMEWORK})
        add_test(NAME mac_backdrop_tests COMMAND mac_backdrop_tests)
        set_tests_properties(mac_backdrop_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" TIMEOUT 30)
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME mqtt_broker_lifecycle_tests
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mqtt_broker_lifecycle.py $<TARGET_FILE:mosquitto>)
    set_tests_properties(mqtt_broker_lifecycle_tests PROPERTIES TIMEOUT 30)
    add_test(NAME mqtt_resource_embedding_tests
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mqtt_resource_embedding.py ${CMAKE_COMMAND} ${CMAKE_CXX_COMPILER})
    set_tests_properties(mqtt_resource_embedding_tests PROPERTIES TIMEOUT 30)
    add_executable(update_checker_tests tests/update_checker_tests.cpp
        src/core/UpdateChecker.h src/core/UpdateChecker.cpp)
    target_include_directories(update_checker_tests PRIVATE src)
    target_link_libraries(update_checker_tests PRIVATE Qt6::Core Qt6::Network Qt6::Gui)
    add_test(NAME update_checker_tests COMMAND update_checker_tests)
    add_test(NAME ui_migration_runtime
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_migration_runtime.py
                $<TARGET_FILE:appLogAnalyQt>)
    add_test(NAME ui_mqtt_parse_runtime
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_migration_runtime.py
                $<TARGET_FILE:appLogAnalyQt> --mqtt-parse-only)
    add_test(NAME ui_parse_table_runtime
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_migration_runtime.py
                $<TARGET_FILE:appLogAnalyQt> --parse-table-only)
    if(APPLE)
        add_test(NAME macos_window_activation
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_macos_window_activation.py
                    $<TARGET_FILE:appLogAnalyQt> --repeat 10)
        set_tests_properties(macos_window_activation PROPERTIES RUN_SERIAL TRUE TIMEOUT 240)
    endif()
    set(PROT698_TEST_ENGINE_SOURCES
        src/protocols/698_prot/Prot698DataEncoder.cpp
        src/protocols/698_prot/Prot698OadManager.cpp
        src/protocols/698_prot/Prot698ServiceSchema.cpp
        src/protocols/698_prot/Prot698ServiceBuilder.cpp
        src/protocols/698_prot/Prot698FrameBuilder.cpp
        src/protocols/698_prot/Prot698Parser.cpp
        src/protocols/698_prot/Prot698Parser_get.cpp
        src/protocols/698_prot/Prot698Parser_set.cpp
        src/protocols/698_prot/Prot698Parser_action.cpp
        src/protocols/698_prot/Prot698Parser_report.cpp
        src/protocols/698_prot/Prot698Parser_proxy.cpp
        src/protocols/698_prot/Prot698Parser_security.cpp
    )
    add_executable(prot698_boundary_tests tests/prot698_boundary_tests.cpp ${PROT698_TEST_ENGINE_SOURCES})
    target_include_directories(prot698_boundary_tests PRIVATE src/protocols src/core)
    target_compile_definitions(prot698_boundary_tests PRIVATE PROT698_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(prot698_boundary_tests PRIVATE Qt6::Core Qt6::Xml)
    add_test(NAME prot698_boundary_tests COMMAND prot698_boundary_tests)
    add_test(NAME prot698_qml_tests
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/prot698_qml_tests.py)
    set_tests_properties(prot698_qml_tests PROPERTIES TIMEOUT 120)
    if(APPLE)
        add_test(NAME stage10_qml_tests
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/stage10_qml_tests.py)
        set_tests_properties(stage10_qml_tests PROPERTIES TIMEOUT 120)
    endif()
    foreach(ai_test IN ITEMS ai_client_tests ai_settings_tests)
        add_executable(${ai_test} tests/${ai_test}.cpp ${AI_FRAMING_BASE_SOURCES})
        target_include_directories(${ai_test} PRIVATE src)
        target_link_libraries(${ai_test} PRIVATE Qt6::Core Qt6::Network)
        if(APPLE)
            target_link_libraries(${ai_test} PRIVATE ${AI_SECURITY_FRAMEWORK} ${AI_CORE_FOUNDATION_FRAMEWORK})
        elseif(WIN32)
            target_link_libraries(${ai_test} PRIVATE advapi32)
        endif()
        add_test(NAME ${ai_test} COMMAND ${ai_test})
    endforeach()
    add_executable(ai698_tools_tests tests/ai698_tools_tests.cpp
        src/protocols/698_prot/Ai698Tools.cpp ${PROT698_TEST_ENGINE_SOURCES})
    target_include_directories(ai698_tools_tests PRIVATE src/protocols src/core)
    target_compile_definitions(ai698_tools_tests PRIVATE AI698_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(ai698_tools_tests PRIVATE Qt6::Core Qt6::Xml)
    add_test(NAME ai698_tools_tests COMMAND ai698_tools_tests)
    add_executable(ai_controller_tests tests/ai_controller_tests.cpp
        src/core/ai/AiFramingController.h src/core/ai/AiFramingController.cpp
        src/protocols/698_prot/Ai698Tools.cpp ${PROT698_TEST_ENGINE_SOURCES} ${AI_FRAMING_BASE_SOURCES})
    target_include_directories(ai_controller_tests PRIVATE src src/protocols src/core)
    target_link_libraries(ai_controller_tests PRIVATE Qt6::Core Qt6::Xml Qt6::Network)
    if(APPLE)
        target_link_libraries(ai_controller_tests PRIVATE ${AI_SECURITY_FRAMEWORK} ${AI_CORE_FOUNDATION_FRAMEWORK})
    elseif(WIN32)
        target_link_libraries(ai_controller_tests PRIVATE advapi32)
    endif()
    add_test(NAME ai_controller_tests COMMAND ai_controller_tests)
    set_tests_properties(ai_controller_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    if(APPLE OR WIN32)
        add_executable(ai_credential_native_check EXCLUDE_FROM_ALL
            tests/ai_credential_native_check.cpp src/core/ai/AiCredentialStore.cpp
            src/core/SecureCredentialStore.cpp)
        target_include_directories(ai_credential_native_check PRIVATE src/core/ai)
        target_link_libraries(ai_credential_native_check PRIVATE Qt6::Core)
        if(APPLE)
            target_link_libraries(ai_credential_native_check PRIVATE ${AI_SECURITY_FRAMEWORK} ${AI_CORE_FOUNDATION_FRAMEWORK})
        else()
            target_link_libraries(ai_credential_native_check PRIVATE advapi32)
        endif()
    endif()
    if(APPLE)
        add_executable(secure_credential_tests tests/secure_credential_tests.cpp
            src/core/SecureCredentialStore.cpp)
        target_include_directories(secure_credential_tests PRIVATE src)
        target_link_libraries(secure_credential_tests PRIVATE Qt6::Core
            ${AI_SECURITY_FRAMEWORK} ${AI_CORE_FOUNDATION_FRAMEWORK})
        add_test(NAME secure_credential_tests COMMAND secure_credential_tests)
        add_executable(ssh_security_tests tests/ssh_security_tests.cpp
            src/core/SshTerminal.h src/core/SshTerminal.cpp
            src/core/SshLogStream.h src/core/SshLogStream.cpp
            src/core/SshHostKeyStore.h src/core/SecureCredentialStore.cpp)
        target_include_directories(ssh_security_tests PRIVATE src src/core)
        target_link_libraries(ssh_security_tests PRIVATE Qt6::Core Qt6::Gui
            Qt6::Network libssh2_static ${AI_SECURITY_FRAMEWORK} ${AI_CORE_FOUNDATION_FRAMEWORK})
        if(NOT WIN32)
            target_link_libraries(ssh_security_tests PRIVATE Iconv::Iconv)
        endif()
        add_test(NAME ssh_security_tests COMMAND ssh_security_tests)
        set_tests_properties(ssh_security_tests PROPERTIES TIMEOUT 60)
        add_executable(secure_credential_native_check EXCLUDE_FROM_ALL
            tests/secure_credential_native_check.cpp src/core/SecureCredentialStore.cpp)
        target_include_directories(secure_credential_native_check PRIVATE src)
        target_compile_definitions(secure_credential_native_check PRIVATE LOGANALYQT_UI_TESTING)
        target_link_libraries(secure_credential_native_check PRIVATE Qt6::Core
            ${AI_SECURITY_FRAMEWORK} ${AI_CORE_FOUNDATION_FRAMEWORK})
    endif()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(ssh_stream_tests tests/ssh_stream_tests.cpp
        src/core/SshTerminal.h src/core/SshTerminal.cpp
        src/core/SshLogStream.h src/core/SshLogStream.cpp src/core/SecureCredentialStore.cpp)
    target_include_directories(ssh_stream_tests PRIVATE src src/core)
    target_link_libraries(ssh_stream_tests PRIVATE Qt6::Gui Qt6::Network Qt6::Test libssh2_static)
    if(APPLE)
        target_link_libraries(ssh_stream_tests PRIVATE Iconv::Iconv ${AI_SECURITY_FRAMEWORK} ${AI_CORE_FOUNDATION_FRAMEWORK})
    elseif(WIN32)
        target_link_libraries(ssh_stream_tests PRIVATE advapi32)
    else()
        target_link_libraries(ssh_stream_tests PRIVATE Iconv::Iconv)
    endif()
    add_test(NAME ssh_stream_tests COMMAND ssh_stream_tests)
    set_tests_properties(ssh_stream_tests PROPERTIES TIMEOUT 30)

    add_executable(app_paths_tests
        tests/app_paths_tests.cpp
    )
    target_include_directories(app_paths_tests PRIVATE src)
    target_compile_definitions(app_paths_tests PRIVATE LOGANALYQT_UI_TESTING)
    target_link_libraries(app_paths_tests PRIVATE Qt6::Core)
    add_test(NAME app_paths_tests COMMAND app_paths_tests)

    add_executable(csg_upstream_tests
        tests/csg_upstream_tests.cpp
        tests/protocol_detector_stubs.cpp
        src/core/ProtocolDetector.h
        src/core/ProtocolDetector.cpp
        src/protocols/csg_upstream/CsgUpstreamDef.h
        src/protocols/csg_upstream/CsgUpstreamConfigManager.h
        src/protocols/csg_upstream/CsgUpstreamConfigManager.cpp
        src/protocols/csg_upstream/CsgUpstreamValueDecoder.h
        src/protocols/csg_upstream/CsgUpstreamValueDecoder.cpp
        src/protocols/csg_upstream/CsgUpstreamFieldParser.h
        src/protocols/csg_upstream/CsgUpstreamFieldParser.cpp
        src/protocols/csg_upstream/CsgUpstreamServiceParser.h
        src/protocols/csg_upstream/CsgUpstreamServiceParser.cpp
        src/protocols/csg_upstream/CsgUpstreamParser.h
        src/protocols/csg_upstream/CsgUpstreamParser.cpp
    )
    target_include_directories(csg_upstream_tests PRIVATE
        src
        src/core
        src/protocols
    )
    target_compile_definitions(csg_upstream_tests PRIVATE
        CSG_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(csg_upstream_tests PRIVATE Qt6::Core Qt6::Xml)
    add_test(NAME csg_upstream_tests COMMAND csg_upstream_tests)

    add_executable(csg_upstream_service_tests
        tests/csg_upstream_service_tests.cpp
        tests/csg_upstream_test_support.h
        tests/protocol_detector_stubs.cpp
        src/core/ProtocolDetector.h
        src/core/ProtocolDetector.cpp
        src/protocols/csg_upstream/CsgUpstreamDef.h
        src/protocols/csg_upstream/CsgUpstreamConfigManager.h
        src/protocols/csg_upstream/CsgUpstreamConfigManager.cpp
        src/protocols/csg_upstream/CsgUpstreamValueDecoder.h
        src/protocols/csg_upstream/CsgUpstreamValueDecoder.cpp
        src/protocols/csg_upstream/CsgUpstreamFieldParser.h
        src/protocols/csg_upstream/CsgUpstreamFieldParser.cpp
        src/protocols/csg_upstream/CsgUpstreamServiceParser.h
        src/protocols/csg_upstream/CsgUpstreamServiceParser.cpp
        src/protocols/csg_upstream/CsgUpstreamParser.h
        src/protocols/csg_upstream/CsgUpstreamParser.cpp
    )
    target_include_directories(csg_upstream_service_tests PRIVATE
        src
        src/core
        src/protocols
        tests
    )
    target_compile_definitions(csg_upstream_service_tests PRIVATE
        CSG_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(csg_upstream_service_tests PRIVATE
        Qt6::Core
        Qt6::Xml
    )
    add_test(NAME csg_upstream_service_tests
             COMMAND csg_upstream_service_tests)

    add_executable(csg_upstream_value_tests
        tests/csg_upstream_value_tests.cpp
        tests/csg_upstream_test_support.h
        tests/protocol_detector_stubs.cpp
        src/core/ProtocolDetector.h
        src/core/ProtocolDetector.cpp
        src/protocols/csg_upstream/CsgUpstreamDef.h
        src/protocols/csg_upstream/CsgUpstreamConfigManager.h
        src/protocols/csg_upstream/CsgUpstreamConfigManager.cpp
        src/protocols/csg_upstream/CsgUpstreamValueDecoder.h
        src/protocols/csg_upstream/CsgUpstreamValueDecoder.cpp
        src/protocols/csg_upstream/CsgUpstreamFieldParser.h
        src/protocols/csg_upstream/CsgUpstreamFieldParser.cpp
        src/protocols/csg_upstream/CsgUpstreamServiceParser.h
        src/protocols/csg_upstream/CsgUpstreamServiceParser.cpp
        src/protocols/csg_upstream/CsgUpstreamParser.h
        src/protocols/csg_upstream/CsgUpstreamParser.cpp
    )
    target_include_directories(csg_upstream_value_tests PRIVATE
        src
        src/core
        src/protocols
        tests
    )
    target_link_libraries(csg_upstream_value_tests PRIVATE
        Qt6::Core
        Qt6::Xml
    )
    add_test(NAME csg_upstream_value_tests
             COMMAND csg_upstream_value_tests)

    add_executable(csg_upstream_parameter_tests
        tests/csg_upstream_parameter_tests.cpp
        tests/csg_upstream_test_support.h
        tests/protocol_detector_stubs.cpp
        src/core/ProtocolDetector.h
        src/core/ProtocolDetector.cpp
        src/protocols/csg_upstream/CsgUpstreamDef.h
        src/protocols/csg_upstream/CsgUpstreamConfigManager.h
        src/protocols/csg_upstream/CsgUpstreamConfigManager.cpp
        src/protocols/csg_upstream/CsgUpstreamValueDecoder.h
        src/protocols/csg_upstream/CsgUpstreamValueDecoder.cpp
        src/protocols/csg_upstream/CsgUpstreamFieldParser.h
        src/protocols/csg_upstream/CsgUpstreamFieldParser.cpp
        src/protocols/csg_upstream/CsgUpstreamServiceParser.h
        src/protocols/csg_upstream/CsgUpstreamServiceParser.cpp
        src/protocols/csg_upstream/CsgUpstreamParser.h
        src/protocols/csg_upstream/CsgUpstreamParser.cpp
    )
    target_include_directories(csg_upstream_parameter_tests PRIVATE
        src
        src/core
        src/protocols
        tests
    )
    target_compile_definitions(csg_upstream_parameter_tests PRIVATE
        CSG_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(csg_upstream_parameter_tests PRIVATE
        Qt6::Core
        Qt6::Xml
    )
    add_test(NAME csg_upstream_parameter_tests
             COMMAND csg_upstream_parameter_tests)

    add_executable(csg_upstream_measurement_tests
        tests/csg_upstream_measurement_tests.cpp
        tests/csg_upstream_test_support.h
        tests/protocol_detector_stubs.cpp
        src/core/ProtocolDetector.h
        src/core/ProtocolDetector.cpp
        src/protocols/csg_upstream/CsgUpstreamDef.h
        src/protocols/csg_upstream/CsgUpstreamConfigManager.h
        src/protocols/csg_upstream/CsgUpstreamConfigManager.cpp
        src/protocols/csg_upstream/CsgUpstreamValueDecoder.h
        src/protocols/csg_upstream/CsgUpstreamValueDecoder.cpp
        src/protocols/csg_upstream/CsgUpstreamFieldParser.h
        src/protocols/csg_upstream/CsgUpstreamFieldParser.cpp
        src/protocols/csg_upstream/CsgUpstreamServiceParser.h
        src/protocols/csg_upstream/CsgUpstreamServiceParser.cpp
        src/protocols/csg_upstream/CsgUpstreamParser.h
        src/protocols/csg_upstream/CsgUpstreamParser.cpp
    )
    target_include_directories(csg_upstream_measurement_tests PRIVATE
        src
        src/core
        src/protocols
        tests
    )
    target_compile_definitions(csg_upstream_measurement_tests PRIVATE
        CSG_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(csg_upstream_measurement_tests PRIVATE
        Qt6::Core
        Qt6::Xml
    )
    add_test(NAME csg_upstream_measurement_tests
             COMMAND csg_upstream_measurement_tests)

    add_executable(csg_upstream_task_family_tests
        tests/csg_upstream_task_family_tests.cpp
        tests/csg_upstream_test_support.h
        tests/protocol_detector_stubs.cpp
        src/core/ProtocolDetector.h src/core/ProtocolDetector.cpp
        src/protocols/csg_upstream/CsgUpstreamDef.h
        src/protocols/csg_upstream/CsgUpstreamConfigManager.h
        src/protocols/csg_upstream/CsgUpstreamConfigManager.cpp
        src/protocols/csg_upstream/CsgUpstreamValueDecoder.h
        src/protocols/csg_upstream/CsgUpstreamValueDecoder.cpp
        src/protocols/csg_upstream/CsgUpstreamFieldParser.h
        src/protocols/csg_upstream/CsgUpstreamFieldParser.cpp
        src/protocols/csg_upstream/CsgUpstreamServiceParser.h
        src/protocols/csg_upstream/CsgUpstreamServiceParser.cpp
        src/protocols/csg_upstream/CsgUpstreamParser.h
        src/protocols/csg_upstream/CsgUpstreamParser.cpp)
    target_include_directories(csg_upstream_task_family_tests PRIVATE
        src src/core src/protocols tests)
    target_compile_definitions(csg_upstream_task_family_tests PRIVATE
        CSG_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(csg_upstream_task_family_tests PRIVATE Qt6::Core Qt6::Xml)
    add_test(NAME csg_upstream_task_family_tests COMMAND csg_upstream_task_family_tests)

    add_executable(csg_upstream_alarm_event_tests
        tests/csg_upstream_alarm_event_tests.cpp
        tests/csg_upstream_test_support.h
        tests/protocol_detector_stubs.cpp
        src/core/ProtocolDetector.h src/core/ProtocolDetector.cpp
        src/protocols/csg_upstream/CsgUpstreamDef.h
        src/protocols/csg_upstream/CsgUpstreamConfigManager.h
        src/protocols/csg_upstream/CsgUpstreamConfigManager.cpp
        src/protocols/csg_upstream/CsgUpstreamValueDecoder.h
        src/protocols/csg_upstream/CsgUpstreamValueDecoder.cpp
        src/protocols/csg_upstream/CsgUpstreamFieldParser.h
        src/protocols/csg_upstream/CsgUpstreamFieldParser.cpp
        src/protocols/csg_upstream/CsgUpstreamServiceParser.h
        src/protocols/csg_upstream/CsgUpstreamServiceParser.cpp
        src/protocols/csg_upstream/CsgUpstreamParser.h
        src/protocols/csg_upstream/CsgUpstreamParser.cpp)
    target_include_directories(csg_upstream_alarm_event_tests PRIVATE
        src src/core src/protocols tests)
    target_compile_definitions(csg_upstream_alarm_event_tests PRIVATE
        CSG_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(csg_upstream_alarm_event_tests PRIVATE Qt6::Core Qt6::Xml)
    add_test(NAME csg_upstream_alarm_event_tests COMMAND csg_upstream_alarm_event_tests)

    add_executable(csg_upstream_transfer_tests
        tests/csg_upstream_transfer_tests.cpp
        tests/csg_upstream_test_support.h
        tests/protocol_detector_stubs.cpp
        src/core/ProtocolDetector.h src/core/ProtocolDetector.cpp
        src/protocols/csg_upstream/CsgUpstreamDef.h
        src/protocols/csg_upstream/CsgUpstreamConfigManager.h
        src/protocols/csg_upstream/CsgUpstreamConfigManager.cpp
        src/protocols/csg_upstream/CsgUpstreamValueDecoder.h
        src/protocols/csg_upstream/CsgUpstreamValueDecoder.cpp
        src/protocols/csg_upstream/CsgUpstreamFieldParser.h
        src/protocols/csg_upstream/CsgUpstreamFieldParser.cpp
        src/protocols/csg_upstream/CsgUpstreamServiceParser.h
        src/protocols/csg_upstream/CsgUpstreamServiceParser.cpp
        src/protocols/csg_upstream/CsgUpstreamParser.h
        src/protocols/csg_upstream/CsgUpstreamParser.cpp)
    target_include_directories(csg_upstream_transfer_tests PRIVATE
        src src/core src/protocols tests)
    target_compile_definitions(csg_upstream_transfer_tests PRIVATE
        CSG_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(csg_upstream_transfer_tests PRIVATE Qt6::Core Qt6::Xml)
    add_test(NAME csg_upstream_transfer_tests COMMAND csg_upstream_transfer_tests)

    add_executable(csg_upstream_security_tests
        tests/csg_upstream_security_tests.cpp
        tests/csg_upstream_test_support.h
        tests/protocol_detector_stubs.cpp
        src/core/ProtocolDetector.h src/core/ProtocolDetector.cpp
        src/protocols/csg_upstream/CsgUpstreamDef.h
        src/protocols/csg_upstream/CsgUpstreamConfigManager.h
        src/protocols/csg_upstream/CsgUpstreamConfigManager.cpp
        src/protocols/csg_upstream/CsgUpstreamValueDecoder.h
        src/protocols/csg_upstream/CsgUpstreamValueDecoder.cpp
        src/protocols/csg_upstream/CsgUpstreamFieldParser.h
        src/protocols/csg_upstream/CsgUpstreamFieldParser.cpp
        src/protocols/csg_upstream/CsgUpstreamServiceParser.h
        src/protocols/csg_upstream/CsgUpstreamServiceParser.cpp
        src/protocols/csg_upstream/CsgUpstreamParser.h
        src/protocols/csg_upstream/CsgUpstreamParser.cpp)
    target_include_directories(csg_upstream_security_tests PRIVATE
        src src/core src/protocols tests)
    target_compile_definitions(csg_upstream_security_tests PRIVATE
        CSG_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(csg_upstream_security_tests PRIVATE Qt6::Core Qt6::Xml)
    add_test(NAME csg_upstream_security_tests COMMAND csg_upstream_security_tests)

    add_executable(protocol_sync_tests
        tests/protocol_sync_tests.cpp
        src/core/SmbShare.h src/core/SmbShare.cpp
        src/core/SecureCredentialStore.cpp
        src/core/ProtocolSyncManager.h
        src/core/ProtocolSyncManager.cpp
        src/protocols/csg_upstream/CsgUpstreamDef.h
        src/protocols/csg_upstream/CsgUpstreamConfigManager.h
        src/protocols/csg_upstream/CsgUpstreamConfigManager.cpp
    )
    target_include_directories(protocol_sync_tests PRIVATE
        src
        src/core
        src/protocols
    )
    target_compile_definitions(protocol_sync_tests PRIVATE
        CSG_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        LOGANALYQT_UI_TESTING
    )
    target_link_libraries(protocol_sync_tests PRIVATE Qt6::Core Qt6::Xml Qt6::Gui)
    if(APPLE)
        target_link_libraries(protocol_sync_tests PRIVATE ${AI_SECURITY_FRAMEWORK} ${AI_CORE_FOUNDATION_FRAMEWORK})
    elseif(WIN32)
        target_link_libraries(protocol_sync_tests PRIVATE advapi32)
    endif()
    set_target_properties(protocol_sync_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            "${CMAKE_CURRENT_BINARY_DIR}/test-runtime/CsgProtocolSync"
    )
    add_test(NAME protocol_sync_tests COMMAND protocol_sync_tests)
endif()

if(WIN32)
    target_sources(appLogAnalyQt PRIVATE assets/logo.rc)
endif()

if(APPLE)
    set_source_files_properties(assets/logo.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    target_sources(appLogAnalyQt PRIVATE assets/logo.icns)
endif()

# 纭繚澶存枃浠惰矾寰勮В鏋愭甯革細娣诲姞 src 鍙婂叾瀛愮洰褰?
target_include_directories(appLogAnalyQt PRIVATE src src/core src/protocols ${CMAKE_SOURCE_DIR}/third_party/libssh2/include)
if(NOT WIN32)
    target_link_libraries(appLogAnalyQt PRIVATE Iconv::Iconv)
endif()

# 瀹氫箟鍗忚XML鏂囦欢
set(PROTOCOL_XML_FILES
    "protocol_xml/698_protocol.xml"
    "protocol_xml/app_msg_protocol.xml"
    "protocol_xml/module_msg_protocol.xml"
    "protocol_xml/3762_protocol.xml"
    "protocol_xml/645_protocol.xml"
    "protocol_xml/csg_upstream_protocol.xml"
)

# 灏嗗崗璁枃浠跺姞鍏ヨ祫婧愮郴缁燂紝瀹炵幇鍗曟枃浠惰繍琛?
qt_add_resources(appLogAnalyQt "protocols"
    PREFIX "/"
    FILES ${PROTOCOL_XML_FILES}
)

qt_add_resources(appLogAnalyQt "documentation"
    PREFIX "/docs"
    FILES README.md CHANGELOG.md
)
qt_add_resources(appLogAnalyQt "mqtt_licenses"
    PREFIX "/mqtt" BASE licenses FILES licenses/MQTT-THIRD-PARTY.txt)

# 灏嗗崗璁?XML 鏂囦欢澶嶅埗鍒版瀯寤虹洰褰曪紝鏂逛究璋冭瘯鍜屽厑璁稿閮ㄤ慨鏀?
foreach(xml_file ${PROTOCOL_XML_FILES})
    get_filename_component(xml_name ${xml_file} NAME)
    configure_file(${xml_file} ${CMAKE_CURRENT_BINARY_DIR}/protocol_xml/${xml_name} COPYONLY)
    # 鍚屾椂澶嶅埗涓€浠藉埌鏍圭洰褰曪紝鍏煎鏌愪簺鍔犺浇閫昏緫
    configure_file(${xml_file} ${CMAKE_CURRENT_BINARY_DIR}/${xml_name} COPYONLY)
endforeach()

add_subdirectory(qml/Next)
target_link_libraries(appLogAnalyQt PRIVATE nextUiPlugin)

qt_add_qml_module(appLogAnalyQt
    URI LogAnalyQt
    VERSION 1.3
    RESOURCES
        assets/logo.ico
        assets/materialdesignicons-webfont.ttf
)

# 闈欐€佺紪璇戞椂閾炬帴鏍囧噯搴?
if(WIN32 AND MINGW)
    target_link_options(appLogAnalyQt PRIVATE "-static" "-static-libgcc" "-static-libstdc++")
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appLogAnalyQt PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER com.loganalyqt.LogAnalyQt
    MACOSX_BUNDLE_BUNDLE_NAME "通联工坊"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/assets/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_ICON_FILE "logo.icns"
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# 闈欐€佺紪璇戞彃浠剁鐞嗭細浣跨敤鎺掗櫎娉曠槮韬?
if(WIN32 AND NOT BUILD_SHARED_LIBS)
    qt_import_plugins(appLogAnalyQt
        EXCLUDE
            # === 澶氬獟浣?(闈欐€佸簱鏈紪璇?WMF 鏀寔) ===
            Qt6::QMultimediaPlugin
            Qt6::quickmultimedia

            # === 3D / 鍔ㄧ敾 / 娓叉煋 (澶т綋绉? ===
            Qt6::QtQuick3DPlugin
            Qt6::QtQuick3DRuntimeRenderPlugin
            Qt6::QtQuick3DHelpersPlugin
            Qt6::QtQuick3DParticleEffectsPlugin
            Qt6::QtQuick3DParticlesPlugin
            Qt6::QLottieAnimationPlugin
            Qt6::QQuickTimelinePlugin

            # === 杈撳叆娉曚笌璁惧 ===
            Qt6::QVirtualKeyboardPlugin
            Qt6::QVirtualKeyboardSettingsPlugin
            Qt6::QTuioTouchPlugin

            # === 缃戠粶 (椤圭洰鏈秹鍙? ===
            Qt6::QNetworkManagerNetworkInformationPlugin

            # === 鎵撳嵃 ===
            Qt6::QWindowsPrinterSupportPlugin

            # === 闈炲繀椤诲浘鐗囨牸寮?(浠呬繚鐣?PNG/JPG/SVG) ===
            Qt6::QGifPlugin
            Qt6::QWebpPlugin
            Qt6::QWbmpPlugin
            Qt6::QTiffPlugin
            Qt6::QHeifPlugin
    )
endif()

target_link_libraries(appLogAnalyQt
    PRIVATE Qt6::Quick Qt6::Xml Qt6::Network Qt6::QuickControls2 Qt6::Svg
    PRIVATE libssh2_static loganalyqt_mqtt
)

if(WIN32)
    target_link_libraries(appLogAnalyQt PRIVATE
        dwmapi shell32 user32 gdi32 advapi32 ole32 oleaut32 uuid version
        winmm ws2_32 iphlpapi bcrypt crypt32
    )
endif()

# 缁堟瀬鐦﹁韩锛歋trip + UPX 鍘嬬缉
if(WIN32 AND MINGW)
    # 姝ラ1: Strip 鍘婚櫎璋冭瘯绗﹀彿
    add_custom_command(TARGET appLogAnalyQt POST_BUILD
        COMMAND "${CMAKE_STRIP}" -s "$<TARGET_FILE:appLogAnalyQt>"
        COMMENT "1. Stripping symbols..."
        VERBATIM
    )
endif()

# === 鐙珛鏇存柊鍣?(updater.exe) ===
if(WIN32)
add_executable(updater WIN32
    updater/updater_main.cpp
)

target_link_libraries(updater PRIVATE
    shell32 user32 advapi32
)

if(WIN32 AND MINGW)
    target_link_options(updater PRIVATE "-static" "-static-libgcc" "-static-libstdc++" "-municode")
    target_compile_options(updater PRIVATE "-ffunction-sections" "-fdata-sections" "-municode")
    target_link_options(updater PRIVATE "-Wl,--gc-sections")

    # Strip updater
    add_custom_command(TARGET updater POST_BUILD
        COMMAND "${CMAKE_STRIP}" -s "$<TARGET_FILE:updater>"
        COMMENT "Stripping updater..."
        VERBATIM
    )
endif()

# 灏?updater.exe 宓屽叆涓荤▼搴忥紙淇濇寔鍗曟枃浠跺彂甯冿級
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/updater_resource.cpp"
    COMMAND ${CMAKE_COMMAND}
        -DINPUT_FILE=$<TARGET_FILE:updater>
        -DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/updater_resource.cpp
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/bin2cpp.cmake
    DEPENDS updater
    COMMENT "Embedding updater.exe into main app..."
    VERBATIM
)
target_sources(appLogAnalyQt PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/updater_resource.cpp")
if(TARGET update_checker_tests)
    target_sources(update_checker_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/updater_resource.cpp")
endif()
endif()

if(APPLE)
    add_custom_target(bundle_mqtt_broker ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:appLogAnalyQt>/Contents/Helpers"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:mosquitto>"
            "$<TARGET_BUNDLE_DIR:appLogAnalyQt>/Contents/Helpers/LogAnalyQtBroker"
        DEPENDS appLogAnalyQt mosquitto VERBATIM)
    add_executable(updater
        updater/updater_main_macos.cpp
    )
    set_target_properties(updater PROPERTIES
        OUTPUT_NAME "LogAnalyQtUpdater"
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )

    add_custom_target(bundle_macos_updater ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:appLogAnalyQt>/Contents/Helpers"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:updater>"
            "$<TARGET_BUNDLE_DIR:appLogAnalyQt>/Contents/Helpers/LogAnalyQtUpdater"
        DEPENDS appLogAnalyQt updater
        COMMENT "Bundling macOS updater helper"
        VERBATIM
    )
endif()

if(LOGANALYQT_BUILD_PROTOCOL_TESTS AND (WIN32 OR APPLE))
    if(WIN32)
        add_executable(updater_test_helper WIN32 updater/updater_main.cpp)
        target_link_libraries(updater_test_helper PRIVATE shell32 user32 advapi32)
        if(MINGW)
            target_link_options(updater_test_helper PRIVATE "-municode")
        endif()
    else()
        add_executable(updater_test_helper updater/updater_main_macos.cpp)
    endif()
    set_target_properties(updater_test_helper PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_compile_definitions(updater_test_helper PRIVATE LOGANALYQT_UPDATER_TESTING)
    add_executable(updater_restart_fixture tests/updater_restart_fixture.cpp)
    add_test(NAME updater_replacement_tests
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_updater.py
            $<TARGET_FILE:updater_test_helper> $<TARGET_FILE:updater_restart_fixture>)
endif()

include(GNUInstallDirs)
install(TARGETS appLogAnalyQt
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# One editable prompt and a bundled fallback for missing portable files.
foreach(prompt_target IN ITEMS appLogAnalyQt ai_controller_tests protocol_sync_tests)
    if(TARGET ${prompt_target})
        target_sources(${prompt_target} PRIVATE src/core/ai/AiPromptFile.cpp src/core/ai/AiPromptFile.h)
        qt_add_resources(${prompt_target} "ai_prompts" PREFIX "/" FILES prompts/ai_698_framing.md)
    endif()
endforeach()

foreach(config_consumer IN ITEMS appLogAnalyQt ai_client_tests ai_settings_tests ai_controller_tests ai_credential_native_check protocol_sync_tests)
    if(TARGET ${config_consumer})
        target_link_libraries(${config_consumer} PRIVATE loganalyqt_credentials)
    endif()
endforeach()
if(LOGANALYQT_BUILD_PROTOCOL_TESTS)
    add_executable(config_tests tests/config_tests.cpp src/core/config/ConfigMigration.cpp src/core/config/ConfigManager.cpp)
    target_include_directories(config_tests PRIVATE src)
    target_link_libraries(config_tests PRIVATE loganalyqt_mqtt Qt6::Xml)
    set_target_properties(config_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME config_tests COMMAND config_tests)
endif()
