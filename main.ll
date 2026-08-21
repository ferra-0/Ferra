%Vec__str = type { i64, i64, i8** }
%Vec__i8 = type { i64, i64, i8* }
%String = type { %Vec__i8 }
declare i32 @printf(i8*, ...)
declare i32 @strcmp(i8*, i8*)
declare i8* @malloc(i64)
declare void @free(i8*)
declare i8* @http_get(i8*)
declare i8* @http_post(i8*, i8*)
declare i8* @http_post_h(i8*, i8*, i8*, i64)
declare void @http_free(i8*)
declare void @http_cleanup()
declare i8* @http_stream_open(i8*, i8*, i8*)
declare i64 @http_stream_read(i8*, i8*, i64)
declare i32 @http_stream_done(i8*)
declare i64 @http_stream_status(i8*)
declare i8* @http_stream_error(i8*)
declare void @http_stream_close(i8*)
declare i64 @strlen(i8*)
declare i8* @memcpy(i8*, i8*, i64)
declare i8* @strdup(i8*)
declare i32 @snprintf(i8*, i64, i8*, ...)
declare i8* @strncpy(i8*, i8*, i64)
declare i8* @strcpy(i8*, i8*)
declare i32 @isspace(i32)
declare i32 @isdigit(i32)
declare i32 @isalpha(i32)
declare i32 @isalnum(i32)
declare float @strtof(i8*, i8*)
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
@.str.0 = private constant [20 x i8] c"https://example.com\00"
@.str.1 = private constant [22 x i8] c"pop from empty vector\00"
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
define internal void @__method__String__String(%String* %__arg_this) {
entry:
  %this_ptr.0 = alloca %String*
  store %String* %__arg_this, %String** %this_ptr.0
  %t1 = load %String*, %String** %this_ptr.0
  %t2 = getelementptr inbounds %String, %String* %t1, i32 0, i32 0
  call void @__method__Vec__Vec__i8(%Vec__i8* %t2)
  %t3 = load %String*, %String** %this_ptr.0
  %t4 = getelementptr inbounds %String, %String* %t3, i32 0, i32 0
  %t5 = getelementptr inbounds %Vec__i8, %Vec__i8* %t4, i32 0, i32 2
  %t6 = load i8*, i8** %t5
  %t7 = getelementptr inbounds i8, i8* %t6, i64 0
 %t8 = trunc i64 0 to i8
 store i8 %t8, i8* %t7
  ret void
}

define internal i64* @__method__String__data(%String* %__arg_this) {
entry:
  %this_ptr.9 = alloca %String*
  store %String* %__arg_this, %String** %this_ptr.9
  %t10 = load %String*, %String** %this_ptr.9
  %t11 = getelementptr inbounds %String, %String* %t10, i32 0, i32 0
  %t12 = getelementptr inbounds %Vec__i8, %Vec__i8* %t11, i32 0, i32 2
  %t13 = load i8*, i8** %t12
  %t14 = bitcast i8* %t13 to i64*
 ret i64* %t14
}

define internal i64 @__method__String__push(%String* %__arg_this, i8 %__arg_c) {
entry:
  %this_ptr.15 = alloca %String*
  store %String* %__arg_this, %String** %this_ptr.15
  %c_ptr.16 = alloca i8
  store i8 %__arg_c, i8* %c_ptr.16
  %t17 = load %String*, %String** %this_ptr.15
  %t18 = getelementptr inbounds %String, %String* %t17, i32 0, i32 0
  %t19 = load %String*, %String** %this_ptr.15
  %t20 = getelementptr inbounds %String, %String* %t19, i32 0, i32 0
  %t21 = getelementptr inbounds %Vec__i8, %Vec__i8* %t20, i32 0, i32 0
  %t22 = load i64, i64* %t21
  %t23 = add i64 %t22, 2
  %t24 = call i1 @__method__Vec__reserve__i8(%Vec__i8* %t18, i64 %t23)
  %t25 = load %String*, %String** %this_ptr.15
  %t26 = getelementptr inbounds %String, %String* %t25, i32 0, i32 0
  %t27 = load i8, i8* %c_ptr.16
  %t28 = call i64 @__method__Vec__push__i8(%Vec__i8* %t26, i8 %t27)
  %t29 = load %String*, %String** %this_ptr.15
  %t30 = getelementptr inbounds %String, %String* %t29, i32 0, i32 0
  %t31 = getelementptr inbounds %Vec__i8, %Vec__i8* %t30, i32 0, i32 2
  %t32 = load i8*, i8** %t31
  %t33 = load %String*, %String** %this_ptr.15
  %t34 = getelementptr inbounds %String, %String* %t33, i32 0, i32 0
  %t35 = getelementptr inbounds %Vec__i8, %Vec__i8* %t34, i32 0, i32 0
  %t36 = load i64, i64* %t35
  %t37 = getelementptr inbounds i8, i8* %t32, i64 %t36
 %t38 = trunc i64 0 to i8
 store i8 %t38, i8* %t37
  ret i64 0
}

define internal i8 @__method__String__pop(%String* %__arg_this) {
entry:
  %this_ptr.39 = alloca %String*
  store %String* %__arg_this, %String** %this_ptr.39
  %t40 = load %String*, %String** %this_ptr.39
  %t41 = getelementptr inbounds %String, %String* %t40, i32 0, i32 0
  %t42 = call i8 @__method__Vec__pop__i8(%Vec__i8* %t41)
 %result_ptr.43 = alloca i8
 store i8 %t42, i8* %result_ptr.43
  %t44 = load %String*, %String** %this_ptr.39
  %t45 = getelementptr inbounds %String, %String* %t44, i32 0, i32 0
  %t46 = getelementptr inbounds %Vec__i8, %Vec__i8* %t45, i32 0, i32 2
  %t47 = load i8*, i8** %t46
  %t48 = load %String*, %String** %this_ptr.39
  %t49 = getelementptr inbounds %String, %String* %t48, i32 0, i32 0
  %t50 = getelementptr inbounds %Vec__i8, %Vec__i8* %t49, i32 0, i32 0
  %t51 = load i64, i64* %t50
  %t52 = getelementptr inbounds i8, i8* %t47, i64 %t51
 %t53 = trunc i64 0 to i8
 store i8 %t53, i8* %t52
  %t54 = load i8, i8* %result_ptr.43
 ret i8 %t54
}

define internal i64 @__method__String__len(%String* %__arg_this) {
entry:
  %this_ptr.55 = alloca %String*
  store %String* %__arg_this, %String** %this_ptr.55
  %t56 = load %String*, %String** %this_ptr.55
  %t57 = getelementptr inbounds %String, %String* %t56, i32 0, i32 0
  %t58 = getelementptr inbounds %Vec__i8, %Vec__i8* %t57, i32 0, i32 0
  %t59 = load i64, i64* %t58
 ret i64 %t59
}

