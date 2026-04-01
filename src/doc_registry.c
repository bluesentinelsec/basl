#include "vigil/doc_registry.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "vigil/stdlib.h"
#include "vigil/type.h"

/* ── Builtin Function Docs ────────────────────────────────── */

static const vigil_doc_entry_t builtin_docs[] = {
    {"builtins", NULL, "Built-in functions available without import.",
     "These functions are always available in VIGIL programs.", NULL},
    {"len", "len(value: string | array | map) -> int", "Return the length of a string, array, or map.", NULL,
     "len(\"hello\")  // 5\nlen([1, 2, 3])  // 3"},
    {"type", "type(value: any) -> string", "Return the type name of a value.", NULL,
     "type(42)       // \"int\"\ntype(\"hello\")  // \"string\""},
    {"str", "str(value: any) -> string", "Convert a value to its string representation.", NULL,
     "str(42)    // \"42\"\nstr(true)  // \"true\""},
    {"int", "int(value: string | float) -> int", "Convert a string or float to an integer.", NULL,
     "int(\"42\")   // 42\nint(3.14)   // 3"},
    {"float", "float(value: string | int) -> float", "Convert a string or integer to a float.", NULL,
     "float(\"3.14\")  // 3.14\nfloat(42)      // 42.0"},
    {"exit", "exit(code: int) -> void", "Exit the program with the given status code.", NULL,
     "exit(0)  // success\nexit(1)  // failure"},
    {"char", "char(code: int) -> string", "Convert a byte value (0-255) to a single-character string.", NULL,
     "char(65)   // \"A\"\nchar(0x0a) // \"\\n\""},
};

#define BUILTIN_COUNT (sizeof(builtin_docs) / sizeof(builtin_docs[0]))

/* ── math Module Docs ─────────────────────────────────────── */

static const vigil_doc_entry_t math_docs[] = {
    {"math", NULL, "Mathematical functions and constants.", "The math module provides common mathematical operations.",
     NULL},
    {"math.sqrt", "math.sqrt(x: float) -> float", "Return the square root of x.", NULL, "math.sqrt(16.0)  // 4.0"},
    {"math.abs", "math.abs(x: int | float) -> int | float", "Return the absolute value of x.", NULL,
     "math.abs(-5)    // 5\nmath.abs(-3.14) // 3.14"},
    {"math.floor", "math.floor(x: float) -> int", "Return the largest integer less than or equal to x.", NULL,
     "math.floor(3.7)   // 3\nmath.floor(-3.2)  // -4"},
    {"math.ceil", "math.ceil(x: float) -> int", "Return the smallest integer greater than or equal to x.", NULL,
     "math.ceil(3.2)   // 4\nmath.ceil(-3.7)  // -3"},
    {"math.round", "math.round(x: f64) -> f64", "Return x rounded to the nearest integer value.", NULL,
     "math.round(3.6)  // 4.0"},
    {"math.trunc", "math.trunc(x: f64) -> f64", "Return x with the fractional part removed.", NULL,
     "math.trunc(-3.7)  // -3.0"},
    {"math.pow", "math.pow(base: float, exp: float) -> float", "Return base raised to the power exp.", NULL,
     "math.pow(2.0, 10.0)  // 1024.0"},
    {"math.sin", "math.sin(x: float) -> float", "Return the sine of x (in radians).", NULL, "math.sin(0.0)  // 0.0"},
    {"math.cos", "math.cos(x: float) -> float", "Return the cosine of x (in radians).", NULL, "math.cos(0.0)  // 1.0"},
    {"math.tan", "math.tan(x: float) -> float", "Return the tangent of x (in radians).", NULL, "math.tan(0.0)  // 0.0"},
    {"math.log", "math.log(x: float) -> float", "Return the natural logarithm of x.", NULL,
     "math.log(2.718281828)  // ~1.0"},
    {"math.exp", "math.exp(x: float) -> float", "Return e raised to the power x.", NULL, "math.exp(1.0)  // ~2.718"},
    {"math.pi", "math.pi() -> f64", "Return pi (approximately 3.141593).", NULL, "math.pi()  // 3.141592..."},
    {"math.e", "math.e() -> f64", "Return Euler's number (approximately 2.718282).", NULL, "math.e()  // 2.718281..."},
    {"math.cbrt", "math.cbrt(x: f64) -> f64", "Return the cube root of x.", NULL, "math.cbrt(27.0)  // 3.0"},
    {"math.sign", "math.sign(x: f64) -> f64", "Return the sign of x as -1.0, 0.0, or 1.0.", NULL,
     "math.sign(-42.0)  // -1.0"},
    {"math.asin", "math.asin(x: f64) -> f64", "Return the arc sine of x in radians.", NULL, "math.asin(0.0)  // 0.0"},
    {"math.acos", "math.acos(x: f64) -> f64", "Return the arc cosine of x in radians.", NULL, "math.acos(1.0)  // 0.0"},
    {"math.atan", "math.atan(x: f64) -> f64", "Return the arc tangent of x in radians.", NULL,
     "math.atan(1.0)  // ~0.785398"},
    {"math.log2", "math.log2(x: f64) -> f64", "Return the base-2 logarithm of x.", NULL, "math.log2(8.0)  // 3.0"},
    {"math.log10", "math.log10(x: f64) -> f64", "Return the base-10 logarithm of x.", NULL,
     "math.log10(1000.0)  // 3.0"},
    {"math.deg2rad", "math.deg2rad(x: f64) -> f64", "Convert degrees to radians.", NULL,
     "math.deg2rad(180.0)  // 3.141592..."},
    {"math.rad2deg", "math.rad2deg(x: f64) -> f64", "Convert radians to degrees.", NULL,
     "math.rad2deg(math.pi())  // 180.0"},
    {"math.min", "math.min(a: f64, b: f64) -> f64", "Return the smaller of two values.", NULL,
     "math.min(2.0, 5.0)  // 2.0"},
    {"math.max", "math.max(a: f64, b: f64) -> f64", "Return the larger of two values.", NULL,
     "math.max(2.0, 5.0)  // 5.0"},
    {"math.atan2", "math.atan2(y: f64, x: f64) -> f64", "Return the angle of the vector (x, y) in radians.", NULL,
     "math.atan2(1.0, 1.0)  // ~0.785398"},
    {"math.hypot", "math.hypot(a: f64, b: f64) -> f64", "Return the Euclidean length sqrt(a*a + b*b).", NULL,
     "math.hypot(3.0, 4.0)  // 5.0"},
    {"math.fmod", "math.fmod(a: f64, b: f64) -> f64", "Return the floating-point remainder of a / b.", NULL,
     "math.fmod(7.5, 2.0)  // 1.5"},
    {"math.step", "math.step(edge: f64, x: f64) -> f64", "Return 0.0 when x is below edge, otherwise 1.0.", NULL,
     "math.step(0.5, 0.7)  // 1.0"},
    {"math.tau", "math.tau() -> f64", "Return tau (2*pi, approximately 6.283185).", NULL, "math.tau()  // 6.283185..."},
    {"math.epsilon", "math.epsilon() -> f64", "Return machine epsilon (smallest f64 such that 1.0 + epsilon > 1.0).",
     NULL, "math.epsilon()  // 2.220446e-16"},
    {"math.isNaN", "math.isNaN(x: f64) -> bool", "Return true if x is NaN.", NULL, "math.isNaN(0.0 / 0.0)  // true"},
    {"math.isInf", "math.isInf(x: f64) -> bool", "Return true if x is positive or negative infinity.", NULL,
     "math.isInf(1.0 / 0.0)  // true"},
    {"math.isFinite", "math.isFinite(x: f64) -> bool", "Return true if x is neither NaN nor infinity.", NULL,
     "math.isFinite(42.0)  // true"},
    {"math.clamp", "math.clamp(x: f64, lo: f64, hi: f64) -> f64", "Clamp x into the inclusive range [lo, hi].", NULL,
     "math.clamp(12.0, 0.0, 10.0)  // 10.0"},
    {"math.lerp", "math.lerp(a: f64, b: f64, t: f64) -> f64", "Linearly interpolate between a and b.", NULL,
     "math.lerp(10.0, 20.0, 0.25)  // 12.5"},
    {"math.inverseLerp", "math.inverseLerp(a: f64, b: f64, x: f64) -> f64",
     "Return the interpolation factor t such that lerp(a, b, t) == x.", NULL,
     "math.inverseLerp(10.0, 20.0, 15.0)  // 0.5"},
    {"math.smoothstep", "math.smoothstep(lo: f64, hi: f64, x: f64) -> f64",
     "Smoothly interpolate from 0.0 to 1.0 across the range [lo, hi].", NULL, "math.smoothstep(0.0, 1.0, 0.5)  // 0.5"},
    {"math.normalize", "math.normalize(x: f64, lo: f64, hi: f64) -> f64",
     "Map x from [lo, hi] into the normalized range [0.0, 1.0].", NULL, "math.normalize(15.0, 10.0, 20.0)  // 0.5"},
    {"math.wrap", "math.wrap(x: f64, lo: f64, hi: f64) -> f64", "Wrap x into the half-open interval [lo, hi).", NULL,
     "math.wrap(13.0, 0.0, 10.0)  // 3.0"},
    {"math.remap", "math.remap(x: f64, in_lo: f64, in_hi: f64, out_lo: f64, out_hi: f64) -> f64",
     "Map x from one numeric range into another.", NULL, "math.remap(5.0, 0.0, 10.0, 0.0, 100.0)  // 50.0"},
    {"math.Vec2", "class math.Vec2", "Two-dimensional floating-point vector.",
     "Represents a 2D vector with x and y components.", NULL},
    {"math.Vec2.x", "math.Vec2.x: f64", "X component.", NULL, NULL},
    {"math.Vec2.y", "math.Vec2.y: f64", "Y component.", NULL, NULL},
    {"math.Vec2.zero", "math.Vec2.zero() -> math.Vec2", "Return the zero vector.", NULL, NULL},
    {"math.Vec2.one", "math.Vec2.one() -> math.Vec2", "Return the all-ones vector.", NULL, NULL},
    {"math.Vec2.length", "math.Vec2.length() -> f64", "Return the vector length.", NULL, NULL},
    {"math.Vec2.lengthSqr", "math.Vec2.lengthSqr() -> f64", "Return the squared vector length.", NULL, NULL},
    {"math.Vec2.dot", "math.Vec2.dot(other: math.Vec2) -> f64", "Return the dot product with another vector.", NULL,
     NULL},
    {"math.Vec2.distance", "math.Vec2.distance(other: math.Vec2) -> f64", "Return the distance to another vector.",
     NULL, NULL},
    {"math.Vec2.normalize", "math.Vec2.normalize() -> math.Vec2", "Return a normalized copy of the vector.", NULL,
     NULL},
    {"math.Vec2.negate", "math.Vec2.negate() -> math.Vec2", "Return the negated vector.", NULL, NULL},
    {"math.Vec2.add", "math.Vec2.add(other: math.Vec2) -> math.Vec2", "Return the sum with another vector.", NULL,
     NULL},
    {"math.Vec2.sub", "math.Vec2.sub(other: math.Vec2) -> math.Vec2", "Return the difference with another vector.",
     NULL, NULL},
    {"math.Vec2.scale", "math.Vec2.scale(scale: f64) -> math.Vec2", "Scale the vector by a scalar.", NULL, NULL},
    {"math.Vec2.lerp", "math.Vec2.lerp(other: math.Vec2, t: f64) -> math.Vec2",
     "Linearly interpolate toward another vector.", NULL, NULL},
    {"math.Vec2.reflect", "math.Vec2.reflect(normal: math.Vec2) -> math.Vec2", "Reflect the vector across a normal.",
     NULL, NULL},
    {"math.Vec2.angle", "math.Vec2.angle() -> f64", "Return the vector angle in radians.", NULL, NULL},
    {"math.Vec2.rotate", "math.Vec2.rotate(angle: f64) -> math.Vec2", "Rotate the vector by an angle in radians.", NULL,
     NULL},
    {"math.Vec3", "class math.Vec3", "Three-dimensional floating-point vector.",
     "Represents a 3D vector with x, y, and z components.", NULL},
    {"math.Vec3.x", "math.Vec3.x: f64", "X component.", NULL, NULL},
    {"math.Vec3.y", "math.Vec3.y: f64", "Y component.", NULL, NULL},
    {"math.Vec3.z", "math.Vec3.z: f64", "Z component.", NULL, NULL},
    {"math.Vec3.zero", "math.Vec3.zero() -> math.Vec3", "Return the zero vector.", NULL, NULL},
    {"math.Vec3.one", "math.Vec3.one() -> math.Vec3", "Return the all-ones vector.", NULL, NULL},
    {"math.Vec3.length", "math.Vec3.length() -> f64", "Return the vector length.", NULL, NULL},
    {"math.Vec3.lengthSqr", "math.Vec3.lengthSqr() -> f64", "Return the squared vector length.", NULL, NULL},
    {"math.Vec3.dot", "math.Vec3.dot(other: math.Vec3) -> f64", "Return the dot product with another vector.", NULL,
     NULL},
    {"math.Vec3.distance", "math.Vec3.distance(other: math.Vec3) -> f64", "Return the distance to another vector.",
     NULL, NULL},
    {"math.Vec3.angle", "math.Vec3.angle(other: math.Vec3) -> f64", "Return the angle to another vector in radians.",
     NULL, NULL},
    {"math.Vec3.cross", "math.Vec3.cross(other: math.Vec3) -> math.Vec3",
     "Return the cross product with another vector.", NULL, NULL},
    {"math.Vec3.normalize", "math.Vec3.normalize() -> math.Vec3", "Return a normalized copy of the vector.", NULL,
     NULL},
    {"math.Vec3.negate", "math.Vec3.negate() -> math.Vec3", "Return the negated vector.", NULL, NULL},
    {"math.Vec3.add", "math.Vec3.add(other: math.Vec3) -> math.Vec3", "Return the sum with another vector.", NULL,
     NULL},
    {"math.Vec3.sub", "math.Vec3.sub(other: math.Vec3) -> math.Vec3", "Return the difference with another vector.",
     NULL, NULL},
    {"math.Vec3.scale", "math.Vec3.scale(scale: f64) -> math.Vec3", "Scale the vector by a scalar.", NULL, NULL},
    {"math.Vec3.lerp", "math.Vec3.lerp(other: math.Vec3, t: f64) -> math.Vec3",
     "Linearly interpolate toward another vector.", NULL, NULL},
    {"math.Vec3.reflect", "math.Vec3.reflect(normal: math.Vec3) -> math.Vec3", "Reflect the vector across a normal.",
     NULL, NULL},
    {"math.Vec3.transform", "math.Vec3.transform(matrix: math.Mat4) -> math.Vec3", "Transform the vector by a matrix.",
     NULL, NULL},
    {"math.Vec3.rotateByQuaternion", "math.Vec3.rotateByQuaternion(rotation: math.Quaternion) -> math.Vec3",
     "Rotate the vector by a quaternion.", NULL, NULL},
    {"math.Vec3.unproject", "math.Vec3.unproject(projection: math.Mat4, view: math.Mat4) -> math.Vec3",
     "Unproject normalized screen coordinates into world space.", NULL, NULL},
    {"math.Vec4", "class math.Vec4", "Four-dimensional floating-point vector.",
     "Represents a 4D vector with x, y, z, and w components.", NULL},
    {"math.Vec4.x", "math.Vec4.x: f64", "X component.", NULL, NULL},
    {"math.Vec4.y", "math.Vec4.y: f64", "Y component.", NULL, NULL},
    {"math.Vec4.z", "math.Vec4.z: f64", "Z component.", NULL, NULL},
    {"math.Vec4.w", "math.Vec4.w: f64", "W component.", NULL, NULL},
    {"math.Vec4.zero", "math.Vec4.zero() -> math.Vec4", "Return the zero vector.", NULL, NULL},
    {"math.Vec4.one", "math.Vec4.one() -> math.Vec4", "Return the all-ones vector.", NULL, NULL},
    {"math.Vec4.length", "math.Vec4.length() -> f64", "Return the vector length.", NULL, NULL},
    {"math.Vec4.lengthSqr", "math.Vec4.lengthSqr() -> f64", "Return the squared vector length.", NULL, NULL},
    {"math.Vec4.dot", "math.Vec4.dot(other: math.Vec4) -> f64", "Return the dot product with another vector.", NULL,
     NULL},
    {"math.Vec4.distance", "math.Vec4.distance(other: math.Vec4) -> f64", "Return the distance to another vector.",
     NULL, NULL},
    {"math.Vec4.normalize", "math.Vec4.normalize() -> math.Vec4", "Return a normalized copy of the vector.", NULL,
     NULL},
    {"math.Vec4.negate", "math.Vec4.negate() -> math.Vec4", "Return the negated vector.", NULL, NULL},
    {"math.Vec4.add", "math.Vec4.add(other: math.Vec4) -> math.Vec4", "Return the sum with another vector.", NULL,
     NULL},
    {"math.Vec4.sub", "math.Vec4.sub(other: math.Vec4) -> math.Vec4", "Return the difference with another vector.",
     NULL, NULL},
    {"math.Vec4.scale", "math.Vec4.scale(scale: f64) -> math.Vec4", "Scale the vector by a scalar.", NULL, NULL},
    {"math.Vec4.lerp", "math.Vec4.lerp(other: math.Vec4, t: f64) -> math.Vec4",
     "Linearly interpolate toward another vector.", NULL, NULL},
    {"math.Quaternion", "class math.Quaternion", "Quaternion rotation value.",
     "Represents a quaternion with x, y, z, and w components.", NULL},
    {"math.Quaternion.x", "math.Quaternion.x: f64", "X component.", NULL, NULL},
    {"math.Quaternion.y", "math.Quaternion.y: f64", "Y component.", NULL, NULL},
    {"math.Quaternion.z", "math.Quaternion.z: f64", "Z component.", NULL, NULL},
    {"math.Quaternion.w", "math.Quaternion.w: f64", "W component.", NULL, NULL},
    {"math.Quaternion.length", "math.Quaternion.length() -> f64", "Return the quaternion magnitude.", NULL, NULL},
    {"math.Quaternion.dot", "math.Quaternion.dot(other: math.Quaternion) -> f64",
     "Return the dot product with another quaternion.", NULL, NULL},
    {"math.Quaternion.normalize", "math.Quaternion.normalize() -> math.Quaternion",
     "Return a normalized copy of the quaternion.", NULL, NULL},
    {"math.Quaternion.conjugate", "math.Quaternion.conjugate() -> math.Quaternion", "Return the quaternion conjugate.",
     NULL, NULL},
    {"math.Quaternion.inverse", "math.Quaternion.inverse() -> math.Quaternion", "Return the quaternion inverse.", NULL,
     NULL},
    {"math.Quaternion.multiply", "math.Quaternion.multiply(other: math.Quaternion) -> math.Quaternion",
     "Return the Hamilton product with another quaternion.", NULL, NULL},
    {"math.Quaternion.slerp", "math.Quaternion.slerp(other: math.Quaternion, t: f64) -> math.Quaternion",
     "Spherically interpolate toward another quaternion.", NULL, NULL},
    {"math.Quaternion.fromAxisAngle", "math.Quaternion.fromAxisAngle(axis: math.Vec3, angle: f64) -> math.Quaternion",
     "Build a quaternion from an axis-angle rotation.", NULL, NULL},
    {"math.Quaternion.fromEuler", "math.Quaternion.fromEuler(pitch: f64, yaw: f64, roll: f64) -> math.Quaternion",
     "Build a quaternion from Euler angles.", NULL, NULL},
    {"math.Quaternion.toEuler", "math.Quaternion.toEuler() -> math.Quaternion",
     "Convert the quaternion to Euler angles.",
     "Returns a quaternion-shaped value whose x, y, and z components hold pitch, yaw, and roll in radians.", NULL},
    {"math.Quaternion.toMat4", "math.Quaternion.toMat4() -> math.Mat4", "Convert the quaternion to a rotation matrix.",
     NULL, NULL},
    {"math.Mat4", "class math.Mat4", "4x4 floating-point matrix.",
     "Represents a column-major 4x4 matrix suitable for transforms and projections.", NULL},
    {"math.Mat4.data", "math.Mat4.data: array<f64>", "Raw matrix elements.",
     "Stores the 16 matrix elements in column-major order.", NULL},
    {"math.Mat4.identity", "math.Mat4.identity() -> math.Mat4", "Return the identity matrix.", NULL, NULL},
    {"math.Mat4.lookAt", "math.Mat4.lookAt(eye: math.Vec3, target: math.Vec3, up: math.Vec3) -> math.Mat4",
     "Build a view matrix from eye, target, and up vectors.", NULL, NULL},
    {"math.Mat4.perspective", "math.Mat4.perspective(fov_y: f64, aspect: f64, near: f64, far: f64) -> math.Mat4",
     "Build a perspective projection matrix.", NULL, NULL},
    {"math.Mat4.ortho",
     "math.Mat4.ortho(left: f64, right: f64, bottom: f64, top: f64, near: f64, far: f64) -> math.Mat4",
     "Build an orthographic projection matrix.", NULL, NULL},
    {"math.Mat4.frustum",
     "math.Mat4.frustum(left: f64, right: f64, bottom: f64, top: f64, near: f64, far: f64) -> math.Mat4",
     "Build a frustum projection matrix.", NULL, NULL},
    {"math.Mat4.get", "math.Mat4.get(row: i32, col: i32) -> f64", "Read a matrix element by row and column.", NULL,
     NULL},
    {"math.Mat4.set", "math.Mat4.set(row: i32, col: i32, value: f64) -> math.Mat4",
     "Return a copy with one matrix element replaced.", NULL, NULL},
    {"math.Mat4.multiply", "math.Mat4.multiply(other: math.Mat4) -> math.Mat4",
     "Multiply the matrix by another matrix.", NULL, NULL},
    {"math.Mat4.transpose", "math.Mat4.transpose() -> math.Mat4", "Return the transposed matrix.", NULL, NULL},
    {"math.Mat4.determinant", "math.Mat4.determinant() -> f64", "Return the matrix determinant.", NULL, NULL},
    {"math.Mat4.trace", "math.Mat4.trace() -> f64", "Return the matrix trace.", NULL, NULL},
    {"math.Mat4.invert", "math.Mat4.invert() -> math.Mat4", "Return the matrix inverse.", NULL, NULL},
    {"math.Mat4.add", "math.Mat4.add(other: math.Mat4) -> math.Mat4", "Add another matrix element-wise.", NULL, NULL},
    {"math.Mat4.scale", "math.Mat4.scale(scale: f64) -> math.Mat4", "Scale the matrix by a scalar.", NULL, NULL},
    {"math.Mat4.scaleV", "math.Mat4.scaleV(scale: math.Vec3) -> math.Mat4", "Apply a non-uniform scale.", NULL, NULL},
    {"math.Mat4.translate", "math.Mat4.translate(offset: math.Vec3) -> math.Mat4", "Apply a translation.", NULL, NULL},
    {"math.Mat4.rotateX", "math.Mat4.rotateX(angle: f64) -> math.Mat4", "Apply a rotation about the X axis.", NULL,
     NULL},
    {"math.Mat4.rotateY", "math.Mat4.rotateY(angle: f64) -> math.Mat4", "Apply a rotation about the Y axis.", NULL,
     NULL},
    {"math.Mat4.rotateZ", "math.Mat4.rotateZ(angle: f64) -> math.Mat4", "Apply a rotation about the Z axis.", NULL,
     NULL},
};

