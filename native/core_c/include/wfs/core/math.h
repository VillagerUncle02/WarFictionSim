// core_c/include/wfs/core/math.h
//
// T008：C11 确定性数学库公开接口（IEEE-754 double）。
//
// 确定性契约：
// - sqrt/floor/ceil/round/trunc/fmod 为 IEEE-754 精确定义运算，对同一输入
//   在符合规范的实现间应逐位一致（NaN 载荷与符号位除外）。
// - sin/cos/tan/atan/atan2 委托宿主 libm。MSVC CRT + /fp:precise 下同一编译
//   器/CRT 版本保证逐位一致；不同 libm（跨编译器/平台）的超越函数结果
//   **不保证**逐位一致，调用方不得跨平台依赖其位模式。
// - NaN/Inf 遵循 C11 与 IEEE-754 语义（例如 sqrt(-1) 为 NaN、sqrt(inf) 为 inf）。

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

double wfs_math_sqrt(double value);
double wfs_math_floor(double value);
double wfs_math_ceil(double value);
double wfs_math_round(double value);
double wfs_math_trunc(double value);
double wfs_math_fmod(double value, double divisor);
double wfs_math_sin(double radians);
double wfs_math_cos(double radians);
double wfs_math_tan(double radians);
double wfs_math_atan(double value);
double wfs_math_atan2(double y, double x);

#ifdef __cplusplus
}
#endif