define internal i8* @__method__String__cstr(%String* %__arg_this) {
entry:
  %this_ptr.60 = alloca %String*
  store %String* %__arg_this, %String** %this_ptr.60
  %t61 = load %String*, %String** %this_ptr.60
  %t62 = getelementptr inbounds %String, %String* %t61, i32 0, i32 0
  %t63 = getelementptr inbounds %Vec__i8, %Vec__i8* %t62, i32 0, i32 2
  %t64 = load i8*, i8** %t63
 ret i8* %t64
}

define internal i64 @__method__String__append(%String* %__arg_this, i8* %__arg_t) {
entry:
  %this_ptr.65 = alloca %String*
  store %String* %__arg_this, %String** %this_ptr.65
  %t_ptr.66 = alloca i8*
  store i8* %__arg_t, i8** %t_ptr.66
  %t67 = load i8*, i8** %t_ptr.66
  %t68 = call i64 @strlen(i8* %t67)
 %n_ptr.69 = alloca i64
 store i64 %t68, i64* %n_ptr.69
  %t70 = load %String*, %String** %this_ptr.65
  %t71 = getelementptr inbounds %String, %String* %t70, i32 0, i32 0
  %t72 = getelementptr inbounds %Vec__i8, %Vec__i8* %t71, i32 0, i32 0
  %t73 = load i64, i64* %t72
 %old_size_ptr.74 = alloca i64
 store i64 %t73, i64* %old_size_ptr.74
  %t75 = load %String*, %String** %this_ptr.65
  %t76 = getelementptr inbounds %String, %String* %t75, i32 0, i32 0
  %t77 = load i64, i64* %old_size_ptr.74
  %t78 = load i64, i64* %n_ptr.69
  %t79 = add i64 %t77, %t78
  %t80 = add i64 %t79, 1
  %t81 = call i1 @__method__Vec__reserve__i8(%Vec__i8* %t76, i64 %t80)
  %t82 = load %String*, %String** %this_ptr.65
  %t83 = getelementptr inbounds %String, %String* %t82, i32 0, i32 0
  %t84 = getelementptr inbounds %Vec__i8, %Vec__i8* %t83, i32 0, i32 2
  %t85 = load i8*, i8** %t84
  %t86 = load i64, i64* %old_size_ptr.74
  %t87 = getelementptr inbounds i8, ptr %t85, i64 %t86
  %t88 = load i8*, i8** %t_ptr.66
  %t89 = load i64, i64* %n_ptr.69
  %t90 = call i8* @memcpy(i8* %t87, i8* %t88, i64 %t89)
  %t91 = load %String*, %String** %this_ptr.65
  %t92 = getelementptr inbounds %String, %String* %t91, i32 0, i32 0
  %t93 = getelementptr inbounds %Vec__i8, %Vec__i8* %t92, i32 0, i32 0
  %t94 = load i64, i64* %old_size_ptr.74
  %t95 = load i64, i64* %n_ptr.69
  %t96 = add i64 %t94, %t95
 store i64 %t96, i64* %t93
  %t97 = load %String*, %String** %this_ptr.65
  %t98 = getelementptr inbounds %String, %String* %t97, i32 0, i32 0
  %t99 = getelementptr inbounds %Vec__i8, %Vec__i8* %t98, i32 0, i32 2
  %t100 = load i8*, i8** %t99
  %t101 = load %String*, %String** %this_ptr.65
  %t102 = getelementptr inbounds %String, %String* %t101, i32 0, i32 0
  %t103 = getelementptr inbounds %Vec__i8, %Vec__i8* %t102, i32 0, i32 0
  %t104 = load i64, i64* %t103
  %t105 = getelementptr inbounds i8, i8* %t100, i64 %t104
 %t106 = trunc i64 0 to i8
 store i8 %t106, i8* %t105
  ret i64 0
}

define internal i64 @__method__String__clear(%String* %__arg_this) {
entry:
  %this_ptr.107 = alloca %String*
  store %String* %__arg_this, %String** %this_ptr.107
  %t108 = load %String*, %String** %this_ptr.107
  %t109 = getelementptr inbounds %String, %String* %t108, i32 0, i32 0
  %t110 = call i64 @__method__Vec__clear__i8(%Vec__i8* %t109)
  %t111 = load %String*, %String** %this_ptr.107
  %t112 = getelementptr inbounds %String, %String* %t111, i32 0, i32 0
  %t113 = getelementptr inbounds %Vec__i8, %Vec__i8* %t112, i32 0, i32 2
  %t114 = load i8*, i8** %t113
  %t115 = getelementptr inbounds i8, i8* %t114, i64 0
 %t116 = trunc i64 0 to i8
 store i8 %t116, i8* %t115
  ret i64 0
}

define internal i64 @__method__String__reserve(%String* %__arg_this, i64 %__arg_n) {
entry:
  %this_ptr.117 = alloca %String*
  store %String* %__arg_this, %String** %this_ptr.117
  %n_ptr.118 = alloca i64
  store i64 %__arg_n, i64* %n_ptr.118
  %t119 = load %String*, %String** %this_ptr.117
  %t120 = getelementptr inbounds %String, %String* %t119, i32 0, i32 0
  %t121 = load i64, i64* %n_ptr.118
  %t122 = call i1 @__method__Vec__reserve__i8(%Vec__i8* %t120, i64 %t121)
  ret i64 0
}

define internal i1 @__method__String__empty(%String* %__arg_this) {
entry:
  %this_ptr.123 = alloca %String*
  store %String* %__arg_this, %String** %this_ptr.123
  %t124 = load %String*, %String** %this_ptr.123
  %t125 = getelementptr inbounds %String, %String* %t124, i32 0, i32 0
  %t126 = getelementptr inbounds %Vec__i8, %Vec__i8* %t125, i32 0, i32 0
  %t127 = load i64, i64* %t126
  %t128 = icmp eq i64 %t127, 0
 ret i1 %t128
}

