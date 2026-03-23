#include "vigil/doc_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/vigil_internal.h"

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

/* ── fmt Module Docs ──────────────────────────────────────── */

static const vigil_doc_entry_t fmt_docs[] = {
    {"fmt", NULL, "Formatted output functions.", "The fmt module provides functions for printing to stdout and stderr.",
     NULL},
    {"fmt.print", "fmt.print(value: string) -> void", "Print a string to stdout without a newline.", NULL,
     "fmt.print(\"hello \")\nfmt.print(\"world\")"},
    {"fmt.println", "fmt.println(value: string) -> void", "Print a string to stdout with a newline.", NULL,
     "fmt.println(\"Hello, world!\")"},
    {"fmt.eprintln", "fmt.eprintln(value: string) -> void", "Print a string to stderr with a newline.", NULL,
     "fmt.eprintln(\"Error: something went wrong\")"},
};

#define FMT_COUNT (sizeof(fmt_docs) / sizeof(fmt_docs[0]))

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

/* ── args Module Docs ─────────────────────────────────────── */

static const vigil_doc_entry_t args_docs[] = {
    {"args", NULL, "Command-line argument access.", "The args module provides access to command-line arguments.", NULL},
    {"args.count", "args.count() -> i32", "Return the number of command-line arguments.",
     "Counts the script arguments passed after the program name.", "i32 argc = args.count()"},
    {"args.at", "args.at(index: i32) -> string", "Return a command-line argument by index.",
     "Returns an empty string if the index is out of range.", "string first = args.at(0)"},
    {"args.Parser", "class args.Parser", "Command-line parser builder.",
     "Builds declarative CLI parsers with flags, options, positional arguments, and generated help text.", NULL},
    {"args.Parser.prog", "args.Parser.prog: string", "Program name.",
     "Stores the program name shown in generated usage text.", "string name = parser.prog"},
    {"args.Parser.desc", "args.Parser.desc: string", "Program description.",
     "Stores the description shown in generated help text.", "string desc = parser.desc"},
    {"args.Parser.names", "args.Parser.names: array<string>", "Declared option names.",
     "Holds the long names for declared options.", "array<string> names = parser.names"},
    {"args.Parser.shorts", "args.Parser.shorts: array<string>", "Declared short option names.",
     "Holds the short aliases for declared options.", "array<string> shorts = parser.shorts"},
    {"args.Parser.types", "args.Parser.types: array<string>", "Declared option types.",
     "Stores the internal option type tags for each declared option.", "array<string> types = parser.types"},
    {"args.Parser.defaults", "args.Parser.defaults: array<string>", "Declared default values.",
     "Stores default string values for declared options.", "array<string> defaults = parser.defaults"},
    {"args.Parser.descs", "args.Parser.descs: array<string>", "Declared option descriptions.",
     "Stores help text for declared options.", "array<string> descs = parser.descs"},
    {"args.Parser.required", "args.Parser.required: array<string>", "Required-option markers.",
     "Stores whether each declared option is required.", "array<string> required = parser.required"},
    {"args.Parser.pos_names", "args.Parser.pos_names: array<string>", "Declared positional argument names.",
     "Stores names for declared positional arguments.", "array<string> names = parser.pos_names"},
    {"args.Parser.pos_descs", "args.Parser.pos_descs: array<string>", "Declared positional argument descriptions.",
     "Stores help text for declared positional arguments.", "array<string> descs = parser.pos_descs"},
    {"args.Parser.values", "args.Parser.values: array<string>", "Parsed option values.",
     "Stores parsed option values after a successful parse.", "array<string> values = parser.values"},
    {"args.Parser.positionals", "args.Parser.positionals: array<string>", "Parsed positional values.",
     "Stores parsed positional arguments after a successful parse.", "array<string> values = parser.positionals"},
    {"args.Parser.new", "args.Parser.new(prog: string, desc: string) -> args.Parser", "Create a parser.",
     "Constructs a new parser configured with a program name and description.",
     "args.Parser parser = args.Parser.new(\"vigil\", \"Run scripts\")"},
    {"args.Parser.flag", "args.Parser.flag(name: string, short: string, desc: string) -> args.Parser",
     "Declare a boolean flag.", "Adds a boolean flag option and returns the parser for chaining.",
     "parser.flag(\"verbose\", \"v\", \"Enable verbose output\")"},
    {"args.Parser.option",
     "args.Parser.option(name: string, short: string, default: string, desc: string) -> args.Parser",
     "Declare a string option.", "Adds a string-valued option and returns the parser for chaining.",
     "parser.option(\"output\", \"o\", \"out.txt\", \"Output file\")"},
    {"args.Parser.option_int",
     "args.Parser.option_int(name: string, short: string, default: string, desc: string) -> args.Parser",
     "Declare an integer option.", "Adds an integer-valued option and returns the parser for chaining.",
     "parser.option_int(\"retries\", \"r\", \"3\", \"Retry count\")"},
    {"args.Parser.option_multi", "args.Parser.option_multi(name: string, short: string, desc: string) -> args.Parser",
     "Declare a repeated string option.",
     "Adds an option that can be provided multiple times and returns the parser for chaining.",
     "parser.option_multi(\"include\", \"I\", \"Include path\")"},
    {"args.Parser.mark_required", "args.Parser.mark_required() -> args.Parser",
     "Mark the last declared option as required.",
     "Sets the most recently declared option to required and returns the parser for chaining.",
     "parser.option(\"config\", \"c\", \"\", \"Config file\").mark_required()"},
    {"args.Parser.positional", "args.Parser.positional(name: string, desc: string) -> args.Parser",
     "Declare a positional argument.", "Adds a positional argument and returns the parser for chaining.",
     "parser.positional(\"input\", \"Input file\")"},
    {"args.Parser.parse", "args.Parser.parse(argc: i32) -> error", "Parse command-line arguments.",
     "Parses the process arguments currently available to the runtime. Returns an error value indicating success or "
     "failure.",
     "error err = parser.parse(args.count())"},
    {"args.Parser.get", "args.Parser.get(name: string) -> string", "Get a parsed string option.",
     "Returns the parsed string value for the named option.", "string out = parser.get(\"output\")"},
    {"args.Parser.get_bool", "args.Parser.get_bool(name: string) -> bool", "Get a parsed boolean flag.",
     "Returns true when the named flag was provided.", "bool verbose = parser.get_bool(\"verbose\")"},
    {"args.Parser.get_multi", "args.Parser.get_multi(name: string) -> array<string>", "Get repeated option values.",
     "Returns all parsed values for a repeated option.", "array<string> includes = parser.get_multi(\"include\")"},
    {"args.Parser.get_positionals", "args.Parser.get_positionals() -> array<string>",
     "Get parsed positional arguments.", "Returns the parsed positional arguments in declaration order.",
     "array<string> values = parser.get_positionals()"},
    {"args.Parser.help", "args.Parser.help() -> string", "Render help text.",
     "Builds formatted usage and option help text for the parser.", "string help = parser.help()"},
};

