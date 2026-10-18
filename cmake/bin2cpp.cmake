# bin2cpp.cmake — 将二进制文件转换为 C++ 源文件（字节数组）
# 用法: cmake -DINPUT_FILE=<path> -DOUTPUT_FILE=<path> -P bin2cpp.cmake

if(NOT DEFINED INPUT_FILE OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "用法: cmake -DINPUT_FILE=<binary> -DOUTPUT_FILE=<output.cpp> -P bin2cpp.cmake")
endif()

if(NOT DEFINED SYMBOL_PREFIX)
    set(SYMBOL_PREFIX updater_exe)
endif()
if(NOT SYMBOL_PREFIX MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
    message(FATAL_ERROR "Invalid embedded binary symbol prefix")
endif()

# 读取二进制文件为十六进制字符串
file(READ "${INPUT_FILE}" hex_content HEX)
string(LENGTH "${hex_content}" hex_len)
math(EXPR byte_count "${hex_len} / 2")

# 将每对十六进制字符转换为 0xNN 格式
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," result "${hex_content}")

# 移除末尾逗号
string(REGEX REPLACE ",$" "" result "${result}")

# 写入 C++ 源文件
# 注意：C++ 中 const 变量默认内部链接，必须加 extern 才能跨翻译单元访问
file(WRITE "${OUTPUT_FILE}"
"// 自动生成文件 - 请勿手动编辑\n\
// 嵌入的 updater.exe 二进制数据\n\
#include <cstddef>\n\
\n\
extern const unsigned char ${SYMBOL_PREFIX}_data[] = {\n\
${result}\n\
};\n\
\n\
extern const size_t ${SYMBOL_PREFIX}_size = ${byte_count};\n"
)

message(STATUS "Embedded ${byte_count} bytes from ${INPUT_FILE} -> ${OUTPUT_FILE}")