define internal i1 @__method__String__contains(%String* %__arg_this, i8* %__arg_c) {
entry:
  %this_ptr.129 = alloca %String*
  store %String* %__arg_this, %String** %this_ptr.129
  %c_ptr.130 = alloca i8*
  store i8* %__arg_c, i8** %c_ptr.130
  %t131 = load %String*, %String** %this_ptr.129
  %t132 = call i64 @__method__String__len(%String* %t131)
 %t133 = alloca i64
 store i64 0, i64* %t133
 br label %loopcond0
loopcond0:
 %t134 = load i64, i64* %t133
 %t135 = icmp ult i64 %t134, %t132
 br i1 %t135, label %loopbody1, label %loopend3
loopbody1:
  %t136 = load %String*, %String** %this_ptr.129
  %t137 = getelementptr inbounds %String, %String* %t136, i32 0, i32 0
  %t138 = getelementptr inbounds %Vec__i8, %Vec__i8* %t137, i32 0, i32 2
  %t139 = load i8*, i8** %t138
  %t140 = load i64, i64* %t133
  %t141 = getelementptr inbounds i8, i8* %t139, i64 %t140
  %t142 = load i8, i8* %t141
  %t143 = load i8*, i8** %c_ptr.130
  %t144 = getelementptr inbounds i8, i8* %t143, i32 0
  %t145 = load i8, i8* %t144
  %t146 = icmp eq i8 %t142, %t145
 br i1 %t146, label %if_then4, label %if_else5
if_then4:
 ret i1 true
if_else5:
 br label %if_end6
if_end6:
 br label %loopstep2
loopstep2:
 %t147 = load i64, i64* %t133
 %t148 = add i64 %t147, 1
 store i64 %t148, i64* %t133
 br label %loopcond0
loopend3:
 ret i1 false
}

define internal i64 @__method__String__capacity(%String* %__arg_this) {
entry:
  %this_ptr.149 = alloca %String*
  store %String* %__arg_this, %String** %this_ptr.149
  %t150 = load %String*, %String** %this_ptr.149
  %t151 = getelementptr inbounds %String, %String* %t150, i32 0, i32 0
  %t152 = getelementptr inbounds %Vec__i8, %Vec__i8* %t151, i32 0, i32 1
  %t153 = load i64, i64* %t152
 ret i64 %t153
}

define internal i64 @__method__String__set(%String* %__arg_this, i64 %__arg_i, i8 %__arg_c) {
entry:
  %this_ptr.154 = alloca %String*
  store %String* %__arg_this, %String** %this_ptr.154
  %i_ptr.155 = alloca i64
  store i64 %__arg_i, i64* %i_ptr.155
  %c_ptr.156 = alloca i8
  store i8 %__arg_c, i8* %c_ptr.156
  %t157 = load %String*, %String** %this_ptr.154
  %t158 = getelementptr inbounds %String, %String* %t157, i32 0, i32 0
  %t159 = getelementptr inbounds %Vec__i8, %Vec__i8* %t158, i32 0, i32 2
  %t160 = load i8*, i8** %t159
  %t161 = load i64, i64* %i_ptr.155
  %t162 = getelementptr inbounds i8, i8* %t160, i64 %t161
  %t163 = load i8, i8* %c_ptr.156
 store i8 %t163, i8* %t162
  ret i64 0
}

define internal i8 @__method__String__front(%String* %__arg_this) {
entry:
  %this_ptr.164 = alloca %String*
  store %String* %__arg_this, %String** %this_ptr.164
  %t165 = load %String*, %String** %this_ptr.164
  %t166 = getelementptr inbounds %String, %String* %t165, i32 0, i32 0
  %t167 = getelementptr inbounds %Vec__i8, %Vec__i8* %t166, i32 0, i32 2
  %t168 = load i8*, i8** %t167
  %t169 = getelementptr inbounds i8, i8* %t168, i64 0
  %t170 = load i8, i8* %t169
 ret i8 %t170
}

define internal i8 @__method__String__back(%String* %__arg_this) {
entry:
  %this_ptr.171 = alloca %String*
  store %String* %__arg_this, %String** %this_ptr.171
  %t172 = load %String*, %String** %this_ptr.171
  %t173 = getelementptr inbounds %String, %String* %t172, i32 0, i32 0
  %t174 = getelementptr inbounds %Vec__i8, %Vec__i8* %t173, i32 0, i32 2
  %t175 = load i8*, i8** %t174
  %t176 = load %String*, %String** %this_ptr.171
  %t177 = getelementptr inbounds %String, %String* %t176, i32 0, i32 0
  %t178 = getelementptr inbounds %Vec__i8, %Vec__i8* %t177, i32 0, i32 0
  %t179 = load i64, i64* %t178
  %t180 = sub i64 %t179, 1
  %t181 = getelementptr inbounds i8, i8* %t175, i64 %t180
  %t182 = load i8, i8* %t181
 ret i8 %t182
}

define internal i1 @__method__String__starts(%String* %__arg_this, i8* %__arg_c) {
entry:
  %this_ptr.183 = alloca %String*
  store %String* %__arg_this, %String** %this_ptr.183
  %c_ptr.184 = alloca i8*
  store i8* %__arg_c, i8** %c_ptr.184
  %t185 = load i8*, i8** %c_ptr.184
  %t186 = call i64 @strlen(i8* %t185)
  %t187 = load %String*, %String** %this_ptr.183
  %t188 = call i64 @__method__String__len(%String* %t187)
  %t189 = icmp ugt i64 %t186, %t188
 br i1 %t189, label %if_then7, label %if_else8
if_then7:
 ret i1 false
if_else8:
 br label %if_end9
if_end9:
  %t190 = load i8*, i8** %c_ptr.184
  %t191 = call i64 @strlen(i8* %t190)
 %t192 = alloca i64
 store i64 0, i64* %t192
 br label %loopcond10
loopcond10:
 %t193 = load i64, i64* %t192
 %t194 = icmp ult i64 %t193, %t191
 br i1 %t194, label %loopbody11, label %loopend13
loopbody11:
  %t195 = load %String*, %String** %this_ptr.183
  %t196 = getelementptr inbounds %String, %String* %t195, i32 0, i32 0
  %t197 = getelementptr inbounds %Vec__i8, %Vec__i8* %t196, i32 0, i32 2
  %t198 = load i8*, i8** %t197
  %t199 = load i64, i64* %t192
  %t200 = getelementptr inbounds i8, i8* %t198, i64 %t199
  %t201 = load i8, i8* %t200
  %t202 = load i8*, i8** %c_ptr.184
  %t203 = load i64, i64* %t192
  %t204 = getelementptr inbounds i8, i8* %t202, i64 %t203
  %t205 = load i8, i8* %t204
  %t206 = icmp ne i8 %t201, %t205
 br i1 %t206, label %if_then14, label %if_else15
if_then14:
 ret i1 false
if_else15:
 br label %if_end16
if_end16:
 br label %loopstep12
loopstep12:
 %t207 = load i64, i64* %t192
 %t208 = add i64 %t207, 1
 store i64 %t208, i64* %t192
 br label %loopcond10
loopend13:
 ret i1 true
}