#define ARGS_COUNT (sizeof(args_docs) / sizeof(args_docs[0]))

/* ── test Module Docs ─────────────────────────────────────── */

static const vigil_doc_entry_t test_docs[] = {
    {"test", NULL, "Testing utilities.",
     "The test module provides the test.T assertion helper used by VIGIL test files.", NULL},
    {"test.T", "class test.T", "Test assertion context.", "Supplies assertion helpers to VIGIL test functions.", NULL},
    {"test.T.assert", "test.T.assert(condition: bool, message: string) -> void", "Assert that a condition is true.",
     "Fails the current test with the provided message when the condition is false.",
     "t.assert(1 + 1 == 2, \"math still works\")"},
    {"test.T.fail", "test.T.fail(message: string) -> void", "Fail the current test.",
     "Unconditionally fails the current test with the provided message.", "t.fail(\"expected connection to close\")"},
};

#define TEST_COUNT (sizeof(test_docs) / sizeof(test_docs[0]))

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

static const vigil_doc_entry_t regex_docs[] = {
    {"regex", NULL, "Regular expression matching (RE2-style).",
     "The regex module provides pattern matching with linear time guarantees.\n"
     "Uses Thompson NFA algorithm - no backtracking, no pathological cases.",
     NULL},
    {"regex.match", "regex.match(pattern: string, input: string) -> bool",
     "Check if input matches the pattern (anchored).", "Returns true if the entire input matches the pattern.",
     "regex.match(\"[a-z]+\", \"hello\")  // true\n"
     "regex.match(\"[a-z]+\", \"hello123\")  // false"},
    {"regex.find", "regex.find(pattern: string, input: string) -> (string, bool)",
     "Find first match of pattern in input.", "Returns the matched substring and whether a match was found.",
     "string m, bool ok = regex.find(\"[0-9]+\", \"abc123\")  // \"123\", true"},
    {"regex.find_all", "regex.find_all(pattern: string, input: string) -> array<string>",
     "Find all non-overlapping matches.", "Returns an array of all matched substrings.",
     "regex.find_all(\"[0-9]+\", \"a1b22c333\")  // [\"1\", \"22\", \"333\"]"},
    {"regex.replace", "regex.replace(pattern: string, input: string, replacement: string) -> string",
     "Replace first match with replacement.", "Returns the input with the first match replaced.",
     "regex.replace(\"[0-9]+\", \"a1b2\", \"X\")  // \"aXb2\""},
    {"regex.replace_all", "regex.replace_all(pattern: string, input: string, replacement: string) -> string",
     "Replace all matches with replacement.", "Returns the input with all matches replaced.",
     "regex.replace_all(\"[0-9]+\", \"a1b2\", \"X\")  // \"aXbX\""},
    {"regex.split", "regex.split(pattern: string, input: string) -> array<string>", "Split input by pattern.",
     "Returns an array of substrings split by the pattern.", "regex.split(\",\", \"a,b,c\")  // [\"a\", \"b\", \"c\"]"},
};

