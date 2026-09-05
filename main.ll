declare i32 @printf(i8*, ...)
declare i32 @strcmp(i8*, i8*)
declare i8* @malloc(i64)
declare void @free(i8*)
@fmt_str = private constant [4 x i8] c"%s\0A\00"
@fmt_ptr = private constant [4 x i8] c"%p\0A\00"
@fmt_num = private constant [6 x i8] c"%lld\0A\00"
@fmt_unum = private constant [6 x i8] c"%llu\0A\00"
@fmt_hex = private constant [8 x i8] c"0x%llX\0A\00"
@fmt_f64 = private constant [5 x i8] c"%lf\0A\00"
@fmt_str_raw = private constant [3 x i8] c"%s\00"
@fmt_ptr_raw = private constant [3 x i8] c"%p\00"
@fmt_num_raw = private constant [5 x i8] c"%lld\00"
@fmt_unum_raw = private constant [5 x i8] c"%llu\00"
@fmt_hex_raw = private constant [7 x i8] c"0x%llX\00"
@fmt_f64_raw = private constant [4 x i8] c"%lf\00"

@.null_str = private constant [7 x i8] c"(null)\00"
@.null_str_raw = private constant [7 x i8] c"(null)\00"
@fmt_newline = private constant [2 x i8] c"\0A\00"

@_args = global i8** null
@_argc = global i64 0
@.str.0 = private constant [7 x i8] c"Hello!\00"
@.str.1 = private constant [5 x i8] c"test\00"
@.str.2 = private constant [5 x i8] c"test\00"
@.str.3 = private constant [7 x i8] c"Print!\00"
@.str.4 = private constant [13 x i8] c"another test\00"
@.str.5 = private constant [13 x i8] c"another test\00"
@.type_str_str = private constant [4 x i8] c"str\00"
@.type_str_int = private constant [4 x i8] c"int\00"
@.type_str_f64 = private constant [4 x i8] c"f64\00"
@.type_str_bol = private constant [4 x i8] c"bol\00"
@.type_str_arr = private constant [4 x i8] c"arr\00"
@.type_str_obj = private constant [4 x i8] c"obj\00"
@.type_str_fn = private constant [5 x i8] c"func\00"
@.type_str_nul = private constant [4 x i8] c"nul\00"
@.type_str_ptr = private constant [4 x i8] c"ptr\00"
@.type_str_i8 = private constant [3 x i8] c"i8\00"
@.type_str_i16 = private constant [4 x i8] c"i16\00"
@.type_str_i32 = private constant [4 x i8] c"i32\00"
@.type_str_i64 = private constant [4 x i8] c"i64\00"
@.type_str_u8 = private constant [3 x i8] c"u8\00"
@.type_str_u16 = private constant [4 x i8] c"u16\00"
@.type_str_u32 = private constant [4 x i8] c"u32\00"
@.type_str_u64 = private constant [4 x i8] c"u64\00"
@.type_str_f32 = private constant [4 x i8] c"f32\00"
@.type_str_isize = private constant [6 x i8] c"isize\00"
@.type_str_usize = private constant [6 x i8] c"usize\00"
@.type_str_hex = private constant [4 x i8] c"hex\00"
@.type_str_tup = private constant [4 x i8] c"tup\00"
define internal i8* @__decorated__print__0() {
entry:
  %t0 = getelementptr [7 x i8], [7 x i8]* @.str.0, i32 0, i32 0
  %t1 = icmp eq i8* %t0, null
  %t2 = getelementptr [7 x i8], [7 x i8]* @.null_str, i32 0, i32 0
  %t3 = select i1 %t1, i8* %t2, i8* %t0
  %t4 = getelementptr [4 x i8], [4 x i8]* @fmt_str, i32 0, i32 0
  call i32 (i8*, ...) @printf(i8* %t4, i8* %t3)
  ret i8* null
}

define internal i8* @print() {
entry:
  %t5 = getelementptr [5 x i8], [5 x i8]* @.str.1, i32 0, i32 0
  %t6 = icmp eq i8* %t5, null
  %t7 = getelementptr [7 x i8], [7 x i8]* @.null_str, i32 0, i32 0
  %t8 = select i1 %t6, i8* %t7, i8* %t5
  %t9 = getelementptr [4 x i8], [4 x i8]* @fmt_str, i32 0, i32 0
  call i32 (i8*, ...) @printf(i8* %t9, i8* %t8)
  %t10 = call i8* @__decorated__print__0()
  %t11 = getelementptr [5 x i8], [5 x i8]* @.str.2, i32 0, i32 0
  %t12 = icmp eq i8* %t11, null
  %t13 = getelementptr [7 x i8], [7 x i8]* @.null_str, i32 0, i32 0
  %t14 = select i1 %t12, i8* %t13, i8* %t11
  %t15 = getelementptr [4 x i8], [4 x i8]* @fmt_str, i32 0, i32 0
  call i32 (i8*, ...) @printf(i8* %t15, i8* %t14)
  ret i8* null
}

define internal i8* @__decorated__hello__1() {
entry:
  %t16 = getelementptr [7 x i8], [7 x i8]* @.str.3, i32 0, i32 0
  %t17 = icmp eq i8* %t16, null
  %t18 = getelementptr [7 x i8], [7 x i8]* @.null_str, i32 0, i32 0
  %t19 = select i1 %t17, i8* %t18, i8* %t16
  %t20 = getelementptr [4 x i8], [4 x i8]* @fmt_str, i32 0, i32 0
  call i32 (i8*, ...) @printf(i8* %t20, i8* %t19)
  ret i8* null
}

define internal i8* @hello() {
entry:
  %t21 = getelementptr [13 x i8], [13 x i8]* @.str.4, i32 0, i32 0
  %t22 = icmp eq i8* %t21, null
  %t23 = getelementptr [7 x i8], [7 x i8]* @.null_str, i32 0, i32 0
  %t24 = select i1 %t22, i8* %t23, i8* %t21
  %t25 = getelementptr [4 x i8], [4 x i8]* @fmt_str, i32 0, i32 0
  call i32 (i8*, ...) @printf(i8* %t25, i8* %t24)
  %t26 = call i8* @__decorated__hello__1()
  %t27 = getelementptr [13 x i8], [13 x i8]* @.str.5, i32 0, i32 0
  %t28 = icmp eq i8* %t27, null
  %t29 = getelementptr [7 x i8], [7 x i8]* @.null_str, i32 0, i32 0
  %t30 = select i1 %t28, i8* %t29, i8* %t27
  %t31 = getelementptr [4 x i8], [4 x i8]* @fmt_str, i32 0, i32 0
  call i32 (i8*, ...) @printf(i8* %t31, i8* %t30)
  ret i8* null
}

define i8* @main(i32 %__ferra_argc, i8** %__ferra_argv) {
entry:
  %t32 = icmp sgt i32 %__ferra_argc, 1
  %t33 = sub i32 %__ferra_argc, 1
  %t34 = select i1 %t32, i32 %t33, i32 0
  %t35 = zext i32 %t34 to i64
  store i64 %t35, i64* @_argc
  %t36 = getelementptr inbounds i8*, i8** %__ferra_argv, i64 1
  store i8** %t36, i8*** @_args
  %t37 = call i8* @print()
  %t38 = call i8* @hello()
  ret i8* null
}