define internal i1 @__method__String__ends(%String* %__arg_this, i8* %__arg_c) {
entry:
  %this_ptr.209 = alloca %String*
  store %String* %__arg_this, %String** %this_ptr.209
  %c_ptr.210 = alloca i8*
  store i8* %__arg_c, i8** %c_ptr.210
  %t211 = load %String*, %String** %this_ptr.209
  %t212 = call i64 @__method__String__len(%String* %t211)
  %t213 = sub i64 %t212, 1
 %len_str_ptr.214 = alloca i64
 store i64 %t213, i64* %len_str_ptr.214
  %t215 = load i8*, i8** %c_ptr.210
  %t216 = call i64 @strlen(i8* %t215)
 %len_suffix_ptr.217 = alloca i64
 store i64 %t216, i64* %len_suffix_ptr.217
  %t218 = load i64, i64* %len_suffix_ptr.217
  %t219 = load i64, i64* %len_str_ptr.214
  %t220 = icmp ugt i64 %t218, %t219
 br i1 %t220, label %if_then17, label %if_else18
if_then17:
 ret i1 false
if_else18:
 br label %if_end19
if_end19:
  %t221 = load i64, i64* %len_suffix_ptr.217
 %t222 = alloca i64
 store i64 0, i64* %t222
 br label %loopcond20
loopcond20:
 %t223 = load i64, i64* %t222
 %t224 = icmp ult i64 %t223, %t221
 br i1 %t224, label %loopbody21, label %loopend23
loopbody21:
  %t225 = load %String*, %String** %this_ptr.209
  %t226 = getelementptr inbounds %String, %String* %t225, i32 0, i32 0
  %t227 = getelementptr inbounds %Vec__i8, %Vec__i8* %t226, i32 0, i32 2
  %t228 = load i8*, i8** %t227
  %t229 = load i64, i64* %len_str_ptr.214
  %t230 = sub i64 %t229, 1
  %t231 = load i64, i64* %t222
  %t232 = sub i64 %t230, %t231
  %t233 = getelementptr inbounds i8, i8* %t228, i64 %t232
  %t234 = load i8, i8* %t233
  %t235 = load i8*, i8** %c_ptr.210
  %t236 = load i64, i64* %len_suffix_ptr.217
  %t237 = sub i64 %t236, 1
  %t238 = load i64, i64* %t222
  %t239 = sub i64 %t237, %t238
  %t240 = getelementptr inbounds i8, i8* %t235, i64 %t239
  %t241 = load i8, i8* %t240
  %t242 = icmp ne i8 %t234, %t241
 br i1 %t242, label %if_then24, label %if_else25
if_then24:
 ret i1 false
if_else25:
 br label %if_end26
if_end26:
 br label %loopstep22
loopstep22:
 %t243 = load i64, i64* %t222
 %t244 = add i64 %t243, 1
 store i64 %t244, i64* %t222
 br label %loopcond20
loopend23:
 ret i1 true
}

define internal i1 @__method__String__split(%String* %__arg_this, i8* %__arg_c, %Vec__str* %__arg_out) {
entry:
  %this_ptr.245 = alloca %String*
  store %String* %__arg_this, %String** %this_ptr.245
  %c_ptr.246 = alloca i8*
  store i8* %__arg_c, i8** %c_ptr.246
  %out_ptr.247 = alloca %Vec__str*
  store %Vec__str* %__arg_out, %Vec__str** %out_ptr.247
  %t248 = load %String*, %String** %this_ptr.245
  %t249 = load i8*, i8** %c_ptr.246
  %t250 = call i1 @__method__String__contains(%String* %t248, i8* %t249)
  %t251 = icmp eq i1 %t250, false
 br i1 %t251, label %if_then27, label %if_else28
if_then27:
 ret i1 false
if_else28:
 br label %if_end29
if_end29:
 %cur_ptr.252 = alloca %String
 store %String zeroinitializer, %String* %cur_ptr.252
  call void @__method__String__String(%String* %cur_ptr.252)
 %t253 = alloca i1
 store i1 true, i1* %t253
  %t254 = load %String*, %String** %this_ptr.245
  %t255 = call i64 @__method__String__len(%String* %t254)
 %t256 = alloca i64
 store i64 0, i64* %t256
 br label %loopcond30
loopcond30:
 %t257 = load i64, i64* %t256
 %t258 = icmp ult i64 %t257, %t255
 br i1 %t258, label %loopbody31, label %loopend33
loopbody31:
  %t259 = load %String*, %String** %this_ptr.245
  %t260 = getelementptr inbounds %String, %String* %t259, i32 0, i32 0
  %t261 = getelementptr inbounds %Vec__i8, %Vec__i8* %t260, i32 0, i32 2
  %t262 = load i8*, i8** %t261
  %t263 = load i64, i64* %t256
  %t264 = getelementptr inbounds i8, i8* %t262, i64 %t263
  %t265 = load i8, i8* %t264
  %t266 = load i8*, i8** %c_ptr.246
  %t267 = getelementptr inbounds i8, i8* %t266, i32 0
  %t268 = load i8, i8* %t267
  %t269 = icmp eq i8 %t265, %t268
 br i1 %t269, label %if_then34, label %if_else35
if_then34:
  %t270 = load %Vec__str*, %Vec__str** %out_ptr.247
  %t271 = call i8* @__method__String__cstr(%String* %cur_ptr.252)
  %t272 = call i8* @strdup(i8* %t271)
  %t273 = call i64 @__method__Vec__push__str(%Vec__str* %t270, i8* %t272)
  %t274 = call i64 @__method__String__clear(%String* %cur_ptr.252)
 br label %if_end36
if_else35:
  %t275 = load %String*, %String** %this_ptr.245
  %t276 = getelementptr inbounds %String, %String* %t275, i32 0, i32 0
  %t277 = getelementptr inbounds %Vec__i8, %Vec__i8* %t276, i32 0, i32 2
  %t278 = load i8*, i8** %t277
  %t279 = load i64, i64* %t256
  %t280 = getelementptr inbounds i8, i8* %t278, i64 %t279
  %t281 = load i8, i8* %t280
  %t282 = alloca [2 x i8]
  %t283 = getelementptr inbounds [2 x i8], [2 x i8]* %t282, i32 0, i32 0
  store i8 %t281, i8* %t283
  %t284 = getelementptr inbounds [2 x i8], [2 x i8]* %t282, i32 0, i32 1
  store i8 0, i8* %t284
  %t285 = getelementptr inbounds [2 x i8], [2 x i8]* %t282, i32 0, i32 0
  %t286 = call i64 @__method__String__append(%String* %cur_ptr.252, i8* %t285)
 br label %if_end36
if_end36:
 br label %loopstep32
loopstep32:
 %t287 = load i64, i64* %t256
 %t288 = add i64 %t287, 1
 store i64 %t288, i64* %t256
 br label %loopcond30
loopend33:
  %t289 = load %Vec__str*, %Vec__str** %out_ptr.247
  %t290 = call i8* @__method__String__cstr(%String* %cur_ptr.252)
  %t291 = call i8* @strdup(i8* %t290)
  %t292 = call i64 @__method__Vec__push__str(%Vec__str* %t289, i8* %t291)
 %t293 = load i1, i1* %t253
 br i1 %t293, label %drop_enabled37, label %drop_continue38
drop_enabled37:
 call void @__method__String____drop__(%String* %cur_ptr.252)
 br label %drop_continue38
drop_continue38:
 ret i1 true
}