#define MATH_COUNT (sizeof(math_docs) / sizeof(math_docs[0]))

/* ── strings Module Docs ──────────────────────────────────── */

static const vigil_doc_entry_t strings_docs[] = {
    {"strings", NULL, "String methods available on all string values.",
     "These methods are called on string values using dot notation.", NULL},
    {"strings.len", "s.len() -> i32", "Return the length of the string in bytes.", NULL, "\"hello\".len()  // 5"},
    {"strings.contains", "s.contains(sub: string) -> bool", "Return true if s contains the substring sub.", NULL,
     "\"hello\".contains(\"ell\")  // true"},
    {"strings.starts_with", "s.starts_with(prefix: string) -> bool", "Return true if s starts with prefix.", NULL,
     "\"hello\".starts_with(\"he\")  // true"},
    {"strings.ends_with", "s.ends_with(suffix: string) -> bool", "Return true if s ends with suffix.", NULL,
     "\"hello\".ends_with(\"lo\")  // true"},
    {"strings.trim", "s.trim() -> string", "Return s with leading and trailing whitespace removed.", NULL,
     "\"  hello  \".trim()  // \"hello\""},
    {"strings.trim_left", "s.trim_left() -> string", "Return s with leading whitespace removed.", NULL,
     "\"  hello\".trim_left()  // \"hello\""},
    {"strings.trim_right", "s.trim_right() -> string", "Return s with trailing whitespace removed.", NULL,
     "\"hello  \".trim_right()  // \"hello\""},
    {"strings.trim_prefix", "s.trim_prefix(prefix: string) -> string",
     "Return s without the leading prefix if present.", NULL, "\"hello\".trim_prefix(\"he\")  // \"llo\""},
    {"strings.trim_suffix", "s.trim_suffix(suffix: string) -> string",
     "Return s without the trailing suffix if present.", NULL, "\"hello\".trim_suffix(\"lo\")  // \"hel\""},
    {"strings.to_upper", "s.to_upper() -> string", "Return s with all ASCII letters converted to uppercase.", NULL,
     "\"Hello\".to_upper()  // \"HELLO\""},
    {"strings.to_lower", "s.to_lower() -> string", "Return s with all ASCII letters converted to lowercase.", NULL,
     "\"Hello\".to_lower()  // \"hello\""},
    {"strings.replace", "s.replace(old: string, new: string) -> string",
     "Return s with all occurrences of old replaced by new.", NULL, "\"hello\".replace(\"l\", \"L\")  // \"heLLo\""},
    {"strings.split", "s.split(sep: string) -> array<string>",
     "Split s by separator and return an array of substrings.", NULL,
     "\"a,b,c\".split(\",\")  // [\"a\", \"b\", \"c\"]"},
    {"strings.index_of", "s.index_of(sub: string) -> (i32, bool)",
     "Return the index of the first occurrence of sub, or (-1, false) if not found.", NULL,
     "i32 idx, bool found = \"hello\".index_of(\"l\")  // 2, true"},
    {"strings.last_index_of", "s.last_index_of(sub: string) -> (i32, bool)",
     "Return the index of the last occurrence of sub, or (-1, false) if not found.", NULL,
     "i32 idx, bool found = \"hello\".last_index_of(\"l\")  // 3, true"},
    {"strings.substr", "s.substr(start: i32, len: i32) -> (string, err)",
     "Return a substring starting at start with length len.", NULL,
     "string sub, err e = \"hello\".substr(1, 3)  // \"ell\""},
    {"strings.char_at", "s.char_at(i: i32) -> (string, err)",
     "Return the character at index i as a single-character string.", NULL,
     "string c, err e = \"hello\".char_at(0)  // \"h\""},
    {"strings.bytes", "s.bytes() -> array<u8>", "Return the raw bytes of the string as an array.", NULL,
     "\"AB\".bytes()  // [65, 66]"},
    {"strings.reverse", "s.reverse() -> string", "Return s with characters in reverse order.", NULL,
     "\"hello\".reverse()  // \"olleh\""},
    {"strings.is_empty", "s.is_empty() -> bool", "Return true if s has length zero.", NULL, "\"\".is_empty()  // true"},
    {"strings.char_count", "s.char_count() -> i32", "Return the number of Unicode code points in s (not bytes).", NULL,
     "\"café\".char_count()  // 4"},
    {"strings.repeat", "s.repeat(n: i32) -> string", "Return s repeated n times.", NULL,
     "\"ab\".repeat(3)  // \"ababab\""},
    {"strings.count", "s.count(sub: string) -> i32", "Return the number of non-overlapping occurrences of sub in s.",
     NULL, "\"banana\".count(\"a\")  // 3"},
    {"strings.fields", "s.fields() -> array<string>", "Split s on whitespace and return non-empty fields.",
     "Similar to Go's strings.Fields. Splits on runs of whitespace.",
     "\"  a  b  c  \".fields()  // [\"a\", \"b\", \"c\"]"},
    {"strings.join", "sep.join(arr: array<string>) -> string", "Join array elements with sep as separator.",
     "The separator is the receiver, the array is the argument.", "\",\".join([\"a\", \"b\", \"c\"])  // \"a,b,c\""},
    {"strings.cut", "s.cut(sep: string) -> (string, string, bool)", "Cut s around the first instance of sep.",
     "Returns (before, after, found). If sep is not found, returns (s, \"\", false).",
     "string k, string v, bool ok = \"key=val\".cut(\"=\")  // \"key\", \"val\", true"},
    {"strings.equal_fold", "s.equal_fold(t: string) -> bool",
     "Return true if s equals t under case-insensitive comparison.", "Compares ASCII letters case-insensitively.",
     "\"Go\".equal_fold(\"go\")  // true"},
};

#define STRINGS_COUNT (sizeof(strings_docs) / sizeof(strings_docs[0]))

/* ── regex Module Docs ────────────────────────────────────── */

/* ── random Module Docs ───────────────────────────────────── */

/* ── url Module Docs ──────────────────────────────────────── */

/* ── json module ──────────────────────────────────────────── */

static const vigil_doc_entry_t json_docs[] = {
    {"json", NULL, "JSON parsing and traversal.",
     "The json module exposes Vigil's built-in JSON parser through the json.Value class.\n"
     "Use json.Value.parse() or json.Value.read() to create values, then traverse them\n"
     "with get(), at(), keys(), len(), kind(), and the typed as_* accessors.\n"
     "Phase 2 also adds json.encode() and json.decode() for class-based object marshaling.",
     NULL},
    {"json.encode", "json.encode(value: object) -> (string, error)", "Encode a Vigil object as JSON.",
     "Encodes class instances, arrays, maps with string keys, strings, booleans, and numbers.\n"
     "Class encoding uses public fields only.",
     "string text, err encode_err = json.encode(person)"},
    {"json.decode", "json.decode(text: string, prototype: T) -> (T, error)", "Decode JSON into a class instance.",
     "Decodes a JSON object into the same class as the prototype instance. Missing and extra fields are errors.\n"
     "Currently supports public fields, nested classes, arrays, and map<string, T> members.",
     "Person p, err decode_err = json.decode(text, Person(\"\", [0], {\"\": \"\"}, Meta(0)))"},
    {"json.Value", "json.Value", "Opaque JSON value wrapper.",
     "json.Value stores canonical JSON text and provides dynamic traversal helpers for\n"
     "objects, arrays, strings, numbers, booleans, and null values.",
     "json.Value v, err parse_err = json.Value.parse(\"{\\\"name\\\":\\\"vigil\\\"}\")"},
    {"json.Value.parse", "json.Value.parse(text: string) -> (json.Value, error)", "Parse JSON text.",
     "Parses arbitrary JSON and returns a json.Value on success.",
     "json.Value v, err parse_err = json.Value.parse(\"[1,2,3]\")"},
    {"json.Value.read", "json.Value.read(path: string) -> (json.Value, error)", "Read JSON from a file.",
     "Reads a file from disk, parses it as JSON, and returns a json.Value.",
     "json.Value cfg, err read_err = json.Value.read(\"config.json\")"},
    {"json.Value.kind", "json.Value.kind() -> string", "Get the JSON type name.",
     "Returns one of: null, bool, number, string, array, object, or invalid.", "string k = value.kind()"},
    {"json.Value.is_null", "json.Value.is_null() -> bool", "Check for null.", "Returns true when the value is null.",
     "if (value.is_null()) { fmt.println(\"nil\") }"},
    {"json.Value.is_bool", "json.Value.is_bool() -> bool", "Check for bool.", "Returns true when the value is a bool.",
     "if (value.is_bool()) { }"},
    {"json.Value.is_number", "json.Value.is_number() -> bool", "Check for number.",
     "Returns true when the value is a JSON number.", "if (value.is_number()) { }"},
    {"json.Value.is_string", "json.Value.is_string() -> bool", "Check for string.",
     "Returns true when the value is a JSON string.", "if (value.is_string()) { }"},
    {"json.Value.is_array", "json.Value.is_array() -> bool", "Check for array.",
     "Returns true when the value is a JSON array.", "if (value.is_array()) { }"},
    {"json.Value.is_object", "json.Value.is_object() -> bool", "Check for object.",
     "Returns true when the value is a JSON object.", "if (value.is_object()) { }"},
    {"json.Value.len", "json.Value.len() -> (i32, error)", "Get array or object length.",
     "Returns the number of elements in an array or the number of keys in an object.",
     "i32 n, err len_err = value.len()"},
    {"json.Value.as_bool", "json.Value.as_bool() -> (bool, error)", "Read a bool value.",
     "Returns the underlying bool when the JSON value is a boolean.", "bool ok, err bool_err = value.as_bool()"},
    {"json.Value.as_number", "json.Value.as_number() -> (f64, error)", "Read a number value.",
     "Returns the underlying number when the JSON value is numeric.", "f64 n, err num_err = value.as_number()"},
    {"json.Value.as_string", "json.Value.as_string() -> (string, error)", "Read a string value.",
     "Returns the underlying string when the JSON value is a string.", "string s, err str_err = value.as_string()"},
    {"json.Value.at", "json.Value.at(index: i32) -> (json.Value, error)", "Get an array element.",
     "Returns the array element at the given index.", "json.Value item, err item_err = value.at(0)"},
    {"json.Value.get", "json.Value.get(key: string) -> (json.Value, error)", "Get an object member.",
     "Returns the object member at the given key.", "json.Value name, err get_err = value.get(\"name\")"},
    {"json.Value.has", "json.Value.has(key: string) -> bool", "Check if an object has a key.",
     "Returns true when the key exists on a JSON object.", "if (value.has(\"enabled\")) { }"},
    {"json.Value.keys", "json.Value.keys() -> array<string>", "List object keys.",
     "Returns the object's keys in insertion order.", "array<string> ks = value.keys()"},
    {"json.Value.stringify", "json.Value.stringify() -> string", "Serialize JSON back to text.",
     "Returns the JSON text stored by the value.", "string text = value.stringify()"},
    {"json.Value.write", "json.Value.write(path: string) -> error", "Write JSON to a file.",
     "Writes the value's JSON text to the given path.", "err write_err = value.write(\"out.json\")"},
};

#define JSON_COUNT (sizeof(json_docs) / sizeof(json_docs[0]))

/* ── fs module ────────────────────────────────────────────── */

static const vigil_doc_entry_t fs_docs[] = {
    {"fs", NULL, "Filesystem operations.",
     "The fs module provides cross-platform filesystem operations:\n"
     "path manipulation, file I/O, directory operations, and standard locations.",
     NULL},
    {"fs.join", "fs.join(a: string, b: string) -> string", "Join path segments.",
     "Joins two path segments with the platform separator.", "fs.join(\"dir\", \"file.txt\")  // \"dir/file.txt\""},
    {"fs.clean", "fs.clean(path: string) -> string", "Normalize a path.",
     "Removes . and .. components and duplicate separators.", "fs.clean(\"a/./b/../c\")  // \"a/c\""},
    {"fs.dir", "fs.dir(path: string) -> string", "Get directory portion.", "Returns the directory part of a path.",
     "fs.dir(\"/foo/bar.txt\")  // \"/foo\""},
    {"fs.base", "fs.base(path: string) -> string", "Get filename portion.", "Returns the filename part of a path.",
     "fs.base(\"/foo/bar.txt\")  // \"bar.txt\""},
    {"fs.ext", "fs.ext(path: string) -> string", "Get file extension.", "Returns the extension including the dot.",
     "fs.ext(\"file.txt\")  // \".txt\""},
    {"fs.is_abs", "fs.is_abs(path: string) -> bool", "Check if path is absolute.",
     "Returns true if the path is absolute.", "fs.is_abs(\"/foo\")  // true"},
    {"fs.read", "fs.read(path: string) -> string", "Read file contents.", "Reads entire file as a string.",
     "fs.read(\"config.txt\")"},
    {"fs.write", "fs.write(path: string, data: string) -> bool", "Write to file.",
     "Writes data to file, creating or truncating.", "fs.write(\"out.txt\", \"hello\")"},
    {"fs.append", "fs.append(path: string, data: string) -> bool", "Append to file.", "Appends data to end of file.",
     "fs.append(\"log.txt\", \"entry\\n\")"},
    {"fs.copy", "fs.copy(src: string, dst: string) -> bool", "Copy a file.", "Copies file from src to dst.",
     "fs.copy(\"a.txt\", \"b.txt\")"},
    {"fs.move", "fs.move(src: string, dst: string) -> bool", "Move/rename a file.",
     "Moves or renames a file or directory.", "fs.move(\"old.txt\", \"new.txt\")"},
    {"fs.remove", "fs.remove(path: string) -> bool", "Delete file or directory.", "Removes a file or empty directory.",
     "fs.remove(\"temp.txt\")"},
    {"fs.remove_all", "fs.remove_all(path: string) -> bool", "Recursively delete directory.",
     "Removes a file or directory tree recursively.", "fs.remove_all(\"build\")"},
    {"fs.exists", "fs.exists(path: string) -> bool", "Check if path exists.", "Returns true if path exists.",
     "fs.exists(\"/tmp\")  // true"},
    {"fs.is_dir", "fs.is_dir(path: string) -> bool", "Check if path is directory.",
     "Returns true if path is a directory.", "fs.is_dir(\"/tmp\")  // true"},
    {"fs.is_file", "fs.is_file(path: string) -> bool", "Check if path is file.",
     "Returns true if path is a regular file.", "fs.is_file(\"test.txt\")"},
    {"fs.is_symlink", "fs.is_symlink(path: string) -> bool", "Check if path is symlink.",
     "Returns true if path is a symbolic link.", "fs.is_symlink(\"link.txt\")"},
    {"fs.mkdir", "fs.mkdir(path: string) -> bool", "Create directory.", "Creates a single directory.",
     "fs.mkdir(\"newdir\")"},
    {"fs.mkdir_all", "fs.mkdir_all(path: string) -> bool", "Create directory tree.",
     "Creates directory and all parents.", "fs.mkdir_all(\"a/b/c\")"},
    {"fs.list", "fs.list(path: string) -> array<string>", "List directory contents.",
     "Returns array of filenames in directory.", "fs.list(\"/tmp\")"},
    {"fs.walk", "fs.walk(path: string) -> array<string>", "Recursively list directory.",
     "Returns all files and directories recursively.", "fs.walk(\"src\")"},
    {"fs.glob", "fs.glob(dir: string, pattern: string) -> array<string>", "Glob match directory.",
     "Returns files matching a glob pattern (*, ?).", "fs.glob(\"src\", \"*.vigil\")"},
    {"fs.symlink", "fs.symlink(target: string, link: string) -> bool", "Create symbolic link.",
     "Creates a symbolic link pointing to target.", "fs.symlink(\"file.txt\", \"link.txt\")"},
    {"fs.readlink", "fs.readlink(path: string) -> string", "Read symlink target.",
     "Returns the target path of a symbolic link.", "fs.readlink(\"link.txt\")"},
    {"fs.size", "fs.size(path: string) -> i64", "Get file size.", "Returns file size in bytes, -1 on error.",
     "fs.size(\"file.txt\")"},
    {"fs.mtime", "fs.mtime(path: string) -> i64", "Get modification time.",
     "Returns Unix timestamp of last modification.", "fs.mtime(\"file.txt\")"},
    {"fs.temp_dir", "fs.temp_dir() -> string", "Get temp directory.", "Returns system temporary directory path.",
     "fs.temp_dir()  // \"/tmp\""},
    {"fs.temp_file", "fs.temp_file(prefix: string) -> string", "Create temp file.", "Creates a unique temporary file.",
     "fs.temp_file(\"myapp\")"},
    {"fs.home_dir", "fs.home_dir() -> string", "Get home directory.", "Returns user's home directory.",
     "fs.home_dir()"},
    {"fs.config_dir", "fs.config_dir() -> string", "Get config directory.",
     "Returns user config directory (XDG/AppSupport/APPDATA).", "fs.config_dir()"},
    {"fs.cache_dir", "fs.cache_dir() -> string", "Get cache directory.", "Returns user cache directory.",
     "fs.cache_dir()"},
    {"fs.data_dir", "fs.data_dir() -> string", "Get data directory.", "Returns user data directory.", "fs.data_dir()"},
    {"fs.cwd", "fs.cwd() -> string", "Get current directory.", "Returns current working directory.", "fs.cwd()"},
    /* ── fs.Reader class ── */
    {"fs.Reader", "class fs.Reader", "Buffered file reader.",
     "Provides line-by-line and byte-oriented reading from an open file. "
     "Obtain an instance with fs.Reader.open; close with r.close() when done.",
     "fs.Reader r, err open_err = fs.Reader.open(\"data.txt\")"},
    {"fs.Reader.handle", "fs.Reader.handle: i64", "Internal file handle.",
     "Opaque integer handle used by the runtime to track the underlying file. "
     "Not intended for direct use.",
     "i64 h = r.handle"},
    {"fs.Reader.open", "fs.Reader.open(path: string) -> (fs.Reader, err)", "Open a file for reading.",
     "Opens the file at path for sequential reading. Returns a Reader and an error value.",
     "fs.Reader r, err open_err = fs.Reader.open(\"input.txt\")"},
    {"fs.Reader.read_line", "fs.Reader.read_line() -> (string, err)", "Read the next line.",
     "Reads up to the next newline (stripped) or end of file. Returns an eof error when no more data.",
     "string line, err read_err = r.read_line()"},
    {"fs.Reader.read_bytes", "fs.Reader.read_bytes(n: i32) -> (string, err)", "Read up to n bytes.",
     "Reads up to n raw bytes from the file. Returns an eof error when no more data.",
     "string chunk, err read_err = r.read_bytes(4096)"},
    {"fs.Reader.read_all", "fs.Reader.read_all() -> (string, err)", "Read entire file contents.",
     "Reads the remaining contents of the file into a single string.", "string contents, err read_err = r.read_all()"},
    {"fs.Reader.close", "fs.Reader.close() -> err", "Close the file.",
     "Closes the underlying file handle. Subsequent reads will return an error.", "err close_err = r.close()"},
    /* ── fs.Writer class ── */
    {"fs.Writer", "class fs.Writer", "Buffered file writer.",
     "Provides string and line writing to an open file. "
     "Obtain an instance with fs.Writer.open or fs.Writer.open_append; close with w.close() when done.",
     "fs.Writer w, err open_err = fs.Writer.open(\"out.txt\")"},
    {"fs.Writer.handle", "fs.Writer.handle: i64", "Internal file handle.",
     "Opaque integer handle used by the runtime to track the underlying file. "
     "Not intended for direct use.",
     "i64 h = w.handle"},
    {"fs.Writer.open", "fs.Writer.open(path: string) -> (fs.Writer, err)", "Open a file for writing.",
     "Opens the file at path for writing, creating or truncating it. Returns a Writer and an error value.",
     "fs.Writer w, err open_err = fs.Writer.open(\"output.txt\")"},
    {"fs.Writer.open_append", "fs.Writer.open_append(path: string) -> (fs.Writer, err)", "Open a file for appending.",
     "Opens the file at path for appending, creating it if it does not exist. Returns a Writer and an error value.",
     "fs.Writer w, err open_err = fs.Writer.open_append(\"log.txt\")"},
    {"fs.Writer.write", "fs.Writer.write(s: string) -> (i32, err)", "Write a string.",
     "Writes s to the file. Returns the number of bytes written and an error value.",
     "i32 n, err write_err = w.write(\"hello\")"},
    {"fs.Writer.write_line", "fs.Writer.write_line(s: string) -> (i32, err)", "Write a string followed by a newline.",
     "Writes s and a trailing newline to the file. Returns the number of bytes written and an error value.",
     "i32 n, err write_err = w.write_line(\"hello\")"},
    {"fs.Writer.flush", "fs.Writer.flush() -> err", "Flush buffered data.",
     "Flushes any buffered data to the underlying file system.", "err flush_err = w.flush()"},
    {"fs.Writer.close", "fs.Writer.close() -> err", "Close the file.",
     "Flushes and closes the underlying file handle. Subsequent writes will return an error.",
     "err close_err = w.close()"},
};

