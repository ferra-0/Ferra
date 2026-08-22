%timespec = type { i64, i64 }
declare i32 @printf(i8*, ...)
declare i32 @strcmp(i8*, i8*)
declare i8* @malloc(i64)
declare void @free(i8*)
declare i32 @timespec_get(i8*, i32)
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

@fmt_newline = private constant [2 x i8] c"\0A\00"

@_args = global i8** null
@_argc = global i64 0
@.type_str_str = private constant [4 x i8] c"str\00"
@.type_str_int = private constant [4 x i8] c"int\00"
@.type_str_f64 = private constant [4 x i8] c"f64\00"
@.type_str_bol = private constant [4 x i8] c"bol\00"
@.type_str_arr = private constant [4 x i8] c"arr\00"
@.type_str_obj = private constant [4 x i8] c"obj\00"
@.type_str_fn = private constant [3 x i8] c"fn\00"
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
define i64 @main(i32 %__ferra_argc, i8** %__ferra_argv) {
entry:
  %t0 = icmp sgt i32 %__ferra_argc, 1
  %t1 = sub i32 %__ferra_argc, 1
  %t2 = select i1 %t0, i32 %t1, i32 0
  %t3 = zext i32 %t2 to i64
  store i64 %t3, i64* @_argc
  %t4 = getelementptr inbounds i8*, i8** %__ferra_argv, i64 1
  store i8** %t4, i8*** @_args
 %t_ptr.5 = alloca %timespec
 store %timespec zeroinitializer, %timespec* %t_ptr.5
  %t6 = bitcast %timespec* %t_ptr.5 to i64*
  %t7 = bitcast i64* %t6 to i8*
  %t8 = trunc i64 1 to i32
  %t9 = call i32 @timespec_get(i8* %t7, i32 %t8)
  %t10 = getelementptr inbounds %timespec, %timespec* %t_ptr.5, i32 0, i32 1
  %t11 = load i64, i64* %t10
  %t12 = getelementptr [6 x i8], [6 x i8]* @fmt_num, i32 0, i32 0
  call i32 (i8*, ...) @printf(i8* %t12, i64 %t11)
  %t13 = getelementptr inbounds %timespec, %timespec* %t_ptr.5, i32 0, i32 0
  %t14 = load i64, i64* %t13
  %t15 = getelementptr [6 x i8], [6 x i8]* @fmt_num, i32 0, i32 0
  call i32 (i8*, ...) @printf(i8* %t15, i64 %t14)
 ret i64 0
}

