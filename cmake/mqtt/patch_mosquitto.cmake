# Mosquitto 2.1.2 builds libcommon as a DLL on Windows even for static clients.
# Keep the public API unchanged, but include it in our standalone EXE/helper.
set(common "${SOURCE_DIR}/libcommon/CMakeLists.txt")
file(READ "${common}" contents)
set(original "${contents}")
string(REPLACE "add_library(libmosquitto_common SHARED" "add_library(libmosquitto_common OBJECT" contents "${contents}")
if(NOT contents STREQUAL original)
    file(WRITE "${common}" "${contents}")
endif()
set(header "${SOURCE_DIR}/include/mosquitto/libcommon.h")
file(READ "${header}" contents)
set(original "${contents}")
string(REPLACE "#ifdef WIN32\n#  ifdef libmosquitto_common_EXPORTS"
    "#if defined(WIN32) && !defined(LIBMOSQUITTO_STATIC)\n#  ifdef libmosquitto_common_EXPORTS" contents "${contents}")
if(NOT contents STREQUAL original)
    file(WRITE "${header}" "${contents}")
endif()

# Limit allocations before an incoming packet body is read (also for MQTT 3.1.1).
set(packet "${SOURCE_DIR}/lib/packet_mosq.c")
file(READ "${packet}" contents)
set(original "${contents}")
string(REPLACE "/* FIXME - client case for incoming message received from broker too large */"
    "/* LogAnalyQt debug client: bound allocations before reading the body. */\n\t\tif(mosq->in_packet.remaining_length > 2U*1024U*1024U){\n\t\t\treturn MOSQ_ERR_OVERSIZE_PACKET;\n\t\t}"
    contents "${contents}")
if(NOT contents STREQUAL original)
    file(WRITE "${packet}" "${contents}")
endif()

# Match OpenSSL's UTF-8 filename handling instead of the Windows ANSI precheck.
set(options "${SOURCE_DIR}/lib/options.c")
file(READ "${options}" contents)
set(original "${contents}")
string(REPLACE "FILE *fptr;" "BIO *fptr;" contents "${contents}")
foreach(path cafile certfile keyfile)
    string(REPLACE "mosquitto_fopen(${path}, \"rt\", false)" "BIO_new_file(${path}, \"rt\")" contents "${contents}")
endforeach()
string(REPLACE "fclose(fptr);" "BIO_free(fptr);" contents "${contents}")
if(NOT contents STREQUAL original)
    file(WRITE "${options}" "${contents}")
endif()