#define REGEX_COUNT (sizeof(regex_docs) / sizeof(regex_docs[0]))

/* ── random Module Docs ───────────────────────────────────── */

static const vigil_doc_entry_t random_docs[] = {
    {"random", NULL, "Random number generation.",
     "The random module provides pseudo-random number generation\n"
     "using xorshift128+ algorithm.",
     NULL},
    {"random.seed", "random.seed(n: i32)", "Seed the random number generator.",
     "Sets the seed for reproducible sequences.", "random.seed(42)"},
    {"random.i64", "random.i64() -> i64", "Generate a random 64-bit integer.", "Returns a random i64 value.",
     "i64 n = random.i64()"},
    {"random.i32", "random.i32() -> i32", "Generate a random 32-bit integer.", "Returns a random i32 value.",
     "i32 n = random.i32()"},
    {"random.f64", "random.f64() -> f64", "Generate a random float in [0, 1).",
     "Returns a random f64 value between 0 (inclusive) and 1 (exclusive).", "f64 x = random.f64()  // e.g. 0.7234..."},
    {"random.range", "random.range(min: i32, max: i32) -> i32", "Generate a random integer in [min, max).",
     "Returns a random i32 value between min (inclusive) and max (exclusive).",
     "i32 dice = random.range(1, 7)  // 1-6"},
    {"random.gaussian", "random.gaussian() -> f64",
     "Generate a random value from the standard normal distribution (mean=0, stddev=1).",
     "Uses the Box-Muller transform. Call repeatedly for multiple samples.",
     "f64 g = random.gaussian()  // e.g. -0.42"},
};

#define RANDOM_COUNT (sizeof(random_docs) / sizeof(random_docs[0]))

/* ── url Module Docs ──────────────────────────────────────── */

static const vigil_doc_entry_t url_docs[] = {
    {"url", NULL, "URL parsing and manipulation.",
     "The url module provides functions for parsing and manipulating URLs\n"
     "according to RFC 3986.",
     NULL},
    {"url.parse", "url.parse(url: string) -> string", "Parse a URL into components.",
     "Returns components as pipe-separated string:\n"
     "scheme|user|pass|host|port|path|query|fragment",
     "url.parse(\"https://user:pass@example.com:8080/path?q=1#frag\")"},
    {"url.scheme", "url.scheme(url: string) -> string", "Get the scheme (protocol) from a URL.",
     "Returns the scheme component (e.g. \"https\", \"http\").", "url.scheme(\"https://example.com\")  // \"https\""},
    {"url.host", "url.host(url: string) -> string", "Get the hostname from a URL.",
     "Returns the host component without port.", "url.host(\"https://example.com:8080/path\")  // \"example.com\""},
    {"url.port", "url.port(url: string) -> string", "Get the port from a URL.",
     "Returns the port as a string, or empty if not specified.", "url.port(\"https://example.com:8080\")  // \"8080\""},
    {"url.path", "url.path(url: string) -> string", "Get the path from a URL.", "Returns the decoded path component.",
     "url.path(\"https://example.com/foo/bar\")  // \"/foo/bar\""},
    {"url.query", "url.query(url: string) -> string", "Get the query string from a URL.",
     "Returns the raw query string without the leading '?'.",
     "url.query(\"https://example.com?a=1&b=2\")  // \"a=1&b=2\""},
    {"url.fragment", "url.fragment(url: string) -> string", "Get the fragment from a URL.",
     "Returns the decoded fragment without the leading '#'.",
     "url.fragment(\"https://example.com#section\")  // \"section\""},
    {"url.encode", "url.encode(s: string) -> string", "Percent-encode a string for use in URLs.",
     "Encodes special characters as %XX sequences.", "url.encode(\"hello world\")  // \"hello+world\""},
    {"url.decode", "url.decode(s: string) -> string", "Decode a percent-encoded string.",
     "Decodes %XX sequences and '+' to space.", "url.decode(\"hello%20world\")  // \"hello world\""},
};