#define FS_COUNT (sizeof(fs_docs) / sizeof(fs_docs[0]))

/* ── log module ───────────────────────────────────────────── */

/* ── thread module ────────────────────────────────────────── */

static const vigil_doc_entry_t thread_docs[] = {
    {"thread", NULL, "Threading primitives.",
     "The thread module provides cross-platform threading:\n"
     "spawn threads, mutexes, condition variables, and read-write locks.",
     NULL},
    {"thread.current_id", "thread.current_id() -> i64", "Get current thread ID.",
     "Returns unique identifier for current thread.", "thread.current_id()"},
    {"thread.spawn", "thread.spawn(fn: function) -> i64", "Spawn a new thread.",
     "Runs a zero-argument function on a new OS thread. Returns a thread handle for join, or -1 on error.",
     "i64 t = thread.spawn(fn() -> void { fmt.println(\"hello\") })"},
    {"thread.join", "thread.join(t: i64) -> i64", "Wait for thread to finish.",
     "Blocks until the spawned thread completes and returns its i64 result. Returns INT64_MIN on invalid handle or "
     "join failure.",
     "i64 result = thread.join(t)"},
    {"thread.detach", "thread.detach(t: i64) -> bool", "Detach a thread.",
     "Marks a spawned thread as detached so its resources are released without joining.", "thread.detach(t)"},
    {"thread.yield", "thread.yield() -> bool", "Yield to other threads.", "Hints scheduler to run other threads.",
     "thread.yield()"},
    {"thread.sleep", "thread.sleep(ms: i64) -> bool", "Sleep for milliseconds.",
     "Pauses current thread for specified duration.", "thread.sleep(100)"},
    {"thread.mutex", "thread.mutex() -> i64", "Create a mutex.",
     "Creates a mutual exclusion lock and returns its handle.", "i64 m = thread.mutex()"},
    {"thread.lock", "thread.lock(m: i64) -> bool", "Lock a mutex.",
     "Acquires a mutex handle, blocking if it is already held.", "thread.lock(m)"},
    {"thread.unlock", "thread.unlock(m: i64) -> bool", "Unlock a mutex.", "Releases a held mutex handle.",
     "thread.unlock(m)"},
    {"thread.try_lock", "thread.try_lock(m: i64) -> bool", "Try to lock a mutex.",
     "Attempts to acquire a mutex without blocking.", "if (thread.try_lock(m)) { /* critical section */ }"},
    {"thread.mutex_destroy", "thread.mutex_destroy(m: i64) -> bool", "Destroy a mutex.",
     "Destroys a mutex handle and releases its underlying platform resource.", "thread.mutex_destroy(m)"},
    {"thread.cond", "thread.cond() -> i64", "Create a condition variable.",
     "Creates a condition variable handle for signaling between threads.", "i64 c = thread.cond()"},
    {"thread.wait", "thread.wait(c: i64, m: i64) -> bool", "Wait on a condition variable.",
     "Atomically releases the mutex and waits until the condition variable is signaled.", "thread.wait(c, m)"},
    {"thread.wait_timeout", "thread.wait_timeout(c: i64, m: i64, ms: i64) -> bool",
     "Wait on a condition variable with timeout.",
     "Waits until signaled or the timeout expires. Returns false on timeout or invalid handles.",
     "thread.wait_timeout(c, m, 5000)"},
    {"thread.signal", "thread.signal(c: i64) -> bool", "Signal one waiter.",
     "Wakes one thread waiting on the condition variable.", "thread.signal(c)"},
    {"thread.broadcast", "thread.broadcast(c: i64) -> bool", "Signal all waiters.",
     "Wakes all threads waiting on the condition variable.", "thread.broadcast(c)"},
    {"thread.cond_destroy", "thread.cond_destroy(c: i64) -> bool", "Destroy a condition variable.",
     "Destroys a condition-variable handle and releases its underlying platform resource.", "thread.cond_destroy(c)"},
    {"thread.rwlock", "thread.rwlock() -> i64", "Create a read-write lock.",
     "Creates a read-write lock handle allowing multiple readers or one writer.", "i64 rw = thread.rwlock()"},
    {"thread.read_lock", "thread.read_lock(rw: i64) -> bool", "Acquire read lock.",
     "Acquires shared read access on a read-write lock handle.", "thread.read_lock(rw)"},
    {"thread.write_lock", "thread.write_lock(rw: i64) -> bool", "Acquire write lock.",
     "Acquires exclusive write access on a read-write lock handle.", "thread.write_lock(rw)"},
    {"thread.rw_unlock", "thread.rw_unlock(rw: i64) -> bool", "Release read-write lock.",
     "Releases either a read or write lock previously acquired on the handle.", "thread.rw_unlock(rw)"},
    {"thread.rwlock_destroy", "thread.rwlock_destroy(rw: i64) -> bool", "Destroy a read-write lock.",
     "Destroys a read-write lock handle and releases its underlying platform resource.", "thread.rwlock_destroy(rw)"},
};

#define THREAD_COUNT (sizeof(thread_docs) / sizeof(thread_docs[0]))

/* ── atomic module ────────────────────────────────────────── */

/* ── compress module ──────────────────────────────────────── */

static const vigil_doc_entry_t compress_docs[] = {
    {"compress", NULL, "Data compression and decompression.",
     "The compress module provides deflate, zlib, gzip, lz4, zip, and tar support.\n"
     "Uses miniz (MIT) and lz4 (BSD-2) libraries.",
     NULL},
    {"compress.deflate_compress", "compress.deflate_compress(data: string) -> string", "Compress with raw deflate.",
     "Returns deflate-compressed data.", "compress.deflate_compress(data)"},
    {"compress.deflate_compress_level", "compress.deflate_compress_level(data: string, level: i32) -> string",
     "Compress with raw deflate at level.", "Level 0=store, 1=fast, 9=best, 10=uber.",
     "compress.deflate_compress_level(data, 9)"},
    {"compress.deflate_decompress", "compress.deflate_decompress(data: string) -> string", "Decompress raw deflate.",
     "Returns decompressed data.", "compress.deflate_decompress(compressed)"},
    {"compress.zlib_compress", "compress.zlib_compress(data: string) -> string", "Compress with zlib format.",
     "Returns zlib-compressed data (deflate + header/checksum).", "compress.zlib_compress(data)"},
    {"compress.zlib_compress_level", "compress.zlib_compress_level(data: string, level: i32) -> string",
     "Compress with zlib at level.", "Level 0=store, 1=fast, 9=best, 10=uber.",
     "compress.zlib_compress_level(data, 9)"},
    {"compress.zlib_decompress", "compress.zlib_decompress(data: string) -> string", "Decompress zlib format.",
     "Returns decompressed data.", "compress.zlib_decompress(compressed)"},
    {"compress.gzip_compress", "compress.gzip_compress(data: string) -> string", "Compress with gzip format.",
     "Returns gzip-compressed data.", "compress.gzip_compress(data)"},
    {"compress.gzip_compress_level", "compress.gzip_compress_level(data: string, level: i32) -> string",
     "Compress with gzip at level.", "Level 0=store, 1=fast, 9=best. Sets XFL header byte.",
     "compress.gzip_compress_level(data, 9)"},
    {"compress.gzip_decompress", "compress.gzip_decompress(data: string) -> string", "Decompress gzip format.",
     "Returns decompressed data.", "compress.gzip_decompress(compressed)"},
    {"compress.lz4_compress", "compress.lz4_compress(data: string) -> string", "Compress with LZ4.",
     "Returns LZ4-compressed data. Very fast.", "compress.lz4_compress(data)"},
    {"compress.lz4_decompress", "compress.lz4_decompress(data: string) -> string", "Decompress LZ4.",
     "Returns decompressed data.", "compress.lz4_decompress(compressed)"},
    {"compress.crc32", "compress.crc32(data: string) -> i64", "Compute CRC-32 checksum.",
     "Returns CRC-32 of the input data.", "compress.crc32(\"hello\")"},
    {"compress.adler32", "compress.adler32(data: string) -> i64", "Compute Adler-32 checksum.",
     "Returns Adler-32 of the input data.", "compress.adler32(\"hello\")"},
    {"compress.zip_list", "compress.zip_list(data: string) -> array<string>", "List files in ZIP archive.",
     "Returns array of filenames in the archive.", "compress.zip_list(zip_data)"},
    {"compress.zip_read", "compress.zip_read(data: string, filename: string) -> string", "Read file from ZIP archive.",
     "Extracts and returns contents of named file.", "compress.zip_read(zip_data, \"file.txt\")"},
    {"compress.zip_create", "compress.zip_create(names: array<string>, contents: array<string>) -> string",
     "Create ZIP archive.", "Creates archive from parallel arrays of names and contents.",
     "compress.zip_create([\"a.txt\"], [\"data\"])"},
    {"compress.zip_create_level",
     "compress.zip_create_level(names: array<string>, contents: array<string>, level: i32) -> string",
     "Create ZIP archive at level.", "Level 0=store, 1=fast, 9=best, 10=uber.",
     "compress.zip_create_level([\"a.txt\"], [\"data\"], 9)"},
    {"compress.tar_list", "compress.tar_list(data: string) -> array<string>", "List files in TAR archive.",
     "Returns array of filenames in the archive.", "compress.tar_list(tar_data)"},
    {"compress.tar_read", "compress.tar_read(data: string, filename: string) -> string", "Read file from TAR archive.",
     "Extracts and returns contents of named file.", "compress.tar_read(tar_data, \"file.txt\")"},
    {"compress.tar_create", "compress.tar_create(names: array<string>, contents: array<string>) -> string",
     "Create TAR archive.", "Creates archive from parallel arrays of names and contents.",
     "compress.tar_create([\"a.txt\"], [\"data\"])"},
    {"compress.tar_gz_create", "compress.tar_gz_create(names: array<string>, contents: array<string>) -> string",
     "Create TAR.GZ archive.", "Creates tar archive and gzip-compresses it.",
     "compress.tar_gz_create([\"a.txt\"], [\"data\"])"},
    {"compress.gzip_decompress_max", "compress.gzip_decompress_max(data: string, max_bytes: i32) -> string",
     "Decompress gzip with size limit.", "Stops decompression at max_bytes. Protects against zip bombs.",
     "compress.gzip_decompress_max(data, 1048576)"},
    {"compress.gzip_info", "compress.gzip_info(data: string) -> map<string, string>", "Read gzip header metadata.",
     "Returns map with method, xfl, os, flags, size, and optional filename/comment.", "compress.gzip_info(gz_data)"},
};

#define COMPRESS_COUNT (sizeof(compress_docs) / sizeof(compress_docs[0]))

/* ── csv Module Docs ──────────────────────────────────────── */

/* ── net Module Docs ──────────────────────────────────────── */

static const vigil_doc_entry_t net_docs[] = {
    {"net", NULL, "TCP and UDP socket networking.",
     "The net module provides TCP and UDP socket support for network\n"
     "programming. Handles both client and server connections.",
     NULL},
    {"net.tcp_listen", "net.tcp_listen(host: string, port: i32) -> i64", "Create TCP server socket.",
     "Binds and listens on the given address. Returns socket handle or -1.",
     "i64 server = net.tcp_listen(\"0.0.0.0\", 8080)"},
    {"net.tcp_accept", "net.tcp_accept(listener: i64) -> i64", "Accept TCP connection.",
     "Blocks until a client connects. Returns client socket handle.", "i64 client = net.tcp_accept(server)"},
    {"net.tcp_connect", "net.tcp_connect(host: string, port: i32) -> i64", "Connect to TCP server.",
     "Returns socket handle or -1 on failure.", "i64 sock = net.tcp_connect(\"example.com\", 80)"},
    {"net.read", "net.read(sock: i64, max_bytes: i32) -> string", "Read from socket.",
     "Returns data read, or empty string on error/EOF.", "string data = net.read(sock, 4096)"},
    {"net.write", "net.write(sock: i64, data: string) -> i32", "Write to socket.",
     "Returns bytes written or -1 on error.", "net.write(sock, \"Hello\")"},
    {"net.close", "net.close(sock: i64)", "Close socket.", "Closes and releases the socket.", "net.close(sock)"},
    {"net.udp_bind", "net.udp_bind(host: string, port: i32) -> i64", "Create bound UDP socket.",
     "Binds UDP socket to address for receiving.", "i64 sock = net.udp_bind(\"0.0.0.0\", 5000)"},
    {"net.udp_new", "net.udp_new() -> i64", "Create unbound UDP socket.", "Creates UDP socket for sending.",
     "i64 sock = net.udp_new()"},
    {"net.udp_send", "net.udp_send(sock: i64, host: string, port: i32, data: string) -> i32", "Send UDP datagram.",
     "Returns bytes sent or -1 on error.", "net.udp_send(sock, \"127.0.0.1\", 5000, \"hello\")"},
    {"net.udp_recv", "net.udp_recv(sock: i64, max_bytes: i32) -> string", "Receive UDP datagram.",
     "Returns data received or empty string.", "string data = net.udp_recv(sock, 1024)"},
    {"net.set_timeout", "net.set_timeout(sock: i64, ms: i32) -> bool", "Set socket timeout.",
     "Sets read/write timeout in milliseconds.", "net.set_timeout(sock, 5000)"},
};

#define NET_COUNT (sizeof(net_docs) / sizeof(net_docs[0]))

/* ── time Module Docs ─────────────────────────────────────── */

static const vigil_doc_entry_t time_docs[] = {
    {"time", NULL, "Date and time operations.",
     "The time module provides functions for working with dates and times.\n"
     "Timestamps are Unix timestamps (seconds since 1970-01-01 UTC).\n"
     "Format strings use strftime syntax: %Y=year, %m=month, %d=day,\n"
     "%H=hour, %M=minute, %S=second, %A=weekday name, etc.",
     NULL},
    {"time.now", "time.now() -> i64", "Get current Unix timestamp.", "Returns seconds since 1970-01-01 UTC.",
     "i64 ts = time.now()"},
    {"time.now_ms", "time.now_ms() -> i64", "Get current time in milliseconds.",
     "Returns milliseconds since Unix epoch.", "i64 ms = time.now_ms()"},
    {"time.now_ns", "time.now_ns() -> i64", "Get current time in nanoseconds.",
     "Returns nanoseconds since Unix epoch. Note: values may overflow\nVIGIL's 48-bit integer limit for dates after "
     "~1970.",
     "i64 ns = time.now_ns()"},
    {"time.sleep", "time.sleep(ms: i64)", "Sleep for milliseconds.", "Pauses execution for the specified duration.",
     "time.sleep(i64(1000))  // sleep 1 second"},
    {"time.year", "time.year(ts: i64) -> i32", "Get year from timestamp.", "Returns the year (e.g. 2024).",
     "time.year(time.now())"},
    {"time.month", "time.month(ts: i64) -> i32", "Get month from timestamp.", "Returns month 1-12.",
     "time.month(time.now())"},
    {"time.day", "time.day(ts: i64) -> i32", "Get day from timestamp.", "Returns day of month 1-31.",
     "time.day(time.now())"},
    {"time.hour", "time.hour(ts: i64) -> i32", "Get hour from timestamp.", "Returns hour 0-23.",
     "time.hour(time.now())"},
    {"time.minute", "time.minute(ts: i64) -> i32", "Get minute from timestamp.", "Returns minute 0-59.",
     "time.minute(time.now())"},
    {"time.second", "time.second(ts: i64) -> i32", "Get second from timestamp.", "Returns second 0-59.",
     "time.second(time.now())"},
    {"time.weekday", "time.weekday(ts: i64) -> i32", "Get day of week.", "Returns 0=Sunday through 6=Saturday.",
     "time.weekday(time.now())"},
    {"time.yearday", "time.yearday(ts: i64) -> i32", "Get day of year.", "Returns 1-366.", "time.yearday(time.now())"},
    {"time.is_dst", "time.is_dst(ts: i64) -> bool", "Check if DST is active.",
     "Returns true if daylight saving time is in effect.", "time.is_dst(time.now())"},
    {"time.utc_offset", "time.utc_offset() -> i32", "Get local UTC offset.", "Returns offset from UTC in seconds.",
     "time.utc_offset()  // e.g. -18000 for EST"},
    {"time.date", "time.date(y: i32, m: i32, d: i32, h: i32, min: i32, s: i32) -> i64",
     "Create timestamp from components.", "Returns Unix timestamp for the given local time.",
     "time.date(2024, 12, 25, 0, 0, 0)"},
    {"time.format", "time.format(ts: i64, fmt: string) -> string", "Format timestamp as string.",
     "Uses strftime format codes.", "time.format(time.now(), \"%Y-%m-%d %H:%M:%S\")"},
    {"time.parse", "time.parse(s: string, fmt: string) -> i64", "Parse string to timestamp.",
     "Returns -1 on parse failure. Uses strptime format.", "time.parse(\"2024-12-25\", \"%Y-%m-%d\")"},
    {"time.add_days", "time.add_days(ts: i64, n: i32) -> i64", "Add days to timestamp.", "Returns new timestamp.",
     "time.add_days(time.now(), 7)"},
    {"time.add_hours", "time.add_hours(ts: i64, n: i32) -> i64", "Add hours to timestamp.", "Returns new timestamp.",
     "time.add_hours(time.now(), 24)"},
    {"time.add_minutes", "time.add_minutes(ts: i64, n: i32) -> i64", "Add minutes to timestamp.",
     "Returns new timestamp.", "time.add_minutes(time.now(), 30)"},
    {"time.add_seconds", "time.add_seconds(ts: i64, n: i64) -> i64", "Add seconds to timestamp.",
     "Returns new timestamp.", "time.add_seconds(time.now(), i64(3600))"},
    {"time.diff_days", "time.diff_days(a: i64, b: i64) -> i64", "Get difference in days.", "Returns (a - b) / 86400.",
     "time.diff_days(future, past)"},
};

#define TIME_COUNT (sizeof(time_docs) / sizeof(time_docs[0]))

/* ── crypto Module Docs ───────────────────────────────────── */

static const vigil_doc_entry_t crypto_docs[] = {
    {"crypto", NULL, "Cryptographic operations.",
     "The crypto module provides secure hashing, encryption, and key derivation.\n"
     "Uses AES-256-GCM for authenticated encryption and SHA-2 for hashing.\n"
     "All functions return hex-encoded strings for hash outputs.",
     NULL},
    {"crypto.sha256", "crypto.sha256(data: string) -> string", "SHA-256 hash.", "Returns 64-character hex string.",
     "crypto.sha256(\"hello\")"},
    {"crypto.sha384", "crypto.sha384(data: string) -> string", "SHA-384 hash.", "Returns 96-character hex string.",
     "crypto.sha384(\"hello\")"},
    {"crypto.sha512", "crypto.sha512(data: string) -> string", "SHA-512 hash.", "Returns 128-character hex string.",
     "crypto.sha512(\"hello\")"},
    {"crypto.hmac_sha256", "crypto.hmac_sha256(key: string, data: string) -> string", "HMAC-SHA256.",
     "Returns 64-character hex string.", "crypto.hmac_sha256(\"key\", \"message\")"},
    {"crypto.pbkdf2", "crypto.pbkdf2(password: string, salt: string, iterations: i32, key_len: i32) -> string",
     "PBKDF2 key derivation.", "Returns hex-encoded derived key. Use 100000+ iterations.",
     "crypto.pbkdf2(\"password\", \"salt\", 100000, 32)"},
    {"crypto.random_bytes", "crypto.random_bytes(len: i32) -> string", "Cryptographically secure random bytes.",
     "Returns raw bytes (not hex). Max 65536 bytes.", "crypto.random_bytes(32)"},
    {"crypto.constant_time_eq", "crypto.constant_time_eq(a: string, b: string) -> bool", "Constant-time comparison.",
     "Prevents timing attacks when comparing secrets.", "crypto.constant_time_eq(hash1, hash2)"},
    {"crypto.encrypt", "crypto.encrypt(key: string, nonce: string, plaintext: string) -> string",
     "AES-256-GCM encryption.", "Key must be 32 bytes. Returns nonce||ciphertext||tag.",
     "crypto.encrypt(key, nonce, \"secret\")"},
    {"crypto.decrypt", "crypto.decrypt(key: string, ciphertext: string) -> string", "AES-256-GCM decryption.",
     "Returns empty string on authentication failure.", "crypto.decrypt(key, encrypted)"},
    {"crypto.password_encrypt", "crypto.password_encrypt(password: string, plaintext: string) -> string",
     "Password-based encryption.", "Uses PBKDF2 + AES-256-GCM. Password can be any length.",
     "crypto.password_encrypt(\"my password\", \"secret\")"},
    {"crypto.password_decrypt", "crypto.password_decrypt(password: string, ciphertext: string) -> string",
     "Password-based decryption.", "Returns empty string on wrong password or auth failure.",
     "crypto.password_decrypt(\"my password\", encrypted)"},
    {"crypto.hex_encode", "crypto.hex_encode(data: string) -> string", "Encode bytes as hex.",
     "Returns lowercase hex string.", "crypto.hex_encode(\"\\x00\\xff\")"},
    {"crypto.hex_decode", "crypto.hex_decode(hex: string) -> string", "Decode hex to bytes.",
     "Returns empty string on invalid input.", "crypto.hex_decode(\"00ff\")"},
    {"crypto.base64_encode", "crypto.base64_encode(data: string) -> string", "Encode bytes as base64.",
     "Standard base64 with padding.", "crypto.base64_encode(\"hello\")"},
    {"crypto.base64_decode", "crypto.base64_decode(data: string) -> string", "Decode base64 to bytes.",
     "Returns empty string on invalid input.", "crypto.base64_decode(\"aGVsbG8=\")"},
};

#define CRYPTO_COUNT (sizeof(crypto_docs) / sizeof(crypto_docs[0]))

/* ── http module ─────────────────────────────────────────────────── */