define internal i8 @__method__String____operator_index(%String* %__arg_this, i64 %__arg_i) {
entry:
  %this_ptr.294 = alloca %String*
  store %String* %__arg_this, %String** %this_ptr.294
  %i_ptr.295 = alloca i64
  store i64 %__arg_i, i64* %i_ptr.295
  %t296 = load %String*, %String** %this_ptr.294
  %t297 = getelementptr inbounds %String, %String* %t296, i32 0, i32 0
  %t298 = load i64, i64* %i_ptr.295
  %t299 = call i8 @__method__Vec____operator_index__i8(%Vec__i8* %t297, i64 %t298)
 ret i8 %t299
}

define internal void @__method__String____drop__(%String* %__arg_this) {
entry:
  %this_ptr.300 = alloca %String*
  store %String* %__arg_this, %String** %this_ptr.300
  %t301 = load %String*, %String** %this_ptr.300
  %t302 = getelementptr inbounds %String, %String* %t301, i32 0, i32 0
  %t303 = call i64 @__method__Vec__unvec__i8(%Vec__i8* %t302)
  ret void
}

define internal i64 @http_get_str(i8* %__arg_u, %String* %__arg_o) {
entry:
  %u_ptr.304 = alloca i8*
  store i8* %__arg_u, i8** %u_ptr.304
  %o_ptr.305 = alloca %String*
  store %String* %__arg_o, %String** %o_ptr.305
  %t306 = load %String*, %String** %o_ptr.305
  %t307 = load i8*, i8** %u_ptr.304
  %t308 = call i8* @http_get(i8* %t307)
  %t309 = call i64 @__method__String__append(%String* %t306, i8* %t308)
  ret i64 0
}

define internal i64 @http_post_str(i8* %__arg_u, i8* %__arg_b, %String* %__arg_o) {
entry:
  %u_ptr.310 = alloca i8*
  store i8* %__arg_u, i8** %u_ptr.310
  %b_ptr.311 = alloca i8*
  store i8* %__arg_b, i8** %b_ptr.311
  %o_ptr.312 = alloca %String*
  store %String* %__arg_o, %String** %o_ptr.312
  %t313 = load %String*, %String** %o_ptr.312
  %t314 = load i8*, i8** %u_ptr.310
  %t315 = load i8*, i8** %b_ptr.311
  %t316 = call i8* @http_post(i8* %t314, i8* %t315)
  %t317 = call i64 @__method__String__append(%String* %t313, i8* %t316)
  ret i64 0
}

define internal i64 @http_post_h_str(i8* %__arg_u, i8* %__arg_b, i8* %__arg_h, i64 %__arg_c, %String* %__arg_o) {
entry:
  %u_ptr.318 = alloca i8*
  store i8* %__arg_u, i8** %u_ptr.318
  %b_ptr.319 = alloca i8*
  store i8* %__arg_b, i8** %b_ptr.319
  %h_ptr.320 = alloca i8*
  store i8* %__arg_h, i8** %h_ptr.320
  %c_ptr.321 = alloca i64
  store i64 %__arg_c, i64* %c_ptr.321
  %o_ptr.322 = alloca %String*
  store %String* %__arg_o, %String** %o_ptr.322
  %t323 = load %String*, %String** %o_ptr.322
  %t324 = load i8*, i8** %u_ptr.318
  %t325 = load i8*, i8** %b_ptr.319
  %t326 = load i8*, i8** %h_ptr.320
  %t327 = load i64, i64* %c_ptr.321
  %t328 = call i8* @http_post_h(i8* %t324, i8* %t325, i8* %t326, i64 %t327)
  %t329 = call i64 @__method__String__append(%String* %t323, i8* %t328)
  ret i64 0
}

define i64 @main(i32 %__ferra_argc, i8** %__ferra_argv) {
entry:
  %t330 = icmp sgt i32 %__ferra_argc, 1
  %t331 = sub i32 %__ferra_argc, 1
  %t332 = select i1 %t330, i32 %t331, i32 0
  %t333 = zext i32 %t332 to i64
  store i64 %t333, i64* @_argc
  %t334 = getelementptr inbounds i8*, i8** %__ferra_argv, i64 1
  store i8** %t334, i8*** @_args
  %t335 = getelementptr [20 x i8], [20 x i8]* @.str.0, i32 0, i32 0
  %t336 = call i8* @http_get(i8* %t335)
  %t337 = getelementptr [4 x i8], [4 x i8]* @fmt_str, i32 0, i32 0
  call i32 (i8*, ...) @printf(i8* %t337, i8* %t336)
 ret i64 0
}

define internal i64 @__method__Vec__push__i8(%Vec__i8* %__arg_this, i8 %__arg_i) {
entry:
  %this_ptr.338 = alloca %Vec__i8*
  store %Vec__i8* %__arg_this, %Vec__i8** %this_ptr.338
  %i_ptr.339 = alloca i8
  store i8 %__arg_i, i8* %i_ptr.339
  %t340 = load %Vec__i8*, %Vec__i8** %this_ptr.338
  %t341 = getelementptr inbounds %Vec__i8, %Vec__i8* %t340, i32 0, i32 0
  %t342 = load i64, i64* %t341
  %t343 = load %Vec__i8*, %Vec__i8** %this_ptr.338
  %t344 = getelementptr inbounds %Vec__i8, %Vec__i8* %t343, i32 0, i32 1
  %t345 = load i64, i64* %t344
  %t346 = icmp eq i64 %t342, %t345
 br i1 %t346, label %if_then39, label %if_else40
if_then39:
  %t347 = load %Vec__i8*, %Vec__i8** %this_ptr.338
  %t348 = load %Vec__i8*, %Vec__i8** %this_ptr.338
  %t349 = getelementptr inbounds %Vec__i8, %Vec__i8* %t348, i32 0, i32 1
  %t350 = load i64, i64* %t349
  %t351 = mul i64 %t350, 2
  %t352 = call i1 @__method__Vec__reserve__i8(%Vec__i8* %t347, i64 %t351)
 br label %if_end41
if_else40:
 br label %if_end41
if_end41:
  %t353 = load %Vec__i8*, %Vec__i8** %this_ptr.338
  %t354 = getelementptr inbounds %Vec__i8, %Vec__i8* %t353, i32 0, i32 2
  %t355 = load i8*, i8** %t354
  %t356 = load %Vec__i8*, %Vec__i8** %this_ptr.338
  %t357 = getelementptr inbounds %Vec__i8, %Vec__i8* %t356, i32 0, i32 0
  %t358 = load i64, i64* %t357
  %t359 = getelementptr inbounds i8, i8* %t355, i64 %t358
  %t360 = load i8, i8* %i_ptr.339
 store i8 %t360, i8* %t359
  %t361 = load %Vec__i8*, %Vec__i8** %this_ptr.338
  %t362 = getelementptr inbounds %Vec__i8, %Vec__i8* %t361, i32 0, i32 0
  %t363 = load %Vec__i8*, %Vec__i8** %this_ptr.338
  %t364 = getelementptr inbounds %Vec__i8, %Vec__i8* %t363, i32 0, i32 0
  %t365 = load i64, i64* %t364
  %t366 = add i64 %t365, 1
 store i64 %t366, i64* %t362
  ret i64 0
}