#define URL_COUNT (sizeof(url_docs) / sizeof(url_docs[0]))

/* ── yaml module ──────────────────────────────────────────── */

static const vigil_doc_entry_t yaml_docs[] = {
    {"yaml", NULL, "YAML parsing.",
     "The yaml module parses a subset of YAML 1.2 covering most real-world usage:\n"
     "scalars, block mappings, block sequences, comments, and quoted strings.",
     NULL},
    {"yaml.parse", "yaml.parse(yaml: string) -> string", "Parse YAML string to JSON.",
     "Parses a YAML document and returns it as a JSON string.",
     "yaml.parse(\"name: test\\ncount: 42\")  // {\"name\":\"test\",\"count\":42}"},
    {"yaml.get", "yaml.get(yaml: string, path: string) -> string", "Get value at path from YAML.",
     "Parses YAML and returns the value at the given path. "
     "Use dot notation for objects and [n] for arrays.",
     "yaml.get(\"items:\\n  - a\\n  - b\", \"items[1]\")  // \"b\""},
};

#define YAML_COUNT (sizeof(yaml_docs) / sizeof(yaml_docs[0]))

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
     "fs.Reader r, error err = fs.Reader.open(\"data.txt\")"},
    {"fs.Reader.handle", "fs.Reader.handle: i64", "Internal file handle.",
     "Opaque integer handle used by the runtime to track the underlying file. "
     "Not intended for direct use.", "i64 h = r.handle"},
    {"fs.Reader.open", "fs.Reader.open(path: string) -> (fs.Reader, err)", "Open a file for reading.",
     "Opens the file at path for sequential reading. Returns a Reader and an error value.",
     "fs.Reader r, error err = fs.Reader.open(\"input.txt\")"},
    {"fs.Reader.read_line", "fs.Reader.read_line() -> (string, err)", "Read the next line.",
     "Reads up to the next newline (stripped) or end of file. Returns an eof error when no more data.",
     "string line, error err = r.read_line()"},
    {"fs.Reader.read_bytes", "fs.Reader.read_bytes(n: i32) -> (string, err)", "Read up to n bytes.",
     "Reads up to n raw bytes from the file. Returns an eof error when no more data.",
     "string chunk, error err = r.read_bytes(4096)"},
    {"fs.Reader.read_all", "fs.Reader.read_all() -> (string, err)", "Read entire file contents.",
     "Reads the remaining contents of the file into a single string.",
     "string contents, error err = r.read_all()"},
    {"fs.Reader.close", "fs.Reader.close() -> err", "Close the file.",
     "Closes the underlying file handle. Subsequent reads will return an error.",
     "error err = r.close()"},
    /* ── fs.Writer class ── */
    {"fs.Writer", "class fs.Writer", "Buffered file writer.",
     "Provides string and line writing to an open file. "
     "Obtain an instance with fs.Writer.open or fs.Writer.open_append; close with w.close() when done.",
     "fs.Writer w, error err = fs.Writer.open(\"out.txt\")"},
    {"fs.Writer.handle", "fs.Writer.handle: i64", "Internal file handle.",
     "Opaque integer handle used by the runtime to track the underlying file. "
     "Not intended for direct use.", "i64 h = w.handle"},
    {"fs.Writer.open", "fs.Writer.open(path: string) -> (fs.Writer, err)", "Open a file for writing.",
     "Opens the file at path for writing, creating or truncating it. Returns a Writer and an error value.",
     "fs.Writer w, error err = fs.Writer.open(\"output.txt\")"},
    {"fs.Writer.open_append", "fs.Writer.open_append(path: string) -> (fs.Writer, err)",
     "Open a file for appending.",
     "Opens the file at path for appending, creating it if it does not exist. Returns a Writer and an error value.",
     "fs.Writer w, error err = fs.Writer.open_append(\"log.txt\")"},
    {"fs.Writer.write", "fs.Writer.write(s: string) -> (i32, err)", "Write a string.",
     "Writes s to the file. Returns the number of bytes written and an error value.",
     "i32 n, error err = w.write(\"hello\")"},
    {"fs.Writer.write_line", "fs.Writer.write_line(s: string) -> (i32, err)", "Write a string followed by a newline.",
     "Writes s and a trailing newline to the file. Returns the number of bytes written and an error value.",
     "i32 n, error err = w.write_line(\"hello\")"},
    {"fs.Writer.flush", "fs.Writer.flush() -> err", "Flush buffered data.",
     "Flushes any buffered data to the underlying file system.", "error err = w.flush()"},
    {"fs.Writer.close", "fs.Writer.close() -> err", "Close the file.",
     "Flushes and closes the underlying file handle. Subsequent writes will return an error.",
     "error err = w.close()"},
};