static const vigil_doc_entry_t http_docs[] = {
    {"http", NULL, "HTTP client and server.",
     "Client functions use native HTTPS (WinHTTP/libcurl) with plain HTTP fallback. Server functions provide a simple "
     "HTTP/1.1 listener.",
     NULL},
    {"http.get", "http.get(url: string) -> (i32, string, string)", "HTTP GET request.",
     "Returns (status_code, body, headers). Uses WinHTTP/libcurl for HTTPS.",
     "i32 status, string body, string hdrs = http.get(\"https://example.com\")"},
    {"http.post", "http.post(url: string, body: string, content_type?: string) -> (i32, string, string)",
     "HTTP POST request.", "Returns (status_code, response_body, headers).",
     "i32 status, string resp, string hdrs = http.post(url, data, \"application/json\")"},
    {"http.request",
     "http.request(method: string, url: string, headers?: string, body?: string) -> (i32, string, string)",
     "Generic HTTP request.", "Supports any HTTP method. Returns (status_code, body, headers).",
     "i32 status, string body, string hdrs = http.request(\"PUT\", url, \"X-Custom: value\\r\\n\", data)"},
    {"http.listen", "http.listen(host: string, port: i32) -> i64", "Start an HTTP server.",
     "Binds a TCP listener. Returns server handle or -1 on error.", "i64 srv = http.listen(\"127.0.0.1\", 8080)"},
    {"http.accept", "http.accept(server: i64) -> i64", "Accept an HTTP request.",
     "Blocks until a client connects and parses the request. Returns connection handle.",
     "i64 conn = http.accept(srv)"},
    {"http.req_method", "http.req_method(conn: i64) -> string", "Get request method.",
     "Returns the HTTP method (GET, POST, etc.) from an accepted connection.", "string method = http.req_method(conn)"},
    {"http.req_path", "http.req_path(conn: i64) -> string", "Get request path.",
     "Returns the request path from an accepted connection.", "string path = http.req_path(conn)"},
    {"http.req_body", "http.req_body(conn: i64) -> string", "Get request body.",
     "Returns the request body from an accepted connection.", "string body = http.req_body(conn)"},
    {"http.req_headers", "http.req_headers(conn: i64) -> string", "Get all request headers.",
     "Returns raw headers as a CRLF-separated string.", "string hdrs = http.req_headers(conn)"},
    {"http.req_header", "http.req_header(conn: i64, name: string) -> string", "Get a request header by name.",
     "Case-insensitive lookup. Returns empty string if not found.",
     "string ct = http.req_header(conn, \"Content-Type\")"},
    {"http.req_query", "http.req_query(conn: i64) -> string", "Get query string.",
     "Returns the query string from the request path (without leading '?'). Empty if none.",
     "string q = http.req_query(conn)"},
    {"http.respond", "http.respond(conn: i64, status: i32, headers: string, body: string) -> i32",
     "Send HTTP response.", "Sends response and closes the connection. Returns 0 on success.",
     "http.respond(conn, 200, \"Content-Type: text/plain\\r\\n\", \"hello\")"},
    {"http.redirect", "http.redirect(conn: i64, url: string, status?: i32) -> i32", "Send redirect response.",
     "Sends a redirect with Location header. Default status is 302.", "http.redirect(conn, \"/new-path\", 301)"},
    {"http.set_cookie", "http.set_cookie(conn: i64, name: string, value: string, options?: string) -> i32",
     "Set a response cookie.",
     "Buffers a Set-Cookie header for the next http.respond call. Options string can include Path, Domain, Max-Age, "
     "etc.",
     "http.set_cookie(conn, \"session\", \"abc123\", \"Path=/; HttpOnly\")"},
    {"http.req_cookies", "http.req_cookies(conn: i64) -> string", "Get request cookies.",
     "Returns the Cookie header value from the request.", "string cookies = http.req_cookies(conn)"},
    {"http.handle", "http.handle(server: i64, pattern: string, handler: function) -> i32", "Register a route handler.",
     "Associates a URL path pattern with a zero-argument handler function. The handler uses http.current_conn() to "
     "access the request.",
     "http.handle(srv, \"/api/\", fn() -> void { http.respond(http.current_conn(), 200, \"\", \"ok\") })"},
    {"http.serve", "http.serve(server: i64) -> i32", "Start serving HTTP requests.",
     "Blocking loop that accepts connections and dispatches to registered handlers. Unmatched routes get 404. Returns "
     "when the listener is closed.",
     "http.serve(srv)"},
    {"http.current_conn", "http.current_conn() -> i64", "Get current connection handle.",
     "Returns the connection handle for the request being served. Only valid inside a handler registered with "
     "http.handle.",
     "i64 conn = http.current_conn()"},
    {"http.write_header", "http.write_header(conn: i64, status: i32, headers?: string) -> i32",
     "Begin streaming response.",
     "Sends HTTP status and headers with chunked transfer encoding. Follow with http.write() calls and end with "
     "http.flush().",
     "http.write_header(conn, 200, \"Content-Type: text/plain\\r\\n\")"},
    {"http.write", "http.write(conn: i64, data: string) -> i32", "Write a chunk to streaming response.",
     "Sends data as a chunked transfer chunk. Must call http.write_header first.", "http.write(conn, \"hello \")"},
    {"http.flush", "http.flush(conn: i64) -> i32", "End streaming response.",
     "Sends the final zero-length chunk and closes the connection.", "http.flush(conn)"},
    {"http.close", "http.close(server: i64) -> void", "Close HTTP server.", "Closes the listener socket.",
     "http.close(srv)"},
    {"http.set_read_timeout", "http.set_read_timeout(server: i64, ms: i64) -> i32", "Set read timeout.",
     "Sets the read timeout in milliseconds for accepted connections.", "http.set_read_timeout(srv, 30000)"},
    {"http.set_write_timeout", "http.set_write_timeout(server: i64, ms: i64) -> i32", "Set write timeout.",
     "Sets the write timeout in milliseconds for accepted connections.", "http.set_write_timeout(srv, 30000)"},
    {"http.set_idle_timeout", "http.set_idle_timeout(server: i64, ms: i64) -> i32", "Set idle timeout.",
     "Sets the idle timeout in milliseconds. Connections idle longer are closed.",
     "http.set_idle_timeout(srv, 120000)"},
};

#define HTTP_COUNT (sizeof(http_docs) / sizeof(http_docs[0]))

/* ── readline module ─────────────────────────────────────────────── */

static const vigil_doc_entry_t readline_docs[] = {
    {"readline", NULL, "Interactive line input.", "Read user input with prompt and history support.", NULL},
    {"readline.input", "readline.input(prompt: string) -> string", "Read a line of input.",
     "Displays the prompt and reads a line from the terminal.", "string line = readline.input(\"> \")"},
    {"readline.history_add", "readline.history_add(line: string) -> void", "Add a line to history.",
     "Stores the line for recall with history_get.", "readline.history_add(line)"},
    {"readline.history_get", "readline.history_get(index: i32) -> string", "Get a history entry.",
     "Returns the history entry at the given index.", "string h = readline.history_get(0)"},
    {"readline.history_length", "readline.history_length() -> i32", "Get history length.",
     "Returns the number of entries in the history.", "i32 n = readline.history_length()"},
    {"readline.history_clear", "readline.history_clear() -> void", "Clear line history.",
     "Removes all entries from the in-memory history list.", "readline.history_clear()"},
    {"readline.history_load", "readline.history_load(path: string) -> void", "Load history from a file.",
     "Loads previously saved history entries from the given file path.", "readline.history_load(\".vigil_history\")"},
    {"readline.history_save", "readline.history_save(path: string) -> void", "Save history to a file.",
     "Writes the current in-memory history entries to the given file path.",
     "readline.history_save(\".vigil_history\")"},
};

#define READLINE_COUNT (sizeof(readline_docs) / sizeof(readline_docs[0]))

/* ── ffi module ──────────────────────────────────────────────────── */

static const vigil_doc_entry_t ffi_docs[] = {
    {"ffi", NULL, "Foreign function interface.",
     "Load shared libraries and call C functions at runtime. Use 'extern fn' for type-safe declarations.", NULL},
    {"ffi.open", "ffi.open(path: string) -> i64", "Open a shared library.", "Returns a library handle or 0 on failure.",
     "i64 lib = ffi.open(\"libm.so\")"},
    {"ffi.sym", "ffi.sym(lib: i64, name: string) -> i64", "Look up a symbol.",
     "Returns a function pointer address or 0 if not found.", "i64 fn = ffi.sym(lib, \"sqrt\")"},
    {"ffi.close", "ffi.close(lib: i64) -> void", "Close a shared library.", "Releases the library handle.",
     "ffi.close(lib)"},
    {"ffi.bind", "ffi.bind(lib: i64, name: string, signature: string) -> i64", "Bind a C function by signature.",
     "Signature format: \"ret_type(param_types)\". Returns a callable handle.",
     "i64 fn = ffi.bind(lib, \"sqrt\", \"f64(f64)\")"},
    {"ffi.call", "ffi.call(fn: i64, a1-a6: i64) -> i64", "Call a bound function returning i64.",
     "Pass up to 6 integer/pointer arguments.", "i64 r = ffi.call(fn, arg1, arg2, 0, 0, 0, 0)"},
    {"ffi.call_f", "ffi.call_f(fn: i64, a1: f64, a2: f64) -> f64", "Call a bound function returning f64.",
     "For functions that take and return floating-point values.", "f64 r = ffi.call_f(fn, 2.0, 0.0)"},
    {"ffi.call_s", "ffi.call_s(fn: i64, a1: i64, a2: i64) -> string", "Call a bound function returning a string.",
     "Reads a null-terminated C string from the returned pointer.", "string s = ffi.call_s(fn, ptr, len)"},
    {"ffi.callback", "ffi.callback(fn: function, sig: string) -> i64", "Wrap a Vigil function as a C callback.",
     "Returns a C function pointer (as i64) that invokes the Vigil function. Up to 8 active callbacks.",
     "i64 cb = ffi.callback(my_cmp, \"i64(i64,i64)\")"},
    {"ffi.callback_free", "ffi.callback_free(slot: i32) -> void", "Free a callback slot.",
     "Releases the callback slot for reuse.", "ffi.callback_free(0)"},
};

#define FFI_COUNT (sizeof(ffi_docs) / sizeof(ffi_docs[0]))

/* ── unsafe module ───────────────────────────────────────────────── */

static const vigil_doc_entry_t unsafe_docs[] = {
    {"unsafe", NULL, "Low-level memory operations.", "Allocate, read, and write raw memory buffers. Use with care.",
     NULL},
    {"unsafe.alloc", "unsafe.alloc(size: i32) -> i64", "Allocate a buffer.",
     "Returns a handle to a zero-initialized buffer.", "i64 buf = unsafe.alloc(256)"},
    {"unsafe.realloc", "unsafe.realloc(buf: i64, size: i32) -> i64", "Resize a buffer.",
     "Returns the (possibly new) handle.", "buf = unsafe.realloc(buf, 512)"},
    {"unsafe.free", "unsafe.free(buf: i64) -> void", "Free a buffer.", "Releases the buffer memory.",
     "unsafe.free(buf)"},
    {"unsafe.ptr", "unsafe.ptr(buf: i64) -> i64", "Get raw pointer.", "Returns the underlying C pointer for FFI use.",
     "i64 p = unsafe.ptr(buf)"},
    {"unsafe.len", "unsafe.len(buf: i64) -> i32", "Get buffer length.", "Returns the allocated size in bytes.",
     "i32 n = unsafe.len(buf)"},
    {"unsafe.get", "unsafe.get(buf: i64, offset: i32) -> i32", "Read a byte.",
     "Returns the byte value at the given offset.", "i32 b = unsafe.get(buf, 0)"},
    {"unsafe.set", "unsafe.set(buf: i64, offset: i32, value: i32) -> void", "Write a byte.",
     "Sets the byte at the given offset.", "unsafe.set(buf, 0, 0xFF)"},
    {"unsafe.get_i32", "unsafe.get_i32(buf: i64, offset: i32) -> i32", "Read a 32-bit integer.",
     "Reads a native-endian i32 at the given byte offset.", "i32 v = unsafe.get_i32(buf, 0)"},
    {"unsafe.set_i32", "unsafe.set_i32(buf: i64, offset: i32, value: i32) -> void", "Write a 32-bit integer.",
     "Writes a native-endian i32 at the given byte offset.", "unsafe.set_i32(buf, 0, 42)"},
    {"unsafe.get_i64", "unsafe.get_i64(buf: i64, offset: i32) -> i64", "Read a 64-bit integer.",
     "Reads a native-endian i64 at the given byte offset.", "i64 v = unsafe.get_i64(buf, 0)"},
    {"unsafe.set_i64", "unsafe.set_i64(buf: i64, offset: i32, value: i64) -> void", "Write a 64-bit integer.",
     "Writes a native-endian i64 at the given byte offset.", "unsafe.set_i64(buf, 0, 100)"},
    {"unsafe.get_f32", "unsafe.get_f32(buf: i64, offset: i32) -> f64", "Read a 32-bit float.",
     "Reads a native-endian f32 at the given byte offset and returns it as f64.", "f64 v = unsafe.get_f32(buf, 0)"},
    {"unsafe.set_f32", "unsafe.set_f32(buf: i64, offset: i32, value: f64) -> void", "Write a 32-bit float.",
     "Writes a native-endian f32 value at the given byte offset.", "unsafe.set_f32(buf, 0, 3.5)"},
    {"unsafe.get_f64", "unsafe.get_f64(buf: i64, offset: i32) -> f64", "Read a 64-bit float.",
     "Reads a native-endian f64 at the given byte offset.", "f64 v = unsafe.get_f64(buf, 0)"},
    {"unsafe.set_f64", "unsafe.set_f64(buf: i64, offset: i32, value: f64) -> void", "Write a 64-bit float.",
     "Writes a native-endian f64 at the given byte offset.", "unsafe.set_f64(buf, 0, 3.14)"},
    {"unsafe.write_str", "unsafe.write_str(buf: i64, offset: i32, value: string) -> void",
     "Write a string into a buffer.", "Copies the string bytes into the buffer starting at the given byte offset.",
     "unsafe.write_str(buf, 0, \"hello\")"},
    {"unsafe.peek_u8", "unsafe.peek_u8(ptr: i64, offset: i32) -> i32", "Read a byte from a raw pointer.",
     "Reads an unchecked u8 from ptr + offset.", "i32 b = unsafe.peek_u8(ptr, 0)"},
    {"unsafe.peek_i32", "unsafe.peek_i32(ptr: i64, offset: i32) -> i32", "Read a 32-bit integer from a raw pointer.",
     "Reads an unchecked native-endian i32 from ptr + offset.", "i32 v = unsafe.peek_i32(ptr, 0)"},
    {"unsafe.peek_i64", "unsafe.peek_i64(ptr: i64, offset: i32) -> i64", "Read a 64-bit integer from a raw pointer.",
     "Reads an unchecked native-endian i64 from ptr + offset.", "i64 v = unsafe.peek_i64(ptr, 0)"},
    {"unsafe.peek_f32", "unsafe.peek_f32(ptr: i64, offset: i32) -> f64", "Read a 32-bit float from a raw pointer.",
     "Reads an unchecked native-endian f32 from ptr + offset and returns it as f64.",
     "f64 v = unsafe.peek_f32(ptr, 0)"},
    {"unsafe.peek_f64", "unsafe.peek_f64(ptr: i64, offset: i32) -> f64", "Read a 64-bit float from a raw pointer.",
     "Reads an unchecked native-endian f64 from ptr + offset.", "f64 v = unsafe.peek_f64(ptr, 0)"},
    {"unsafe.peek_ptr", "unsafe.peek_ptr(ptr: i64, offset: i32) -> i64", "Read a pointer from a raw pointer.",
     "Reads an unchecked pointer-sized value from ptr + offset.", "i64 p = unsafe.peek_ptr(ptr, 0)"},
    {"unsafe.poke_u8", "unsafe.poke_u8(ptr: i64, offset: i32, value: i32) -> void", "Write a byte to a raw pointer.",
     "Writes an unchecked u8 value to ptr + offset.", "unsafe.poke_u8(ptr, 0, 0xff)"},
    {"unsafe.poke_i32", "unsafe.poke_i32(ptr: i64, offset: i32, value: i32) -> void",
     "Write a 32-bit integer to a raw pointer.", "Writes an unchecked native-endian i32 to ptr + offset.",
     "unsafe.poke_i32(ptr, 0, 42)"},
    {"unsafe.poke_i64", "unsafe.poke_i64(ptr: i64, offset: i32, value: i64) -> void",
     "Write a 64-bit integer to a raw pointer.", "Writes an unchecked native-endian i64 to ptr + offset.",
     "unsafe.poke_i64(ptr, 0, 99)"},
    {"unsafe.poke_f32", "unsafe.poke_f32(ptr: i64, offset: i32, value: f64) -> void",
     "Write a 32-bit float to a raw pointer.", "Writes an unchecked native-endian f32 to ptr + offset.",
     "unsafe.poke_f32(ptr, 0, 1.5)"},
    {"unsafe.poke_f64", "unsafe.poke_f64(ptr: i64, offset: i32, value: f64) -> void",
     "Write a 64-bit float to a raw pointer.", "Writes an unchecked native-endian f64 to ptr + offset.",
     "unsafe.poke_f64(ptr, 0, 1.5)"},
    {"unsafe.poke_ptr", "unsafe.poke_ptr(ptr: i64, offset: i32, value: i64) -> void",
     "Write a pointer to a raw pointer.", "Writes an unchecked pointer-sized value to ptr + offset.",
     "unsafe.poke_ptr(ptr, 0, other_ptr)"},
    {"unsafe.null", "unsafe.null() -> i64", "Get null pointer.", "Returns 0 (null pointer constant).",
     "i64 p = unsafe.null()"},
    {"unsafe.sizeof", "unsafe.sizeof(type: string) -> i32", "Get type size.",
     "Returns the size in bytes of a C type name.", "i32 n = unsafe.sizeof(\"int\")"},
    {"unsafe.sizeof_ptr", "unsafe.sizeof_ptr() -> i32", "Get pointer size.",
     "Returns the size of a pointer on this platform (4 or 8).", "i32 n = unsafe.sizeof_ptr()"},
    {"unsafe.alignof", "unsafe.alignof(type: string) -> i32", "Get type alignment.",
     "Returns the alignment requirement in bytes of a C type name.", "i32 n = unsafe.alignof(\"double\")"},
    {"unsafe.offsetof", "unsafe.offsetof(type: string, field: i32) -> i32", "Get field offset.",
     "Returns the byte offset of a generated struct field index within the named C struct layout.",
     "i32 off = unsafe.offsetof(\"sockaddr_in\", 0)"},
    {"unsafe.struct_size", "unsafe.struct_size(type: string) -> i32", "Get struct size.",
     "Returns the size in bytes of a named C struct layout.", "i32 n = unsafe.struct_size(\"sockaddr_in\")"},
    {"unsafe.errno", "unsafe.errno() -> i32", "Get errno.", "Returns the current C errno value.",
     "i32 e = unsafe.errno()"},
    {"unsafe.set_errno", "unsafe.set_errno(value: i32) -> void", "Set errno.", "Sets the C errno value.",
     "unsafe.set_errno(0)"},
    {"unsafe.str", "unsafe.str(ptr: i64) -> string", "Read C string.",
     "Reads a null-terminated string from a raw pointer.", "string s = unsafe.str(ptr)"},
    {"unsafe.copy", "unsafe.copy(dst: i64, dst_off: i32, src: i64, src_off: i32, len: i32) -> void",
     "Copy bytes between buffers.", "Copies len bytes from src+src_off to dst+dst_off.",
     "unsafe.copy(dst, 0, src, 0, 64)"},
    {"unsafe.cb_alloc", "unsafe.cb_alloc() -> i64", "Allocate a callback slot.",
     "Returns an FFI callback slot handle for advanced unsafe callback plumbing.", "i64 slot = unsafe.cb_alloc()"},
    {"unsafe.cb_free", "unsafe.cb_free(slot: i32) -> void", "Free a callback slot.",
     "Releases a callback slot previously allocated with unsafe.cb_alloc.", "unsafe.cb_free(0)"},
};

#define UNSAFE_COUNT (sizeof(unsafe_docs) / sizeof(unsafe_docs[0]))

/* ── parse module ────────────────────────────────────────────────── */

/* ── sdl module ──────────────────────────────────────────────────── */