define internal i64 @__method__Vec__unvec__i8(%Vec__i8* %__arg_this) {
entry:
  %this_ptr.367 = alloca %Vec__i8*
  store %Vec__i8* %__arg_this, %Vec__i8** %this_ptr.367
  %t368 = load %Vec__i8*, %Vec__i8** %this_ptr.367
  %t369 = getelementptr inbounds %Vec__i8, %Vec__i8* %t368, i32 0, i32 2
  %t370 = load i8*, i8** %t369
  call void @free(i8* %t370)
  ret i64 0
}

define internal i1 @__method__Vec__reserve__i8(%Vec__i8* %__arg_this, i64 %__arg_n) {
entry:
  %this_ptr.371 = alloca %Vec__i8*
  store %Vec__i8* %__arg_this, %Vec__i8** %this_ptr.371
  %n_ptr.372 = alloca i64
  store i64 %__arg_n, i64* %n_ptr.372
  %t373 = load i64, i64* %n_ptr.372
  %t374 = load %Vec__i8*, %Vec__i8** %this_ptr.371
  %t375 = getelementptr inbounds %Vec__i8, %Vec__i8* %t374, i32 0, i32 1
  %t376 = load i64, i64* %t375
  %t377 = icmp slt i64 %t373, %t376
  br i1 %t377, label %logic_short43, label %logic_rhs42
logic_rhs42:
  %t378 = load i64, i64* %n_ptr.372
  %t379 = load %Vec__i8*, %Vec__i8** %this_ptr.371
  %t380 = getelementptr inbounds %Vec__i8, %Vec__i8* %t379, i32 0, i32 1
  %t381 = load i64, i64* %t380
  %t382 = icmp eq i64 %t378, %t381
  br label %logic_end44
logic_short43:
  br label %logic_end44
logic_end44:
  %t383 = phi i1 [ %t382, %logic_rhs42 ], [ true, %logic_short43 ]
 br i1 %t383, label %if_then45, label %if_else46
if_then45:
 ret i1 false
if_else46:
 br label %if_end47
if_end47:
  %t384 = load i64, i64* %n_ptr.372
 %r_ptr.385 = alloca i8*
 %t386 = mul i64 %t384, 1
 %t387 = call i8* @malloc(i64 %t386)
 %t388 = bitcast i8* %t387 to i8*
 store i8* %t388, i8** %r_ptr.385
  %t389 = load %Vec__i8*, %Vec__i8** %this_ptr.371
  %t390 = getelementptr inbounds %Vec__i8, %Vec__i8* %t389, i32 0, i32 0
  %t391 = load i64, i64* %t390
 %t392 = alloca i64
 store i64 0, i64* %t392
 br label %loopcond48
loopcond48:
 %t393 = load i64, i64* %t392
 %t394 = icmp ult i64 %t393, %t391
 br i1 %t394, label %loopbody49, label %loopend51
loopbody49:
  %t395 = load i8*, i8** %r_ptr.385
  %t396 = load i64, i64* %t392
  %t397 = getelementptr inbounds i8, i8* %t395, i64 %t396
  %t398 = load %Vec__i8*, %Vec__i8** %this_ptr.371
  %t399 = getelementptr inbounds %Vec__i8, %Vec__i8* %t398, i32 0, i32 2
  %t400 = load i8*, i8** %t399
  %t401 = load i64, i64* %t392
  %t402 = getelementptr inbounds i8, i8* %t400, i64 %t401
  %t403 = load i8, i8* %t402
 store i8 %t403, i8* %t397
 br label %loopstep50
loopstep50:
 %t404 = load i64, i64* %t392
 %t405 = add i64 %t404, 1
 store i64 %t405, i64* %t392
 br label %loopcond48
loopend51:
  %t406 = load %Vec__i8*, %Vec__i8** %this_ptr.371
  %t407 = getelementptr inbounds %Vec__i8, %Vec__i8* %t406, i32 0, i32 2
  %t408 = load i8*, i8** %t407
  call void @free(i8* %t408)
  %t409 = load %Vec__i8*, %Vec__i8** %this_ptr.371
  %t410 = getelementptr inbounds %Vec__i8, %Vec__i8* %t409, i32 0, i32 2
  %t411 = load i8*, i8** %r_ptr.385
 store i8* %t411, i8** %t410
  %t412 = load %Vec__i8*, %Vec__i8** %this_ptr.371
  %t413 = getelementptr inbounds %Vec__i8, %Vec__i8* %t412, i32 0, i32 1
  %t414 = load i64, i64* %n_ptr.372
 store i64 %t414, i64* %t413
 ret i1 true
}

define internal i64 @__method__Vec__clear__i8(%Vec__i8* %__arg_this) {
entry:
  %this_ptr.415 = alloca %Vec__i8*
  store %Vec__i8* %__arg_this, %Vec__i8** %this_ptr.415
  %t416 = load %Vec__i8*, %Vec__i8** %this_ptr.415
  %t417 = getelementptr inbounds %Vec__i8, %Vec__i8* %t416, i32 0, i32 0
 store i64 0, i64* %t417
  ret i64 0
}

define internal void @__method__Vec__Vec__i8(%Vec__i8* %__arg_this) {
entry:
  %this_ptr.418 = alloca %Vec__i8*
  store %Vec__i8* %__arg_this, %Vec__i8** %this_ptr.418
  %t419 = load %Vec__i8*, %Vec__i8** %this_ptr.418
  %t420 = getelementptr inbounds %Vec__i8, %Vec__i8* %t419, i32 0, i32 0
 store i64 0, i64* %t420
  %t421 = load %Vec__i8*, %Vec__i8** %this_ptr.418
  %t422 = getelementptr inbounds %Vec__i8, %Vec__i8* %t421, i32 0, i32 1
 store i64 8, i64* %t422
  %t423 = load %Vec__i8*, %Vec__i8** %this_ptr.418
  %t424 = getelementptr inbounds %Vec__i8, %Vec__i8* %t423, i32 0, i32 1
  %t425 = load i64, i64* %t424
 %r_ptr.426 = alloca i8*
 %t427 = mul i64 %t425, 1
 %t428 = call i8* @malloc(i64 %t427)
 %t429 = bitcast i8* %t428 to i8*
 store i8* %t429, i8** %r_ptr.426
  %t430 = load %Vec__i8*, %Vec__i8** %this_ptr.418
  %t431 = getelementptr inbounds %Vec__i8, %Vec__i8* %t430, i32 0, i32 2
  %t432 = load i8*, i8** %r_ptr.426
 store i8* %t432, i8** %t431
  ret void
}