#define FS_COUNT (sizeof(fs_docs) / sizeof(fs_docs[0]))

/* ── log module ───────────────────────────────────────────── */

static const vigil_doc_entry_t log_docs[] = {
    {"log", NULL, "Structured logging with levels and formats.",
     "The log module provides structured logging similar to Go's slog package.\n"
     "Supports debug/info/warn/error levels, text/JSON output formats,\n"
     "and custom log handlers for embedders.",
     NULL},
    {"log.debug", "log.debug(msg: string) -> void", "Log at DEBUG level.", "Logs message to configured output.",
     "log.debug(\"starting parser\")"},
    {"log.info", "log.info(msg: string) -> void", "Log at INFO level.", "Logs message to configured output.",
     "log.info(\"request received\")"},
    {"log.warn", "log.warn(msg: string) -> void", "Log at WARN level.", "Logs message to configured output.",
     "log.warn(\"slow query detected\")"},
    {"log.error", "log.error(msg: string) -> void", "Log at ERROR level.", "Logs message to configured output.",
     "log.error(\"connection failed\")"},
    {"log.debug_l", "log.debug_l(logger: i64, msg: string) -> void", "Log at DEBUG with logger.",
     "Uses preset attributes from logger handle.", "log.debug_l(logger, \"msg\")"},
    {"log.info_l", "log.info_l(logger: i64, msg: string) -> void", "Log at INFO with logger.",
     "Uses preset attributes from logger handle.", "log.info_l(logger, \"msg\")"},
    {"log.warn_l", "log.warn_l(logger: i64, msg: string) -> void", "Log at WARN with logger.",
     "Uses preset attributes from logger handle.", "log.warn_l(logger, \"msg\")"},
    {"log.error_l", "log.error_l(logger: i64, msg: string) -> void", "Log at ERROR with logger.",
     "Uses preset attributes from logger handle.", "log.error_l(logger, \"msg\")"},
    {"log.set_level", "log.set_level(level: string) -> void", "Set minimum log level.",
     "Levels: \"debug\", \"info\", \"warn\", \"error\". Default is \"info\".", "log.set_level(\"debug\")"},
    {"log.set_format", "log.set_format(format: string) -> void", "Set output format.",
     "Formats: \"text\" (default), \"json\".", "log.set_format(\"json\")"},
    {"log.set_output", "log.set_output(dest: string) -> void", "Set output destination.",
     "Values: \"stdout\", \"stderr\" (default), or file path.", "log.set_output(\"/var/log/app.log\")"},
    {"log.set_time_format", "log.set_time_format(format: string) -> void", "Set timestamp format.",
     "Formats: \"rfc3339\" (default), \"unix\", \"none\".", "log.set_time_format(\"unix\")"},
    {"log.with", "log.with(key: string, value: string) -> i64", "Create logger with preset attribute.",
     "Returns logger handle for use with _l functions.", "i64 logger = log.with(\"service\", \"api\")"},
};

#define LOG_COUNT (sizeof(log_docs) / sizeof(log_docs[0]))

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

