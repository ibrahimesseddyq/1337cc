; ModuleID = '1337cc'
source_filename = "1337cc"

@.str = private unnamed_addr constant [15 x i8] c"Hello, World!\0A\00", align 1

declare i32 @printf(ptr, ...)

define i32 @main() {
entry:
  %call = call i32 (ptr, ...) @printf(ptr @.str)
  ret i32 0
}