define internal i64 @__method__Vec__push__str(%Vec__str* %__arg_this, i8* %__arg_i) {
entry:
  %this_ptr.433 = alloca %Vec__str*
  store %Vec__str* %__arg_this, %Vec__str** %this_ptr.433
  %i_ptr.434 = alloca i8*
  store i8* %__arg_i, i8** %i_ptr.434
  %t435 = load %Vec__str*, %Vec__str** %this_ptr.433
  %t436 = getelementptr inbounds %Vec__str, %Vec__str* %t435, i32 0, i32 0
  %t437 = load i64, i64* %t436
  %t438 = load %Vec__str*, %Vec__str** %this_ptr.433
  %t439 = getelementptr inbounds %Vec__str, %Vec__str* %t438, i32 0, i32 1
  %t440 = load i64, i64* %t439
  %t441 = icmp eq i64 %t437, %t440
 br i1 %t441, label %if_then52, label %if_else53
if_then52:
  %t442 = load %Vec__str*, %Vec__str** %this_ptr.433
  %t443 = load %Vec__str*, %Vec__str** %this_ptr.433
  %t444 = getelementptr inbounds %Vec__str, %Vec__str* %t443, i32 0, i32 1
  %t445 = load i64, i64* %t444
  %t446 = mul i64 %t445, 2
  %t447 = call i1 @__method__Vec__reserve__str(%Vec__str* %t442, i64 %t446)
 br label %if_end54
if_else53:
 br label %if_end54
if_end54:
  %t448 = load %Vec__str*, %Vec__str** %this_ptr.433
  %t449 = getelementptr inbounds %Vec__str, %Vec__str* %t448, i32 0, i32 2
  %t450 = load i8**, i8*** %t449
  %t451 = load %Vec__str*, %Vec__str** %this_ptr.433
  %t452 = getelementptr inbounds %Vec__str, %Vec__str* %t451, i32 0, i32 0
  %t453 = load i64, i64* %t452
  %t454 = getelementptr inbounds i8*, i8** %t450, i64 %t453
  %t455 = load i8*, i8** %i_ptr.434
 store i8* %t455, i8** %t454
  %t456 = load %Vec__str*, %Vec__str** %this_ptr.433
  %t457 = getelementptr inbounds %Vec__str, %Vec__str* %t456, i32 0, i32 0
  %t458 = load %Vec__str*, %Vec__str** %this_ptr.433
  %t459 = getelementptr inbounds %Vec__str, %Vec__str* %t458, i32 0, i32 0
  %t460 = load i64, i64* %t459
  %t461 = add i64 %t460, 1
 store i64 %t461, i64* %t457
  ret i64 0
}

define internal i8 @__method__Vec__pop__i8(%Vec__i8* %__arg_this) {
entry:
  %this_ptr.462 = alloca %Vec__i8*
  store %Vec__i8* %__arg_this, %Vec__i8** %this_ptr.462
  %t463 = load %Vec__i8*, %Vec__i8** %this_ptr.462
  %t464 = getelementptr inbounds %Vec__i8, %Vec__i8* %t463, i32 0, i32 0
  %t465 = load i64, i64* %t464
  %t466 = icmp slt i64 %t465, 0
  br i1 %t466, label %logic_short56, label %logic_rhs55
logic_rhs55:
  %t467 = load %Vec__i8*, %Vec__i8** %this_ptr.462
  %t468 = getelementptr inbounds %Vec__i8, %Vec__i8* %t467, i32 0, i32 0
  %t469 = load i64, i64* %t468
  %t470 = icmp eq i64 %t469, 0
  br label %logic_end57
logic_short56:
  br label %logic_end57
logic_end57:
  %t471 = phi i1 [ %t470, %logic_rhs55 ], [ true, %logic_short56 ]
 br i1 %t471, label %if_then58, label %if_else59
if_then58:
  %t472 = getelementptr [22 x i8], [22 x i8]* @.str.1, i32 0, i32 0
  %t473 = getelementptr [4 x i8], [4 x i8]* @fmt_str, i32 0, i32 0
  call i32 (i8*, ...) @printf(i8* %t473, i8* %t472)
 br label %if_end60
if_else59:
 br label %if_end60
if_end60:
  %t474 = load %Vec__i8*, %Vec__i8** %this_ptr.462
  %t475 = getelementptr inbounds %Vec__i8, %Vec__i8* %t474, i32 0, i32 0
  %t476 = load %Vec__i8*, %Vec__i8** %this_ptr.462
  %t477 = getelementptr inbounds %Vec__i8, %Vec__i8* %t476, i32 0, i32 0
  %t478 = load i64, i64* %t477
  %t479 = sub i64 %t478, 1
 store i64 %t479, i64* %t475
  %t480 = load %Vec__i8*, %Vec__i8** %this_ptr.462
  %t481 = getelementptr inbounds %Vec__i8, %Vec__i8* %t480, i32 0, i32 2
  %t482 = load i8*, i8** %t481
  %t483 = load %Vec__i8*, %Vec__i8** %this_ptr.462
  %t484 = getelementptr inbounds %Vec__i8, %Vec__i8* %t483, i32 0, i32 0
  %t485 = load i64, i64* %t484
  %t486 = getelementptr inbounds i8, i8* %t482, i64 %t485
  %t487 = load i8, i8* %t486
 ret i8 %t487
}

define internal void @__method__Vec____drop____str(%Vec__str* %__arg_this) {
entry:
  %this_ptr.488 = alloca %Vec__str*
  store %Vec__str* %__arg_this, %Vec__str** %this_ptr.488
  %t489 = load %Vec__str*, %Vec__str** %this_ptr.488
  %t490 = getelementptr inbounds %Vec__str, %Vec__str* %t489, i32 0, i32 0
  %t491 = load i64, i64* %t490
 %t492 = alloca i64
 store i64 0, i64* %t492
 br label %loopcond61
loopcond61:
 %t493 = load i64, i64* %t492
 %t494 = icmp ult i64 %t493, %t491
 br i1 %t494, label %loopbody62, label %loopend64
loopbody62:
 br label %loopstep63
loopstep63:
 %t495 = load i64, i64* %t492
 %t496 = add i64 %t495, 1
 store i64 %t496, i64* %t492
 br label %loopcond61
loopend64:
  %t497 = load %Vec__str*, %Vec__str** %this_ptr.488
  %t498 = call i64 @__method__Vec__unvec__str(%Vec__str* %t497)
  ret void
}

