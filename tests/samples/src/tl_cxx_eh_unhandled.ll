; Fixture PE32+ mínima para validar o encerramento controlado de uma exceção
; C++ sem handler de linguagem.
target triple = "x86_64-pc-windows-msvc"

@cxx_parameters = private constant [3 x i64] zeroinitializer
declare dllimport void @RaiseException(i32, i32, i32, ptr)

define void @tl_entry() {
entry:
  call void @RaiseException(i32 3765269347, i32 0, i32 3, ptr @cxx_parameters)
  unreachable
}