static const vigil_doc_entry_t sdl_docs[] = {
    {"sdl", NULL, "SDL3 multimedia library bindings.",
     "Provides access to SDL3 for window management, rendering, input,\n"
     "audio, and more. The SDL3 library is statically linked into Vigil.\n"
     "Disable at build time with -DVIGIL_PLUGIN_SDL=OFF.",
     "import \"sdl\";\n\n"
     "sdl.init(sdl.INIT_VIDEO());\n"
     "defer sdl.quit();"},
    {"sdl.init", "sdl.init(flags: i32) -> (bool, err)", "Initialize SDL subsystems.",
     "Initializes the SDL library. Pass one or more INIT_* flag constants\n"
     "combined with bitwise OR. Returns (true, ok) on success.",
     "bool ok, err e = sdl.init(sdl.INIT_VIDEO() | sdl.INIT_AUDIO());\n"
     "defer sdl.quit();"},
    {"sdl.init_sub_system", "sdl.init_sub_system(flags: i32) -> (bool, err)", "Initialize additional SDL subsystems.",
     "Initialize subsystems that were not included in the initial sdl.init() call.",
     "sdl.init_sub_system(sdl.INIT_JOYSTICK())"},
    {"sdl.quit_sub_system", "sdl.quit_sub_system(flags: i32) -> void", "Shut down specific SDL subsystems.",
     "Shuts down the specified subsystems without quitting SDL entirely.", "sdl.quit_sub_system(sdl.INIT_AUDIO())"},
    {"sdl.quit", "sdl.quit() -> void", "Clean up all initialized SDL subsystems.",
     "Call this when done with SDL. Typically used with defer.", "defer sdl.quit();"},
    {"sdl.was_init", "sdl.was_init(flags: i32) -> i32", "Check which subsystems are initialized.",
     "Returns a mask of the specified subsystems that are currently initialized.",
     "i32 active = sdl.was_init(sdl.INIT_VIDEO())"},
    {"sdl.get_error", "sdl.get_error() -> string", "Get the last SDL error message.",
     "Returns the error message for the last SDL function that failed.", "string msg = sdl.get_error()"},
    {"sdl.get_version", "sdl.get_version() -> i32", "Get the linked SDL version.",
     "Returns the version as a packed integer. Use bitwise operations to\n"
     "extract major/minor/patch: major = v / 1000000, minor = (v / 1000) % 1000,\n"
     "patch = v % 1000.",
     "i32 v = sdl.get_version()"},
    {"sdl.get_revision", "sdl.get_revision() -> string", "Get the SDL revision string.",
     "Returns the source revision of the linked SDL library.", "string rev = sdl.get_revision()"},
    {"sdl.INIT_VIDEO", "sdl.INIT_VIDEO() -> i32", "Video subsystem init flag.", NULL, NULL},
    {"sdl.INIT_AUDIO", "sdl.INIT_AUDIO() -> i32", "Audio subsystem init flag.", NULL, NULL},
    {"sdl.INIT_CAMERA", "sdl.INIT_CAMERA() -> i32", "Camera subsystem init flag.", NULL, NULL},
    {"sdl.INIT_EVENTS", "sdl.INIT_EVENTS() -> i32", "Events subsystem init flag.", NULL, NULL},
    {"sdl.INIT_JOYSTICK", "sdl.INIT_JOYSTICK() -> i32", "Joystick subsystem init flag.", NULL, NULL},
    {"sdl.INIT_HAPTIC", "sdl.INIT_HAPTIC() -> i32", "Haptic (force feedback) subsystem init flag.", NULL, NULL},
    {"sdl.INIT_GAMEPAD", "sdl.INIT_GAMEPAD() -> i32", "Gamepad subsystem init flag.", NULL, NULL},
    {"sdl.INIT_SENSOR", "sdl.INIT_SENSOR() -> i32", "Sensor subsystem init flag.", NULL, NULL},
    /* Window class */
    {"sdl.Window", NULL, "SDL window handle.", "Create with sdl.Window.create(). Wraps an SDL_Window.", NULL},
    {"sdl.Window.create", "sdl.Window.create(title: string, w: i32, h: i32, flags: i32) -> (sdl.Window, err)",
     "Create a window.", "Returns a window handle and ok, or nil and an error.",
     "sdl.Window win, err e = sdl.Window.create(\"Hello\", 800, 600, sdl.WINDOW_RESIZABLE())"},
    {"sdl.Window.destroy", "win.destroy() -> void", "Destroy a window.", NULL, "win.destroy()"},
    {"sdl.Window.get_id", "win.get_id() -> i32", "Get the window's numeric ID.", NULL, NULL},
    {"sdl.Window.set_title", "win.set_title(title: string) -> (bool, err)", "Set the window title.", NULL, NULL},
    {"sdl.Window.get_title", "win.get_title() -> string", "Get the window title.", NULL, NULL},
    {"sdl.Window.set_position", "win.set_position(x: i32, y: i32) -> (bool, err)", "Set window position.", NULL, NULL},
    {"sdl.Window.get_position", "win.get_position() -> (i32, i32)", "Get window position (x, y).", NULL, NULL},
    {"sdl.Window.set_size", "win.set_size(w: i32, h: i32) -> (bool, err)", "Set window client area size.", NULL, NULL},
    {"sdl.Window.get_size", "win.get_size() -> (i32, i32)", "Get window client area size (w, h).", NULL, NULL},
    {"sdl.Window.set_fullscreen", "win.set_fullscreen(fs: bool) -> (bool, err)", "Toggle fullscreen.", NULL, NULL},
    {"sdl.Window.show", "win.show() -> void", "Show the window.", NULL, NULL},
    {"sdl.Window.hide", "win.hide() -> void", "Hide the window.", NULL, NULL},
    {"sdl.Window.raise", "win.raise() -> void", "Raise the window above others.", NULL, NULL},
    {"sdl.Window.minimize", "win.minimize() -> void", "Minimize the window.", NULL, NULL},
    {"sdl.Window.maximize", "win.maximize() -> void", "Maximize the window.", NULL, NULL},
    {"sdl.Window.restore", "win.restore() -> void", "Restore a minimized/maximized window.", NULL, NULL},
    {"sdl.Window.set_resizable", "win.set_resizable(r: bool) -> void", "Set whether the window is resizable.", NULL,
     NULL},
    {"sdl.Window.set_bordered", "win.set_bordered(b: bool) -> void", "Set whether the window has a border.", NULL,
     NULL},
    {"sdl.Window.get_flags", "win.get_flags() -> i32", "Get the window flags bitmask.", NULL, NULL},
    /* Window flag constants */
    {"sdl.WINDOW_FULLSCREEN", "sdl.WINDOW_FULLSCREEN() -> i32", "Fullscreen window flag.", NULL, NULL},
    {"sdl.WINDOW_RESIZABLE", "sdl.WINDOW_RESIZABLE() -> i32", "Resizable window flag.", NULL, NULL},
    {"sdl.WINDOW_HIDDEN", "sdl.WINDOW_HIDDEN() -> i32", "Hidden window flag.", NULL, NULL},
    {"sdl.WINDOW_BORDERLESS", "sdl.WINDOW_BORDERLESS() -> i32", "Borderless window flag.", NULL, NULL},
    {"sdl.WINDOW_MINIMIZED", "sdl.WINDOW_MINIMIZED() -> i32", "Minimized window flag.", NULL, NULL},
    {"sdl.WINDOW_MAXIMIZED", "sdl.WINDOW_MAXIMIZED() -> i32", "Maximized window flag.", NULL, NULL},
    {"sdl.WINDOW_ALWAYS_ON_TOP", "sdl.WINDOW_ALWAYS_ON_TOP() -> i32", "Always-on-top window flag.", NULL, NULL},
    /* Renderer class */
    {"sdl.Renderer", NULL, "SDL 2D renderer handle.", "Create with sdl.Renderer.create(). Wraps an SDL_Renderer.",
     NULL},
    {"sdl.Renderer.create", "sdl.Renderer.create(win: sdl.Window, driver: string) -> (sdl.Renderer, err)",
     "Create a 2D renderer for a window.", "Pass empty string for driver to use the default.",
     "sdl.Renderer ren, err e = sdl.Renderer.create(win, \"\")"},
    {"sdl.Renderer.destroy", "ren.destroy() -> void", "Destroy a renderer.", NULL, NULL},
    {"sdl.Renderer.clear", "ren.clear() -> (bool, err)", "Clear the rendering target.", NULL, NULL},
    {"sdl.Renderer.present", "ren.present() -> (bool, err)", "Present the rendered frame.", NULL, NULL},
    {"sdl.Renderer.set_draw_color", "ren.set_draw_color(r: i32, g: i32, b: i32, a: i32) -> (bool, err)",
     "Set the draw color (0-255 per channel).", NULL, "ren.set_draw_color(255, 0, 0, 255)"},
    {"sdl.Renderer.get_draw_color", "ren.get_draw_color() -> (i32, i32, i32, i32)",
     "Get the current draw color (r, g, b, a).", NULL, NULL},
    {"sdl.Renderer.draw_point", "ren.draw_point(x: f64, y: f64) -> (bool, err)", "Draw a point.", NULL, NULL},
    {"sdl.Renderer.draw_line", "ren.draw_line(x1: f64, y1: f64, x2: f64, y2: f64) -> (bool, err)", "Draw a line.", NULL,
     NULL},
    {"sdl.Renderer.draw_rect", "ren.draw_rect(x: f64, y: f64, w: f64, h: f64) -> (bool, err)",
     "Draw a rectangle outline.", NULL, NULL},
    {"sdl.Renderer.fill_rect", "ren.fill_rect(x: f64, y: f64, w: f64, h: f64) -> (bool, err)",
     "Draw a filled rectangle.", NULL, NULL},
    {"sdl.Renderer.set_vsync", "ren.set_vsync(vsync: i32) -> (bool, err)", "Set VSync mode.", NULL, NULL},
    /* Event class */
    {"sdl.Event", NULL, "SDL event handle.", "Create with sdl.Event.new(). Poll events in a loop with ev.poll().",
     NULL},
    {"sdl.Event.new", "sdl.Event.new() -> (sdl.Event, err)", "Allocate an event object.", NULL,
     "sdl.Event ev, err e = sdl.Event.new()"},
    {"sdl.Event.poll", "ev.poll() -> bool", "Poll for a pending event.", "Returns true if an event was available.",
     NULL},
    {"sdl.Event.wait", "ev.wait() -> bool", "Wait for the next event.", NULL, NULL},
    {"sdl.Event.wait_timeout", "ev.wait_timeout(ms: i32) -> bool", "Wait for an event with timeout.", NULL, NULL},
    {"sdl.Event.type", "ev.type() -> i32", "Get the event type.", "Compare with EVENT_* constants.", NULL},
    {"sdl.Event.key_scancode", "ev.key_scancode() -> i32", "Get keyboard scancode.", NULL, NULL},
    {"sdl.Event.key_keycode", "ev.key_keycode() -> i32", "Get keyboard keycode.", NULL, NULL},
    {"sdl.Event.key_mod", "ev.key_mod() -> i32", "Get keyboard modifier state.", NULL, NULL},
    {"sdl.Event.key_repeat", "ev.key_repeat() -> bool", "Check if key event is a repeat.", NULL, NULL},
    {"sdl.Event.mouse_x", "ev.mouse_x() -> f64", "Get mouse X position.", NULL, NULL},
    {"sdl.Event.mouse_y", "ev.mouse_y() -> f64", "Get mouse Y position.", NULL, NULL},
    {"sdl.Event.mouse_button", "ev.mouse_button() -> i32", "Get mouse button number.", NULL, NULL},
    {"sdl.Event.wheel_x", "ev.wheel_x() -> f64", "Get mouse wheel horizontal scroll.", NULL, NULL},
    {"sdl.Event.wheel_y", "ev.wheel_y() -> f64", "Get mouse wheel vertical scroll.", NULL, NULL},
    {"sdl.EVENT_QUIT", "sdl.EVENT_QUIT() -> i32", "Quit event type.", NULL, NULL},
    {"sdl.EVENT_KEY_DOWN", "sdl.EVENT_KEY_DOWN() -> i32", "Key press event type.", NULL, NULL},
    {"sdl.EVENT_KEY_UP", "sdl.EVENT_KEY_UP() -> i32", "Key release event type.", NULL, NULL},
    {"sdl.EVENT_MOUSE_MOTION", "sdl.EVENT_MOUSE_MOTION() -> i32", "Mouse motion event type.", NULL, NULL},
    {"sdl.EVENT_MOUSE_BUTTON_DOWN", "sdl.EVENT_MOUSE_BUTTON_DOWN() -> i32", "Mouse button press event type.", NULL,
     NULL},
    {"sdl.EVENT_MOUSE_BUTTON_UP", "sdl.EVENT_MOUSE_BUTTON_UP() -> i32", "Mouse button release event type.", NULL, NULL},
    {"sdl.EVENT_MOUSE_WHEEL", "sdl.EVENT_MOUSE_WHEEL() -> i32", "Mouse wheel event type.", NULL, NULL},
    /* Keyboard / mouse queries (slice 5) */
    {"sdl.is_key_pressed", "sdl.is_key_pressed(scancode: i32) -> bool", "Check if a key is currently pressed.",
     "Uses SDL keyboard state snapshot. Pass a SCANCODE_* constant.", "if (sdl.is_key_pressed(sdl.SCANCODE_W())) { }"},
    {"sdl.get_mod_state", "sdl.get_mod_state() -> i32", "Get current keyboard modifier state.", NULL, NULL},
    {"sdl.get_key_name", "sdl.get_key_name(keycode: i32) -> string", "Get human-readable name for a keycode.", NULL,
     NULL},
    {"sdl.get_scancode_name", "sdl.get_scancode_name(scancode: i32) -> string",
     "Get human-readable name for a scancode.", NULL, NULL},
    {"sdl.get_mouse_state", "sdl.get_mouse_state() -> (f64, f64)", "Get mouse position relative to the focused window.",
     "Returns (x, y). Use get_mouse_buttons() for button state.", NULL},
    {"sdl.get_mouse_buttons", "sdl.get_mouse_buttons() -> i32", "Get mouse button bitmask.",
     "Test with BUTTON_LEFT, BUTTON_MIDDLE, BUTTON_RIGHT constants.", NULL},
    {"sdl.get_global_mouse_state", "sdl.get_global_mouse_state() -> (f64, f64)", "Get desktop-relative mouse position.",
     "Returns (x, y). Use get_mouse_buttons() for button state.", NULL},
    {"sdl.warp_mouse", "sdl.warp_mouse(win: sdl.Window, x: f64, y: f64) -> void",
     "Move the mouse cursor within a window.", NULL, NULL},
    {"sdl.show_cursor", "sdl.show_cursor() -> bool", "Show the mouse cursor.", NULL, NULL},
    {"sdl.hide_cursor", "sdl.hide_cursor() -> bool", "Hide the mouse cursor.", NULL, NULL},
    {"sdl.cursor_visible", "sdl.cursor_visible() -> bool", "Check if the cursor is visible.", NULL, NULL},
    {"sdl.delay", "sdl.delay(ms: i32) -> void", "Wait for the specified number of milliseconds.", NULL, NULL},
    /* Timer / timing (slice 7) */
    {"sdl.get_ticks", "sdl.get_ticks() -> i64", "Milliseconds since SDL_Init.", NULL, NULL},
    {"sdl.get_ticks_ns", "sdl.get_ticks_ns() -> i64", "Nanoseconds since SDL_Init.", NULL, NULL},
    {"sdl.get_performance_counter", "sdl.get_performance_counter() -> i64", "High-resolution counter value.",
     "Divide by get_performance_frequency() for seconds.", NULL},
    {"sdl.get_performance_frequency", "sdl.get_performance_frequency() -> i64",
     "High-resolution counter frequency (counts per second).", NULL, NULL},
    {"sdl.delay_ns", "sdl.delay_ns(ns: i64) -> void", "Wait for the specified number of nanoseconds.", NULL, NULL},
    {"sdl.delay_precise", "sdl.delay_precise(ns: i64) -> void", "Precise nanosecond delay using busy-wait.",
     "More accurate than delay_ns but uses more CPU.", NULL},
    {"sdl.SCANCODE_A", "sdl.SCANCODE_A() -> i32", "Scancode for the A key.", NULL, NULL},
    {"sdl.SCANCODE_W", "sdl.SCANCODE_W() -> i32", "Scancode for the W key.", NULL, NULL},
    {"sdl.SCANCODE_S", "sdl.SCANCODE_S() -> i32", "Scancode for the S key.", NULL, NULL},
    {"sdl.SCANCODE_D", "sdl.SCANCODE_D() -> i32", "Scancode for the D key.", NULL, NULL},
    {"sdl.SCANCODE_UP", "sdl.SCANCODE_UP() -> i32", "Scancode for the Up arrow key.", NULL, NULL},
    {"sdl.SCANCODE_DOWN", "sdl.SCANCODE_DOWN() -> i32", "Scancode for the Down arrow key.", NULL, NULL},
    {"sdl.SCANCODE_LEFT", "sdl.SCANCODE_LEFT() -> i32", "Scancode for the Left arrow key.", NULL, NULL},
    {"sdl.SCANCODE_RIGHT", "sdl.SCANCODE_RIGHT() -> i32", "Scancode for the Right arrow key.", NULL, NULL},
    {"sdl.SCANCODE_SPACE", "sdl.SCANCODE_SPACE() -> i32", "Scancode for the Space key.", NULL, NULL},
    {"sdl.SCANCODE_ESCAPE", "sdl.SCANCODE_ESCAPE() -> i32", "Scancode for the Escape key.", NULL, NULL},
    {"sdl.SCANCODE_RETURN", "sdl.SCANCODE_RETURN() -> i32", "Scancode for the Return/Enter key.", NULL, NULL},
    {"sdl.KEY_RETURN", "sdl.KEY_RETURN() -> i32", "Keycode for Return/Enter.", NULL, NULL},
    {"sdl.KEY_ESCAPE", "sdl.KEY_ESCAPE() -> i32", "Keycode for Escape.", NULL, NULL},
    {"sdl.KEY_SPACE", "sdl.KEY_SPACE() -> i32", "Keycode for Space.", NULL, NULL},
    {"sdl.BUTTON_LEFT", "sdl.BUTTON_LEFT() -> i32", "Left mouse button constant.", NULL, NULL},
    {"sdl.BUTTON_MIDDLE", "sdl.BUTTON_MIDDLE() -> i32", "Middle mouse button constant.", NULL, NULL},
    {"sdl.BUTTON_RIGHT", "sdl.BUTTON_RIGHT() -> i32", "Right mouse button constant.", NULL, NULL},
    /* Surface class (slice 6) */
    {"sdl.Surface", NULL, "SDL surface handle.", "Load with sdl.Surface.load() or sdl.Surface.load_bmp().", NULL},
    {"sdl.Surface.load", "sdl.Surface.load(path: string) -> (sdl.Surface, err)", "Load a BMP or PNG image from a file.",
     NULL, "sdl.Surface surf, err e = sdl.Surface.load(\"sprite.png\")"},
    {"sdl.Surface.load_bmp", "sdl.Surface.load_bmp(path: string) -> (sdl.Surface, err)",
     "Load a BMP image from a file.", NULL, NULL},
    {"sdl.Surface.destroy", "surf.destroy() -> void", "Free a surface.", NULL, NULL},
    /* Texture class (slice 6) */
    {"sdl.Texture", NULL, "SDL texture handle.", "Create with sdl.Texture.from_surface() or sdl.Texture.create().",
     NULL},
    {"sdl.Texture.create",
     "sdl.Texture.create(ren: sdl.Renderer, format: i32, access: i32, w: i32, h: i32) -> (sdl.Texture, err)",
     "Create a blank texture.", NULL, NULL},
    {"sdl.Texture.from_surface", "sdl.Texture.from_surface(ren: sdl.Renderer, surf: sdl.Surface) -> (sdl.Texture, err)",
     "Create a texture from a surface.", NULL, "sdl.Texture tex, err e = sdl.Texture.from_surface(ren, surf)"},
    {"sdl.Texture.destroy", "tex.destroy() -> void", "Destroy a texture.", NULL, NULL},
    {"sdl.Texture.get_size", "tex.get_size() -> (f64, f64)", "Get texture size (w, h).", NULL, NULL},
    {"sdl.Texture.set_color_mod", "tex.set_color_mod(r: i32, g: i32, b: i32) -> void", "Set color modulation (0-255).",
     NULL, NULL},
    {"sdl.Texture.set_alpha_mod", "tex.set_alpha_mod(alpha: i32) -> void", "Set alpha modulation (0-255).", NULL, NULL},
    {"sdl.Texture.set_blend_mode", "tex.set_blend_mode(mode: i32) -> void", "Set blend mode.",
     "Use BLENDMODE_* constants.", NULL},
    {"sdl.Renderer.render_texture",
     "ren.render_texture(tex: sdl.Texture, sx: f64, sy: f64, sw: f64, sh: f64, dx: f64, dy: f64, dw: f64, dh: f64) "
     "-> (bool, err)",
     "Render a texture.", "Pass all-zero src rect to use full texture. Pass all-zero dst rect for full target.", NULL},
    {"sdl.Renderer.render_texture_rotated",
     "ren.render_texture_rotated(tex, sx, sy, sw, sh, dx, dy, dw, dh, angle, cx, cy, flip) -> (bool, err)",
     "Render a texture with rotation and flip.", "Use FLIP_* constants for flip parameter.", NULL},
    {"sdl.TEXTUREACCESS_STATIC", "sdl.TEXTUREACCESS_STATIC() -> i32", "Static texture access.", NULL, NULL},
    {"sdl.TEXTUREACCESS_STREAMING", "sdl.TEXTUREACCESS_STREAMING() -> i32", "Streaming texture access.", NULL, NULL},
    {"sdl.TEXTUREACCESS_TARGET", "sdl.TEXTUREACCESS_TARGET() -> i32", "Render target texture access.", NULL, NULL},
    {"sdl.BLENDMODE_NONE", "sdl.BLENDMODE_NONE() -> i32", "No blending.", NULL, NULL},
    {"sdl.BLENDMODE_BLEND", "sdl.BLENDMODE_BLEND() -> i32", "Alpha blending.", NULL, NULL},
    {"sdl.BLENDMODE_ADD", "sdl.BLENDMODE_ADD() -> i32", "Additive blending.", NULL, NULL},
    {"sdl.BLENDMODE_MOD", "sdl.BLENDMODE_MOD() -> i32", "Color modulate blending.", NULL, NULL},
    {"sdl.BLENDMODE_MUL", "sdl.BLENDMODE_MUL() -> i32", "Color multiply blending.", NULL, NULL},
    {"sdl.FLIP_NONE", "sdl.FLIP_NONE() -> i32", "No flip.", NULL, NULL},
    {"sdl.FLIP_HORIZONTAL", "sdl.FLIP_HORIZONTAL() -> i32", "Flip horizontally.", NULL, NULL},
    {"sdl.FLIP_VERTICAL", "sdl.FLIP_VERTICAL() -> i32", "Flip vertically.", NULL, NULL},
    /* Audio (slice 8) */
    {"sdl.load_wav", "sdl.load_wav(path: string) -> (i64, err)", "Load a WAV file.",
     "Returns a wav handle. Query format with wav_format/wav_channels/wav_freq.",
     "i64 wav, err e = sdl.load_wav(\"sound.wav\")"},
    {"sdl.wav_free", "sdl.wav_free(handle: i64) -> void", "Free a loaded WAV buffer.", NULL, NULL},
    {"sdl.wav_format", "sdl.wav_format(handle: i64) -> i32", "Get WAV audio format.", NULL, NULL},
    {"sdl.wav_channels", "sdl.wav_channels(handle: i64) -> i32", "Get WAV channel count.", NULL, NULL},
    {"sdl.wav_freq", "sdl.wav_freq(handle: i64) -> i32", "Get WAV sample rate.", NULL, NULL},
    {"sdl.AudioStream", NULL, "SDL audio stream for playback.",
     "Open with sdl.AudioStream.open(). Push WAV data with put_wav().", NULL},
    {"sdl.AudioStream.open", "sdl.AudioStream.open(format: i32, channels: i32, freq: i32) -> (sdl.AudioStream, err)",
     "Open the default playback device.", "Use AUDIO_* constants for format.",
     "sdl.AudioStream stream, err e = sdl.AudioStream.open(sdl.AUDIO_S16(), 2, 44100)"},
    {"sdl.AudioStream.destroy", "stream.destroy() -> void", "Destroy an audio stream.", NULL, NULL},
    {"sdl.AudioStream.put_wav", "stream.put_wav(wav_handle: i64) -> (bool, err)", "Queue WAV data for playback.", NULL,
     NULL},
    {"sdl.AudioStream.get_queued", "stream.get_queued() -> i32", "Get bytes queued for playback.", NULL, NULL},
    {"sdl.AudioStream.resume", "stream.resume() -> (bool, err)", "Start/resume playback.", NULL, NULL},
    {"sdl.AudioStream.pause", "stream.pause() -> (bool, err)", "Pause playback.", NULL, NULL},
    {"sdl.AUDIO_S16", "sdl.AUDIO_S16() -> i32", "Signed 16-bit audio format.", NULL, NULL},
    {"sdl.AUDIO_S32", "sdl.AUDIO_S32() -> i32", "Signed 32-bit audio format.", NULL, NULL},
    {"sdl.AUDIO_F32", "sdl.AUDIO_F32() -> i32", "32-bit float audio format.", NULL, NULL},
    /* Gamepad (slice 9) */
    {"sdl.has_gamepad", "sdl.has_gamepad() -> bool", "Check if any gamepad is connected.", NULL, NULL},
    {"sdl.get_gamepad_count", "sdl.get_gamepad_count() -> i32", "Get the number of connected gamepads.",
     "Also refreshes the internal gamepad list.", NULL},
    {"sdl.get_gamepad_id", "sdl.get_gamepad_id(index: i32) -> i32", "Get the instance ID of a gamepad by index.",
     "Call get_gamepad_count() first to refresh the list.", NULL},
    {"sdl.Gamepad", NULL, "SDL gamepad handle.", "Open with sdl.Gamepad.open(instance_id).", NULL},
    {"sdl.Gamepad.open", "sdl.Gamepad.open(instance_id: i32) -> (sdl.Gamepad, err)", "Open a gamepad.", NULL,
     "sdl.Gamepad gp, err e = sdl.Gamepad.open(sdl.get_gamepad_id(0))"},
    {"sdl.Gamepad.close", "gp.close() -> void", "Close a gamepad.", NULL, NULL},
    {"sdl.Gamepad.get_name", "gp.get_name() -> string", "Get gamepad name.", NULL, NULL},
    {"sdl.Gamepad.get_axis", "gp.get_axis(axis: i32) -> i32", "Get axis value (-32768 to 32767).",
     "Use GAMEPAD_AXIS_* constants.", NULL},
    {"sdl.Gamepad.get_button", "gp.get_button(button: i32) -> bool", "Check if a button is pressed.",
     "Use GAMEPAD_BUTTON_* constants.", NULL},
    {"sdl.Gamepad.rumble", "gp.rumble(low: i32, high: i32, duration_ms: i32) -> (bool, err)", "Start rumble effect.",
     NULL, NULL},
    {"sdl.Event.gamepad_which", "ev.gamepad_which() -> i32", "Get gamepad instance ID from event.", NULL, NULL},
    {"sdl.Event.gamepad_axis", "ev.gamepad_axis() -> i32", "Get gamepad axis from event.", NULL, NULL},
    {"sdl.Event.gamepad_axis_value", "ev.gamepad_axis_value() -> i32", "Get gamepad axis value from event.", NULL,
     NULL},
    {"sdl.Event.gamepad_button", "ev.gamepad_button() -> i32", "Get gamepad button from event.", NULL, NULL},
    {"sdl.GAMEPAD_AXIS_LEFTX", "sdl.GAMEPAD_AXIS_LEFTX() -> i32", "Left stick X axis.", NULL, NULL},
    {"sdl.GAMEPAD_AXIS_LEFTY", "sdl.GAMEPAD_AXIS_LEFTY() -> i32", "Left stick Y axis.", NULL, NULL},
    {"sdl.GAMEPAD_AXIS_RIGHTX", "sdl.GAMEPAD_AXIS_RIGHTX() -> i32", "Right stick X axis.", NULL, NULL},
    {"sdl.GAMEPAD_AXIS_RIGHTY", "sdl.GAMEPAD_AXIS_RIGHTY() -> i32", "Right stick Y axis.", NULL, NULL},
    {"sdl.GAMEPAD_BUTTON_SOUTH", "sdl.GAMEPAD_BUTTON_SOUTH() -> i32", "South face button (A/Cross).", NULL, NULL},
    {"sdl.GAMEPAD_BUTTON_EAST", "sdl.GAMEPAD_BUTTON_EAST() -> i32", "East face button (B/Circle).", NULL, NULL},
    {"sdl.GAMEPAD_BUTTON_WEST", "sdl.GAMEPAD_BUTTON_WEST() -> i32", "West face button (X/Square).", NULL, NULL},
    {"sdl.GAMEPAD_BUTTON_NORTH", "sdl.GAMEPAD_BUTTON_NORTH() -> i32", "North face button (Y/Triangle).", NULL, NULL},
    {"sdl.GAMEPAD_BUTTON_START", "sdl.GAMEPAD_BUTTON_START() -> i32", "Start button.", NULL, NULL},
    {"sdl.EVENT_GAMEPAD_AXIS_MOTION", "sdl.EVENT_GAMEPAD_AXIS_MOTION() -> i32", "Gamepad axis motion event.", NULL,
     NULL},
    {"sdl.EVENT_GAMEPAD_BUTTON_DOWN", "sdl.EVENT_GAMEPAD_BUTTON_DOWN() -> i32", "Gamepad button press event.", NULL,
     NULL},
    {"sdl.EVENT_GAMEPAD_BUTTON_UP", "sdl.EVENT_GAMEPAD_BUTTON_UP() -> i32", "Gamepad button release event.", NULL,
     NULL},
    /* Clipboard, MessageBox, Misc (slice 10) */
    {"sdl.set_clipboard_text", "sdl.set_clipboard_text(text: string) -> (bool, err)", "Copy text to the clipboard.",
     NULL, NULL},
    {"sdl.get_clipboard_text", "sdl.get_clipboard_text() -> string", "Get text from the clipboard.", NULL, NULL},
    {"sdl.has_clipboard_text", "sdl.has_clipboard_text() -> bool", "Check if clipboard has text.", NULL, NULL},
    {"sdl.show_simple_message_box",
     "sdl.show_simple_message_box(flags: i32, title: string, message: string) -> (bool, err)",
     "Show a modal message box.", "Use MESSAGEBOX_* constants for flags.",
     "sdl.show_simple_message_box(sdl.MESSAGEBOX_INFORMATION(), \"Hello\", \"World\")"},
    {"sdl.open_url", "sdl.open_url(url: string) -> (bool, err)", "Open a URL in the default browser.", NULL, NULL},
    {"sdl.get_base_path", "sdl.get_base_path() -> string", "Get the application base directory.", NULL, NULL},
    {"sdl.get_pref_path", "sdl.get_pref_path(org: string, app: string) -> string",
     "Get the user preferences directory.", NULL, NULL},
    {"sdl.get_num_cpu_cores", "sdl.get_num_cpu_cores() -> i32", "Get the number of logical CPU cores.", NULL, NULL},
    {"sdl.get_system_ram", "sdl.get_system_ram() -> i32", "Get system RAM in MB.", NULL, NULL},
    {"sdl.log", "sdl.log(msg: string) -> void", "Log an info message via SDL.", NULL, NULL},
    {"sdl.log_error", "sdl.log_error(msg: string) -> void", "Log an error message via SDL.", NULL, NULL},
    {"sdl.log_warn", "sdl.log_warn(msg: string) -> void", "Log a warning message via SDL.", NULL, NULL},
    {"sdl.MESSAGEBOX_ERROR", "sdl.MESSAGEBOX_ERROR() -> i32", "Error message box flag.", NULL, NULL},
    {"sdl.MESSAGEBOX_WARNING", "sdl.MESSAGEBOX_WARNING() -> i32", "Warning message box flag.", NULL, NULL},
    {"sdl.MESSAGEBOX_INFORMATION", "sdl.MESSAGEBOX_INFORMATION() -> i32", "Info message box flag.", NULL, NULL},
    /* Renderer extras & display info (slice 11) */
    {"sdl.Renderer.set_target", "ren.set_target(tex_handle: i64) -> (bool, err)", "Set a texture as the render target.",
     "Pass -1 to reset to the default target (the window).", "ren.set_target(tex_handle)"},
    {"sdl.Renderer.set_scale", "ren.set_scale(sx: f64, sy: f64) -> (bool, err)", "Set the drawing scale.", NULL, NULL},
    {"sdl.Renderer.get_scale", "ren.get_scale() -> (f64, f64)", "Get the current drawing scale.", NULL, NULL},
    {"sdl.Renderer.set_clip_rect", "ren.set_clip_rect(x: i32, y: i32, w: i32, h: i32) -> (bool, err)",
     "Set the clip rectangle.", "Pass all zeros to clear the clip rect.", NULL},
    {"sdl.Renderer.set_logical_size", "ren.set_logical_size(w: i32, h: i32, mode: i32) -> (bool, err)",
     "Set device-independent resolution.", "Use LOGICAL_* constants for mode.", NULL},
    {"sdl.get_display_count", "sdl.get_display_count() -> i32", "Get the number of connected displays.", NULL, NULL},
    {"sdl.get_display_name", "sdl.get_display_name(index: i32) -> string", "Get display name.", NULL, NULL},
    {"sdl.get_display_bounds", "sdl.get_display_bounds(index: i32) -> (i32, i32)", "Get display size (w, h).",
     "Call get_display_count() first to refresh the list.", NULL},
    {"sdl.LOGICAL_DISABLED", "sdl.LOGICAL_DISABLED() -> i32", "No logical presentation.", NULL, NULL},
    {"sdl.LOGICAL_STRETCH", "sdl.LOGICAL_STRETCH() -> i32", "Stretch to fill.", NULL, NULL},
    {"sdl.LOGICAL_LETTERBOX", "sdl.LOGICAL_LETTERBOX() -> i32", "Letterbox to fit.", NULL, NULL},
    {"sdl.LOGICAL_OVERSCAN", "sdl.LOGICAL_OVERSCAN() -> i32", "Overscan to fill.", NULL, NULL},
    {"sdl.LOGICAL_INTEGER_SCALE", "sdl.LOGICAL_INTEGER_SCALE() -> i32", "Integer scaling.", NULL, NULL},
    /* Renderer drawing extras (slice 12) */
    {"sdl.Renderer.render_debug_text", "ren.render_debug_text(x: f64, y: f64, text: string) -> (bool, err)",
     "Draw debug text.", "Uses SDL's built-in 8x8 font. Set color with set_draw_color first.",
     "ren.render_debug_text(10.0, 10.0, \"FPS: 60\")"},
    {"sdl.Renderer.set_viewport", "ren.set_viewport(x: i32, y: i32, w: i32, h: i32) -> (bool, err)",
     "Set the drawing area.", "Pass all zeros to reset to the full target.", NULL},
    {"sdl.Renderer.set_draw_blend_mode", "ren.set_draw_blend_mode(mode: i32) -> (bool, err)",
     "Set blend mode for draw operations.", "Use BLENDMODE_* constants.", NULL},
    {"sdl.Renderer.get_draw_blend_mode", "ren.get_draw_blend_mode() -> i32", "Get current draw blend mode.", NULL,
     NULL},
    {"sdl.Renderer.set_color_scale", "ren.set_color_scale(scale: f64) -> (bool, err)",
     "Set color scale for render operations.", NULL, NULL},
    {"sdl.Renderer.get_color_scale", "ren.get_color_scale() -> f64", "Get current color scale.", NULL, NULL},
    {"sdl.Renderer.flush", "ren.flush() -> (bool, err)", "Flush pending render commands.", NULL, NULL},
    {"sdl.Renderer.get_output_size", "ren.get_output_size() -> (i32, i32)", "Get renderer output size in pixels.", NULL,
     NULL},
    {"sdl.Renderer.get_current_output_size", "ren.get_current_output_size() -> (i32, i32)",
     "Get current output size in pixels.", NULL, NULL},
    {"sdl.Renderer.get_name", "ren.get_name() -> string", "Get renderer driver name.", NULL, NULL},
    /* Extended window, display, power (slice 13) */
    {"sdl.Window.flash", "win.flash(op: i32) -> (bool, err)", "Flash the window taskbar entry.",
     "Use FLASH_* constants.", NULL},
    {"sdl.Window.set_icon", "win.set_icon(surf: sdl.Surface) -> (bool, err)", "Set the window icon.", NULL, NULL},
    {"sdl.Window.set_opacity", "win.set_opacity(opacity: f64) -> (bool, err)", "Set window opacity (0.0-1.0).", NULL,
     NULL},
    {"sdl.Window.get_opacity", "win.get_opacity() -> f64", "Get window opacity.", NULL, NULL},
    {"sdl.Window.set_min_size", "win.set_min_size(w: i32, h: i32) -> (bool, err)", "Set minimum window size.", NULL,
     NULL},
    {"sdl.Window.get_min_size", "win.get_min_size() -> (i32, i32)", "Get minimum window size.", NULL, NULL},
    {"sdl.Window.set_max_size", "win.set_max_size(w: i32, h: i32) -> (bool, err)", "Set maximum window size.", NULL,
     NULL},
    {"sdl.Window.get_max_size", "win.get_max_size() -> (i32, i32)", "Get maximum window size.", NULL, NULL},
    {"sdl.Window.set_always_on_top", "win.set_always_on_top(on: bool) -> (bool, err)", "Set always-on-top.", NULL,
     NULL},
    {"sdl.Window.set_mouse_grab", "win.set_mouse_grab(grabbed: bool) -> (bool, err)", "Confine mouse to window.", NULL,
     NULL},
    {"sdl.Window.set_keyboard_grab", "win.set_keyboard_grab(grabbed: bool) -> (bool, err)", "Grab keyboard input.",
     NULL, NULL},
    {"sdl.Window.get_size_in_pixels", "win.get_size_in_pixels() -> (i32, i32)", "Get size in pixels (HiDPI-aware).",
     NULL, NULL},
    {"sdl.Window.get_display_scale", "win.get_display_scale() -> f64", "Get display content scale.", NULL, NULL},
    {"sdl.Window.set_relative_mouse", "win.set_relative_mouse(enabled: bool) -> (bool, err)",
     "Enable relative mouse mode (FPS-style).", NULL, NULL},
    {"sdl.get_primary_display", "sdl.get_primary_display() -> i32", "Get primary display index.", NULL, NULL},
    {"sdl.get_display_content_scale", "sdl.get_display_content_scale(index: i32) -> f64", "Get display DPI scale.",
     NULL, NULL},
    {"sdl.get_display_usable_bounds", "sdl.get_display_usable_bounds(index: i32) -> (i32, i32)",
     "Get usable display area (w, h), excluding taskbar.", NULL, NULL},
    {"sdl.get_power_info", "sdl.get_power_info() -> (i32, i32)", "Get battery info (percent, seconds).",
     "Returns -1 for unknown values.", NULL},
    {"sdl.screen_saver_enabled", "sdl.screen_saver_enabled() -> bool", "Check if screen saver is enabled.", NULL, NULL},
    {"sdl.enable_screen_saver", "sdl.enable_screen_saver() -> bool", "Enable the screen saver.", NULL, NULL},
    {"sdl.disable_screen_saver", "sdl.disable_screen_saver() -> bool", "Disable the screen saver.", NULL, NULL},
    {"sdl.FLASH_CANCEL", "sdl.FLASH_CANCEL() -> i32", "Cancel window flash.", NULL, NULL},
    {"sdl.FLASH_BRIEFLY", "sdl.FLASH_BRIEFLY() -> i32", "Flash window briefly.", NULL, NULL},
    {"sdl.FLASH_UNTIL_FOCUSED", "sdl.FLASH_UNTIL_FOCUSED() -> i32", "Flash until focused.", NULL, NULL},
    /* Extended audio (slice 14) */
    {"sdl.get_audio_playback_count", "sdl.get_audio_playback_count() -> i32", "Get number of audio playback devices.",
     NULL, NULL},
    {"sdl.get_audio_device_name", "sdl.get_audio_device_name(index: i32) -> string", "Get audio device name by index.",
     NULL, NULL},
    {"sdl.get_current_audio_driver", "sdl.get_current_audio_driver() -> string", "Get the current audio driver name.",
     NULL, NULL},
    {"sdl.AudioStream.set_gain", "stream.set_gain(gain: f64) -> (bool, err)", "Set audio stream gain.", NULL, NULL},
    {"sdl.AudioStream.get_gain", "stream.get_gain() -> f64", "Get audio stream gain.", NULL, NULL},
    {"sdl.AudioStream.set_freq_ratio", "stream.set_freq_ratio(ratio: f64) -> (bool, err)",
     "Set frequency ratio (pitch).", "1.0 = normal, 2.0 = double speed.", NULL},
    {"sdl.AudioStream.get_freq_ratio", "stream.get_freq_ratio() -> f64", "Get frequency ratio.", NULL, NULL},
    {"sdl.AudioStream.get_available", "stream.get_available() -> i32", "Get available converted bytes.", NULL, NULL},
    {"sdl.AudioStream.flush", "stream.flush() -> (bool, err)", "Flush buffered audio data.", NULL, NULL},
    {"sdl.AudioStream.clear", "stream.clear() -> (bool, err)", "Clear pending audio data.", NULL, NULL},
    /* Cursor (slice 15) */
    {"sdl.create_system_cursor", "sdl.create_system_cursor(id: i32) -> (i64, err)", "Create a system cursor.",
     "Use CURSOR_* constants. Returns cursor handle.", NULL},
    {"sdl.create_color_cursor", "sdl.create_color_cursor(surf: sdl.Surface, hot_x: i32, hot_y: i32) -> (i64, err)",
     "Create a color cursor from a surface.", NULL, NULL},
    {"sdl.set_cursor", "sdl.set_cursor(handle: i64) -> (bool, err)", "Set the active cursor.", NULL, NULL},
    {"sdl.destroy_cursor", "sdl.destroy_cursor(handle: i64) -> void", "Free a cursor.", NULL, NULL},
    {"sdl.capture_mouse", "sdl.capture_mouse(enabled: bool) -> (bool, err)", "Capture mouse input outside the window.",
     NULL, NULL},
    {"sdl.get_relative_mouse_state", "sdl.get_relative_mouse_state() -> (f64, f64)", "Get mouse delta since last call.",
     NULL, NULL},
    {"sdl.warp_mouse_global", "sdl.warp_mouse_global(x: f64, y: f64) -> (bool, err)",
     "Move cursor to global screen position.", NULL, NULL},
    {"sdl.CURSOR_DEFAULT", "sdl.CURSOR_DEFAULT() -> i32", "Default arrow cursor.", NULL, NULL},
    {"sdl.CURSOR_TEXT", "sdl.CURSOR_TEXT() -> i32", "Text I-beam cursor.", NULL, NULL},
    {"sdl.CURSOR_WAIT", "sdl.CURSOR_WAIT() -> i32", "Wait/hourglass cursor.", NULL, NULL},
    {"sdl.CURSOR_CROSSHAIR", "sdl.CURSOR_CROSSHAIR() -> i32", "Crosshair cursor.", NULL, NULL},
    {"sdl.CURSOR_POINTER", "sdl.CURSOR_POINTER() -> i32", "Pointing hand cursor.", NULL, NULL},
    {"sdl.CURSOR_MOVE", "sdl.CURSOR_MOVE() -> i32", "Move/drag cursor.", NULL, NULL},
    {"sdl.CURSOR_NOT_ALLOWED", "sdl.CURSOR_NOT_ALLOWED() -> i32", "Not-allowed cursor.", NULL, NULL},
    /* Events (slice 16) */
    {"sdl.pump_events", "sdl.pump_events() -> void", "Pump the event loop.", NULL, NULL},
    {"sdl.has_event", "sdl.has_event(type: i32) -> bool", "Check if event type is queued.", NULL, NULL},
    {"sdl.flush_event", "sdl.flush_event(type: i32) -> void", "Clear events of a specific type.", NULL, NULL},
    {"sdl.flush_events", "sdl.flush_events(min_type: i32, max_type: i32) -> void", "Clear events in a type range.",
     NULL, NULL},
    {"sdl.event_enabled", "sdl.event_enabled(type: i32) -> bool", "Check if event type is enabled.", NULL, NULL},
    {"sdl.set_event_enabled", "sdl.set_event_enabled(type: i32, enabled: bool) -> void",
     "Enable or disable an event type.", NULL, NULL},
    /* Renderer getters (slice 17) */
    {"sdl.Renderer.get_vsync", "ren.get_vsync() -> i32", "Get VSync mode.", NULL, NULL},
    {"sdl.Renderer.clip_enabled", "ren.clip_enabled() -> bool", "Check if clipping is active.", NULL, NULL},
    {"sdl.Renderer.get_viewport", "ren.get_viewport() -> (i32, i32)", "Get viewport size (w, h).", NULL, NULL},
    /* Texture extras (slice 18) */
    {"sdl.Texture.get_color_mod", "tex.get_color_mod() -> i32", "Get color mod as packed 0xRRGGBB.", NULL, NULL},
    {"sdl.Texture.get_alpha_mod", "tex.get_alpha_mod() -> i32", "Get alpha mod (0-255).", NULL, NULL},
    {"sdl.Texture.get_blend_mode", "tex.get_blend_mode() -> i32", "Get blend mode.", NULL, NULL},
    {"sdl.Texture.set_scale_mode", "tex.set_scale_mode(mode: i32) -> (bool, err)", "Set texture scaling filter.",
     "Use SCALEMODE_* constants.", NULL},
    {"sdl.Texture.get_scale_mode", "tex.get_scale_mode() -> i32", "Get texture scaling filter.", NULL, NULL},
    {"sdl.SCALEMODE_NEAREST", "sdl.SCALEMODE_NEAREST() -> i32", "Nearest-pixel sampling.", NULL, NULL},
    {"sdl.SCALEMODE_LINEAR", "sdl.SCALEMODE_LINEAR() -> i32", "Linear filtering.", NULL, NULL},
    /* System info (slice 19) */
    {"sdl.get_current_time", "sdl.get_current_time() -> i64", "Get system time in nanoseconds since Unix epoch.", NULL,
     NULL},
    {"sdl.get_user_folder", "sdl.get_user_folder(folder: i32) -> string", "Get a user folder path.",
     "Use FOLDER_* constants.", "sdl.get_user_folder(sdl.FOLDER_DOCUMENTS())"},
    {"sdl.get_system_theme", "sdl.get_system_theme() -> i32", "Get system light/dark theme.",
     "Use SYSTEM_THEME_* constants.", NULL},
    {"sdl.is_tablet", "sdl.is_tablet() -> bool", "Check if running on a tablet.", NULL, NULL},
    {"sdl.is_tv", "sdl.is_tv() -> bool", "Check if running on a TV.", NULL, NULL},
    {"sdl.set_app_metadata", "sdl.set_app_metadata(name: string, version: string, id: string) -> (bool, err)",
     "Set application metadata.", NULL, NULL},
    {"sdl.get_current_video_driver", "sdl.get_current_video_driver() -> string", "Get the current video driver name.",
     NULL, NULL},
    {"sdl.SYSTEM_THEME_UNKNOWN", "sdl.SYSTEM_THEME_UNKNOWN() -> i32", "Unknown theme.", NULL, NULL},
    {"sdl.SYSTEM_THEME_LIGHT", "sdl.SYSTEM_THEME_LIGHT() -> i32", "Light theme.", NULL, NULL},
    {"sdl.SYSTEM_THEME_DARK", "sdl.SYSTEM_THEME_DARK() -> i32", "Dark theme.", NULL, NULL},
    {"sdl.FOLDER_HOME", "sdl.FOLDER_HOME() -> i32", "User home folder.", NULL, NULL},
    {"sdl.FOLDER_DOCUMENTS", "sdl.FOLDER_DOCUMENTS() -> i32", "Documents folder.", NULL, NULL},
    {"sdl.FOLDER_DOWNLOADS", "sdl.FOLDER_DOWNLOADS() -> i32", "Downloads folder.", NULL, NULL},
    {"sdl.FOLDER_PICTURES", "sdl.FOLDER_PICTURES() -> i32", "Pictures folder.", NULL, NULL},
    {"sdl.FOLDER_SAVEDGAMES", "sdl.FOLDER_SAVEDGAMES() -> i32", "Saved games folder.", NULL, NULL},
    /* Surface operations (slice 20) */
    {"sdl.Surface.clear", "surf.clear(r: f64, g: f64, b: f64, a: f64) -> (bool, err)",
     "Clear surface with a color (0.0-1.0 per channel).", NULL, NULL},
    {"sdl.Surface.fill_rect", "surf.fill_rect(x: i32, y: i32, w: i32, h: i32, color: i32) -> (bool, err)",
     "Fill a rectangle with a packed pixel color.", NULL, NULL},
    {"sdl.Surface.flip", "surf.flip(mode: i32) -> (bool, err)", "Flip surface. Use FLIP_* constants.", NULL, NULL},
    {"sdl.Surface.set_color_mod", "surf.set_color_mod(r: i32, g: i32, b: i32) -> (bool, err)",
     "Set color modulation for blits.", NULL, NULL},
    {"sdl.Surface.set_alpha_mod", "surf.set_alpha_mod(alpha: i32) -> (bool, err)", "Set alpha modulation for blits.",
     NULL, NULL},
    {"sdl.Surface.set_blend_mode", "surf.set_blend_mode(mode: i32) -> (bool, err)", "Set blend mode for blits.", NULL,
     NULL},
    {"sdl.Surface.set_color_key", "surf.set_color_key(enabled: bool, key: i32) -> (bool, err)",
     "Set transparent color key.", NULL, NULL},
    {"sdl.Surface.blit",
     "surf.blit(dst: sdl.Surface, sx: i32, sy: i32, sw: i32, sh: i32, dx: i32, dy: i32) -> (bool, err)",
     "Blit to another surface.", "Zero src size = full surface.", NULL},
    /* Window getters (slice 21) */
    {"sdl.Window.get_pixel_density", "win.get_pixel_density() -> f64", "Get pixel density (HiDPI).", NULL, NULL},
    {"sdl.Window.get_mouse_grab", "win.get_mouse_grab() -> bool", "Check if mouse is grabbed.", NULL, NULL},
    {"sdl.Window.get_keyboard_grab", "win.get_keyboard_grab() -> bool", "Check if keyboard is grabbed.", NULL, NULL},
    {"sdl.Window.get_relative_mouse", "win.get_relative_mouse() -> bool", "Check relative mouse mode.", NULL, NULL},
    {"sdl.Window.set_progress", "win.set_progress(state: i32, value: f64) -> (bool, err)", "Set taskbar progress bar.",
     "Use PROGRESS_* constants. Value 0.0-1.0.", NULL},
    {"sdl.Window.set_aspect_ratio", "win.set_aspect_ratio(min: f64, max: f64) -> (bool, err)",
     "Constrain window aspect ratio.", NULL, NULL},
    {"sdl.PROGRESS_NONE", "sdl.PROGRESS_NONE() -> i32", "No progress bar.", NULL, NULL},
    {"sdl.PROGRESS_NORMAL", "sdl.PROGRESS_NORMAL() -> i32", "Normal progress bar.", NULL, NULL},
    {"sdl.PROGRESS_INDETERMINATE", "sdl.PROGRESS_INDETERMINATE() -> i32", "Indeterminate progress.", NULL, NULL},
    {"sdl.PROGRESS_PAUSED", "sdl.PROGRESS_PAUSED() -> i32", "Paused progress.", NULL, NULL},
    {"sdl.PROGRESS_ERROR", "sdl.PROGRESS_ERROR() -> i32", "Error progress.", NULL, NULL},
    /* Gamepad extras (slice 22) */
    {"sdl.Gamepad.connected", "gp.connected() -> bool", "Check if gamepad is still connected.", NULL, NULL},
    {"sdl.Gamepad.get_type", "gp.get_type() -> i32", "Get gamepad type.", NULL, NULL},
    {"sdl.Gamepad.get_power_percent", "gp.get_power_percent() -> i32", "Get battery percent (-1 if unknown).", NULL,
     NULL},
    {"sdl.Gamepad.set_led", "gp.set_led(r: i32, g: i32, b: i32) -> (bool, err)", "Set LED color.", NULL, NULL},
    {"sdl.Gamepad.rumble_triggers", "gp.rumble_triggers(left: i32, right: i32, ms: i32) -> (bool, err)",
     "Rumble the triggers.", NULL, NULL},
    {"sdl.Gamepad.has_axis", "gp.has_axis(axis: i32) -> bool", "Check if gamepad has an axis.", NULL, NULL},
    {"sdl.Gamepad.has_button", "gp.has_button(button: i32) -> bool", "Check if gamepad has a button.", NULL, NULL},
    {"sdl.update_gamepads", "sdl.update_gamepads() -> void", "Manually pump gamepad updates.", NULL, NULL},
    /* Advanced surface (slice 23) */
    {"sdl.Surface.create", "sdl.Surface.create(w: i32, h: i32, format: i32) -> (sdl.Surface, err)",
     "Create a blank surface.", NULL, NULL},
    {"sdl.Surface.duplicate", "surf.duplicate() -> (sdl.Surface, err)", "Copy a surface.", NULL, NULL},
    {"sdl.Surface.scale", "surf.scale(w: i32, h: i32, mode: i32) -> (sdl.Surface, err)", "Scale to new size.",
     "Use SCALEMODE_* constants.", NULL},
    {"sdl.Surface.rotate", "surf.rotate(angle: f64) -> (sdl.Surface, err)", "Rotate (degrees).", NULL, NULL},
    {"sdl.Surface.save_bmp", "surf.save_bmp(path: string) -> (bool, err)", "Save as BMP.", NULL, NULL},
    {"sdl.Surface.save_png", "surf.save_png(path: string) -> (bool, err)", "Save as PNG.", NULL, NULL},
    {"sdl.Surface.read_pixel", "surf.read_pixel(x: i32, y: i32) -> i32", "Read pixel as packed 0xAARRGGBB.", NULL,
     NULL},
    {"sdl.Surface.write_pixel", "surf.write_pixel(x: i32, y: i32, r: i32, g: i32, b: i32, a: i32) -> (bool, err)",
     "Write a single pixel.", NULL, NULL},
    /* Keyboard/scancode lookups (slice 24) */
    {"sdl.get_key_from_scancode", "sdl.get_key_from_scancode(scancode: i32) -> i32", "Convert scancode to keycode.",
     NULL, NULL},
    {"sdl.get_scancode_from_key", "sdl.get_scancode_from_key(keycode: i32) -> i32", "Convert keycode to scancode.",
     NULL, NULL},
    {"sdl.get_key_from_name", "sdl.get_key_from_name(name: string) -> i32", "Get keycode from name.", NULL, NULL},
    {"sdl.get_scancode_from_name", "sdl.get_scancode_from_name(name: string) -> i32", "Get scancode from name.", NULL,
     NULL},
    {"sdl.has_keyboard", "sdl.has_keyboard() -> bool", "Check if a keyboard is connected.", NULL, NULL},
    {"sdl.has_mouse", "sdl.has_mouse() -> bool", "Check if a mouse is connected.", NULL, NULL},
    {"sdl.start_text_input", "sdl.start_text_input(win: sdl.Window) -> (bool, err)", "Start text input for a window.",
     NULL, NULL},
    {"sdl.stop_text_input", "sdl.stop_text_input(win: sdl.Window) -> (bool, err)", "Stop text input.", NULL, NULL},
    {"sdl.text_input_active", "sdl.text_input_active(win: sdl.Window) -> bool", "Check if text input is active.", NULL,
     NULL},
    /* Tiled rendering (slice 25) */
    {"sdl.Renderer.render_texture_tiled",
     "ren.render_texture_tiled(tex, sx, sy, sw, sh, scale, dx, dy, dw, dh) -> (bool, err)",
     "Tile a texture across a destination rect.", NULL, NULL},
    /* Display modes (slice 26) */
    {"sdl.get_desktop_display_mode", "sdl.get_desktop_display_mode(index: i32) -> (i32, i32)",
     "Get desktop display mode (w, h).", NULL, NULL},
    {"sdl.get_current_display_mode", "sdl.get_current_display_mode(index: i32) -> (i32, i32)",
     "Get current display mode (w, h).", NULL, NULL},
    {"sdl.get_display_refresh_rate", "sdl.get_display_refresh_rate(index: i32) -> f64",
     "Get display refresh rate in Hz.", NULL, NULL},
    {"sdl.get_display_for_window", "sdl.get_display_for_window(win: sdl.Window) -> i32",
     "Get display index for a window.", NULL, NULL},
    {"sdl.Window.sync", "win.sync() -> (bool, err)", "Block until pending window state is finalized.", NULL, NULL},
    /* Renderer completions (slice 27) */
    {"sdl.Renderer.get_render_target", "ren.get_render_target() -> i64",
     "Get current render target handle (-1 for default).", NULL, NULL},
    {"sdl.Renderer.set_draw_color_float", "ren.set_draw_color_float(r: f64, g: f64, b: f64, a: f64) -> (bool, err)",
     "Set draw color with float precision (0.0-1.0).", NULL, NULL},
    {"sdl.Renderer.get_logical_presentation", "ren.get_logical_presentation() -> (i32, i32)",
     "Get logical presentation size (w, h).", NULL, NULL},
    /* Surface extras (slice 28) */
    {"sdl.Surface.load_png", "sdl.Surface.load_png(path: string) -> (sdl.Surface, err)", "Load a PNG image.", NULL,
     NULL},
    {"sdl.Surface.blit_scaled", "surf.blit_scaled(dst, sx, sy, sw, sh, dx, dy, dw, dh, mode) -> (bool, err)",
     "Scaled blit to another surface.", "Use SCALEMODE_* constants.", NULL},
    {"sdl.Surface.get_color_key", "surf.get_color_key() -> i32", "Get transparent color key.", NULL, NULL},
    /* Joystick (slice 29) */
    {"sdl.has_joystick", "sdl.has_joystick() -> bool", "Check if any joystick is connected.", NULL, NULL},
    {"sdl.get_joystick_count", "sdl.get_joystick_count() -> i32", "Get number of connected joysticks.", NULL, NULL},
    {"sdl.get_joystick_id", "sdl.get_joystick_id(index: i32) -> i32", "Get joystick instance ID.", NULL, NULL},
    {"sdl.is_gamepad_id", "sdl.is_gamepad_id(id: i32) -> bool", "Check if joystick ID is a gamepad.", NULL, NULL},
    {"sdl.Joystick", NULL, "Raw joystick handle.", "For non-gamepad devices. Use sdl.Gamepad for standard controllers.",
     NULL},
    {"sdl.Joystick.open", "sdl.Joystick.open(id: i32) -> (sdl.Joystick, err)", "Open a joystick.", NULL, NULL},
    {"sdl.Joystick.close", "joy.close() -> void", "Close a joystick.", NULL, NULL},
    {"sdl.Joystick.get_name", "joy.get_name() -> string", "Get joystick name.", NULL, NULL},
    {"sdl.Joystick.get_type", "joy.get_type() -> i32", "Get joystick type.", NULL, NULL},
    {"sdl.Joystick.connected", "joy.connected() -> bool", "Check if connected.", NULL, NULL},
    {"sdl.Joystick.num_axes", "joy.num_axes() -> i32", "Get number of axes.", NULL, NULL},
    {"sdl.Joystick.num_buttons", "joy.num_buttons() -> i32", "Get number of buttons.", NULL, NULL},
    {"sdl.Joystick.num_hats", "joy.num_hats() -> i32", "Get number of POV hats.", NULL, NULL},
    {"sdl.Joystick.get_axis", "joy.get_axis(axis: i32) -> i32", "Get axis value (-32768..32767).", NULL, NULL},
    {"sdl.Joystick.get_button", "joy.get_button(btn: i32) -> bool", "Get button state.", NULL, NULL},
    {"sdl.Joystick.get_hat", "joy.get_hat(hat: i32) -> i32", "Get hat position. Use HAT_* constants.", NULL, NULL},
    {"sdl.Joystick.rumble", "joy.rumble(low: i32, high: i32, ms: i32) -> (bool, err)", "Rumble effect.", NULL, NULL},
    {"sdl.HAT_CENTERED", "sdl.HAT_CENTERED() -> i32", "Hat centered.", NULL, NULL},
    {"sdl.HAT_UP", "sdl.HAT_UP() -> i32", "Hat up.", NULL, NULL},
    {"sdl.HAT_RIGHT", "sdl.HAT_RIGHT() -> i32", "Hat right.", NULL, NULL},
    {"sdl.HAT_DOWN", "sdl.HAT_DOWN() -> i32", "Hat down.", NULL, NULL},
    {"sdl.HAT_LEFT", "sdl.HAT_LEFT() -> i32", "Hat left.", NULL, NULL},
    /* Window getters (slice 30) */
    {"sdl.Window.get_aspect_ratio", "win.get_aspect_ratio() -> (f64, f64)", "Get aspect ratio (min, max).", NULL, NULL},
    {"sdl.Window.get_pixel_format", "win.get_pixel_format() -> i32", "Get window pixel format.", NULL, NULL},
    /* Renderer/Surface (slice 31) */
    {"sdl.Renderer.read_pixels", "ren.read_pixels(x: i32, y: i32, w: i32, h: i32) -> (sdl.Surface, err)",
     "Read pixels from render target.", "Pass all zeros for full target.", NULL},
    {"sdl.Surface.convert", "surf.convert(format: i32) -> (sdl.Surface, err)",
     "Convert surface to a different pixel format.", NULL, NULL},
    /* Haptic (slice 32) */
    {"sdl.get_haptic_count", "sdl.get_haptic_count() -> i32", "Get number of haptic devices.", NULL, NULL},
    {"sdl.is_mouse_haptic", "sdl.is_mouse_haptic() -> bool", "Check if mouse has haptic.", NULL, NULL},
    {"sdl.Haptic", NULL, "Haptic (force feedback) device.", "Open with sdl.Haptic.open(index).", NULL},
    {"sdl.Haptic.open", "sdl.Haptic.open(index: i32) -> (sdl.Haptic, err)", "Open haptic device.", NULL, NULL},
    {"sdl.Haptic.close", "hap.close() -> void", "Close haptic device.", NULL, NULL},
    {"sdl.Haptic.get_name", "hap.get_name() -> string", "Get device name.", NULL, NULL},
    {"sdl.Haptic.rumble_supported", "hap.rumble_supported() -> bool", "Check rumble support.", NULL, NULL},
    {"sdl.Haptic.init_rumble", "hap.init_rumble() -> (bool, err)", "Initialize simple rumble.", NULL, NULL},
    {"sdl.Haptic.play_rumble", "hap.play_rumble(strength: f64, ms: i32) -> (bool, err)",
     "Play rumble (0.0-1.0 strength).", NULL, NULL},
    {"sdl.Haptic.stop_rumble", "hap.stop_rumble() -> (bool, err)", "Stop rumble.", NULL, NULL},
    {"sdl.Haptic.pause", "hap.pause() -> (bool, err)", "Pause haptic.", NULL, NULL},
    {"sdl.Haptic.resume", "hap.resume() -> (bool, err)", "Resume haptic.", NULL, NULL},
    /* Filesystem (slice 33) */
    {"sdl.get_current_directory", "sdl.get_current_directory() -> string", "Get current working directory.", NULL,
     NULL},
    {"sdl.create_directory", "sdl.create_directory(path: string) -> (bool, err)", "Create directory.", NULL, NULL},
    {"sdl.remove_path", "sdl.remove_path(path: string) -> (bool, err)", "Remove file or empty directory.", NULL, NULL},
    {"sdl.rename_path", "sdl.rename_path(old: string, new: string) -> (bool, err)", "Rename file/dir.", NULL, NULL},
    {"sdl.copy_file", "sdl.copy_file(src: string, dst: string) -> (bool, err)", "Copy a file.", NULL, NULL},
    {"sdl.get_path_type", "sdl.get_path_type(path: string) -> i32", "Get path type. Use PATHTYPE_* constants.", NULL,
     NULL},
    {"sdl.get_path_size", "sdl.get_path_size(path: string) -> i64", "Get file size in bytes.", NULL, NULL},
    {"sdl.PATHTYPE_NONE", "sdl.PATHTYPE_NONE() -> i32", "Path does not exist.", NULL, NULL},
    {"sdl.PATHTYPE_FILE", "sdl.PATHTYPE_FILE() -> i32", "Regular file.", NULL, NULL},
    {"sdl.PATHTYPE_DIRECTORY", "sdl.PATHTYPE_DIRECTORY() -> i32", "Directory.", NULL, NULL},
    /* Window/Surface extras (slice 34) */
    {"sdl.Window.get_borders_size", "win.get_borders_size() -> (i32, i32)",
     "Get border sizes packed as (top<<16|bottom, left<<16|right).", NULL, NULL},
    {"sdl.Window.get_safe_area", "win.get_safe_area() -> (i32, i32)", "Get safe area (w, h).", NULL, NULL},
    {"sdl.Surface.set_clip_rect", "surf.set_clip_rect(x: i32, y: i32, w: i32, h: i32) -> (bool, err)",
     "Set clipping rectangle. Zeros to clear.", NULL, NULL},
    /* Camera (slice 35) */
    {"sdl.get_camera_count", "sdl.get_camera_count() -> i32", "Get number of cameras.", NULL, NULL},
    {"sdl.get_camera_name", "sdl.get_camera_name(index: i32) -> string", "Get camera name.", NULL, NULL},
    {"sdl.get_current_camera_driver", "sdl.get_current_camera_driver() -> string", "Get camera driver.", NULL, NULL},
    {"sdl.Camera", NULL, "Camera (webcam) device.", "Open with sdl.Camera.open(). Acquire frames in a loop.", NULL},
    {"sdl.Camera.open", "sdl.Camera.open(index: i32, w: i32, h: i32, fps: i32) -> (sdl.Camera, err)", "Open a camera.",
     "Pass 0 for w/h/fps to use defaults.", NULL},
    {"sdl.Camera.close", "cam.close() -> void", "Close camera.", NULL, NULL},
    {"sdl.Camera.get_permission", "cam.get_permission() -> i32",
     "Get permission state (-1 denied, 0 pending, 1 approved).", NULL, NULL},
    {"sdl.Camera.get_format", "cam.get_format() -> (i32, i32)", "Get camera resolution (w, h).", NULL, NULL},
    {"sdl.Camera.acquire_frame", "cam.acquire_frame() -> (i64, err)", "Acquire a frame as surface handle.",
     "Returns -1 with ok err if no frame ready yet. Must release_frame after use.", NULL},
    {"sdl.Camera.release_frame", "cam.release_frame(surface_handle: i64) -> void", "Release an acquired camera frame.",
     NULL, NULL},
    /* Window mouse rect (slice 36) */
    {"sdl.Window.set_mouse_rect", "win.set_mouse_rect(x: i32, y: i32, w: i32, h: i32) -> (bool, err)",
     "Confine cursor to area. Zeros to clear.", NULL, NULL},
    /* Event description (slice 36) */
    {"sdl.Event.get_description", "ev.get_description() -> string", "Get English description of the event.", NULL,
     NULL},
    /* Surface extras (slice 37) */
    {"sdl.Surface.get_clip_rect", "surf.get_clip_rect() -> (i32, i32)", "Get clip rect size (w, h).", NULL, NULL},
    {"sdl.Surface.stretch", "surf.stretch(dst, sx, sy, sw, sh, dx, dy, dw, dh, mode) -> (bool, err)",
     "Stretched pixel copy to another surface.", NULL, NULL},
};