define internal void @__method__Vec____drop____i8(%Vec__i8* %__arg_this) {
entry:
  %this_ptr.499 = alloca %Vec__i8*
  store %Vec__i8* %__arg_this, %Vec__i8** %this_ptr.499
  %t500 = load %Vec__i8*, %Vec__i8** %this_ptr.499
  %t501 = getelementptr inbounds %Vec__i8, %Vec__i8* %t500, i32 0, i32 0
  %t502 = load i64, i64* %t501
 %t503 = alloca i64
 store i64 0, i64* %t503
 br label %loopcond65
loopcond65:
 %t504 = load i64, i64* %t503
 %t505 = icmp ult i64 %t504, %t502
 br i1 %t505, label %loopbody66, label %loopend68
loopbody66:
 br label %loopstep67
loopstep67:
 %t506 = load i64, i64* %t503
 %t507 = add i64 %t506, 1
 store i64 %t507, i64* %t503
 br label %loopcond65
loopend68:
  %t508 = load %Vec__i8*, %Vec__i8** %this_ptr.499
  %t509 = call i64 @__method__Vec__unvec__i8(%Vec__i8* %t508)
  ret void
}

define internal i8 @__method__Vec____operator_index__i8(%Vec__i8* %__arg_this, i64 %__arg_i) {
entry:
  %this_ptr.510 = alloca %Vec__i8*
  store %Vec__i8* %__arg_this, %Vec__i8** %this_ptr.510
  %i_ptr.511 = alloca i64
  store i64 %__arg_i, i64* %i_ptr.511
  %t512 = load %Vec__i8*, %Vec__i8** %this_ptr.510
  %t513 = getelementptr inbounds %Vec__i8, %Vec__i8* %t512, i32 0, i32 2
  %t514 = load i8*, i8** %t513
  %t515 = load i64, i64* %i_ptr.511
  %t516 = getelementptr inbounds i8, i8* %t514, i64 %t515
  %t517 = load i8, i8* %t516
 ret i8 %t517
}

define internal i64 @__method__Vec__unvec__str(%Vec__str* %__arg_this) {
entry:
  %this_ptr.518 = alloca %Vec__str*
  store %Vec__str* %__arg_this, %Vec__str** %this_ptr.518
  %t519 = load %Vec__str*, %Vec__str** %this_ptr.518
  %t520 = getelementptr inbounds %Vec__str, %Vec__str* %t519, i32 0, i32 2
  %t521 = load i8**, i8*** %t520
  %t522 = bitcast i8** %t521 to i8*
  call void @free(i8* %t522)
  ret i64 0
}

define internal i1 @__method__Vec__reserve__str(%Vec__str* %__arg_this, i64 %__arg_n) {
entry:
  %this_ptr.523 = alloca %Vec__str*
  store %Vec__str* %__arg_this, %Vec__str** %this_ptr.523
  %n_ptr.524 = alloca i64
  store i64 %__arg_n, i64* %n_ptr.524
  %t525 = load i64, i64* %n_ptr.524
  %t526 = load %Vec__str*, %Vec__str** %this_ptr.523
  %t527 = getelementptr inbounds %Vec__str, %Vec__str* %t526, i32 0, i32 1
  %t528 = load i64, i64* %t527
  %t529 = icmp slt i64 %t525, %t528
  br i1 %t529, label %logic_short70, label %logic_rhs69
logic_rhs69:
  %t530 = load i64, i64* %n_ptr.524
  %t531 = load %Vec__str*, %Vec__str** %this_ptr.523
  %t532 = getelementptr inbounds %Vec__str, %Vec__str* %t531, i32 0, i32 1
  %t533 = load i64, i64* %t532
  %t534 = icmp eq i64 %t530, %t533
  br label %logic_end71
logic_short70:
  br label %logic_end71
logic_end71:
  %t535 = phi i1 [ %t534, %logic_rhs69 ], [ true, %logic_short70 ]
 br i1 %t535, label %if_then72, label %if_else73
if_then72:
 ret i1 false
if_else73:
 br label %if_end74
if_end74:
  %t536 = load i64, i64* %n_ptr.524
 %r_ptr.537 = alloca i8**
 %t538 = mul i64 %t536, 8
 %t539 = call i8* @malloc(i64 %t538)
 %t540 = bitcast i8* %t539 to i8**
 store i8** %t540, i8*** %r_ptr.537
  %t541 = load %Vec__str*, %Vec__str** %this_ptr.523
  %t542 = getelementptr inbounds %Vec__str, %Vec__str* %t541, i32 0, i32 0
  %t543 = load i64, i64* %t542
 %t544 = alloca i64
 store i64 0, i64* %t544
 br label %loopcond75
loopcond75:
 %t545 = load i64, i64* %t544
 %t546 = icmp ult i64 %t545, %t543
 br i1 %t546, label %loopbody76, label %loopend78
loopbody76:
  %t547 = load i8**, i8*** %r_ptr.537
  %t548 = load i64, i64* %t544
  %t549 = getelementptr inbounds i8*, i8** %t547, i64 %t548
  %t550 = load %Vec__str*, %Vec__str** %this_ptr.523
  %t551 = getelementptr inbounds %Vec__str, %Vec__str* %t550, i32 0, i32 2
  %t552 = load i8**, i8*** %t551
  %t553 = load i64, i64* %t544
  %t554 = getelementptr inbounds i8*, i8** %t552, i64 %t553
  %t555 = load i8*, i8** %t554
 store i8* %t555, i8** %t549
 br label %loopstep77
loopstep77:
 %t556 = load i64, i64* %t544
 %t557 = add i64 %t556, 1
 store i64 %t557, i64* %t544
 br label %loopcond75
loopend78:
  %t558 = load %Vec__str*, %Vec__str** %this_ptr.523
  %t559 = getelementptr inbounds %Vec__str, %Vec__str* %t558, i32 0, i32 2
  %t560 = load i8**, i8*** %t559
  %t561 = bitcast i8** %t560 to i8*
  call void @free(i8* %t561)
  %t562 = load %Vec__str*, %Vec__str** %this_ptr.523
  %t563 = getelementptr inbounds %Vec__str, %Vec__str* %t562, i32 0, i32 2
  %t564 = load i8**, i8*** %r_ptr.537
 store i8** %t564, i8*** %t563
  %t565 = load %Vec__str*, %Vec__str** %this_ptr.523
  %t566 = getelementptr inbounds %Vec__str, %Vec__str* %t565, i32 0, i32 1
  %t567 = load i64, i64* %n_ptr.524
 store i64 %t567, i64* %t566
 ret i1 true
}