static const vigil_doc_entry_t atomic_docs[] = {
    {"atomic", NULL, "Atomic operations.",
     "The atomic module provides lock-free atomic integer operations\n"
     "for thread-safe programming without mutexes.",
     NULL},
    {"atomic.new", "atomic.new(initial: i64) -> i64", "Create atomic integer handle.",
     "Creates an atomic integer cell with the given initial value and returns its handle.", "i64 a = atomic.new(0)"},
    {"atomic.load", "atomic.load(a: i64) -> i64", "Atomically load value.",
     "Returns the current value stored in the atomic cell handle.", "atomic.load(a)"},
    {"atomic.store", "atomic.store(a: i64, val: i64) -> bool", "Atomically store value.",
     "Sets the atomic cell to the given value.", "atomic.store(a, 42)"},
    {"atomic.add", "atomic.add(a: i64, val: i64) -> i64", "Atomically add.",
     "Adds val to the cell and returns the previous value.", "atomic.add(a, 1)"},
    {"atomic.sub", "atomic.sub(a: i64, val: i64) -> i64", "Atomically subtract.",
     "Subtracts val from the cell and returns the previous value.", "atomic.sub(a, 1)"},
    {"atomic.cas", "atomic.cas(a: i64, expected: i64, desired: i64) -> bool", "Compare and swap.",
     "Sets the cell to desired if the current value equals expected.", "atomic.cas(a, 0, 1)"},
    {"atomic.exchange", "atomic.exchange(a: i64, val: i64) -> i64", "Atomically exchange value.",
     "Stores val in the cell and returns the previous value.", "i64 prev = atomic.exchange(a, 5)"},
    {"atomic.fetch_or", "atomic.fetch_or(a: i64, val: i64) -> i64", "Atomically bitwise-OR value.",
     "Applies bitwise OR with val and returns the previous value.", "i64 prev = atomic.fetch_or(a, 0x10)"},
    {"atomic.fetch_and", "atomic.fetch_and(a: i64, val: i64) -> i64", "Atomically bitwise-AND value.",
     "Applies bitwise AND with val and returns the previous value.", "i64 prev = atomic.fetch_and(a, 0xff)"},
    {"atomic.fetch_xor", "atomic.fetch_xor(a: i64, val: i64) -> i64", "Atomically bitwise-XOR value.",
     "Applies bitwise XOR with val and returns the previous value.", "i64 prev = atomic.fetch_xor(a, 0x01)"},
    {"atomic.inc", "atomic.inc(a: i64) -> i64", "Atomically increment.",
     "Increments the cell by 1 and returns the previous value.", "atomic.inc(a)"},
    {"atomic.dec", "atomic.dec(a: i64) -> i64", "Atomically decrement.",
     "Decrements the cell by 1 and returns the previous value.", "atomic.dec(a)"},
    {"atomic.fence", "atomic.fence() -> void", "Issue a full memory fence.",
     "Forces a full memory barrier for ordering surrounding atomic operations.", "atomic.fence()"},
};

#define ATOMIC_COUNT (sizeof(atomic_docs) / sizeof(atomic_docs[0]))

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

static const vigil_doc_entry_t csv_docs[] = {
    {"csv", NULL, "CSV parsing and generation.",
     "The csv module provides RFC 4180 compliant CSV parsing and generation.\n"
     "Handles quoted fields, escaped quotes, and CRLF line endings.",
     NULL},
    {"csv.parse", "csv.parse(data: string) -> array<array<string>>", "Parse CSV to 2D array.",
     "Parses CSV data into array of rows, each row an array of fields.", "array<array<string>> rows = csv.parse(data)"},
    {"csv.parse_row", "csv.parse_row(line: string) -> array<string>", "Parse single CSV row.",
     "Parses one line of CSV into array of fields.", "array<string> fields = csv.parse_row(line)"},
    {"csv.stringify", "csv.stringify(rows: array<array<string>>) -> string", "Convert 2D array to CSV.",
     "Generates RFC 4180 CSV with CRLF line endings.", "string csv = csv.stringify(rows)"},
    {"csv.stringify_row", "csv.stringify_row(row: array<string>) -> string", "Convert row to CSV line.",
     "Generates single CSV line without trailing newline.", "string line = csv.stringify_row(row)"},
};

#define CSV_COUNT (sizeof(csv_docs) / sizeof(csv_docs[0]))

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

static const vigil_doc_entry_t parse_docs[] = {
    {"parse", NULL, "String-to-value parsing.", "Parse strings into typed values with error handling.", NULL},
    {"parse.i32", "parse.i32(s: string) -> (i32, error)", "Parse string to i32.",
     "Returns (value, error). Error is ok on success.", "i32 v, error err = parse.i32(\"42\")"},
    {"parse.i64", "parse.i64(s: string) -> (i64, error)", "Parse string to i64.",
     "Returns (value, error). Error is ok on success.", "i64 v, error err = parse.i64(\"123456789\")"},
    {"parse.f64", "parse.f64(s: string) -> (f64, error)", "Parse string to f64.",
     "Returns (value, error). Error is ok on success.", "f64 v, error err = parse.f64(\"3.14\")"},
    {"parse.bool", "parse.bool(s: string) -> (bool, error)", "Parse string to bool.",
     "Accepts \"true\" and \"false\". Returns (value, error).", "bool v, error err = parse.bool(\"true\")"},
};

#define PARSE_COUNT (sizeof(parse_docs) / sizeof(parse_docs[0]))

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

/* ── Module List ──────────────────────────────────────────── */

static const char *module_names[] = {
    "builtins", "compress", "crypto", "csv",     "ffi",   "fmt",    "fs",   "http",   "log", "math", "net",    "parse",
    "args",     "readline", "test",   "strings", "regex", "random", "time", "unsafe", "url", "yaml", "thread", "atomic",
};

#define MODULE_COUNT (sizeof(module_names) / sizeof(module_names[0]))

/* ── Lookup Implementation ────────────────────────────────── */