#define SDL_DOC_COUNT (sizeof(sdl_docs) / sizeof(sdl_docs[0]))

/* ── Module List ──────────────────────────────────────────── */

typedef struct native_doc_cache
{
    const vigil_native_module_t *module;
    vigil_doc_entry_t *entries;
    size_t count;
} native_doc_cache_t;

typedef struct native_doc_buf
{
    char *data;
    size_t length;
    size_t capacity;
} native_doc_buf_t;

static native_doc_cache_t native_doc_caches[32];
static size_t native_doc_cache_count = 0U;
static const char *generated_module_names[32];
static size_t generated_module_name_count = 0U;
static int generated_module_names_ready = 0;

static int native_doc_module_has_docs(const vigil_native_module_t *module)
{
    size_t i;

    if (module == NULL)
        return 0;
    if (module->doc != NULL)
        return 1;

    for (i = 0U; i < module->function_count; i++)
    {
        if (module->functions[i].doc != NULL)
            return 1;
    }
    for (i = 0U; i < module->class_count; i++)
    {
        size_t j;
        const vigil_native_class_t *klass = &module->classes[i];
        if (klass->doc != NULL)
            return 1;
        for (j = 0U; j < klass->field_count; j++)
        {
            if (klass->fields[j].doc != NULL)
                return 1;
        }
        for (j = 0U; j < klass->method_count; j++)
        {
            if (klass->methods[j].doc != NULL)
                return 1;
        }
    }
    return 0;
}

