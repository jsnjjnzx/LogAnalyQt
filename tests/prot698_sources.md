# 698 独立验证来源登记

查阅时间：2026-10-14。测试不联网。以下来源用于验证当前工程行为，没有选择或升级标准年号；开源项目实现不是适用标准全文或设备认证。

## I01：独立项目中的固定完整请求帧

- 项目：`vedrlabs/go-dlt698-45`（原 `VaccariaSeed/go-dlt698-45`）。通过 GitHub connector 取得原始文件。
- 固定提交：`aa1d15614c635ca7e4661a43156584c8bc41df45`，不是浮动 main。
- [statute_test.go](https://github.com/vedrlabs/go-dlt698-45/blob/aa1d15614c635ca7e4661a43156584c8bc41df45/statute_test.go)，blob SHA `3489b4aadd8c89b48aeac11f9a142d97c4eb4507`。
- 固定 literal 出现在该文件的透明转发/上报测试输入，原始空格串为 `68 17 00 43 05 01 00 00 00 00 00 00 a7 e6 05 01 01 00 00 03 00 00 83 80 16`。
- 本地独立计算确认：25 字节，总声明长度 23；L 至 CA 的 HCS 为 `E6A7`（线上 `A7E6`），L 至 APDU 末的 FCS 为 `8083`（线上 `8380`）。APDU 为 `0501010000030000`；显示服务器地址 `000000000001`。
- `prot698_boundary_tests.cpp` 直接冻结该外部 literal，并验证生成器、严格帧检查器和独立 CRC。它能支持帧布局、地址顺序和此 GET 请求的回归；原项目未交代设备采集/标准页码，不能声称设备执行、权威标准 golden 或全部 18 服务独立正确。

## I02：普通响应控制域与响应尾的独立实现

同一固定提交：

| 源码链接 | blob SHA | 直接观察 |
| --- | --- | --- |
| [get_response.go](https://github.com/vedrlabs/go-dlt698-45/blob/aa1d15614c635ca7e4661a43156584c8bc41df45/get_response.go) | `8dfcc3ef3ba89711874f3ff0e57bef17405d5aa5` | 普通响应封装函数 `FuncCode:"011"`；`NeedFollowReport()` 与 `NeedTimeTag()` 返回 true |
| [set_response.go](https://github.com/vedrlabs/go-dlt698-45/blob/aa1d15614c635ca7e4661a43156584c8bc41df45/set_response.go) | `a96577b43d51012b184a86921928360332746402` | 普通响应封装函数使用相同功能码；两种响应尾均需要 |
| [action_response.go](https://github.com/vedrlabs/go-dlt698-45/blob/aa1d15614c635ca7e4661a43156584c8bc41df45/action_response.go) | `c7991a360ce41fce0e41faff9686241a3c9d2f71` | 普通响应封装函数使用相同功能码；两种响应尾均需要 |
| [apdu.go](https://github.com/vedrlabs/go-dlt698-45/blob/aa1d15614c635ca7e4661a43156584c8bc41df45/apdu.go) | `54d6fa8645f696317f6bed5a28da8c8e675508f7` | `encode()` 在数据之后先编码 FollowReport，再编码 TimeTag；nil 分别追加一个 `00` |

这些是第一手独立实现旁证，不是该版本标准原文。只将上述已观察事实提交整合者裁定开放范围；不可从独立项目的其他控制位或全部分支推断它们必然正确。响应字节测试若按此逐字段推导，应登记为“独立实现推导”，不得登记为外部固定帧或设备采集。

## I03：浮点线序的独立实现

[dt/data1.go](https://github.com/vedrlabs/go-dlt698-45/blob/aa1d15614c635ca7e4661a43156584c8bc41df45/dt/data1.go)，blob SHA `ec318ec1fd0181dfe28dfe4bccef4a92127be894`：`Float32.Encode()` 和 `Float64.Encode()` 分别对 IEEE 位模式使用 `binary.BigEndian.PutUint32/PutUint64`；Decode 也使用 BigEndian。

可据此独立推导数值 1.0 的数据部分为 `3F800000` / `3FF0000000000000`。这不是从本项目现有 memcpy 输出取期望值；仍须由整合者结合当前协议边界评估，不等同标准版次认证。

## I04：CRC 方法

[RFC 1662 附录 C](https://www.rfc-editor.org/rfc/rfc1662#appendix-C) 给出 FCS-16 多项式、初值、取反和低字节先放的计算方法。测试用非反射多项式 `0x1021` 并显式倒位实现，与工程中反射 `0x8408` 的循环分开；检查串 `123456789` 的取反结果为 `906E`。

CRC 通过仅证明算法与指定覆盖区间吻合，不能证明 APDU 语义、功能码、字典或业务参数正确。
