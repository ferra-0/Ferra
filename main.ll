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
define i8* @main(i32 %__ferra_argc, i8** %__ferra_argv) {
entry:
  %t0 = icmp sgt i32 %__ferra_argc, 1
  %t1 = sub i32 %__ferra_argc, 1
  %t2 = select i1 %t0, i32 %t1, i32 0
  %t3 = zext i32 %t2 to i64
  store i64 %t3, i64* @_argc
  %t4 = getelementptr inbounds i8*, i8** %__ferra_argv, i64 1
  store i8** %t4, i8*** @_args
  %t5 = getelementptr [6 x i8], [6 x i8]* @fmt_unum, i32 0, i32 0
  call i32 (i8*, ...) @printf(i8* %t5, i64 2)
  ret i8* null
}

