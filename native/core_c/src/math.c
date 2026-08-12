// core_c/src/math.c
//
// T008：C11 确定性数学库实现。
//
// 本文件只做一层薄封装：固定函数集、固定参数顺序与固定 C11 调用路径，
// 避免模拟核心直接散落调用 libm，从而把确定性契约收敛到 math.h 的单一声明处。
// 超越函数的跨 libm 位级一致性边界见 math.h；本文件不引入任何自有求值算法，
// 以保证与宿主 CRT 行为一致。

#include <math.h>

#include "wfs/core/math.h"

double wfs_math_sqrt(double value) {
    return sqrt(value);
}

double wfs_math_floor(double value) {
    return floor(value);
}

double wfs_math_ceil(double value) {
    return ceil(value);
}

double wfs_math_round(double value) {
    return round(value);
}

double wfs_math_trunc(double value) {
    return trunc(value);
}

double wfs_math_fmod(double value, double divisor) {
    return fmod(value, divisor);
}

double wfs_math_sin(double radians) {
    return sin(radians);
}

double wfs_math_cos(double radians) {
    return cos(radians);
}

double wfs_math_tan(double radians) {
    return tan(radians);
}

double wfs_math_atan(double value) {
    return atan(value);
}

double wfs_math_atan2(double y_arg, double x_arg) {
    return atan2(y_arg, x_arg);
}