static void native_doc_buf_init(native_doc_buf_t *buf)
{
    buf->data = NULL;
    buf->length = 0U;
    buf->capacity = 0U;
}

static int native_doc_buf_reserve(native_doc_buf_t *buf, size_t extra)
{
    size_t needed = buf->length + extra + 1U;
    char *next;

    if (needed <= buf->capacity)
        return 1;
    buf->capacity = buf->capacity == 0U ? 64U : buf->capacity;
    while (buf->capacity < needed)
        buf->capacity *= 2U;
    next = (char *)realloc(buf->data, buf->capacity);
    if (next == NULL)
        return 0;
    buf->data = next;
    return 1;
}

static int native_doc_buf_append_len(native_doc_buf_t *buf, const char *text, size_t length)
{
    if (text == NULL || length == 0U)
        return 1;
    if (!native_doc_buf_reserve(buf, length))
        return 0;
    memcpy(buf->data + buf->length, text, length);
    buf->length += length;
    buf->data[buf->length] = '\0';
    return 1;
}

static int native_doc_buf_append(native_doc_buf_t *buf, const char *text)
{
    if (text == NULL)
        return 1;
    return native_doc_buf_append_len(buf, text, strlen(text));
}

static int native_doc_buf_append_char(native_doc_buf_t *buf, char ch)
{
    return native_doc_buf_append_len(buf, &ch, 1U);
}