const vigil_doc_entry_t *vigil_doc_lookup(const char *name)
{
    size_t i, len;

    if (name == NULL)
        return NULL;
    len = strlen(name);

    /* Check builtins */
    for (i = 0; i < BUILTIN_COUNT; i++)
    {
        if (strcmp(builtin_docs[i].name, name) == 0)
        {
            return &builtin_docs[i];
        }
    }

    /* Check fmt */
    for (i = 0; i < FMT_COUNT; i++)
    {
        if (strcmp(fmt_docs[i].name, name) == 0)
        {
            return &fmt_docs[i];
        }
    }

    /* Check math */
    for (i = 0; i < MATH_COUNT; i++)
    {
        if (strcmp(math_docs[i].name, name) == 0)
        {
            return &math_docs[i];
        }
    }

    /* Check args */
    for (i = 0; i < ARGS_COUNT; i++)
    {
        if (strcmp(args_docs[i].name, name) == 0)
        {
            return &args_docs[i];
        }
    }

    /* Check test */
    for (i = 0; i < TEST_COUNT; i++)
    {
        if (strcmp(test_docs[i].name, name) == 0)
        {
            return &test_docs[i];
        }
    }

    /* Check strings */
    for (i = 0; i < STRINGS_COUNT; i++)
    {
        if (strcmp(strings_docs[i].name, name) == 0)
        {
            return &strings_docs[i];
        }
    }

    /* Check regex */
    for (i = 0; i < REGEX_COUNT; i++)
    {
        if (strcmp(regex_docs[i].name, name) == 0)
        {
            return &regex_docs[i];
        }
    }

    /* Check random */
    for (i = 0; i < RANDOM_COUNT; i++)
    {
        if (strcmp(random_docs[i].name, name) == 0)
        {
            return &random_docs[i];
        }
    }

    /* Check url */
    for (i = 0; i < URL_COUNT; i++)
    {
        if (strcmp(url_docs[i].name, name) == 0)
        {
            return &url_docs[i];
        }
    }

    /* Check yaml */
    for (i = 0; i < YAML_COUNT; i++)
    {
        if (strcmp(yaml_docs[i].name, name) == 0)
        {
            return &yaml_docs[i];
        }
    }

    /* Check fs */
    for (i = 0; i < FS_COUNT; i++)
    {
        if (strcmp(fs_docs[i].name, name) == 0)
        {
            return &fs_docs[i];
        }
    }

    /* Check log */
    for (i = 0; i < LOG_COUNT; i++)
    {
        if (strcmp(log_docs[i].name, name) == 0)
        {
            return &log_docs[i];
        }
    }

    /* Check thread */
    for (i = 0; i < THREAD_COUNT; i++)
    {
        if (strcmp(thread_docs[i].name, name) == 0)
        {
            return &thread_docs[i];
        }
    }

    /* Check atomic */
    for (i = 0; i < ATOMIC_COUNT; i++)
    {
        if (strcmp(atomic_docs[i].name, name) == 0)
        {
            return &atomic_docs[i];
        }
    }

    /* Check compress */
    for (i = 0; i < COMPRESS_COUNT; i++)
    {
        if (strcmp(compress_docs[i].name, name) == 0)
        {
            return &compress_docs[i];
        }
    }

    /* Check crypto */
    for (i = 0; i < CRYPTO_COUNT; i++)
    {
        if (strcmp(crypto_docs[i].name, name) == 0)
        {
            return &crypto_docs[i];
        }
    }

    /* Check http */
    for (i = 0; i < HTTP_COUNT; i++)
    {
        if (strcmp(http_docs[i].name, name) == 0)
        {
            return &http_docs[i];
        }
    }

    /* Check csv */
    for (i = 0; i < CSV_COUNT; i++)
    {
        if (strcmp(csv_docs[i].name, name) == 0)
        {
            return &csv_docs[i];
        }
    }

    /* Check net */
    for (i = 0; i < NET_COUNT; i++)
    {
        if (strcmp(net_docs[i].name, name) == 0)
        {
            return &net_docs[i];
        }
    }

    /* Check time */
    for (i = 0; i < TIME_COUNT; i++)
    {
        if (strcmp(time_docs[i].name, name) == 0)
        {
            return &time_docs[i];
        }
    }

    /* Check ffi */
    for (i = 0; i < FFI_COUNT; i++)
    {
        if (strcmp(ffi_docs[i].name, name) == 0)
        {
            return &ffi_docs[i];
        }
    }

    /* Check unsafe */
    for (i = 0; i < UNSAFE_COUNT; i++)
    {
        if (strcmp(unsafe_docs[i].name, name) == 0)
        {
            return &unsafe_docs[i];
        }
    }

    /* Check parse */
    for (i = 0; i < PARSE_COUNT; i++)
    {
        if (strcmp(parse_docs[i].name, name) == 0)
        {
            return &parse_docs[i];
        }
    }

    /* Check readline */
    for (i = 0; i < READLINE_COUNT; i++)
    {
        if (strcmp(readline_docs[i].name, name) == 0)
        {
            return &readline_docs[i];
        }
    }

    (void)len;
    return NULL;
}

