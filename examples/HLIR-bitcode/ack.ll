; ModuleID = 'ack.c'
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: nounwind uwtable
define i32 @ack(i32 %m, i32 %n) #0 {
entry:
  %retval = alloca i32, align 4
  %m.addr = alloca i32, align 4
  %n.addr = alloca i32, align 4
  store i32 %m, i32* %m.addr, align 4
  store i32 %n, i32* %n.addr, align 4
  %0 = load i32, i32* %m.addr, align 4
  %cmp = icmp eq i32 %0, 0
  br i1 %cmp, label %if.then, label %if.else

if.then:                                          ; preds = %entry
  %1 = load i32, i32* %n.addr, align 4
  %add = add nsw i32 %1, 1
  store i32 %add, i32* %retval
  br label %return

if.else:                                          ; preds = %entry
  %2 = load i32, i32* %n.addr, align 4
  %cmp1 = icmp eq i32 %2, 0
  br i1 %cmp1, label %if.then.2, label %if.else.3

if.then.2:                                        ; preds = %if.else
  %3 = load i32, i32* %m.addr, align 4
  %sub = sub nsw i32 %3, 1
  %call = call i32 @ack(i32 %sub, i32 1)
  store i32 %call, i32* %retval
  br label %return

if.else.3:                                        ; preds = %if.else
  %4 = load i32, i32* %m.addr, align 4
  %sub4 = sub nsw i32 %4, 1
  %5 = load i32, i32* %m.addr, align 4
  %6 = load i32, i32* %n.addr, align 4
  %sub5 = sub nsw i32 %6, 1
  %call6 = call i32 @ack(i32 %5, i32 %sub5)
  %call7 = call i32 @ack(i32 %sub4, i32 %call6)
  store i32 %call7, i32* %retval
  br label %return

return:                                           ; preds = %if.else.3, %if.then.2, %if.then
  %7 = load i32, i32* %retval
  ret i32 %7
}

attributes #0 = { nounwind uwtable "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.ident = !{!0}

!0 = !{!"clang version 3.8.0 "}