static char *native_doc_buf_take(native_doc_buf_t *buf)
{
    char *out = buf->data;
    if (out == NULL)
    {
        out = (char *)malloc(1U);
        if (out != NULL)
            out[0] = '\0';
    }
    buf->data = NULL;
    buf->length = 0U;
    buf->capacity = 0U;
    return out;
}

static char *native_doc_printf(const char *fmt, ...)
{
    va_list args;
    va_list copy;
    int length;
    char *out;

    va_start(args, fmt);
    va_copy(copy, args);
    length = vsnprintf(NULL, 0U, fmt, copy);
    va_end(copy);
    if (length < 0)
    {
        va_end(args);
        return NULL;
    }

    out = (char *)malloc((size_t)length + 1U);
    if (out == NULL)
    {
        va_end(args);
        return NULL;
    }
    vsnprintf(out, (size_t)length + 1U, fmt, args);
    va_end(args);
    return out;
}

static void native_doc_append_qualified_class_name(native_doc_buf_t *buf, const char *module_name,
                                                   const char *class_name)
{
    if (class_name == NULL)
    {
        native_doc_buf_append(buf, "object");
        return;
    }
    if (strchr(class_name, '.') != NULL || module_name == NULL)
    {
        native_doc_buf_append(buf, class_name);
        return;
    }
    native_doc_buf_append(buf, module_name);
    native_doc_buf_append_char(buf, '.');
    native_doc_buf_append(buf, class_name);
}

typedef struct native_doc_type_spec
{
    int kind;
    int object_kind;
    int element_type;
    const char *class_name;
    const vigil_native_type_t *ext;
    const char *override_name;
} native_doc_type_spec_t;

static native_doc_type_spec_t native_doc_type_spec(int kind, int object_kind, int element_type, const char *class_name,
                                                   const vigil_native_type_t *ext, const char *override_name)
{
    native_doc_type_spec_t spec;

    spec.kind = kind;
    spec.object_kind = object_kind;
    spec.element_type = element_type;
    spec.class_name = class_name;
    spec.ext = ext;
    spec.override_name = override_name;
    return spec;
}

static void native_doc_append_type_name(native_doc_buf_t *buf, const char *module_name, native_doc_type_spec_t spec)
{
    if (spec.override_name != NULL)
    {
        native_doc_buf_append(buf, spec.override_name);
        return;
    }

    if (spec.class_name != NULL && spec.class_name[0] != '\0')
    {
        native_doc_append_qualified_class_name(buf, module_name, spec.class_name);
        return;
    }

    if (spec.ext != NULL)
    {
        if (spec.ext->kind != VIGIL_TYPE_OBJECT || spec.ext->object_kind == 0)
        {
            native_doc_buf_append(buf, vigil_type_kind_name((vigil_type_kind_t)spec.ext->kind));
            return;
        }
        if (spec.ext->object_kind == 4)
        {
            native_doc_buf_append(buf, "array<");
            native_doc_append_type_name(buf, module_name,
                                        native_doc_type_spec(spec.ext->element_type, 0, 0, NULL, NULL, NULL));
            native_doc_buf_append_char(buf, '>');
            return;
        }
        if (spec.ext->object_kind == 5)
        {
            native_doc_buf_append(buf, "map<");
            native_doc_append_type_name(buf, module_name,
                                        native_doc_type_spec(spec.ext->key_type, 0, 0, NULL, NULL, NULL));
            native_doc_buf_append(buf, ", ");
            native_doc_append_type_name(buf, module_name,
                                        native_doc_type_spec(spec.ext->value_type, 0, 0, NULL, NULL, NULL));
            native_doc_buf_append_char(buf, '>');
            return;
        }
    }

    if (spec.object_kind == VIGIL_NATIVE_FIELD_ARRAY || (spec.kind == VIGIL_TYPE_OBJECT && spec.element_type != 0))
    {
        native_doc_buf_append(buf, "array<");
        native_doc_append_type_name(buf, module_name, native_doc_type_spec(spec.element_type, 0, 0, NULL, NULL, NULL));
        native_doc_buf_append_char(buf, '>');
        return;
    }

    native_doc_buf_append(buf, vigil_type_kind_name((vigil_type_kind_t)spec.kind));
}

static char *native_doc_build_function_signature(const vigil_native_module_t *module,
                                                 const vigil_native_module_function_t *function)
{
    native_doc_buf_t buf;
    size_t i;

    native_doc_buf_init(&buf);
    native_doc_buf_append(&buf, module->name);
    native_doc_buf_append_char(&buf, '.');
    native_doc_buf_append(&buf, function->name);
    native_doc_buf_append_char(&buf, '(');
    for (i = 0U; i < function->param_count; i++)
    {
        if (i != 0U)
            native_doc_buf_append(&buf, ", ");
        if (function->doc_param_names != NULL && function->doc_param_names[i] != NULL)
        {
            native_doc_buf_append(&buf, function->doc_param_names[i]);
            native_doc_buf_append(&buf, ": ");
        }
        native_doc_append_type_name(
            &buf, module->name,
            native_doc_type_spec(function->param_types != NULL ? function->param_types[i] : VIGIL_TYPE_INVALID, 0, 0,
                                 NULL, function->param_types_ext != NULL ? &function->param_types_ext[i] : NULL,
                                 function->doc_param_type_names != NULL ? function->doc_param_type_names[i] : NULL));
    }
    native_doc_buf_append(&buf, ") -> ");

    if (function->return_count > 1U && function->return_types != NULL)
    {
        native_doc_buf_append_char(&buf, '(');
        for (i = 0U; i < function->return_count; i++)
        {
            if (i != 0U)
                native_doc_buf_append(&buf, ", ");
            native_doc_append_type_name(&buf, module->name,
                                        native_doc_type_spec(function->return_types[i], 0, 0, NULL, NULL, NULL));
        }
        native_doc_buf_append_char(&buf, ')');
    }
    else
    {
        native_doc_append_type_name(&buf, module->name,
                                    native_doc_type_spec(function->return_type, 0, function->return_element_type, NULL,
                                                         function->return_type_ext, function->doc_return_type_name));
    }

    return native_doc_buf_take(&buf);
}

static char *native_doc_build_method_signature(const vigil_native_module_t *module, const vigil_native_class_t *klass,
                                               const vigil_native_class_method_t *method)
{
    native_doc_buf_t buf;
    size_t i;

    native_doc_buf_init(&buf);
    native_doc_buf_append(&buf, module->name);
    native_doc_buf_append_char(&buf, '.');
    native_doc_buf_append(&buf, klass->name);
    native_doc_buf_append_char(&buf, '.');
    native_doc_buf_append(&buf, method->name);
    native_doc_buf_append_char(&buf, '(');
    for (i = 0U; i < method->param_count; i++)
    {
        if (i != 0U)
            native_doc_buf_append(&buf, ", ");
        if (method->doc_param_names != NULL && method->doc_param_names[i] != NULL)
        {
            native_doc_buf_append(&buf, method->doc_param_names[i]);
            native_doc_buf_append(&buf, ": ");
        }
        native_doc_append_type_name(
            &buf, module->name,
            native_doc_type_spec(method->param_types != NULL ? method->param_types[i] : VIGIL_TYPE_INVALID, 0, 0, NULL,
                                 NULL, method->doc_param_type_names != NULL ? method->doc_param_type_names[i] : NULL));
    }
    native_doc_buf_append(&buf, ") -> ");

    if (method->return_count > 1U && method->return_types != NULL)
    {
        native_doc_buf_append_char(&buf, '(');
        for (i = 0U; i < method->return_count; i++)
        {
            if (i != 0U)
                native_doc_buf_append(&buf, ", ");
            native_doc_append_type_name(&buf, module->name,
                                        native_doc_type_spec(method->return_types[i], 0, 0, NULL, NULL, NULL));
        }
        native_doc_buf_append_char(&buf, ')');
    }
    else
    {
        native_doc_append_type_name(&buf, module->name,
                                    native_doc_type_spec(method->return_type, 0, method->return_element_type,
                                                         method->return_class_name, NULL,
                                                         method->doc_return_type_name));
    }

    return native_doc_buf_take(&buf);
}

static char *native_doc_build_field_signature(const vigil_native_module_t *module, const vigil_native_class_t *klass,
                                              const vigil_native_class_field_t *field)
{
    native_doc_buf_t buf;

    native_doc_buf_init(&buf);
    native_doc_buf_append(&buf, module->name);
    native_doc_buf_append_char(&buf, '.');
    native_doc_buf_append(&buf, klass->name);
    native_doc_buf_append_char(&buf, '.');
    native_doc_buf_append(&buf, field->name);
    native_doc_buf_append(&buf, ": ");
    native_doc_append_type_name(&buf, module->name,
                                native_doc_type_spec(field->type, field->object_kind, field->element_type,
                                                     field->class_name, NULL, field->doc_type_name));
    return native_doc_buf_take(&buf);
}

static char *native_doc_build_class_signature(const vigil_native_module_t *module, const vigil_native_class_t *klass)
{
    return native_doc_printf("class %s.%s", module->name, klass->name);
}

static const vigil_native_module_t *native_doc_find_stdlib_module(const char *name)
{
    VIGIL_STDLIB_MODULE_TABLE(mods);
    size_t i;

    if (name == NULL)
        return NULL;

    for (i = 0U; i < sizeof(mods) / sizeof(mods[0]); i++)
    {
        if (mods[i].module != NULL && strcmp(mods[i].name, name) == 0)
            return mods[i].module;
    }
    return NULL;
}

static size_t native_doc_count_class_entries(const vigil_native_class_t *klass)
{
    size_t count = 0U;
    size_t i;

    if (klass->doc != NULL)
        count += 1U;
    for (i = 0U; i < klass->field_count; i++)
    {
        if (klass->fields[i].doc != NULL)
            count += 1U;
    }
    for (i = 0U; i < klass->method_count; i++)
    {
        if (klass->methods[i].doc != NULL)
            count += 1U;
    }
    return count;
}

static size_t native_doc_count_module_entries(const vigil_native_module_t *module)
{
    size_t count = 1U;
    size_t i;

    for (i = 0U; i < module->function_count; i++)
    {
        if (module->functions[i].doc != NULL)
            count += 1U;
    }
    for (i = 0U; i < module->class_count; i++)
        count += native_doc_count_class_entries(&module->classes[i]);
    return count;
}

static void native_doc_fill_class_entries(native_doc_cache_t *cache, size_t *index, const vigil_native_module_t *module,
                                          const vigil_native_class_t *klass)
{
    size_t i;

    if (klass->doc != NULL)
    {
        cache->entries[*index].name = native_doc_printf("%s.%s", module->name, klass->name);
        cache->entries[*index].signature = native_doc_build_class_signature(module, klass);
        cache->entries[*index].summary = klass->doc->summary;
        cache->entries[*index].description = klass->doc->description;
        cache->entries[*index].example = klass->doc->example;
        *index += 1U;
    }

    for (i = 0U; i < klass->field_count; i++)
    {
        const vigil_native_class_field_t *field = &klass->fields[i];
        if (field->doc == NULL)
            continue;
        cache->entries[*index].name = native_doc_printf("%s.%s.%s", module->name, klass->name, field->name);
        cache->entries[*index].signature = native_doc_build_field_signature(module, klass, field);
        cache->entries[*index].summary = field->doc->summary;
        cache->entries[*index].description = field->doc->description;
        cache->entries[*index].example = field->doc->example;
        *index += 1U;
    }

    for (i = 0U; i < klass->method_count; i++)
    {
        const vigil_native_class_method_t *method = &klass->methods[i];
        if (method->doc == NULL)
            continue;
        cache->entries[*index].name = native_doc_printf("%s.%s.%s", module->name, klass->name, method->name);
        cache->entries[*index].signature = native_doc_build_method_signature(module, klass, method);
        cache->entries[*index].summary = method->doc->summary;
        cache->entries[*index].description = method->doc->description;
        cache->entries[*index].example = method->doc->example;
        *index += 1U;
    }
}

static native_doc_cache_t *native_doc_build_module_cache(const vigil_native_module_t *module)
{
    native_doc_cache_t *cache;
    size_t count;
    size_t i;
    size_t index = 0U;

    if (module == NULL || !native_doc_module_has_docs(module) || native_doc_cache_count >= 32U)
        return NULL;

    count = native_doc_count_module_entries(module);

    cache = &native_doc_caches[native_doc_cache_count++];
    memset(cache, 0, sizeof(*cache));
    cache->module = module;
    cache->entries = (vigil_doc_entry_t *)calloc(count, sizeof(vigil_doc_entry_t));
    if (cache->entries == NULL)
    {
        native_doc_cache_count -= 1U;
        return NULL;
    }
    cache->count = count;

    cache->entries[index].name = module->name;
    cache->entries[index].signature = NULL;
    cache->entries[index].summary = module->doc != NULL ? module->doc->summary : NULL;
    cache->entries[index].description = module->doc != NULL ? module->doc->description : NULL;
    cache->entries[index].example = module->doc != NULL ? module->doc->example : NULL;
    index += 1U;

    for (i = 0U; i < module->function_count; i++)
    {
        const vigil_native_module_function_t *function = &module->functions[i];
        if (function->doc == NULL)
            continue;
        cache->entries[index].name = native_doc_printf("%s.%s", module->name, function->name);
        cache->entries[index].signature = native_doc_build_function_signature(module, function);
        cache->entries[index].summary = function->doc->summary;
        cache->entries[index].description = function->doc->description;
        cache->entries[index].example = function->doc->example;
        index += 1U;
    }

    for (i = 0U; i < module->class_count; i++)
        native_doc_fill_class_entries(cache, &index, module, &module->classes[i]);

    cache->count = index;
    return cache;
}

static native_doc_cache_t *native_doc_get_module_cache(const char *module_name)
{
    const vigil_native_module_t *module;
    size_t i;

    if (module_name == NULL)
        return NULL;

    for (i = 0U; i < native_doc_cache_count; i++)
    {
        if (strcmp(native_doc_caches[i].module->name, module_name) == 0)
            return &native_doc_caches[i];
    }

    module = native_doc_find_stdlib_module(module_name);
    if (module == NULL)
        return NULL;
    return native_doc_build_module_cache(module);
}

static const vigil_doc_entry_t *native_doc_lookup_entry(const char *name)
{
    const char *dot;
    native_doc_cache_t *cache;
    size_t i;
    char module_name[128];
    size_t module_length;

    if (name == NULL)
        return NULL;

    dot = strchr(name, '.');
    if (dot == NULL)
        cache = native_doc_get_module_cache(name);
    else
    {
        module_length = (size_t)(dot - name);
        if (module_length >= sizeof(module_name))
            return NULL;
        memcpy(module_name, name, module_length);
        module_name[module_length] = '\0';
        cache = native_doc_get_module_cache(module_name);
    }

    if (cache == NULL)
        return NULL;

    for (i = 0U; i < cache->count; i++)
    {
        if (strcmp(cache->entries[i].name, name) == 0)
            return &cache->entries[i];
    }
    return NULL;
}

static void native_doc_init_module_names(void)
{
    VIGIL_STDLIB_MODULE_TABLE(mods);
    size_t i;

    if (generated_module_names_ready)
        return;

    generated_module_name_count = 0U;
    generated_module_names[generated_module_name_count++] = "builtins";
    for (i = 0U; i < sizeof(mods) / sizeof(mods[0]); i++)
    {
        if (mods[i].module != NULL)
            generated_module_names[generated_module_name_count++] = mods[i].name;
    }
    generated_module_names_ready = 1;
}

/* ── Lookup Implementation ────────────────────────────────── */

typedef struct
{
    const char *name;
    const vigil_doc_entry_t *entries;
    size_t count;
} doc_module_table_entry_t;

static const doc_module_table_entry_t doc_module_table[] = {
    {"builtins", builtin_docs, BUILTIN_COUNT},
    {"math", math_docs, MATH_COUNT},
    {"strings", strings_docs, STRINGS_COUNT},
    {"json", json_docs, JSON_COUNT},
    {"fs", fs_docs, FS_COUNT},
    {"thread", thread_docs, THREAD_COUNT},
    {"compress", compress_docs, COMPRESS_COUNT},
    {"crypto", crypto_docs, CRYPTO_COUNT},
    {"http", http_docs, HTTP_COUNT},
    {"readline", readline_docs, READLINE_COUNT},
    {"net", net_docs, NET_COUNT},
    {"time", time_docs, TIME_COUNT},
    {"ffi", ffi_docs, FFI_COUNT},
    {"unsafe", unsafe_docs, UNSAFE_COUNT},
    {"sdl", sdl_docs, SDL_DOC_COUNT},
};

#define DOC_MODULE_TABLE_COUNT (sizeof(doc_module_table) / sizeof(doc_module_table[0]))

const vigil_doc_entry_t *vigil_doc_lookup(const char *name)
{
    const vigil_doc_entry_t *generated;
    size_t m, i;

    if (name == NULL)
        return NULL;

    generated = native_doc_lookup_entry(name);
    if (generated != NULL)
        return generated;

    for (m = 0; m < DOC_MODULE_TABLE_COUNT; m++)
    {
        for (i = 0; i < doc_module_table[m].count; i++)
        {
            if (strcmp(doc_module_table[m].entries[i].name, name) == 0)
                return &doc_module_table[m].entries[i];
        }
    }
    return NULL;
}

const char **vigil_doc_list_modules(size_t *count)
{
    native_doc_init_module_names();
    if (count != NULL)
        *count = generated_module_name_count;
    return generated_module_names;
}

const vigil_doc_entry_t *vigil_doc_list_module(const char *module_name, size_t *count)
{
    native_doc_cache_t *generated;
    size_t m;
    if (module_name == NULL)
        return NULL;

    generated = native_doc_get_module_cache(module_name);
    if (generated != NULL)
    {
        if (count != NULL)
            *count = generated->count;
        return generated->entries;
    }

    for (m = 0; m < DOC_MODULE_TABLE_COUNT; m++)
    {
        if (strcmp(doc_module_table[m].name, module_name) == 0)
        {
            if (count)
                *count = doc_module_table[m].count;
            return doc_module_table[m].entries;
        }
    }
    return NULL;
}

vigil_status_t vigil_doc_entry_render(const vigil_allocator_t *allocator, const vigil_doc_entry_t *entry,
                                      char **out_text, size_t *out_length, vigil_error_t *error)
{
    char *buf;
    size_t len = 0;
    size_t cap = 1024;
    vigil_allocator_t a;

    if (entry == NULL || out_text == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "doc: invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (allocator != NULL && vigil_allocator_is_valid(allocator))
        a = *allocator;
    else
        a = vigil_default_allocator();

    buf = (char *)a.allocate(a.user_data, cap);
    if (buf == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "out of memory");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    /* Name/signature */
    if (entry->signature != NULL)
    {
        len += (size_t)snprintf(buf + len, cap - len, "%s\n\n", entry->signature);
    }
    else
    {
        len += (size_t)snprintf(buf + len, cap - len, "%s\n\n", entry->name);
    }

    /* Summary */
    if (entry->summary != NULL)
    {
        len += (size_t)snprintf(buf + len, cap - len, "%s\n", entry->summary);
    }

    /* Description */
    if (entry->description != NULL)
    {
        len += (size_t)snprintf(buf + len, cap - len, "\n%s\n", entry->description);
    }

    /* Example */
    if (entry->example != NULL)
    {
        len += (size_t)snprintf(buf + len, cap - len, "\nExample:\n  %s\n", entry->example);
    }

    *out_text = buf;
    if (out_length)
        *out_length = len;
    return VIGIL_STATUS_OK;
}