const char **vigil_doc_list_modules(size_t *count)
{
    if (count != NULL)
    {
        *count = MODULE_COUNT;
    }
    return module_names;
}

const vigil_doc_entry_t *vigil_doc_list_module(const char *module_name, size_t *count)
{
    if (module_name == NULL)
        return NULL;

    if (strcmp(module_name, "builtins") == 0)
    {
        if (count)
            *count = BUILTIN_COUNT;
        return builtin_docs;
    }
    if (strcmp(module_name, "fmt") == 0)
    {
        if (count)
            *count = FMT_COUNT;
        return fmt_docs;
    }
    if (strcmp(module_name, "math") == 0)
    {
        if (count)
            *count = MATH_COUNT;
        return math_docs;
    }
    if (strcmp(module_name, "args") == 0)
    {
        if (count)
            *count = ARGS_COUNT;
        return args_docs;
    }
    if (strcmp(module_name, "test") == 0)
    {
        if (count)
            *count = TEST_COUNT;
        return test_docs;
    }
    if (strcmp(module_name, "strings") == 0)
    {
        if (count)
            *count = STRINGS_COUNT;
        return strings_docs;
    }
    if (strcmp(module_name, "regex") == 0)
    {
        if (count)
            *count = REGEX_COUNT;
        return regex_docs;
    }
    if (strcmp(module_name, "random") == 0)
    {
        if (count)
            *count = RANDOM_COUNT;
        return random_docs;
    }
    if (strcmp(module_name, "url") == 0)
    {
        if (count)
            *count = URL_COUNT;
        return url_docs;
    }
    if (strcmp(module_name, "yaml") == 0)
    {
        if (count)
            *count = YAML_COUNT;
        return yaml_docs;
    }
    if (strcmp(module_name, "fs") == 0)
    {
        if (count)
            *count = FS_COUNT;
        return fs_docs;
    }
    if (strcmp(module_name, "log") == 0)
    {
        if (count)
            *count = LOG_COUNT;
        return log_docs;
    }
    if (strcmp(module_name, "thread") == 0)
    {
        if (count)
            *count = THREAD_COUNT;
        return thread_docs;
    }
    if (strcmp(module_name, "atomic") == 0)
    {
        if (count)
            *count = ATOMIC_COUNT;
        return atomic_docs;
    }
    if (strcmp(module_name, "compress") == 0)
    {
        if (count)
            *count = COMPRESS_COUNT;
        return compress_docs;
    }
    if (strcmp(module_name, "crypto") == 0)
    {
        if (count)
            *count = CRYPTO_COUNT;
        return crypto_docs;
    }
    if (strcmp(module_name, "http") == 0)
    {
        if (count)
            *count = HTTP_COUNT;
        return http_docs;
    }
    if (strcmp(module_name, "csv") == 0)
    {
        if (count)
            *count = CSV_COUNT;
        return csv_docs;
    }
    if (strcmp(module_name, "net") == 0)
    {
        if (count)
            *count = NET_COUNT;
        return net_docs;
    }
    if (strcmp(module_name, "time") == 0)
    {
        if (count)
            *count = TIME_COUNT;
        return time_docs;
    }
    if (strcmp(module_name, "ffi") == 0)
    {
        if (count)
            *count = FFI_COUNT;
        return ffi_docs;
    }
    if (strcmp(module_name, "unsafe") == 0)
    {
        if (count)
            *count = UNSAFE_COUNT;
        return unsafe_docs;
    }
    if (strcmp(module_name, "parse") == 0)
    {
        if (count)
            *count = PARSE_COUNT;
        return parse_docs;
    }
    if (strcmp(module_name, "readline") == 0)
    {
        if (count)
            *count = READLINE_COUNT;
        return readline_docs;
    }

    return NULL;
}

vigil_status_t vigil_doc_entry_render(const vigil_doc_entry_t *entry, char **out_text, size_t *out_length,
                                      vigil_error_t *error)
{
    char *buf;
    size_t len = 0;
    size_t cap = 1024;

    if (entry == NULL || out_text == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "doc: invalid arguments");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    buf = malloc(cap);
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
